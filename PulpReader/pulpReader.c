/*
 * PULP (Precompressed Upstream Layer Pipeline)
 * High‑performance, low‑latency telemetry and logging layer for Windows.
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later OR LicenseRef-Superwired-Commercial-v1.0
 *
 * Copyright (C) 2026  Francois Gauthier - Superwired-Labs
 *
 * ===================== DUAL LICENSE NOTICE =====================
 * This program is available under a dual License model:
 * The GNU Affero General Public License v3.0 (AGPLv3)
 * or a commercial license from Superwired-Labs.
 *
 * ===================== FREE LICENSE ============================
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published
 * by the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 * ===================== COMMERCIAL LICENSE ======================
 * This software is available under a commercial license from Superwired-Labs,
 * if you intend to use this software in a closed-source product or service without
 * complying with AGPLv3 copyleft terms, a commercial license is required.

 * Full license texts are located in the LICENSES/ directory at the root of this
 * repository. See COMMERCIAL.md for licensing options and contact information.
 */

#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
#include <limits.h>
#include <time.h>
#include <windows.h>
#include <process.h>
#include "lz4.h"
#include "pulpReader.h"

/* Global synchronization */
static CRITICAL_SECTION queue_cs;
static CONDITION_VARIABLE queue_cv;
static Task* queue_head = NULL;
static Task* queue_tail = NULL;

static CRITICAL_SECTION results_cs;
static CONDITION_VARIABLE results_cv;
static BlockStream** results = NULL;   // one stream per block index
static size_t results_capacity = 0;
static uint32_t next_write_index = 0;

/* Deterministic termination */
static CRITICAL_SECTION stats_cs;
static uint32_t tasks_enqueued = 0;
static uint32_t tasks_completed = 0;

static HANDLE in_flight_sem = NULL;
static volatile int no_more_tasks = 0;
static HANDLE worker_threads[MAX_CONCURRENT_BLOCKS];
static unsigned int worker_count = 0;

/* URL to punycode option (not implemented)*/
static uint8_t puny_output = 0;

/* Utilities */
static int compare_dict_entry(const void* a, const void* b) {
    const DictEntry* da = (const DictEntry*)a;
    const DictEntry* db = (const DictEntry*)b;
    return (da->index > db->index) - (da->index < db->index);
}

static const char* dict_lookup(const DictEntry* dict, uint32_t count, uint32_t index) {
    DictEntry key = { .index = index, .str = NULL };
    const DictEntry* found = (const DictEntry*)bsearch(&key, dict, count, sizeof(DictEntry), compare_dict_entry);
    return found ? found->str : "NOT_FOUND";
}

static void free_dict(DictEntry* dict, uint32_t count) {
    if (!dict) return;
    for (uint32_t i = 0; i < count; i++) {
        if (dict[i].str != NULL) {
            free(dict[i].str);
            dict[i].str = NULL;
        }
    }
    free(dict);
}

static void format_ip(uint32_t ip, char* buf, size_t buf_size) {
    snprintf(buf, buf_size, "%u.%u.%u.%u",
        (unsigned)((ip >> 24) & 0xFF), (unsigned)((ip >> 16) & 0xFF),
        (unsigned)((ip >> 8) & 0xFF), (unsigned)(ip & 0xFF));
}

static char* format_timestamp_iso8601_utc(uint64_t timestamp, char* buffer, size_t buf_size) {
    const uint64_t epoch_diff_us = WINDOWS_TO_UNIX_EPOCH_SECS * 1000000ULL;
    if (timestamp < epoch_diff_us) {
        snprintf(buffer, buf_size, "NULL");
        return buffer;
    }
    uint64_t unix_us = timestamp - epoch_diff_us;
    time_t secs = (time_t)(unix_us / 1000000ULL);
    unsigned micros = (unsigned)(unix_us % 1000000ULL);

    struct tm tm_utc;
    memset(&tm_utc, 0, sizeof(tm_utc));
#if defined(_WIN32)
    _gmtime64_s(&tm_utc, &secs);
#else
    gmtime_r(&secs, &tm_utc);
#endif
    char datebuf[32];
    strftime(datebuf, sizeof(datebuf), "%Y-%m-%dT%H:%M:%S", &tm_utc);
    snprintf(buffer, buf_size, "%s.%06uZ", datebuf, micros);
    return buffer;
}

static int parse_full_dictionary(const uint8_t* buf, size_t size, size_t* pos, Dicts* out) {
    if (!buf || !pos || !out) return -1;
    size_t p = *pos;

    // verify the start marker
    if (p + sizeof(uint64_t) > size) return -2;
    uint64_t canary_begin;
    memcpy(&canary_begin, buf + p, sizeof(canary_begin));
    p += sizeof(uint64_t);
    if (canary_begin != DICT_BEGIN) return -3;

    //  Read the number of URLs
    if (p + sizeof(uint32_t) > size) return -4;
    uint32_t url_n;
    memcpy(&url_n, buf + p, sizeof(url_n));
    p += sizeof(uint32_t);

    // Basic verification
    if (url_n > (1u << 28)) return -5;

    // Allocate the URL dictionary
    DictEntry* url_dict = calloc(url_n, sizeof(DictEntry));
    if (!url_dict) return -6;

    // Read each URL entry  
    for (uint32_t i = 0; i < url_n; i++) {
        if (p + sizeof(uint32_t) + sizeof(uint16_t) > size) {
            free_dict(url_dict, i);
            return -7;
        }

        // Read the index
        uint32_t idx;
        memcpy(&idx, buf + p, sizeof(idx));
        p += sizeof(idx);

        // Read the length (includes the null terminator)
        uint16_t len_with_null;
        memcpy(&len_with_null, buf + p, sizeof(len_with_null));
        p += sizeof(len_with_null);

        // Verify if we have enough data for the URL
        if (p + len_with_null > size) {
            free_dict(url_dict, i);
            return -8;
        }

        // Allocate with the exact size
        char* s = malloc(len_with_null);
        if (!s) {
            free_dict(url_dict, i);
            return -9;
        }

        // Copy the data
        memcpy(s, buf + p, len_with_null);
        p += len_with_null;

        // Ensure null termination
        if (len_with_null == 0) {
            // Cas anormal : chaîne vide
            free(s);
            s = malloc(1);
            if (!s) {
                free_dict(url_dict, i);
                return -9;   // or an appropriate error code
            }
            s[0] = '\0';
        }
        else if (s[len_with_null - 1] != '\0') {
            // Last byte is not null → add a byte for the '\0'
            char* new_s = realloc(s, len_with_null + 1);
            if (new_s) {
                s = new_s;
                s[len_with_null] = '\0';
            }
            else {
                // Realloc failed: force null termination by overwriting the last character 
                s[len_with_null - 1] = '\0';
            }
        }

        url_dict[i].index = idx;
        url_dict[i].str = s;
    }

    //  Read the number of IPs
    if (p + sizeof(uint32_t) > size) {
        free_dict(url_dict, url_n);
        return -10;
    }
    uint32_t ip_n;
    memcpy(&ip_n, buf + p, sizeof(ip_n));
    p += sizeof(uint32_t);

    // Basic verification
    if (ip_n > (1u << 28)) {
        free_dict(url_dict, url_n);
        return -11;
    }

    // Allocate the IP dictionary
    DictEntry* ip_dict = calloc(ip_n, sizeof(DictEntry));
    if (!ip_dict) {
        free_dict(url_dict, url_n);
        return -12;
    }

    // Read each IP entry
    for (uint32_t i = 0; i < ip_n; ++i) {
        if (p + sizeof(uint32_t) + sizeof(uint16_t) > size) {
            free_dict(ip_dict, i);
            free_dict(url_dict, url_n);
            return -13;
        }

        // Read the index
        uint32_t idx;
        memcpy(&idx, buf + p, sizeof(idx));
        p += sizeof(idx);

        // Read the length
        uint16_t len_with_null;
        memcpy(&len_with_null, buf + p, sizeof(len_with_null));
        p += sizeof(len_with_null);

        if (p + len_with_null > size) {
            free_dict(ip_dict, i);
            free_dict(url_dict, url_n);
            return -14;
        }

        // Allocate and copy
        char* s = malloc(len_with_null);
        if (!s) {
            free_dict(ip_dict, i);
            free_dict(url_dict, url_n);
            return -15;
        }
        memcpy(s, buf + p, len_with_null);
        p += len_with_null;

        // Ensure null termination
        if (len_with_null == 0) {
            free(s);
            s = malloc(1);
            if (!s) {
                free_dict(ip_dict, i);
                free_dict(url_dict, url_n);
                return -15;
            }
            s[0] = '\0';
        }
        else if (s[len_with_null - 1] != '\0') {
            char* new_s = realloc(s, len_with_null + 1);
            if (new_s) {
                s = new_s;
                s[len_with_null] = '\0';
            }
            else {
                s[len_with_null - 1] = '\0';
            }
        }

        ip_dict[i].index = idx;
        ip_dict[i].str = s;
    }

    // Verify the end marker
    if (p + sizeof(uint64_t) > size) {
        free_dict(url_dict, url_n);
        free_dict(ip_dict, ip_n);
        return -16;
    }
    uint64_t canary_end;
    memcpy(&canary_end, buf + p, sizeof(canary_end));
    p += sizeof(uint64_t);
    if (canary_end != DICT_END) {
        free_dict(url_dict, url_n);
        free_dict(ip_dict, ip_n);
        return -17;
    }

    // Sort the dictionaries
    qsort(url_dict, url_n, sizeof(DictEntry), compare_dict_entry);
    qsort(ip_dict, ip_n, sizeof(DictEntry), compare_dict_entry);

    out->urls = url_dict;
    out->url_count = url_n;
    out->ips = ip_dict;
    out->ips_count = ip_n;
    *pos = p;

    return 0;
}

static void free_dicts(Dicts* dicts) {
    if (dicts->urls) {
        free_dict(dicts->urls, dicts->url_count);
        dicts->urls = NULL;
    }
    if (dicts->ips) {
        free_dict(dicts->ips, dicts->ips_count);
        dicts->ips = NULL;
    }
}

static int decompress_block(const uint8_t* in, int compSize, uint8_t* out, int decompSize) {
    int got = LZ4_decompress_safe((const char*)in, (char*)out, compSize, decompSize);
    return (got == decompSize) ? 0 : -1;
}

/* Queue management */
static void enqueue_task(Task* t) {
    EnterCriticalSection(&queue_cs);
    if (!queue_tail) queue_head = queue_tail = t;
    else { queue_tail->next = t; queue_tail = t; }
    t->next = NULL;
    WakeConditionVariable(&queue_cv);
    LeaveCriticalSection(&queue_cs);
}

static Task* dequeue_task_wait() {
    Task* t = NULL;
    EnterCriticalSection(&queue_cs);
    while (!queue_head && !no_more_tasks) {
        SleepConditionVariableCS(&queue_cv, &queue_cs, INFINITE);
    }
    if (queue_head) {
        t = queue_head;
        queue_head = queue_head->next;
        if (!queue_head) queue_tail = NULL;
    }
    LeaveCriticalSection(&queue_cs);
    return t;
}

/* Results registration */
static int ensure_results_capacity(uint32_t idx) {
    EnterCriticalSection(&results_cs);
    if (idx < results_capacity) { LeaveCriticalSection(&results_cs); return 0; }
    size_t newcap = results_capacity ? results_capacity : 16;
    while ((uint32_t)newcap <= idx) newcap *= 2;
    BlockStream** tmp = (BlockStream**)realloc(results, newcap * sizeof(BlockStream*));
    if (!tmp) { LeaveCriticalSection(&results_cs); return -1; }
    for (size_t i = results_capacity; i < newcap; ++i) tmp[i] = NULL;
    results = tmp;
    results_capacity = newcap;
    LeaveCriticalSection(&results_cs);
    return 0;
}

static void results_register_stream(uint32_t idx, BlockStream* s) {
    if (ensure_results_capacity(idx) != 0) {
        fprintf(stderr, "Out of memory registering stream for %u\n", idx);
        return;
    }
    EnterCriticalSection(&results_cs);
    results[idx] = s;
    WakeConditionVariable(&results_cv);
    LeaveCriticalSection(&results_cs);
}

/* BlockStream helpers */
static BlockStream* stream_create(void) {
    BlockStream* s = (BlockStream*)calloc(1, sizeof(BlockStream));
    if (!s) return NULL;
    InitializeCriticalSection(&s->cs);
    InitializeConditionVariable(&s->cv);
    s->head = s->tail = NULL;
    s->done = 0;
    s->failed = 0;
    s->total_bytes = 0;
    return s;
}

static void stream_destroy(BlockStream* s) {
    if (!s) return;
    EnterCriticalSection(&s->cs);
    OutChunk* c = s->head;
    while (c) {
        OutChunk* n = c->next;
        free(c->data);
        free(c);
        c = n;
    }
    s->head = s->tail = NULL;
    LeaveCriticalSection(&s->cs);
    DeleteCriticalSection(&s->cs);
    free(s);
}

static int stream_push_chunk(BlockStream* s, char* data, size_t size) {
    OutChunk* c = (OutChunk*)malloc(sizeof(OutChunk));
    if (!c) { free(data); return -1; }
    c->data = data; c->size = size; c->next = NULL;

    EnterCriticalSection(&s->cs);
    if (!s->tail) s->head = s->tail = c;
    else { s->tail->next = c; s->tail = c; }
    s->total_bytes += size;
    WakeConditionVariable(&s->cv);
    LeaveCriticalSection(&s->cs);
    return 0;
}

static void stream_mark_done(BlockStream* s, int failed) {
    EnterCriticalSection(&s->cs);
    if (failed) s->failed = 1;
    s->done = 1;
    WakeConditionVariable(&s->cv);
    LeaveCriticalSection(&s->cs);

    EnterCriticalSection(&stats_cs);
    tasks_completed++;
    WakeConditionVariable(&results_cv);
    LeaveCriticalSection(&stats_cs);
}

/* Per-worker emit buffer */
typedef struct {
    char* buf;
    size_t cap;
    size_t used;
} EmitBuf;

static int emitbuf_init(EmitBuf* eb, size_t cap) {
    eb->buf = (char*)malloc(cap);
    if (!eb->buf) return -1;
    eb->cap = cap;
    eb->used = 0;
    return 0;
}

static int emitbuf_flush(BlockStream* s, EmitBuf* eb) {
    if (eb->used == 0) return 0;
    char* out = eb->buf;
    size_t sz = eb->used;
    eb->buf = (char*)malloc(eb->cap);
    if (!eb->buf) {
        (void)stream_push_chunk(s, out, sz);
        eb->cap = eb->used = 0;
        return -1;
    }
    eb->used = 0;
    return stream_push_chunk(s, out, sz);
}

static int emitbuf_append(BlockStream* s, EmitBuf* eb, const char* data, size_t len) {
    if (len > eb->cap) {
        char* d = (char*)malloc(len);
        if (!d) return -1;
        memcpy(d, data, len);
        return stream_push_chunk(s, d, len);
    }
    if (eb->used + len > eb->cap) {
        if (emitbuf_flush(s, eb) != 0) return -1;
    }
    memcpy(eb->buf + eb->used, data, len);
    eb->used += len;
    return 0;
}

static int process_payload_block_streaming(const uint8_t* data,
    size_t data_size,
    BlockStream* stream,
    int block_idx)
{
    const size_t entry_size = sizeof(SerializedEntry);
    size_t p = 0;

    /* -------------------------------------------------------------
     *  Find the start of the dictionary by scanning for DICT_BEGIN
     *  at 32-byte aligned offsets first, then byte-by-byte if needed.
     * ------------------------------------------------------------- */
    size_t canary_pos = 0;
    int found = 0;
    // First, try aligned offsets
    for (canary_pos = 0; canary_pos + sizeof(uint64_t) <= data_size; canary_pos += 32) {
        uint64_t cand = 0;
        memcpy(&cand, data + canary_pos, sizeof(cand));
        if (cand == DICT_BEGIN) {
            found = 1;
            break;
        }
    }
    // If not found, try byte-by-byte
    if (!found) {
        for (canary_pos = 0; canary_pos + sizeof(uint64_t) <= data_size; canary_pos++) {
            uint64_t cand = 0;
            memcpy(&cand, data + canary_pos, sizeof(cand));
            if (cand == DICT_BEGIN) {
                found = 1;
                break;
            }
        }
    }

    if (!found) {
        fprintf(stderr, "DICT_BEGIN not found in block %d\n", block_idx);
        return 1;
    }

    // Now, the entries are from the start to canary_pos
    size_t entries_bytes = canary_pos;
    if (entries_bytes % entry_size != 0) {
        fprintf(stderr,
            "Entries size not multiple of entry size in block %d: %zu %% %zu = %zu\n",
            block_idx, entries_bytes, entry_size, entries_bytes % entry_size);
        return -2;
    }
    size_t num_entries = entries_bytes / entry_size;

    /* -------------------------------------------------------------
     *  Parse the dictionaries.
     * ------------------------------------------------------------- */
    Dicts dicts = { 0 };
    size_t dict_pos = canary_pos;

    int pr = parse_full_dictionary(data, data_size, &dict_pos, &dicts);
    if (pr != 0) {
        fprintf(stderr, "Dictionary error (block %d), code %d\n", block_idx, pr);
        return -3;
    }

    /* -------------------------------------------------------------
     *  Prepare the streaming buffer.
     * ------------------------------------------------------------- */
    EmitBuf eb;
    if (emitbuf_init(&eb, STREAM_CHUNK_SIZE) != 0) {
        free_dicts(&dicts);
        return -6;
    }

    /* ---- Header line ------------------------------------------------ */
    char header[128];
    int hn = snprintf(header, sizeof(header),
        "\n=== BLOCK %d === (%zu entries)\n",
        block_idx, num_entries);
    if (hn < 0) {
        free_dicts(&dicts);
        free(eb.buf);
        return -7;
    }
    if (emitbuf_append(stream, &eb, header, (size_t)hn) != 0) {
        free_dicts(&dicts);
        free(eb.buf);
        return -8;
    }

    /* -------------------------------------------------------------
     *  Walk through each serialized entry and emit a human‐readable line.
     * ------------------------------------------------------------- */
    char timestamp_buf[64];
    char ip_buf[128];          /* big enough for IPv6 text */
    char line[1024];

    if (p + num_entries * entry_size > data_size) {
        fprintf(stderr,
            "Block %d: entries extend beyond data buffer\n",
            block_idx);
        free_dicts(&dicts);
        free(eb.buf);
        return -9;
    }

    for (size_t i = 0; i < num_entries; ++i) {
        const SerializedEntry* e = (const SerializedEntry*)(data + p) + i;

        /* ---- URL lookup  ---------------------------------------------- */
        const char* url_str = dict_lookup(dicts.urls,
            dicts.url_count,
            e->url_idx);

        /* ---- Verb name ------------------------------------------------ */
        char numstr[8] = { 0 };
        const char* verb = itoa(e->http_verb, numstr, 10);
        size_t verb_cnt = sizeof(HTTP_VERB_NAMES) / sizeof(HTTP_VERB_NAMES[0]);
        if (e->http_verb < verb_cnt) verb = HTTP_VERB_NAMES[e->http_verb];

        /* ---- Timestamp ------------------------------------------------ */
        format_timestamp_iso8601_utc(e->timestamp,
            timestamp_buf,
            sizeof(timestamp_buf));

        /* ---- IP resolution -------------------------------------------- */
        const char* ip_lookup = dict_lookup(dicts.ips, dicts.ips_count, e->ip_idx);
        const char* ip_src = (strcmp(ip_lookup, "NOT_FOUND") == 0) ? "NOT_FOUND" : ip_lookup;
        strncpy(ip_buf, ip_src, sizeof(ip_buf) - 1);
        ip_buf[sizeof(ip_buf) - 1] = '\0';

        /* ---- Assemble the final line --------------------------------- */
        int ln = snprintf(line, sizeof(line),
            "[%" PRIu64 "] [%s] [%s] [%s] [%u] [%s] "
            "[%ums] [%ub] [%u]\n",
            e->sequence,
            timestamp_buf,
            url_str,
            ip_buf,
            e->http_code,
            verb,
            (unsigned)e->duration_ms,
            (unsigned)e->response_size,
            (unsigned)e->flags);
        if (ln < 0) {
            free_dicts(&dicts);
            free(eb.buf);
            return -10;
        }

        if (emitbuf_append(stream, &eb, line, (size_t)ln) != 0) {
            free_dicts(&dicts);
            free(eb.buf);
            return -11;
        }
    }

    /* -------------------------------------------------------------
     *  Flush any remaining buffered data, clean up and exit.
     * ------------------------------------------------------------- */
    (void)emitbuf_flush(stream, &eb);
    free(eb.buf);
    free_dicts(&dicts);
    return 0;
}

/* Worker thread */
static unsigned __stdcall worker_thread_func(void* arg) {
    (void)arg;
    for (;;) {
        Task* t = dequeue_task_wait();
        if (!t) {
            if (no_more_tasks) break;
            continue;
        }

        uint8_t* decomp_data = NULL;
        int need_free_decomp = 0;
        int failed = 0;

        if (t->comp_size == t->decomp_size) {
            decomp_data = t->comp_data;
            need_free_decomp = 1; // free after use
        }
        else {
            decomp_data = (uint8_t*)malloc(t->decomp_size);
            if (!decomp_data) {
                fprintf(stderr, "Memory allocation failed for decompressed data (block %u)\n", t->block_index);
                free(t->comp_data);
                t->comp_data = NULL;
                failed = 1;
                goto finish;
            }
            int dr = decompress_block(t->comp_data, (int)t->comp_size, decomp_data, (int)t->decomp_size);
            free(t->comp_data);
            t->comp_data = NULL;
            if (dr != 0) {
                fprintf(stderr, "Decompression failed for block %u: error %d\n", t->block_index, dr);
                failed = 1;
                goto finish;
            }
            need_free_decomp = 1;
        }

        {
            int pr = process_payload_block_streaming(decomp_data, t->decomp_size, t->stream, (int)t->block_index);
            if (pr != 0) {
                fprintf(stderr, "Processing block to buffer failed for %u (code %d)\n", t->block_index, pr);
                failed = 1;
            }
        }

    finish:
        if (need_free_decomp && decomp_data) { free(decomp_data); decomp_data = NULL; }
        // If failed and nothing was written, push a minimal marker
        if (failed && t->stream) {
            EnterCriticalSection(&t->stream->cs);
            int empty = (t->stream->total_bytes == 0);
            LeaveCriticalSection(&t->stream->cs);
            if (empty) {
                char* msg = (char*)malloc(64);
                if (msg) {
                    int n = snprintf(msg, 64, "\n=== BLOCK %u FAILED ===\n", t->block_index);
                    if (n > 0) (void)stream_push_chunk(t->stream, msg, (size_t)n);
                    else free(msg);
                }
            }
        }
        if (t->stream) stream_mark_done(t->stream, failed);

        ReleaseSemaphore(in_flight_sem, 1, NULL);
        free(t);
    }
    return 0;
}

/* Writer thread */
static unsigned __stdcall writer_thread_func(void* arg) {
    WriterArg* wa = (WriterArg*)arg;
    FILE* fout = wa->fout;

    setvbuf(fout, NULL, _IOFBF, 64 << 20);

    for (;;) {
        BlockStream* s = NULL;

        EnterCriticalSection(&results_cs);
        for (;;) {
            int have_next = (next_write_index < results_capacity) && (results[next_write_index] != NULL);

            if (have_next) {
                s = results[next_write_index];
                break;
            }

            EnterCriticalSection(&stats_cs);
            int should_exit = (no_more_tasks && (tasks_completed >= tasks_enqueued) && (next_write_index >= tasks_enqueued));
            LeaveCriticalSection(&stats_cs);

            if (should_exit) {
                LeaveCriticalSection(&results_cs);
                goto writer_done;
            }

            SleepConditionVariableCS(&results_cv, &results_cs, INFINITE);
        }
        LeaveCriticalSection(&results_cs);

        // Consume the stream s in order
        for (;;) {
            OutChunk* c = NULL;
            int done = 0;

            EnterCriticalSection(&s->cs);
            while (!s->head && !s->done) {
                SleepConditionVariableCS(&s->cv, &s->cs, INFINITE);
            }
            if (s->head) {
                c = s->head;
                s->head = c->next;
                if (!s->head) s->tail = NULL;
            }
            done = s->done && (s->head == NULL);
            LeaveCriticalSection(&s->cs);

            if (c) {
                if (c->size) fwrite(c->data, 1, c->size, fout);
                free(c->data);
                free(c);
            }
            else if (done) {
                break;
            }
        }

        // After finishing this stream, advance to next block
        EnterCriticalSection(&results_cs);
        results[next_write_index] = NULL;
        next_write_index++;
        LeaveCriticalSection(&results_cs);

        //DeleteCriticalSection(&s->cs);

        stream_destroy(s);
    }

writer_done:
    fflush(fout);
    return 0;
}

int main(int argc, char* argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <input.bin> <output.txt>\n", argv[0]);
        return 1;
    }

    FILE* fin = NULL;
    FILE* fout = NULL;
    if (fopen_s(&fin, argv[1], "rb") != 0 || !fin) {
        fprintf(stderr, "Error opening input: %s\n", argv[1]);
        return 2;
    }
    if (fopen_s(&fout, argv[2], "wb") != 0 || !fout) {
        fprintf(stderr, "Error opening output: %s\n", argv[2]);
        fclose(fin);
        return 3;
    }

    // Not implemented
    if (argc == 4)
        puny_output = 1;

    InitializeCriticalSection(&queue_cs);
    InitializeConditionVariable(&queue_cv);
    InitializeCriticalSection(&results_cs);
    InitializeConditionVariable(&results_cv);
    InitializeCriticalSection(&stats_cs);


    in_flight_sem = CreateSemaphore(NULL, MAX_CONCURRENT_BLOCKS, MAX_CONCURRENT_BLOCKS, NULL);
    if (!in_flight_sem) {
        fprintf(stderr, "Failed to create semaphore\n");
        fclose(fin);
        fclose(fout);
        DeleteCriticalSection(&queue_cs);
        DeleteCriticalSection(&results_cs);
        DeleteCriticalSection(&stats_cs);
        return 4;
    }

    // Start worker threads
    worker_count = MAX_CONCURRENT_BLOCKS;
    for (unsigned i = 0; i < worker_count; ++i) {
        unsigned threadId;
        HANDLE h = (HANDLE)_beginthreadex(NULL, 0, worker_thread_func, NULL, 0, &threadId);
        if (!h) {
            fprintf(stderr, "Failed to create worker thread %u\n", i);
            worker_count = i;
            break;
        }
        worker_threads[i] = h;
    }

    // Start writer thread
    WriterArg warg = { .fout = fout };
    unsigned writerThreadId;
    HANDLE hWriter = (HANDLE)_beginthreadex(NULL, 0, writer_thread_func, &warg, 0, &writerThreadId);
    if (!hWriter) {
        fprintf(stderr, "Failed to create writer thread\n");
        no_more_tasks = 1;
        WakeAllConditionVariable(&queue_cv);
        WakeAllConditionVariable(&results_cv);
        goto cleanup;
    }

    printf("Processing file: %s (workers=%u)\n", argv[1], worker_count);

    // Main reading loop
    uint32_t block_index = 0;
    for (;;) {
        BlockHeader hdr;
        size_t r = fread(&hdr, 1, sizeof(hdr), fin);
        if (r != sizeof(hdr)) {
            if (feof(fin)) break;
            fprintf(stderr, "Error reading header at block %u\n", block_index);
            break;
        }

        if (hdr.compSize == 0 || hdr.decompSize == 0 /*||
            hdr.compSize > 500u * 1024u * 1024u ||
            hdr.decompSize > 1000u * 1024u * 1024u*/) {
            fprintf(stderr, "Invalid header at block %u: comp=%u, decomp=%u\n",
                block_index, hdr.compSize, hdr.decompSize);
            break;
        }

        // Admission control: cap in-flight tasks
        WaitForSingleObject(in_flight_sem, INFINITE);

        printf("Queueing block %u: comp=%u decomp=%u\n", block_index, hdr.compSize, hdr.decompSize);

        uint8_t* comp_data = (uint8_t*)malloc(hdr.compSize);
        if (!comp_data) {
            fprintf(stderr, "Memory alloc failed reading compressed data\n");
            ReleaseSemaphore(in_flight_sem, 1, NULL);
            break;
        }
        size_t got = fread(comp_data, 1, hdr.compSize, fin);
        if (got != hdr.compSize) {
            fprintf(stderr, "Error reading compressed data at block %u\n", block_index);
            free(comp_data);
            ReleaseSemaphore(in_flight_sem, 1, NULL);
            break;
        }

        Task* t = (Task*)calloc(1, sizeof(Task));
        if (!t) {
            fprintf(stderr, "Task allocation failed\n");
            free(comp_data);
            ReleaseSemaphore(in_flight_sem, 1, NULL);
            break;
        }
        t->block_index = block_index;
        t->comp_data = comp_data;
        t->comp_size = hdr.compSize;
        t->decomp_size = hdr.decompSize;

        // Create a stream for this block and register it so writer can consume in order
        BlockStream* s = stream_create();
        if (!s) {
            fprintf(stderr, "Stream allocation failed\n");
            free(t->comp_data);
            free(t);
            ReleaseSemaphore(in_flight_sem, 1, NULL);
            break;
        }
        t->stream = s;
        results_register_stream(block_index, s);

        // Track enqueued tasks
        EnterCriticalSection(&stats_cs);
        tasks_enqueued++;
        LeaveCriticalSection(&stats_cs);

        enqueue_task(t);
        WakeConditionVariable(&results_cv); // nudge writer if waiting

        block_index++;
    }

    // Signal end of tasks
    no_more_tasks = 1;
    WakeAllConditionVariable(&queue_cv);
    WakeAllConditionVariable(&results_cv);

    // Wait for workers
    for (unsigned i = 0; i < worker_count; ++i) {
        if (worker_threads[i]) {
            WaitForSingleObject(worker_threads[i], INFINITE);
            CloseHandle(worker_threads[i]);
            worker_threads[i] = NULL;
        }
    }

    // Wait for writer to finish
    if (hWriter) {
        WaitForSingleObject(hWriter, INFINITE);
        CloseHandle(hWriter);
        hWriter = NULL;
    }

cleanup:
    if (in_flight_sem) { CloseHandle(in_flight_sem); in_flight_sem = NULL; }
    if (results) {
        // Any remaining streams (unlikely) must be destroyed
        for (size_t i = next_write_index; i < results_capacity; ++i) {
            if (results[i]) {
                stream_destroy(results[i]);
                results[i] = NULL;
            }
        }
        free(results);
        results = NULL;
        results_capacity = 0;
    }

    DeleteCriticalSection(&queue_cs);
    DeleteCriticalSection(&results_cs);
    DeleteCriticalSection(&stats_cs);

    fclose(fin);
    fflush(fout);
    fclose(fout);

    printf("Processing complete. Processed %u blocks.\n", block_index);
    return 0;
}
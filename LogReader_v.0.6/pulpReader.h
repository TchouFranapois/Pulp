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
 *
 * Full license texts are located in the LICENSES/ directory at the root of this
 * repository. See COMMERCIAL.md for licensing options and contact information.
 */

#pragma once
#ifndef PULP_READER_H
#define PULP_READER_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* -----------------------------------------------------------------------
 *  Configuration switches – change only if the writer format changes
 * ----------------------------------------------------------------------- */
#define USE_OUTER_CANARY   0          /* set to 1 if you add outer canaries */
#define OUTER_CANARY_BEGIN 0xA1B2C3D4E5F60708ULL
#define OUTER_CANARY_END   0x08070605E4D3C2B1ULL

 /* Expected inner canaries – must match the writer’s DICT_BEGIN / DICT_END */
#define DICT_BEGIN 0xDEADBEEFCAFEBABEULL
#define DICT_END   0xBEEFBABEDEADCAFEULL

#define WINDOWS_TO_UNIX_EPOCH_SECS 11644473600ULL

/* Configuration */
#ifndef MAX_CONCURRENT_BLOCKS
#define MAX_CONCURRENT_BLOCKS 4
#endif
#ifndef STREAM_CHUNK_SIZE
#define STREAM_CHUNK_SIZE (1u << 26) // 64 MiB per chunk
#endif

/* -----------------------------------------------------------------------
 *  Structures – must be kept in sync with the writer’s definitions
 * ----------------------------------------------------------------------- */


typedef struct {
    uint32_t compSize;
    uint32_t decompSize;
} BlockHeader;

typedef struct {
    uint32_t index;
    char* str;
} DictEntry;

typedef struct {
    DictEntry* urls;
    uint32_t url_count;
    DictEntry* ips;
    uint32_t ips_count;
} Dicts;

/* Writer thread argument */
typedef struct {
    FILE* fout;
} WriterArg;

/* HTTP verbs */
static const char* HTTP_VERB_NAMES[] = {
   "UNKNOWN",             //  0: HTTP_VERB_UNKNOWN
    "GET",                 //  1: HTTP_VERB_GET
    "POST",                //  2: HTTP_VERB_POST
    "PUT",                 //  3: HTTP_VERB_PUT
    "PATCH",               //  4: HTTP_VERB_PATCH
    "HEAD",                //  5: HTTP_VERB_HEAD
    "OPTIONS",             //  6: HTTP_VERB_OPTIONS
    "TRACE",               //  7: HTTP_VERB_TRACE
    "DELETE",              //  8: HTTP_VERB_DELETE
    "CONNECT",             //  9: HTTP_VERB_CONNECT
    "PRI",                 // 10: HTTP_VERB_PRI
    "ACL",                 // 11: HTTP_VERB_ACL
    "COPY",                // 12: HTTP_VERB_COPY
    "LOCK",                // 13: HTTP_VERB_LOCK
    "LINK",                // 14: HTTP_VERB_LINK
    "MOVE",                // 15: HTTP_VERB_MOVE
    "BIND",                // 16: HTTP_VERB_BIND
    "MERGE",               // 17: HTTP_VERB_MERGE
    "MKCOL",               // 18: HTTP_VERB_MKCOL
    "LABEL",               // 19: HTTP_VERB_LABEL
    "REPORT",              // 20: HTTP_VERB_REPORT
    "SEARCH",              // 21: HTTP_VERB_SEARCH
    "UNBIND",              // 22: HTTP_VERB_UNBIND
    "UNLINK",              // 23: HTTP_VERB_UNLINK
    "UNLOCK",              // 24: HTTP_VERB_UNLOCK
    "UPDATE",              // 25: HTTP_VERB_UPDATE
    "CHECKIN",             // 26: HTTP_VERB_CHECKIN
    "PROPFIND",            // 27: HTTP_VERB_PROPFIND
    "CHECKOUT",            // 28: HTTP_VERB_CHECKOUT
    "PROPPATCH",           // 29: HTTP_VERB_PROPPATCH
    "MKACTIVITY",          // 30: HTTP_VERB_MKACTIVITY
    "MKCALENDAR",          // 31: HTTP_VERB_MKCALENDAR
    "ORDERPATCH",          // 32: HTTP_VERB_ORDERPATCH
    "UNCHECKOUT",          // 33: HTTP_VERB_UNCHECKOUT
    "MKWORKSPACE",         // 34: HTTP_VERB_MKWORKSPACE
    "MKREDIRECTREF",       // 35: HTTP_VERB_MKREDIRECTREF
    "VERSION-CONTROL",     // 36: HTTP_VERB_VERSION_CONTROL
    "BASELINE-CONTROL",    // 37: HTTP_VERB_BASELINE_CONTROL
    "UPDATE-REDIRECT-REF"  // 38: HTTP_VERB_UPDATEREDIRECTREF
};

/* Streaming output types */
typedef struct OutChunk {
    char* data;
    size_t size;
    struct OutChunk* next;
} OutChunk;

typedef struct BlockStream {
    CRITICAL_SECTION cs;
    CONDITION_VARIABLE cv;
    OutChunk* head;
    OutChunk* tail;
    int done;
    int failed;
    size_t total_bytes;
} BlockStream;

typedef struct Task {
    uint32_t block_index;
    uint8_t* comp_data;
    uint32_t comp_size;
    uint32_t decomp_size;
    BlockStream* stream;
    struct Task* next;
} Task;

 /* The exact layout of a serialized log entry (see logger.c) */
typedef struct __declspec(align(32)) {
    uint64_t sequence;
    uint64_t timestamp;
    uint32_t url_idx;
    uint32_t ip_idx;
    uint16_t http_code;
    uint16_t duration_ms;
    uint16_t response_size;
    uint8_t  http_verb;
    uint8_t  flags;
} SerializedEntry;



/* Dictionary entry – the writer stores the raw string (including the terminating NUL) */
typedef struct {
    uint32_t id;          /* the index used in SerializedEntry */
    uint16_t len;         /* length of the string INCLUDING the NUL */
    char    data[];       /* flexible array member – actual bytes follow */
} DictItem;

/* Helper containers used by the reader */
typedef struct {
    uint32_t count;
    DictItem** items;     /* array of pointers, indexed by id (sparse) */
} DictTable;

/* -----------------------------------------------------------------------
 *  Public API
 * ----------------------------------------------------------------------- */
typedef struct {
    const uint8_t* payload;   /* pointer to the whole buffer read from disk/network */
    size_t         payload_sz;

    /* Parsed sections */
    const SerializedEntry* entries;
    size_t                 entry_cnt;

    DictTable url_dict;
    DictTable ip_dict;
} LogPayload;

#endif 
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
#include <Windows.h>

// STAT
#define PULP_STAT_ACTIVE  1
#if PULP_STAT_ACTIVE
#define LOG_STAT(fmt, ...) printf("[STAT] " fmt "\n", ##__VA_ARGS__)
#else
#define LOG_STAT(fmt, ...)
#endif

#if PULP_STAT_ACTIVE
typedef struct _stats {
	__declspec(align(64)) uint64_t L1_url_cache_hit;
	__declspec(align(64)) uint64_t L2_url_cache_hit;
	__declspec(align(64)) uint64_t L3_url_cache_hit;
	__declspec(align(64)) uint64_t L1_ip_cache_hit;
	__declspec(align(64)) uint64_t L2_ip_cache_hit;
	__declspec(align(64)) uint64_t L3_ip_cache_hit;
	__declspec(align(64)) uint64_t url_cache_probes_total;
	__declspec(align(64)) uint64_t ip_cache_probes_total;
	__declspec(align(64)) uint64_t url_cache_probes_max;
	__declspec(align(64)) uint64_t ip_cache_probes_max;
	__declspec(align(64)) uint64_t url_cache_fullprobescan_total;
	__declspec(align(64)) uint64_t ip_cache_fullprobescan_total;

	__declspec(align(64)) uint64_t log_processed_total;
	__declspec(align(64)) uint64_t batch_flushed_total;
	__declspec(align(64)) uint64_t batch_compressed_total;
	__declspec(align(64)) uint64_t batch_written_total;
	__declspec(align(64)) uint64_t writer_waitfile_max;
	__declspec(align(64)) uint64_t backpressure_count;
	__declspec(align(64)) uint64_t lost_logs_total;
	__declspec(align(64)) uint64_t log_rotation;
	__declspec(align(64)) uint64_t log_refused_total;
	__declspec(align(64)) uint64_t rotation_resync_total;

	__declspec(align(64)) uint64_t compression_lz4_ratio_avg;
	__declspec(align(64)) uint64_t compression_failure_total;
} _stats;

// Global instance declaration
extern _stats g_stats;

// Atomic increment macros
// 64-bit macros
#define STATS_INC64(field)      InterlockedIncrement64((LONG64*)&g_stats.field)
#define STATS_ADD64(field, val) InterlockedAdd64((LONG64*)&g_stats.field, (LONGLONG)(val))
#define STATS_MAX(field, val) do { \
        LONG64 current, new_val = (LONG64)(val); \
        do { \
            current = g_stats.field; \
            if (new_val <= current) break; \
        } while (InterlockedCompareExchange64((LONG64*)&g_stats.field, new_val, current) != current); \
} while (0)
#else
#define STATS_INC64(field)      ((void)0)
#define STATS_ADD64(field, val) ((void)0)
#define STATS_MAX(field, val)   ((void)0)
#endif


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
#include "pch.h"

BOOL APIENTRY DllMain( HMODULE hModule,
                       DWORD  ul_reason_for_call,
                       LPVOID lpReserved
                     )
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
    case DLL_PROCESS_DETACH:
        break;
    }
    return TRUE;
}


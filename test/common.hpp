/*
 * Copyright (c) 2025 averne <averne381@gmail.com>
 *
 * This file is part of Envideo.

 * Envideo is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License,
 * or (at your option) any later version.

 * Envideo is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License
 * along with Envideo. If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

#include <cstdint>

#ifdef __SWITCH__

#include <switch.h>

extern "C" {

std::uint32_t __nx_nv_service_type;

void userAppInit(void) {
    // We need higher permissions to access NVJPG
    __nx_nv_service_type = NvServiceType_Factory;

    socketInitializeDefault();
    nxlinkStdio();
}

void userAppExit(void) {
    socketExit();
}

} // extern "C"

#endif

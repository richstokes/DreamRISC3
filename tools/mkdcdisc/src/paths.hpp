/* mkdcdisc - Sega Dreamcast Disc Image Generator
 *
 * SPDX-License-Identifier: MIT
 * Copyright (C) 2022-2026 Simulant and contributors
 * (https://www.simulant.dev/) */

#pragma once

/* std::filesystem::path::c_str() returns wchar_t* on Windows, so a narrow
 * string has to be materialised first. The result is only valid for the
 * enclosing expression, which is all any caller here needs. */
#ifdef _WIN32
#define PATH_TO_CSTR(p) (p).string().c_str()
#else
#define PATH_TO_CSTR(p) (p).c_str()
#endif

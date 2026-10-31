/* mkdcdisc - Sega Dreamcast Disc Image Generator
 *
 * SPDX-License-Identifier: MIT
 * Copyright (C) 2022-2026 Simulant and contributors
 * (https://www.simulant.dev/) */

#ifndef _WINPATCH_FNMATCH_H
#define _WINPATCH_FNMATCH_H

#include <string.h>
#include <ctype.h>

// fnmatch flags
#define FNM_PATHNAME    (1 << 0)  // Slash must be matched by slash
#define FNM_NOESCAPE    (1 << 1)  // Disable backslash escaping
#define FNM_PERIOD      (1 << 2)  // Period must be matched explicitly
#define FNM_FILE_NAME   FNM_PATHNAME
#define FNM_LEADING_DIR (1 << 3)  // Ignore /... after Imatch
#define FNM_CASEFOLD    (1 << 4)  // Case insensitive search

// Return values
#define FNM_NOMATCH     1

// Simple fnmatch implementation for Windows
static inline
int fnmatch(const char *pattern, const char *string, int flags) {
    const char *p = pattern;
    const char *s = string;
    
    while (*p) {
        switch (*p) {
            case '*':
                // Skip consecutive asterisks
                while (*p == '*') p++;
                
                // If pattern ends with *, it matches
                if (*p == '\0') return 0;
                
                // Try to match the rest of pattern with each suffix of string
                while (*s) {
                    if (fnmatch(p, s, flags) == 0) return 0;
                    
                    // Handle FNM_PATHNAME - don't let * cross directory separators
                    if ((flags & FNM_PATHNAME) && (*s == '/' || *s == '\\')) {
                        break;
                    }
                    s++;
                }
                return FNM_NOMATCH;
                
            case '?':
                if (*s == '\0') return FNM_NOMATCH;
                
                // Handle FNM_PATHNAME - ? shouldn't match path separators
                if ((flags & FNM_PATHNAME) && (*s == '/' || *s == '\\')) {
                    return FNM_NOMATCH;
                }
                
                // Handle FNM_PERIOD - ? shouldn't match leading period
                if ((flags & FNM_PERIOD) && *s == '.' && 
                    (s == string || ((flags & FNM_PATHNAME) && (s[-1] == '/' || s[-1] == '\\')))) {
                    return FNM_NOMATCH;
                }
                
                p++;
                s++;
                break;
                
            case '[': {
                // Character class matching [abc], [a-z], [!abc]
                int negate = 0;
                int matched = 0;
                
                if (*s == '\0') return FNM_NOMATCH;
                
                p++; // skip '['
                if (*p == '!' || *p == '^') {
                    negate = 1;
                    p++;
                }
                
                while (*p && *p != ']') {
                    char c1 = *p++;
                    char c2 = c1;
                    
                    // Handle range like a-z
                    if (*p == '-' && p[1] && p[1] != ']') {
                        p++; // skip '-'
                        c2 = *p++;
                    }
                    
                    // Case folding if requested
                    char sc = *s;
                    if (flags & FNM_CASEFOLD) {
                        c1 = tolower(c1);
                        c2 = tolower(c2);
                        sc = tolower(sc);
                    }
                    
                    if (sc >= c1 && sc <= c2) {
                        matched = 1;
                    }
                }
                
                if (*p == ']') p++; // skip closing ']'
                
                if (matched == negate) return FNM_NOMATCH;
                s++;
                break;
            }
            
            case '\\':
                // Handle escape sequences (unless FNM_NOESCAPE is set)
                if (!(flags & FNM_NOESCAPE) && p[1]) {
                    p++; // skip backslash, use next char literally
                }
                // Fall through to literal match
                
            default: {
                // Literal character match
                char pc = *p;
                char sc = *s;
                
                // Case folding if requested
                if (flags & FNM_CASEFOLD) {
                    pc = tolower(pc);
                    sc = tolower(sc);
                }
                
                // On Windows, treat / and \ as equivalent in paths
                if ((flags & FNM_PATHNAME) && 
                    ((pc == '/' && sc == '\\') || (pc == '\\' && sc == '/'))) {
                    // They match
                } else if (pc != sc) {
                    return FNM_NOMATCH;
                }
                
                p++;
                s++;
                break;
            }
        }
    }
    
    // Pattern consumed, check if string is also consumed
    return (*s == '\0') ? 0 : FNM_NOMATCH;
}

// Simplified version for basic use cases
static inline
int fnmatch_simple(const char *pattern, const char *string) {
    return fnmatch(pattern, string, 0);
}

#endif /* _WINPATCH_FNMATCH_H */

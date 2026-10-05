/*
 * SPDX-License-Identifier: AGPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Gregor B. Rosenauer & Claude, and the authors of BePDF
 */
#ifndef _TRACE_H
#define _TRACE_H

#include <stdio.h>

#if defined(TRACE_LEVEL) && TRACE_LEVEL > 0
#define TRACE(level, args) { \
	printf args; \
	fflush(stdout); \
}
#else
#define TRACE(level, args)
#endif

#endif

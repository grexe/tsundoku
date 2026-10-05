/*
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Tsundoku: a universal document reader for Haiku, extended for SEN.
 * 	 Copyright (C) 2026 Gregor B. Rosenauer & Claude
 *
 * Based on BePDF:
 * 	 Copyright (C) 1997 Benoit Triquet.
 * 	 Copyright (C) 1998-2000 Hubert Figuiere.
 * 	 Copyright (C) 2000-2011 Michael Pfeiffer.
 * 	 Copyright (C) 2013 waddlesplash.
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU Affero
 * General Public License as published by the Free Software Foundation, either version 3 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the
 * implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public
 * License for more details.
 */

#ifndef _DJVU_DOCUMENT_H_
#define _DJVU_DOCUMENT_H_

#include <mupdf/fitz.h>

// DjVu (https://djvu.org) for MuPDF: a document in the sense of MuPDF's document API, made of DjVuLibre (ddjvuapi), so that
// everything that works with a document (rendering, the text layer for search and selection, outline, links, deep links,
// printing) works with DjVu files as with any other fixed-layout document.
namespace Djvu {

// Whether the name or the first bytes of the file say it is a DjVu file ("AT&TFORM" with DJVU or DJVM at offset 12).
bool IsDjvuFile(const char* path);

// Opens a DjVu file (bundled or indirect, with an index and files next to it). Throws if DjVuLibre cannot read it.
fz_document* Open(fz_context* context, const char* path);

}	// namespace Djvu

#endif

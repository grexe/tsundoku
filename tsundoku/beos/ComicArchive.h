/*
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

#ifndef _COMIC_ARCHIVE_H_
#define _COMIC_ARCHIVE_H_

#include <mupdf/fitz.h>

// Comic book archives (CBZ, CBR, CB7, CBT): MuPDF reads the pages (an archive of images) and reads ZIP and TAR on
// its own. RAR and 7z come from libarchive, which this adds to MuPDF as an archive handler, and the archive is
// shown to MuPDF without what file managers put into it (__MACOSX, ._*, .DS_Store), which would be pages otherwise.
namespace ComicArchive {

// Adds the libarchive handler (RAR, 7z) to the archive formats of the context. Once for each context.
void RegisterHandlers(fz_context* context);

// Whether the name of the file says it is a comic book archive.
bool IsComicFile(const char* path);

// Opens the archive of a comic book without the junk. Throws if the file is no archive.
fz_archive* Open(fz_context* context, const char* path);

// Whether the name of an entry is junk of a file manager (__MACOSX, ._*, .DS_Store, Thumbs.db, desktop.ini).
bool IsJunk(const char* name);

}	// namespace ComicArchive

#endif

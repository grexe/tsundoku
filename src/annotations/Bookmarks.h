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


#ifndef _BOOKMARKS_H_
#define _BOOKMARKS_H_

class BMessage;

// The bookmarks of the reader are annotations with the motivation oa:bookmarking (the label is the body, the target the
// page, or in a book the place in the text), kept with the other annotations in the attribute SEN:annotations of the file.
// They are written when the window of the file is closed; what writes the other annotations of the file leaves them as
// they are (Carry).
namespace Bookmarks {

// The bookmarks of the file in the form that the list of the sidebar uses: "l" is the label, "p" the page (1-based) and "a"
// the anchor of a place in a book (empty if the bookmark is of a page), one entry of each for a bookmark.
void Read(const char* path, BMessage* bookmarks);

// The bookmarks that BePDF kept in the attribute bepdf:bookmarks, in the same form (they are written as annotations when the
// window is closed).
void ReadLegacy(const char* path, BMessage* bookmarks);

// Replaces the bookmarks of the file by these (the other annotations are left as they are).
bool Write(const char* path, const BMessage& bookmarks);

// Whether an annotation of the attribute is a bookmark
bool IsBookmark(const BMessage& annotation);

// Adds the bookmarks of the attribute of the file at path to an archive of annotations that is about to replace it; true
// if there were any.
bool Carry(const char* path, BMessage* archive);

}	// namespace Bookmarks

#endif

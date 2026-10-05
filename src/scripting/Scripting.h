/*
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Toji: a universal document reader for Haiku, extended for SEN.
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


#ifndef _SCRIPTING_H_
#define _SCRIPTING_H_

#include <Handler.h>
#include <String.h>

#include <vector>

class BMessage;
class PDFWindow;

// The scripting suite of Toji, "suite/vnd.sen-labs.Toji", on the window (`hey Toji get Path of Document of
// Window 0`). The objects are the document, its pages, its annotations and its bookmarks:
//
//   Document         Path, Title, Type, PageCount, TextSize, Page (also SET: goes to the page), Selection
//                    do Goto (page), Save, Undo, Redo, UpgradeAnnotations
//   Page N           Text, Size, do Goto
//   Annotation ...   by index, reverse index or name (the identifier): Id, Kind, Page, Text, Quote, Color, Bounds, Author,
//                    JSON (the Web Annotation); SET Text, Color, Bounds, Quote (the mark moves to other words, it keeps
//                    its identifier); do Goto; DELETE; COUNT and GET of all of them; CREATE (hey cannot nest the
//                    specifiers of a CREATE, so `do AddAnnotation of Document of Window 0 with kind=highlight and
//                    quote=...` does the same)
//   Bookmark ...     by index, reverse index or name (the label): Label, Page; do Goto; DELETE; COUNT, GET, CREATE
//                    (do AddBookmark with label and page)
//
// The annotations and bookmarks that are counted, listed or addressed by index are those of the whole document
// (context=document, the default) or of one page (context=page, and page=N, the page that is shown if it is not given):
// `hey Toji count Annotation of Document of Window 0 with context=page and page=3`.
// What is changed goes through the same code as what the user does, so it can be undone and counts as unsaved.
namespace Scripting {

extern const char* const kSuite;

// The document of the window, as the object that the specifiers lead to; the window makes it once.
class DocumentHandler : public BHandler {
public:
								DocumentHandler(PDFWindow* window);
	virtual						~DocumentHandler();

	virtual status_t			GetSupportedSuites(BMessage* data);
	virtual BHandler*			ResolveSpecifier(BMessage* message, int32 index, BMessage* specifier, int32 what,
									const char* property);
	virtual void				MessageReceived(BMessage* message);

private:
	friend class AnnotationHandler;
	friend class BookmarkHandler;
	friend class PageHandler;

	PDFWindow*					fWindow;
	BHandler*					fAnnotation;
	BHandler*					fBookmark;
	BHandler*					fPage;
};

// The window's own part: that it has a Document (its suite), and whether a specifier is the one that leads to it.
status_t AddWindowSuite(BMessage* data);
bool IsDocumentSpecifier(BMessage* message, int32 index, BMessage* specifier, int32 what, const char* property);

}	// namespace Scripting

#endif

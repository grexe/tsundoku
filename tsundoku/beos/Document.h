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

#ifndef _DOCUMENT_H_
#define _DOCUMENT_H_

#include <vector>

#include <Locker.h>
#include <String.h>

extern "C" {
#include <mupdf/fitz.h>
}

// A link on a page. The rectangle is in page space, see Document::PageMatrix().
struct DocLink {
	fz_rect rect;
	BString uri;
};

// One entry of the table of contents.
struct DocOutlineEntry {
	BString title;
	int     level;       // 0 is the top level
	bool    open;        // should be shown expanded
	int     page;        // 1-based, 0 if the entry does not point into the document
	float   x, y;        // position on the page, valid if hasPosition
	bool    hasPosition;
	bool    bold, italic;
	bool    hasColor;
	uint8   red, green, blue;
	BString uri;         // for entries that point somewhere else
};

// A file embedded in the document (PDF attachments).
struct DocAttachment {
	BString name;
	BString mimeType;
	int64   size;        // -1 if unknown
	time_t  modified;    // -1 if unknown
};

// A document read by MuPDF (PDF, XPS, CBZ, images, ...).
//
// MuPDF contexts must not be used by two threads at the same time. All threads share the context of the
// document and take Lock() while they use it (BLocker is recursive). The public methods of this class lock
// by themselves, code that uses Context() and Doc() directly must hold Lock().
class Document {
public:
	enum OpenResult {
		kOpened,
		kNeedsPassword,   // password missing or wrong
		kFailed
	};

	// Opens the file. *document is only set if the result is kOpened.
	static OpenResult Open(const char* path, const char* password, Document** document);

	~Document();

	fz_context*  Context() const { return fContext; }
	fz_document* Doc() const { return fDocument; }
	BLocker*     Lock() { return &fLock; }

	const char*  Path() const { return fPath.String(); }
	int          PageCount() const { return fPageCount; }
	bool         IsPDF() const { return fIsPDF; }
	bool         IsEncrypted() const { return fEncrypted; }

	bool         CanPrint();
	bool         CanCopy();
	bool         CanEdit();
	bool         CanAnnotate();

	// 1-based page numbers everywhere.
	// The bounds are in points with the rotation of the page itself applied, the origin may not be 0,0.
	bool         PageBounds(int page, fz_rect* bounds);

	// The matrix that maps page space to pixels of the bitmap that holds the page at the given resolution
	// (72 dpi is 100%) and with the given additional rotation (multiple of 90 degrees). It includes the
	// translation that moves the page to the origin of the bitmap, width and height are the size of the bitmap.
	bool         PageMatrix(int page, float dpi, int rotation, fz_matrix* matrix, int* width, int* height);

	BString      Metadata(const char* key);
	BString      Format();
	BString      PageLabel(int page);

	bool         LoadOutline(std::vector<DocOutlineEntry>& entries);

	// The files embedded in the document (PDF only). The index in the list is what SaveAttachment() takes.
	bool         LoadAttachments(std::vector<DocAttachment>& attachments);
	bool         SaveAttachment(int index, const char* path);

	// For internal links: the page (1-based) and the position (page space, may be NaN) the link goes to.
	bool         ResolveLink(const char* uri, int* page, float* x, float* y);
	bool         IsExternalLink(const char* uri);

private:
	Document(fz_context* context, fz_document* document, const char* path);

	fz_context*     fContext;
	fz_document*    fDocument;
	BString         fPath;
	BLocker         fLock;
	int             fPageCount;
	bool            fIsPDF;
	bool            fEncrypted;
	std::vector<fz_rect> fBounds;   // cache, empty rectangle if unknown
	std::vector<bool>    fBoundsKnown;
};

// Holds the lock of a document in a scope.
class DocumentLocker {
public:
	DocumentLocker(Document* document) : fLock(document->Lock()) { fLock->Lock(); }
	~DocumentLocker() { fLock->Unlock(); }

private:
	BLocker* fLock;
};

#endif

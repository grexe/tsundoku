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

// The kinds of markup that can be added to a selection of text.
enum MarkupType {
	kMarkupHighlight,
	kMarkupUnderline,
	kMarkupStrikeOut,
	kMarkupSquiggly
};

// The shapes that can be drawn on a page.
enum ShapeType {
	kShapeRectangle,
	kShapeEllipse,
	kShapeLine,
	kShapeArrow       // a line with an arrow at its end
};

// What kind of annotation it is, as far as editing it on the page is concerned.
enum AnnotationKind {
	kAnnotOther,
	kAnnotMarkup,       // on text: highlight, underline, strike out, squiggly
	kAnnotNote,         // an icon
	kAnnotText,         // free text
	kAnnotRectangle,
	kAnnotEllipse,
	kAnnotLine,         // also arrows
	kAnnotInk
};

// An annotation of a PDF page (not links, popups and form fields). Rectangle and quads are in page space.
struct DocAnnotation {
	int     type;         // pdf_annot_type
	int     index;        // among the annotations of the page that are listed, to change or delete it
	BString label;        // what to call its kind: Highlight, Note, Rectangle, ...
	BString id;           // the name that stays with the annotation (the /NM of the PDF), empty if it has none
	fz_rect rect;
	std::vector<fz_quad> quads;   // for markup, the lines it covers
	BString contents;
	BString author;
	bool    isMarkup;
	int     kind;         // AnnotationKind
	std::vector<std::vector<fz_point> > paths;   // of a line (one path) and of a drawing (the strokes)
	bool    isFreeText;   // the contents are text on the page, not a note
	bool    hasColor;
	uint32  color;        // 0xRRGGBB, if hasColor
};

// An annotation with the page it is on, for a list of all of them.
struct DocAnnotationEntry {
	int           page;           // 1-based
	DocAnnotation annotation;
	BString       excerpt;        // the text a mark covers, or the note
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

	// Annotations (PDF only). The page is 1-based, the index is DocAnnotation::index.
	// LoadAnnotations() needs the page to be loaded and the lock to be held.
	bool         CanEditAnnotations();
	bool         LoadAnnotations(fz_page* page, std::vector<DocAnnotation>& annotations);
	bool         AddMarkup(int page, MarkupType type, const fz_quad* quads, int count, const float color[3]);
	bool         DeleteAnnotation(int page, int index);
	bool         SetAnnotationContents(int page, int index, const char* text);
	bool         SetAnnotationColor(int page, int index, uint32 rgb);
	// All annotations of the document in the order of the pages. Takes the lock page by page, so it can run beside
	// the rendering; stops early when *cancel becomes true.
	bool         ListAnnotations(std::vector<DocAnnotationEntry>& entries, const volatile bool* cancel);
	// the same for one page (1-based), quick: for the list after a change on that page
	bool         ListAnnotationsOnPage(int page, std::vector<DocAnnotationEntry>& entries);
	// Where the annotation with this id is (page 1-based, index as in DocAnnotation); scans the document.
	bool         FindAnnotationById(const char* id, int* page, int* index);
	// Moves or resizes: the annotation is fitted into the new bounds (a note only follows the corner).
	bool         SetAnnotationBounds(int page, int index, fz_rect bounds, bool resize);

	// New annotations that are not tied to text. Positions are in page space, colors 0xRRGGBB.
	bool         AddNote(int page, fz_point where, const char* text);
	bool         AddFreeText(int page, fz_point where, const char* text);
	bool         AddShape(int page, ShapeType type, fz_point from, fz_point to, uint32 rgb);
	bool         AddInk(int page, const fz_point* points, int count, uint32 rgb);

	// Saving adds the changes to the end of the file (so that its attributes and the rest stay as they are).
	// Undo and redo of the edits above. They return the page (1-based) that changed, 0 if there was nothing to do.
	// The history is not kept over a save.
	bool         CanUndo() const { return fHistoryPosition > 0; }
	bool         CanRedo() const { return fHistoryPosition < (int)fHistory.size(); }
	BString      UndoLabel() const;
	BString      RedoLabel() const;
	int          Undo();
	int          Redo();

	bool         HasUnsavedChanges();
	bool         CanSave();
	// false if the file is on a read-only volume or not writable for the user
	bool         IsWritable() const { return fWritable; }
	// the changes can be added to the file itself: it is writable and MuPDF can do it incrementally
	bool         CanSaveInPlace() const { return fCanSave && fWritable; }
	bool         Save();
	// Writes the whole document, with the changes, to another file (copies the attributes of the original).
	// Marks the document as saved.
	bool         SaveCopy(const char* path);

	// For internal links: the page (1-based) and the position (page space, may be NaN) the link goes to.
	bool         ResolveLink(const char* uri, int* page, float* x, float* y);
	bool         IsExternalLink(const char* uri);

private:
	Document(fz_context* context, fz_document* document, const char* path);
	void ListPage(int pageNo, std::vector<DocAnnotationEntry>& entries);

	struct HistoryEntry {
		BString name;
		int     page;
	};
	void RecordOperation(int page, const char* name);
	void ForgetHistory();

	fz_context*     fContext;
	fz_document*    fDocument;
	BString         fPath;
	BLocker         fLock;
	int             fPageCount;
	bool            fIsPDF;
	bool            fEncrypted;
	bool            fCanSave;      // changes can be added to the file
	bool            fWritable;     // the file can be written
	volatile bool   fModified;     // changed since opened or saved
	std::vector<HistoryEntry> fHistory;   // the edits that can be undone, the first one is the oldest
	int             fHistoryPosition;     // how many of them are done, the others can be redone
	int             fSavedPosition;       // where the file was saved
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

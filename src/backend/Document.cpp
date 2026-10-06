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

#include "Document.h"
#include <map>
#include "WebAnnotation.h"
#include "Globals.h"

#include "BbfInfo.h"
#include "ComicArchive.h"
#include "ComicInfo.h"
#include "DjvuDocument.h"
#include "EpubInfo.h"
#include "Mobi.h"

#include <math.h>
#include <time.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <Directory.h>
#include <Entry.h>
#include <File.h>
#include <FindDirectory.h>
#include <OS.h>
#include <Path.h>
#include <Catalog.h>
#include <string>
#include <unistd.h>
#include <Node.h>
#include <TypeConstants.h>
#include <fs_attr.h>

extern "C" {
#include <mupdf/pdf.h>
}

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "Document"

// Note: fz_try() uses setjmp()/longjmp(), so no C++ objects with destructors may be created or destroyed inside
// of a fz_try() block. The code below keeps to plain C types in there.

// MuPDF only allows to clone a context for another thread if it was created with lock functions.
static pthread_mutex_t sMutexes[FZ_LOCK_MAX];
static pthread_once_t sMutexesInitialized = PTHREAD_ONCE_INIT;

static void
InitMutexes()
{
	for (int i = 0; i < FZ_LOCK_MAX; i++)
		pthread_mutex_init(&sMutexes[i], NULL);
}

static void LockFunction(void*, int index) { pthread_mutex_lock(&sMutexes[index]); }
static void UnlockFunction(void*, int index) { pthread_mutex_unlock(&sMutexes[index]); }

static fz_locks_context sLocks = { NULL, LockFunction, UnlockFunction };

static const size_t kStoreSize = 128 * 1024 * 1024;


// MuPDF warns about every EPUB 3 ("unknown epub version: 3.0") though it reads them; that is no news for the
// error window, which opens by itself when something is printed.
static void
WarningCallback(void*, const char* message)
{
	if (strncmp(message, "unknown epub version", 20) == 0)
		return;
	fprintf(stderr, "warning: %s\n", message);
}


static void
LogError(fz_context* context, const char* what)
{
	fprintf(stderr, "Toji: %s: %s\n", what, fz_caught_message(context));
}


// A zipped FictionBook (.fbz, .fb2.zip) is the first .fb2 file of the archive, which MuPDF reads once it is
// taken out.
static bool
IsZippedFictionBook(const char* path)
{
	size_t length = strlen(path);
	return (length > 4 && strcasecmp(path + length - 4, ".fbz") == 0)
		|| (length > 8 && strcasecmp(path + length - 8, ".fb2.zip") == 0);
}


static fz_document*
OpenZippedFictionBook(fz_context* context, const char* path)
{
	fz_archive* archive = fz_open_archive(context, path);
	fz_buffer* buffer = NULL;
	fz_document* document = NULL;
	fz_var(buffer);
	fz_var(document);
	fz_try(context) {
		const char* name = NULL;
		int count = fz_count_archive_entries(context, archive);
		for (int i = 0; i < count && name == NULL; i++) {
			const char* entry = fz_list_archive_entry(context, archive, i);
			size_t length = entry != NULL ? strlen(entry) : 0;
			if (length > 4 && strcasecmp(entry + length - 4, ".fb2") == 0)
				name = entry;
		}
		if (name == NULL)
			fz_throw(context, FZ_ERROR_FORMAT, "no FictionBook in the archive");
		buffer = fz_read_archive_entry(context, archive, name);
		document = fz_open_document_with_buffer(context, name, buffer);
	}
	fz_always(context) {
		fz_drop_buffer(context, buffer);
		fz_drop_archive(context, archive);
	}
	fz_catch(context) {
		fz_rethrow(context);
	}
	return document;
}


// A Mobipocket book is made into an EPUB in a file of the temporary directory for the time that it is open: MuPDF reads that. 0 if the
// file is not such a book, 1 if the EPUB is made, -1 if it cannot be read (it has DRM, or a kind of compression that is not read).
static int
MakeMobiEpub(const char* path, BString* epubPath)
{
	unsigned char head[68];
	BFile in(path, B_READ_ONLY);
	if (in.InitCheck() != B_OK || in.Read(head, sizeof(head)) != (ssize_t)sizeof(head)
		|| !Mobi::LooksLikeMobi(head, sizeof(head)))
		return 0;
	off_t size = 0;
	in.GetSize(&size);
	if (size <= 0 || size > 512 * 1024 * 1024)
		return -1;
	std::string data((size_t)size, '\0');
	if (in.ReadAt(0, &data[0], (size_t)size) != (ssize_t)size)
		return -1;
	Mobi::Book book;
	switch (Mobi::Read((const unsigned char*)data.data(), data.size(), &book)) {
		case Mobi::kOk:
			break;
		case Mobi::kEncrypted:
			fprintf(stderr, "%s: this book is protected (DRM) and cannot be opened\n", path);
			return -1;
		case Mobi::kUnsupported:
			fprintf(stderr, "%s: this kind of Mobipocket book (Huffman compression or the new format) is not read yet\n", path);
			return -1;
		default:
			fprintf(stderr, "%s: not a Mobipocket book that can be read\n", path);
			return -1;
	}
	std::string epub = Mobi::MakeEpub(book);
	BPath temporary;
	if (find_directory(B_SYSTEM_TEMP_DIRECTORY, &temporary) != B_OK)
		return -1;
	// what an earlier session left (it ended without closing the book) is removed
	{
		BDirectory directory(temporary.Path());
		BEntry entry;
		time_t now = time(NULL);
		while (directory.GetNextEntry(&entry) == B_OK) {
			char leaf[B_FILE_NAME_LENGTH];
			time_t modified;
			if (entry.GetName(leaf) == B_OK && strncmp(leaf, "toji-mobi-", 10) == 0 && entry.GetModificationTime(&modified) == B_OK
				&& now - modified > 6 * 3600)
				entry.Remove();
		}
	}
	char name[96];
	static int32 counter = 0;
	snprintf(name, sizeof(name), "toji-mobi-%d-%d-%d.epub", (int)getpid(), (int)atomic_add(&counter, 1), (int)(real_time_clock() & 0xFFFF));
	temporary.Append(name);
	BFile out(temporary.Path(), B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
	if (out.InitCheck() != B_OK || out.Write(epub.data(), epub.size()) != (ssize_t)epub.size())
		return -1;
	*epubPath = temporary.Path();
	return 1;
}


Document::OpenResult
Document::Open(const char* path, const char* password, Document** _document, float textSize)
{
	pthread_once(&sMutexesInitialized, InitMutexes);

	fz_context* context = fz_new_context(NULL, &sLocks, kStoreSize);
	if (context == NULL)
		return kFailed;

	fz_set_warning_callback(context, WarningCallback, NULL);
	fz_document* document = NULL;
	bool needsPassword = false;
	bool failed = false;

	// the metadata of a comic book comes out of its archive
	ComicInfo* comic = NULL;
	BbfInfo* bbf = NULL;
	bool isComic = ComicArchive::IsComicFile(path);
	BString contentPath;
	int mobi = isComic ? 0 : MakeMobiEpub(path, &contentPath);

	fz_var(document);
	fz_var(comic);
	fz_var(bbf);
	fz_try(context) {
		fz_register_document_handlers(context);
		ComicArchive::RegisterHandlers(context);
		if (isComic) {
			// MuPDF reads the pages from the archive without what file managers put into it
			fz_archive* archive = ComicArchive::Open(context, path);
			fz_try(context) {
				document = fz_open_document_with_stream_and_dir(context, path, NULL, archive);
				comic = ComicArchive::ReadComicInfo(context, archive);
				// a file in the Bound Book Format has its own metadata and sections
				bbf = BbfInfo::Read(path);
				if (comic == NULL && bbf != NULL)
					comic = ComicInfo::FromBbf(*bbf);
			}
			fz_always(context) {
				fz_drop_archive(context, archive);
			}
			fz_catch(context) {
				fz_rethrow(context);
			}
		} else if (mobi < 0)
			fz_throw(context, FZ_ERROR_FORMAT, "the Mobipocket book cannot be read");
		else if (mobi > 0)
			document = fz_open_document(context, contentPath.String());
		else if (IsZippedFictionBook(path))
			document = OpenZippedFictionBook(context, path);
		else if (Djvu::IsDjvuFile(path))
			document = Djvu::Open(context, path);
		else
			document = fz_open_document(context, path);
		if (fz_needs_password(context, document)) {
			if (password == NULL || password[0] == '\0'
				|| !fz_authenticate_password(context, document, password))
				needsPassword = true;
		}
	}
	fz_catch(context) {
		LogError(context, "cannot open document");
		failed = true;
	}

	if (failed || needsPassword) {
		if (mobi > 0)
			BEntry(contentPath.String()).Remove();
		delete comic;
		delete bbf;
		fz_drop_document(context, document);
		fz_drop_context(context);
		return failed ? kFailed : kNeedsPassword;
	}

	Document* result = new Document(context, document, path, textSize, mobi > 0 ? contentPath.String() : NULL);
	result->fComic = comic;
	result->fBbf = bbf;
	if (result->fPageCount <= 0) {
		result->Release();
		return kFailed;
	}
	*_document = result;
	return kOpened;
}


Document::Document(fz_context* context, fz_document* document, const char* path, float textSize, const char* contentPath)
	:
	fRefs(1),
	fContext(context),
	fDocument(document),
	fPath(path),
	fLock("document lock"),
	fPageCount(0),
	fIsPDF(false),
	fEncrypted(false),
	fCanSave(false),
	fWritable(false),
	fModified(false),
	fHistoryPosition(0),
	fSavedPosition(0),
	fReflowable(false),
	fTextSize(kDefaultTextSize),
	fEpub(NULL),
	fContentPath(contentPath != NULL ? contentPath : ""),
	fComic(NULL),
	fBbf(NULL),
	fIsComic(ComicArchive::IsComicFile(path)),
	fIsDjvu(Djvu::IsDjvuFile(path)),
	fAbortLayout(false),
	fKeptBookmark(0),
	fKeptPage(0),
	fStoreSavedDepth(0),
	fOutlineCached(false)
{
	int pages = 0;
	int isPDF = 0;
	int encrypted = 0;
	int canSave = 0;
	int reflowable = 0;

	fz_try(fContext) {
		reflowable = fz_is_document_reflowable(fContext, fDocument);
		if (reflowable && pdf_specifics(fContext, fDocument) == NULL) {
			// laid out for the page and text size before the pages are counted, which lays out the chapters
			float size = textSize >= kMinTextSize && textSize <= kMaxTextSize ? textSize : kDefaultTextSize;
			fz_layout_document(fContext, fDocument, kReflowWidth, kReflowHeight, size);
			fTextSize = size;
		}
		pages = fz_count_pages(fContext, fDocument);
		isPDF = pdf_specifics(fContext, fDocument) != NULL;
		if (isPDF) {
			canSave = pdf_can_be_saved_incrementally(fContext, pdf_specifics(fContext, fDocument));
			// edits are operations that can be undone
			pdf_enable_journal(fContext, pdf_specifics(fContext, fDocument));
		}
		char buffer[128];
		if (fz_lookup_metadata(fContext, fDocument, FZ_META_ENCRYPTION, buffer, sizeof(buffer)) > 0
			&& strcmp(buffer, "None") != 0)
			encrypted = 1;
	}
	fz_catch(fContext) {
		LogError(fContext, "cannot count pages");
		pages = 0;
	}

	fPageCount = pages;
	fIsPDF = isPDF != 0;
	fReflowable = reflowable != 0 && !fIsPDF;
	fEncrypted = encrypted != 0;
	fCanSave = canSave != 0;
	{
		// opening it for writing fails on read-only volumes (system directory) and without permission
		BFile file(path, B_READ_WRITE);
		fWritable = file.InitCheck() == B_OK;
	}

	fz_rect empty = fz_empty_rect;
	fBounds.assign(pages > 0 ? pages : 0, empty);
	fBoundsKnown.assign(pages > 0 ? pages : 0, false);

	if (fReflowable) {
		// the marks are kept in an attribute of the file, the metadata is in its package document
		fCanSave = true;
		LoadStore();
		fEpub = EpubInfo::Read(ContentPath());
	} else if (FixedStore()) {
		// the annotations of a comic book or a DjVu file are kept in an attribute of the file, too
		fCanSave = true;
		LoadStore();
	}
}


void
Document::Acquire()
{
	atomic_add(&fRefs, 1);
}


void
Document::Release()
{
	if (atomic_add(&fRefs, -1) == 1)
		delete this;
}


Document::~Document()
{
	fLock.Lock();
	delete fEpub;
	if (fContentPath.Length() > 0)
		BEntry(fContentPath.String()).Remove();
	delete fComic;
	delete fBbf;
	fz_drop_document(fContext, fDocument);
	fz_drop_context(fContext);
	fLock.Unlock();
}


bool
Document::CanPrint()
{
	DocumentLocker locker(this);
	return fz_has_permission(fContext, fDocument, FZ_PERMISSION_PRINT) != 0;
}


bool
Document::CanCopy()
{
	DocumentLocker locker(this);
	return fz_has_permission(fContext, fDocument, FZ_PERMISSION_COPY) != 0;
}


bool
Document::CanEdit()
{
	DocumentLocker locker(this);
	return fz_has_permission(fContext, fDocument, FZ_PERMISSION_EDIT) != 0;
}


bool
Document::CanAnnotate()
{
	DocumentLocker locker(this);
	return fz_has_permission(fContext, fDocument, FZ_PERMISSION_ANNOTATE) != 0;
}


int
Document::DeclaredReading()
{
	if (fComic != NULL && fComic->rightToLeft)
		return 1;
	if (fComic != NULL && fComic->topToBottom)
		return 2;
	if (fEpub != NULL) {
		if (fEpub->pageProgression.ICompare("rtl") == 0)
			return 1;
		if (fEpub->pageProgression.ICompare("ltr") == 0)
			return 0;
	}
	if (fIsComic && fPageCount >= 1) {
		// pages that are three times as high as they are wide (at least) are not pages of a book: a strip that is read by scrolling
		int sampled = 0;
		for (int page = 1; page <= fPageCount && page <= 5; page++) {
			fz_rect bounds;
			if (!PageBounds(page, &bounds))
				return -1;
			float width = bounds.x1 - bounds.x0, height = bounds.y1 - bounds.y0;
			if (width <= 0 || height < 3 * width)
				return -1;
			sampled++;
		}
		if (sampled > 0)
			return 2;
	}
	return -1;
}


bool
Document::IsWidePage(int page)
{
	if (!fIsComic || page < 1 || page > fPageCount)
		return false;
	if (fComic != NULL) {
		for (size_t i = 0; i < fComic->pages.size(); i++) {
			if (fComic->pages[i].image == page - 1 && fComic->pages[i].doublePage)
				return true;
		}
	}
	fz_rect bounds;
	if (!PageBounds(page, &bounds))
		return false;
	float width = bounds.x1 - bounds.x0, height = bounds.y1 - bounds.y0;
	return height > 0 && width > height * 1.15f;
}


bool
Document::PageBounds(int page, fz_rect* bounds)
{
	if (page < 1 || page > fPageCount)
		return false;

	// all pages of a book are as large as it is laid out; to load a page would lay out its chapter
	if (fReflowable) {
		*bounds = fz_make_rect(0, 0, kReflowWidth, kReflowHeight);
		return true;
	}

	DocumentLocker locker(this);
	int index = page - 1;
	if (!fBoundsKnown[index]) {
		fz_rect rect = fz_empty_rect;
		fz_page* fzPage = NULL;
		bool ok = true;

		fz_var(fzPage);
		fz_var(rect);
		TimingMark("page bounds: load starts");
		fz_try(fContext) {
			fzPage = fz_load_page(fContext, fDocument, index);
			TimingMark("page bounds: loaded");
			rect = fz_bound_page(fContext, fzPage);
		}
		fz_always(fContext) {
			fz_drop_page(fContext, fzPage);
		}
		fz_catch(fContext) {
			LogError(fContext, "cannot load page");
			ok = false;
		}

		if (!ok || fz_is_empty_rect(rect)) {
			// show something instead of failing, so that the user can go on to the next page
			rect = fz_make_rect(0, 0, 612, 792);
		}
		fBounds[index] = rect;
		fBoundsKnown[index] = true;
	}
	*bounds = fBounds[index];
	return true;
}


bool
Document::PageMatrix(int page, float dpi, int rotation, fz_matrix* matrix, int* width, int* height)
{
	fz_rect bounds;
	if (!PageBounds(page, &bounds))
		return false;

	float scale = dpi / 72.0f;
	fz_matrix ctm = fz_scale(scale, scale);
	ctm = fz_pre_rotate(ctm, (float)rotation);
	fz_irect box = fz_round_rect(fz_transform_rect(bounds, ctm));
	ctm = fz_concat(ctm, fz_translate(-box.x0, -box.y0));

	*matrix = ctm;
	*width = box.x1 - box.x0;
	*height = box.y1 - box.y0;
	if (*width < 1)
		*width = 1;
	if (*height < 1)
		*height = 1;
	return true;
}


BString
Document::Metadata(const char* key)
{
	if (fEpub != NULL) {
		// what the package document says, MuPDF only knows the title and the first author
		BString value;
		if (strcmp(key, FZ_META_INFO_TITLE) == 0)
			value = fEpub->title;
		else if (strcmp(key, FZ_META_INFO_AUTHOR) == 0)
			value = fEpub->Authors();
		else if (strcmp(key, FZ_META_INFO_KEYWORDS) == 0)
			value = fEpub->Subjects();
		if (value.Length() > 0)
			return value;
	}

	if (fComic != NULL) {
		BString value;
		if (strcmp(key, FZ_META_INFO_TITLE) == 0)
			value = fComic->title;
		else if (strcmp(key, FZ_META_INFO_AUTHOR) == 0)
			value = fComic->Authors();
		else if (strcmp(key, FZ_META_INFO_KEYWORDS) == 0)
			value = fComic->Keywords();
		if (value.Length() > 0)
			return value;
	}

	char buffer[1024];
	int length = -1;

	{
		DocumentLocker locker(this);
		fz_try(fContext) {
			length = fz_lookup_metadata(fContext, fDocument, key, buffer, sizeof(buffer));
		}
		fz_catch(fContext) {
			length = -1;
		}
	}

	BString result;
	if (length > 0)
		result.SetTo(buffer);
	return result;
}


BString
Document::Format()
{
	return Metadata(FZ_META_FORMAT);
}


BString
Document::PageLabel(int page)
{
	char buffer[64];
	buffer[0] = '\0';

	// only PDF files have page labels (and loading the page of a book lays out the chapter)
	if (!fReflowable && page >= 1 && page <= fPageCount) {
		DocumentLocker locker(this);
		fz_page* fzPage = NULL;

		fz_var(fzPage);
		fz_try(fContext) {
			fzPage = fz_load_page(fContext, fDocument, page - 1);
			fz_page_label(fContext, fzPage, buffer, sizeof(buffer));
		}
		fz_always(fContext) {
			fz_drop_page(fContext, fzPage);
		}
		fz_catch(fContext) {
			buffer[0] = '\0';
		}
	}

	return BString(buffer);
}


static void
AddOutlineEntries(Document* document, fz_outline* outline, int level, std::vector<DocOutlineEntry>& entries)
{
	fz_context* context = document->Context();

	for (; outline != NULL; outline = outline->next) {
		DocOutlineEntry entry;
		entry.title = outline->title != NULL ? outline->title : "";
		entry.level = level;
		entry.open = outline->is_open != 0;
		entry.page = 0;
		entry.x = entry.y = 0;
		entry.hasPosition = false;
		entry.bold = (outline->flags & 1) != 0;
		entry.italic = (outline->flags & 2) != 0;
		entry.hasColor = outline->r != 0 || outline->g != 0 || outline->b != 0;
		entry.red = outline->r;
		entry.green = outline->g;
		entry.blue = outline->b;

		int pageIndex = -1;
		float x = 0, y = 0;
		fz_var(pageIndex);
		fz_var(x);
		fz_var(y);
		fz_try(context) {
			fz_location location = outline->page;
			if (location.page < 0 && outline->uri != NULL && !fz_is_external_link(context, outline->uri))
				location = fz_resolve_link(context, document->Doc(), outline->uri, &x, &y);
			else {
				x = outline->x;
				y = outline->y;
			}
			pageIndex = fz_page_number_from_location(context, document->Doc(), location);
		}
		fz_catch(context) {
			pageIndex = -1;
		}

		if (pageIndex >= 0) {
			entry.page = pageIndex + 1;
			entry.x = x;
			entry.y = y;
			entry.hasPosition = !isnan(x) && !isnan(y);
		} else if (outline->uri != NULL)
			entry.uri = outline->uri;

		entries.push_back(entry);
		AddOutlineEntries(document, outline->down, level + 1, entries);
	}
}


bool
Document::LoadOutline(std::vector<DocOutlineEntry>& entries)
{
	DocumentLocker locker(this);
	if (fBbf != NULL && !fBbf->sections.empty()) {
		// the sections of the book, nested as they say
		for (size_t i = 0; i < fBbf->sections.size(); i++) {
			const BbfInfo::Section& section = fBbf->sections[i];
			DocOutlineEntry entry;
			entry.title = section.title;
			entry.level = 0;
			BString parent = section.parent;
			while (!parent.IsEmpty() && entry.level < 8) {
				entry.level++;
				BString next;
				for (size_t k = 0; k < fBbf->sections.size(); k++) {
					if (fBbf->sections[k].title == parent) {
						next = fBbf->sections[k].parent;
						break;
					}
				}
				parent = next;
			}
			entry.open = true;
			entry.page = (int)(section.firstPage < (uint64)fPageCount ? section.firstPage + 1 : fPageCount);
			entry.x = entry.y = 0;
			entry.hasPosition = false;
			entry.bold = entry.italic = entry.hasColor = false;
			entry.red = entry.green = entry.blue = 0;
			entries.push_back(entry);
		}
		return !entries.empty();
	}
	// to find where the entries of a book lead, its chapters are looked into: that is done once per layout
	if (fReflowable && fOutlineCached) {
		entries = fOutlineCache;
		return !entries.empty();
	}
	fz_outline* outline = NULL;

	fz_var(outline);
	fz_try(fContext) {
		outline = fz_load_outline(fContext, fDocument);
	}
	fz_catch(fContext) {
		LogError(fContext, "cannot load outline");
		outline = NULL;
	}

	if (outline == NULL)
		return false;

	AddOutlineEntries(this, outline, 0, entries);
	fz_drop_outline(fContext, outline);
	if (fReflowable) {
		fOutlineCache = entries;
		fOutlineCached = true;
	}
	return !entries.empty();
}


bool
Document::IsExternalLink(const char* uri)
{
	if (uri == NULL)
		return false;
	return fz_is_external_link(fContext, uri) != 0;
}


bool
Document::ResolveLink(const char* uri, int* page, float* x, float* y)
{
	if (uri == NULL || fz_is_external_link(fContext, uri))
		return false;

	DocumentLocker locker(this);
	int pageIndex = -1;
	float px = NAN, py = NAN;

	fz_var(pageIndex);
	fz_try(fContext) {
		fz_location location = fz_resolve_link(fContext, fDocument, uri, &px, &py);
		pageIndex = fz_page_number_from_location(fContext, fDocument, location);
	}
	fz_catch(fContext) {
		pageIndex = -1;
	}

	if (pageIndex < 0)
		return false;

	*page = pageIndex + 1;
	*x = px;
	*y = py;
	return true;
}


// The embedded files are the name tree /Names /EmbeddedFiles, flattened into one dictionary by MuPDF.
bool
Document::LoadAttachments(std::vector<DocAttachment>& attachments)
{
	if (!fIsPDF)
		return false;

	DocumentLocker locker(this);
	pdf_document* pdf = pdf_specifics(fContext, fDocument);
	pdf_obj* names = NULL;
	int count = 0;

	fz_var(names);
	fz_var(count);
	fz_try(fContext) {
		names = pdf_load_name_tree(fContext, pdf, PDF_NAME(EmbeddedFiles));
		count = pdf_dict_len(fContext, names);
	}
	fz_catch(fContext) {
		LogError(fContext, "cannot load attachments");
		count = 0;
	}

	for (int i = 0; i < count; i++) {
		char name[512], mime[128];
		int ok = 0;
		int size = -1;
		int64_t modified = -1;
		name[0] = mime[0] = '\0';

		fz_var(ok);
		fz_try(fContext) {
			pdf_obj* spec = pdf_dict_get_val(fContext, names, i);
			if (pdf_is_embedded_file(fContext, spec)) {
				pdf_filespec_params params;
				pdf_get_filespec_params(fContext, spec, &params);
				const char* title = params.filename;
				if (title == NULL || title[0] == '\0')
					title = pdf_to_name(fContext, pdf_dict_get_key(fContext, names, i));
				strlcpy(name, title != NULL ? title : "", sizeof(name));
				strlcpy(mime, params.mimetype != NULL ? params.mimetype : "", sizeof(mime));
				size = params.size;
				modified = params.modified;
				ok = 1;
			}
		}
		fz_catch(fContext) {
			ok = 0;
		}

		if (!ok)
			continue;
		DocAttachment attachment;
		attachment.name = name;
		attachment.mimeType = mime;
		attachment.size = size;
		attachment.modified = (time_t)modified;
		attachments.push_back(attachment);
	}

	pdf_drop_obj(fContext, names);
	return !attachments.empty();
}


// The index counts the embedded files in the order of LoadAttachments(), so entries that are not files are
// skipped here too.
bool
Document::SaveAttachment(int index, const char* path)
{
	if (!fIsPDF || index < 0)
		return false;

	DocumentLocker locker(this);
	pdf_document* pdf = pdf_specifics(fContext, fDocument);
	pdf_obj* names = NULL;
	fz_buffer* contents = NULL;
	const unsigned char* data = NULL;
	size_t size = 0;
	int count = 0;

	fz_var(names);
	fz_var(contents);
	fz_var(count);
	fz_try(fContext) {
		names = pdf_load_name_tree(fContext, pdf, PDF_NAME(EmbeddedFiles));
		int length = pdf_dict_len(fContext, names);
		for (int i = 0; i < length; i++) {
			pdf_obj* spec = pdf_dict_get_val(fContext, names, i);
			if (!pdf_is_embedded_file(fContext, spec))
				continue;
			if (count++ == index) {
				contents = pdf_load_embedded_file_contents(fContext, spec);
				size = fz_buffer_storage(fContext, contents, (unsigned char**)&data);
				break;
			}
		}
	}
	fz_catch(fContext) {
		LogError(fContext, "cannot read attachment");
		contents = NULL;
	}

	bool ok = false;
	if (contents != NULL) {
		BFile file(path, B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
		ok = file.InitCheck() == B_OK && file.Write(data, size) == (ssize_t)size;
	}

	fz_drop_buffer(fContext, contents);
	pdf_drop_obj(fContext, names);
	return ok;
}


///////////////////////////////////////////////////////////////////////////
// Annotations

static const int kMaxAnnotationQuads = 512;
static const int kMaxInkPoints = 20000;
static const int kMaxInkStrokes = 2000;


// the annotations that are shown in the list: all but links, popups (belong to a note) and form fields
static bool
IsListedAnnotation(int type)
{
	return type != PDF_ANNOT_LINK && type != PDF_ANNOT_POPUP && type != PDF_ANNOT_WIDGET;
}


bool
Document::CanEditAnnotations()
{
	return (fIsPDF || fReflowable || FixedStore()) && CanAnnotate();
}


bool
Document::CanMarkText()
{
	return (fIsPDF || fReflowable || fIsDjvu) && CanAnnotate();
}


bool
Document::CanDrawAnnotations()
{
	return (fIsPDF || FixedStore()) && CanAnnotate();
}


bool
Document::LoadAnnotations(int pageNo, fz_page* page, std::vector<DocAnnotation>& annotations)
{
	if (UsesStore()) {
		StoreAnnotationsOnPage(pageNo, annotations);
		return true;
	}
	if (!fIsPDF)
		return false;

	pdf_page* pdfPage = NULL;
	fz_var(pdfPage);
	fz_try(fContext) {
		pdfPage = pdf_page_from_fz_page(fContext, page);
	}
	fz_catch(fContext) {
		pdfPage = NULL;
	}
	if (pdfPage == NULL)
		return false;

	std::vector<fz_quad> quads(kMaxAnnotationQuads);
	std::vector<fz_point> inkPoints(kMaxInkPoints);
	std::vector<int> inkStrokes(kMaxInkStrokes);
	int index = 0;
	pdf_annot* annot = NULL;

	fz_var(annot);
	fz_try(fContext) {
		annot = pdf_first_annot(fContext, pdfPage);
	}
	fz_catch(fContext) {
		annot = NULL;
	}

	while (annot != NULL) {
		int type = PDF_ANNOT_UNKNOWN;
		fz_rect rect = fz_empty_rect;
		int quadCount = 0;
		char contents[1024], author[128], name[128];
		contents[0] = author[0] = name[0] = '\0';
		int ok = 0;
		pdf_annot* next = NULL;
		int colorCount = 0;
		float colorValue[4] = { 0, 0, 0, 0 };
		float borderWidth = 0;
		int fillCount = 0;
		float fillValue[4] = { 0, 0, 0, 0 };
		fz_point lineA = fz_make_point(0, 0), lineB = lineA;
		int strokeCount = 0, pointCount = 0;

		fz_var(ok);
		fz_try(fContext) {
			type = pdf_annot_type(fContext, annot);
			if (IsListedAnnotation(type)) {
				rect = pdf_bound_annot(fContext, annot);
				if (pdf_annot_has_quad_points(fContext, annot)) {
					quadCount = pdf_annot_quad_point_count(fContext, annot);
					if (quadCount > kMaxAnnotationQuads)
						quadCount = kMaxAnnotationQuads;
					for (int i = 0; i < quadCount; i++)
						quads[i] = pdf_annot_quad_point(fContext, annot, i);
				}
				const char* text = pdf_annot_contents(fContext, annot);
				strlcpy(contents, text != NULL ? text : "", sizeof(contents));
				const char* by = pdf_annot_author(fContext, annot);
				strlcpy(author, by != NULL ? by : "", sizeof(author));
				pdf_annot_color(fContext, annot, &colorCount, colorValue);
				if (type == PDF_ANNOT_SQUARE || type == PDF_ANNOT_CIRCLE || type == PDF_ANNOT_LINE || type == PDF_ANNOT_INK)
					borderWidth = pdf_annot_border_width(fContext, annot);
				if ((type == PDF_ANNOT_SQUARE || type == PDF_ANNOT_CIRCLE) && pdf_annot_has_interior_color(fContext, annot))
					pdf_annot_interior_color(fContext, annot, &fillCount, fillValue);
				const char* nm = pdf_annot_name(fContext, annot);
				strlcpy(name, nm != NULL ? nm : "", sizeof(name));
				if (type == PDF_ANNOT_LINE)
					pdf_annot_line(fContext, annot, &lineA, &lineB);
				else if (type == PDF_ANNOT_INK) {
					int strokes = pdf_annot_ink_list_count(fContext, annot);
					for (int i = 0; i < strokes && strokeCount < kMaxInkStrokes; i++) {
						int n = pdf_annot_ink_list_stroke_count(fContext, annot, i);
						if (pointCount + n > kMaxInkPoints)
							break;
						for (int k = 0; k < n; k++)
							inkPoints[pointCount++] = pdf_annot_ink_list_stroke_vertex(fContext, annot, i, k);
						inkStrokes[strokeCount++] = n;
					}
				}
				ok = 1;
			}
			next = pdf_next_annot(fContext, annot);
		}
		fz_catch(fContext) {
			ok = 0;
			next = NULL;
		}

		if (ok) {
			DocAnnotation entry;
			entry.type = type;
			entry.index = index++;
			entry.id = name;
			entry.rect = rect;
			entry.quads.assign(quads.begin(), quads.begin() + quadCount);
			entry.contents = contents;
			entry.author = author;
			entry.width = borderWidth;
			if (fillCount == 3 || fillCount == 1) {
				entry.hasFill = true;
				float r = fillValue[0], g = fillCount == 3 ? fillValue[1] : fillValue[0], b = fillCount == 3 ? fillValue[2] : fillValue[0];
				entry.fill = ((uint32)(r * 255 + 0.5f) << 16) | ((uint32)(g * 255 + 0.5f) << 8) | (uint32)(b * 255 + 0.5f);
			}
			entry.isMarkup = type == PDF_ANNOT_HIGHLIGHT || type == PDF_ANNOT_UNDERLINE
				|| type == PDF_ANNOT_STRIKE_OUT || type == PDF_ANNOT_SQUIGGLY;
			entry.isFreeText = type == PDF_ANNOT_FREE_TEXT;
			switch (type) {
				case PDF_ANNOT_HIGHLIGHT:  entry.label = B_TRANSLATE("Highlight"); break;
				case PDF_ANNOT_UNDERLINE:  entry.label = B_TRANSLATE("Underline"); break;
				case PDF_ANNOT_STRIKE_OUT: entry.label = B_TRANSLATE("Strike out"); break;
				case PDF_ANNOT_SQUIGGLY:   entry.label = B_TRANSLATE("Squiggly line"); break;
				case PDF_ANNOT_TEXT:       entry.label = B_TRANSLATE("Note"); break;
				case PDF_ANNOT_FREE_TEXT:  entry.label = B_TRANSLATE("Text"); break;
				case PDF_ANNOT_SQUARE:     entry.label = B_TRANSLATE("Rectangle"); break;
				case PDF_ANNOT_CIRCLE:     entry.label = B_TRANSLATE("Ellipse"); break;
				case PDF_ANNOT_LINE:       entry.label = B_TRANSLATE("Line"); break;
				case PDF_ANNOT_INK:        entry.label = B_TRANSLATE("Drawing"); break;
				case PDF_ANNOT_STAMP:      entry.label = B_TRANSLATE("Stamp"); break;
				case PDF_ANNOT_FILE_ATTACHMENT: entry.label = B_TRANSLATE("File attachment"); break;
				default:                   entry.label = B_TRANSLATE("Annotation"); break;
			}
			switch (type) {
				case PDF_ANNOT_HIGHLIGHT:
				case PDF_ANNOT_UNDERLINE:
				case PDF_ANNOT_STRIKE_OUT:
				case PDF_ANNOT_SQUIGGLY:
					entry.kind = kAnnotMarkup;
					break;
				case PDF_ANNOT_TEXT:
					entry.kind = kAnnotNote;
					break;
				case PDF_ANNOT_FREE_TEXT:
					entry.kind = kAnnotText;
					break;
				case PDF_ANNOT_SQUARE:
					entry.kind = kAnnotRectangle;
					break;
				case PDF_ANNOT_CIRCLE:
					entry.kind = kAnnotEllipse;
					break;
				case PDF_ANNOT_LINE:
					entry.kind = kAnnotLine;
					entry.paths.resize(1);
					entry.paths[0].push_back(lineA);
					entry.paths[0].push_back(lineB);
					break;
				case PDF_ANNOT_INK: {
					entry.kind = kAnnotInk;
					int at = 0;
					for (int i = 0; i < strokeCount; i++) {
						entry.paths.push_back(std::vector<fz_point>(inkPoints.begin() + at,
							inkPoints.begin() + at + inkStrokes[i]));
						at += inkStrokes[i];
					}
					break;
				}
				default:
					entry.kind = kAnnotOther;
					break;
			}
			entry.hasColor = colorCount == 1 || colorCount == 3 || colorCount == 4;
			entry.color = 0;
			if (entry.hasColor) {
				// gray, RGB or CMYK to RGB
				float r, g, b;
				if (colorCount == 1)
					r = g = b = colorValue[0];
				else if (colorCount == 3) {
					r = colorValue[0];
					g = colorValue[1];
					b = colorValue[2];
				} else {
					r = (1 - colorValue[0]) * (1 - colorValue[3]);
					g = (1 - colorValue[1]) * (1 - colorValue[3]);
					b = (1 - colorValue[2]) * (1 - colorValue[3]);
				}
				entry.color = ((uint32)(r * 255 + 0.5f) << 16) | ((uint32)(g * 255 + 0.5f) << 8)
					| (uint32)(b * 255 + 0.5f);
			}
			annotations.push_back(entry);
		}
		annot = next;
	}
	return true;
}


// An edit is one operation of the journal of MuPDF, which is what Undo() and Redo() work with. Everything the
// edit does is in between of begin and end; if it fails it is abandoned as a whole.
#define BEGIN_EDIT(name) \
	fz_var(began); \
	pdf_begin_operation(fContext, pdf_specifics(fContext, fDocument), name); \
	began = 1;

#define END_EDIT() \
	pdf_end_operation(fContext, pdf_specifics(fContext, fDocument)); \
	began = 0;

#define ABANDON_EDIT() \
	if (began) { \
		fz_try(fContext) { \
			pdf_abandon_operation(fContext, pdf_specifics(fContext, fDocument)); \
		} \
		fz_catch(fContext) { \
		} \
	}


bool
Document::AddMarkup(int pageNo, MarkupType type, const fz_quad* quads, int count, const float color[3])
{
	if (!CanEditAnnotations() || pageNo < 1 || pageNo > fPageCount || count <= 0)
		return false;
	if (UsesStore())
		return StoreAddMarkup(pageNo, type, quads, count, color);

	static const int types[] = { PDF_ANNOT_HIGHLIGHT, PDF_ANNOT_UNDERLINE, PDF_ANNOT_STRIKE_OUT,
		PDF_ANNOT_SQUIGGLY };
	const char* names[] = { B_TRANSLATE("Add highlight"), B_TRANSLATE("Add underline"),
		B_TRANSLATE("Add strike out"), B_TRANSLATE("Add squiggly line") };
	BString operation(names[type]);

	const char* author = getenv("USER");
	char annotationId[48];
	WebAnnotation::NewId(annotationId, sizeof(annotationId));
	DocumentLocker locker(this);
	fz_page* page = NULL;
	int ok = 0;
	int began = 0;

	fz_var(page);
	fz_try(fContext) {
		page = fz_load_page(fContext, fDocument, pageNo - 1);
		pdf_page* pdfPage = pdf_page_from_fz_page(fContext, page);
		BEGIN_EDIT(operation.String())
		pdf_annot* annot = pdf_create_annot(fContext, pdfPage, (enum pdf_annot_type)types[type]);
		pdf_set_annot_name(fContext, annot, annotationId);
		pdf_set_annot_quad_points(fContext, annot, count, quads);
		pdf_set_annot_color(fContext, annot, 3, color);
		if (author != NULL && author[0] != '\0')
			pdf_set_annot_author(fContext, annot, author);
		pdf_set_annot_creation_date(fContext, annot, (int64_t)time(NULL));
		pdf_set_annot_modification_date(fContext, annot, (int64_t)time(NULL));
		pdf_update_annot(fContext, annot);
		END_EDIT()
		ok = 1;
	}
	fz_always(fContext) {
		fz_drop_page(fContext, page);
	}
	fz_catch(fContext) {
		LogError(fContext, "cannot add annotation");
		ABANDON_EDIT()
		ok = 0;
	}
	if (ok)
		RecordOperation(pageNo, operation.String());
	return ok != 0;
}


// the annotation with the index (counted as in LoadAnnotations)
static pdf_annot*
FindAnnotation(fz_context* context, pdf_page* page, int index)
{
	int n = 0;
	for (pdf_annot* annot = pdf_first_annot(context, page); annot != NULL;
			annot = pdf_next_annot(context, annot)) {
		if (!IsListedAnnotation(pdf_annot_type(context, annot)))
			continue;
		if (n++ == index)
			return annot;
	}
	return NULL;
}


bool
Document::DeleteAnnotation(int pageNo, int index)
{
	if (!CanEditAnnotations() || pageNo < 1 || pageNo > fPageCount)
		return false;
	if (UsesStore())
		return StoreDelete(pageNo, index);

	BString operation(B_TRANSLATE("Delete annotation"));
	DocumentLocker locker(this);
	fz_page* page = NULL;
	int ok = 0;
	int began = 0;

	fz_var(page);
	fz_try(fContext) {
		page = fz_load_page(fContext, fDocument, pageNo - 1);
		pdf_page* pdfPage = pdf_page_from_fz_page(fContext, page);
		pdf_annot* annot = FindAnnotation(fContext, pdfPage, index);
		if (annot != NULL) {
			BEGIN_EDIT(operation.String())
			pdf_delete_annot(fContext, pdfPage, annot);
			END_EDIT()
			ok = 1;
		}
	}
	fz_always(fContext) {
		fz_drop_page(fContext, page);
	}
	fz_catch(fContext) {
		LogError(fContext, "cannot delete annotation");
		ABANDON_EDIT()
		ok = 0;
	}
	if (ok)
		RecordOperation(pageNo, operation.String());
	return ok != 0;
}


bool
Document::SetAnnotationContents(int pageNo, int index, const char* text)
{
	if (!CanEditAnnotations() || pageNo < 1 || pageNo > fPageCount)
		return false;
	if (UsesStore())
		return StoreSetContents(pageNo, index, text);

	BString operation(B_TRANSLATE("Edit note"));
	DocumentLocker locker(this);
	fz_page* page = NULL;
	int ok = 0;
	int began = 0;

	fz_var(page);
	fz_try(fContext) {
		page = fz_load_page(fContext, fDocument, pageNo - 1);
		pdf_page* pdfPage = pdf_page_from_fz_page(fContext, page);
		pdf_annot* annot = FindAnnotation(fContext, pdfPage, index);
		if (annot != NULL) {
			BEGIN_EDIT(operation.String())
			pdf_set_annot_contents(fContext, annot, text);
			pdf_set_annot_modification_date(fContext, annot, (int64_t)time(NULL));
			pdf_update_annot(fContext, annot);
			END_EDIT()
			ok = 1;
		}
	}
	fz_always(fContext) {
		fz_drop_page(fContext, page);
	}
	fz_catch(fContext) {
		LogError(fContext, "cannot change annotation");
		ABANDON_EDIT()
		ok = 0;
	}
	if (ok)
		RecordOperation(pageNo, operation.String());
	return ok != 0;
}


bool
Document::SetAnnotationId(int pageNo, int index, const char* id)
{
	if (!CanEditAnnotations() || pageNo < 1 || pageNo > fPageCount || id == NULL)
		return false;
	DocumentLocker locker(this);
	if (UsesStore()) {
		int at;
		if (!StoreIndexFor(pageNo, index, &at))
			return false;
		fStore[at].id = id;
		return true;
	}

	fz_page* page = NULL;
	int ok = 0;
	fz_var(page);
	fz_try(fContext) {
		page = fz_load_page(fContext, fDocument, pageNo - 1);
		pdf_page* pdfPage = pdf_page_from_fz_page(fContext, page);
		pdf_annot* annot = FindAnnotation(fContext, pdfPage, index);
		if (annot != NULL) {
			pdf_set_annot_name(fContext, annot, id);
			pdf_update_annot(fContext, annot);
			ok = 1;
		}
	}
	fz_always(fContext) {
		fz_drop_page(fContext, page);
	}
	fz_catch(fContext) {
		LogError(fContext, "cannot name annotation");
		ok = 0;
	}
	return ok != 0;
}


bool
Document::MoveMarkupToQuote(int pageNo, int index, const char* quote)
{
	std::vector<DocAnnotationEntry> entries;
	if (!ListAnnotationsOnPage(pageNo, entries))
		return false;
	const DocAnnotation* old = NULL;
	for (size_t i = 0; i < entries.size(); i++) {
		if (entries[i].annotation.index == index && !entries[i].annotation.continued)
			old = &entries[i].annotation;
	}
	if (old == NULL || !old->isMarkup)
		return false;

	std::vector<fz_quad> quads;
	if (!FindQuoteQuads(pageNo, quote, &quads))
		return false;

	BString id = old->id, contents = old->contents;
	MarkupType type = old->type == PDF_ANNOT_UNDERLINE ? kMarkupUnderline
		: old->type == PDF_ANNOT_STRIKE_OUT ? kMarkupStrikeOut
		: old->type == PDF_ANNOT_SQUIGGLY ? kMarkupSquiggly : kMarkupHighlight;
	uint32 rgb = old->hasColor ? old->color : 0xffeb3b;
	float color[3] = { ((rgb >> 16) & 0xff) / 255.0f, ((rgb >> 8) & 0xff) / 255.0f, (rgb & 0xff) / 255.0f };

	if (!DeleteAnnotation(pageNo, index) || !AddMarkup(pageNo, type, quads.data(), (int)quads.size(), color))
		return false;

	entries.clear();
	int newest = -1;
	if (ListAnnotationsOnPage(pageNo, entries)) {
		for (size_t i = 0; i < entries.size(); i++) {
			if (entries[i].annotation.index > newest)
				newest = entries[i].annotation.index;
		}
	}
	if (newest < 0)
		return false;
	if (id.Length() > 0)
		SetAnnotationId(pageNo, newest, id.String());
	if (contents.Length() > 0)
		SetAnnotationContents(pageNo, newest, contents.String());
	return true;
}


static void
ColorFloats(uint32 rgb, float color[3])
{
	color[0] = ((rgb >> 16) & 0xff) / 255.0f;
	color[1] = ((rgb >> 8) & 0xff) / 255.0f;
	color[2] = (rgb & 0xff) / 255.0f;
}


bool
Document::SetAnnotationStyle(int pageNo, int index, float width, bool hasFill, uint32 fill)
{
	if (!CanEditAnnotations() || pageNo < 1 || pageNo > fPageCount)
		return false;
	if (UsesStore())
		return StoreSetStyle(pageNo, index, width, hasFill, fill);

	BString operation(B_TRANSLATE("Change line"));
	DocumentLocker locker(this);
	fz_page* page = NULL;
	int ok = 0;
	int began = 0;

	fz_var(page);
	fz_try(fContext) {
		page = fz_load_page(fContext, fDocument, pageNo - 1);
		pdf_page* pdfPage = pdf_page_from_fz_page(fContext, page);
		pdf_annot* annot = FindAnnotation(fContext, pdfPage, index);
		if (annot != NULL) {
			BEGIN_EDIT(operation.String())
			int type = pdf_annot_type(fContext, annot);
			if (width > 0)
				pdf_set_annot_border_width(fContext, annot, width);
			if (type == PDF_ANNOT_SQUARE || type == PDF_ANNOT_CIRCLE) {
				if (hasFill) {
					float color[3];
					ColorFloats(fill, color);
					pdf_set_annot_interior_color(fContext, annot, 3, color);
				} else
					pdf_dict_del(fContext, pdf_annot_obj(fContext, annot), PDF_NAME(IC));
			}
			pdf_set_annot_modification_date(fContext, annot, (int64_t)time(NULL));
			pdf_update_annot(fContext, annot);
			END_EDIT()
			ok = 1;
		}
	}
	fz_always(fContext) {
		fz_drop_page(fContext, page);
	}
	fz_catch(fContext) {
		LogError(fContext, "cannot change the line of an annotation");
		ABANDON_EDIT()
		ok = 0;
	}
	if (ok)
		RecordOperation(pageNo, operation.String());
	return ok != 0;
}


bool
Document::SetAnnotationColor(int pageNo, int index, uint32 rgb)
{
	if (!CanEditAnnotations() || pageNo < 1 || pageNo > fPageCount)
		return false;
	if (UsesStore())
		return StoreSetColor(pageNo, index, rgb);

	float color[3] = { ((rgb >> 16) & 0xff) / 255.0f, ((rgb >> 8) & 0xff) / 255.0f, (rgb & 0xff) / 255.0f };
	BString operation(B_TRANSLATE("Change color"));
	DocumentLocker locker(this);
	fz_page* page = NULL;
	int ok = 0;
	int began = 0;

	fz_var(page);
	fz_try(fContext) {
		page = fz_load_page(fContext, fDocument, pageNo - 1);
		pdf_page* pdfPage = pdf_page_from_fz_page(fContext, page);
		pdf_annot* annot = FindAnnotation(fContext, pdfPage, index);
		if (annot != NULL) {
			BEGIN_EDIT(operation.String())
			pdf_set_annot_color(fContext, annot, 3, color);
			pdf_set_annot_modification_date(fContext, annot, (int64_t)time(NULL));
			pdf_update_annot(fContext, annot);
			END_EDIT()
			ok = 1;
		}
	}
	fz_always(fContext) {
		fz_drop_page(fContext, page);
	}
	fz_catch(fContext) {
		LogError(fContext, "cannot change the color of an annotation");
		ABANDON_EDIT()
		ok = 0;
	}
	if (ok)
		RecordOperation(pageNo, operation.String());
	return ok != 0;
}




// the rectangle at a position, as far as it fits on the page
static fz_rect
RectAt(Document* document, int page, fz_point where, float width, float height)
{
	fz_rect bounds = fz_make_rect(0, 0, 612, 792);
	document->PageBounds(page, &bounds);
	float x = fminf(where.x, bounds.x1 - width);
	float y = fminf(where.y, bounds.y1 - height);
	x = fmaxf(x, bounds.x0);
	y = fmaxf(y, bounds.y0);
	return fz_make_rect(x, y, x + width, y + height);
}


static const float kShapeLineWidth = 2;


bool
Document::AddNote(int pageNo, fz_point where, const char* text)
{
	if (FixedStore())
		return StoreAddNote(pageNo, where, text);
	if (!CanDrawAnnotations() || pageNo < 1 || pageNo > fPageCount)
		return false;

	BString operation(B_TRANSLATE("Add note"));
	fz_rect rect = RectAt(this, pageNo, where, 20, 20);
	float color[3];
	ColorFloats(0xffeb3b, color);
	const char* author = getenv("USER");
	char annotationId[48];
	WebAnnotation::NewId(annotationId, sizeof(annotationId));
	DocumentLocker locker(this);
	fz_page* page = NULL;
	int ok = 0;
	int began = 0;

	fz_var(page);
	fz_try(fContext) {
		page = fz_load_page(fContext, fDocument, pageNo - 1);
		pdf_page* pdfPage = pdf_page_from_fz_page(fContext, page);
		BEGIN_EDIT(operation.String())
		pdf_annot* annot = pdf_create_annot(fContext, pdfPage, PDF_ANNOT_TEXT);
		pdf_set_annot_name(fContext, annot, annotationId);
		pdf_set_annot_rect(fContext, annot, rect);
		pdf_set_annot_color(fContext, annot, 3, color);
		pdf_set_annot_contents(fContext, annot, text);
		if (author != NULL && author[0] != '\0')
			pdf_set_annot_author(fContext, annot, author);
		pdf_set_annot_creation_date(fContext, annot, (int64_t)time(NULL));
		pdf_set_annot_modification_date(fContext, annot, (int64_t)time(NULL));
		pdf_update_annot(fContext, annot);
		END_EDIT()
		ok = 1;
	}
	fz_always(fContext) {
		fz_drop_page(fContext, page);
	}
	fz_catch(fContext) {
		LogError(fContext, "cannot add a note");
		ABANDON_EDIT()
		ok = 0;
	}
	if (ok)
		RecordOperation(pageNo, operation.String());
	return ok != 0;
}


bool
Document::AddFreeText(int pageNo, fz_point where, const char* text)
{
	if (FixedStore())
		return StoreAddFreeText(pageNo, where, text);
	if (!CanDrawAnnotations() || pageNo < 1 || pageNo > fPageCount)
		return false;

	BString operation(B_TRANSLATE("Add text"));
	// the box is as wide as a paragraph and as high as its lines, a line is about 34 characters
	const float kFontSize = 12;
	int lines = 1, column = 0;
	for (const char* p = text; *p != '\0'; p++) {
		if (*p == '\n' || ++column > 34) {
			lines++;
			column = 0;
		}
	}
	fz_rect rect = RectAt(this, pageNo, where, 220, lines * (kFontSize + 3) + 8);
	float black[3] = { 0, 0, 0 };
	const char* author = getenv("USER");
	char annotationId[48];
	WebAnnotation::NewId(annotationId, sizeof(annotationId));
	DocumentLocker locker(this);
	fz_page* page = NULL;
	int ok = 0;
	int began = 0;

	fz_var(page);
	fz_try(fContext) {
		page = fz_load_page(fContext, fDocument, pageNo - 1);
		pdf_page* pdfPage = pdf_page_from_fz_page(fContext, page);
		BEGIN_EDIT(operation.String())
		pdf_annot* annot = pdf_create_annot(fContext, pdfPage, PDF_ANNOT_FREE_TEXT);
		pdf_set_annot_name(fContext, annot, annotationId);
		pdf_set_annot_rect(fContext, annot, rect);
		pdf_set_annot_default_appearance(fContext, annot, "Helv", kFontSize, 3, black);
		pdf_set_annot_contents(fContext, annot, text);
		if (author != NULL && author[0] != '\0')
			pdf_set_annot_author(fContext, annot, author);
		pdf_set_annot_creation_date(fContext, annot, (int64_t)time(NULL));
		pdf_set_annot_modification_date(fContext, annot, (int64_t)time(NULL));
		pdf_update_annot(fContext, annot);
		END_EDIT()
		ok = 1;
	}
	fz_always(fContext) {
		fz_drop_page(fContext, page);
	}
	fz_catch(fContext) {
		LogError(fContext, "cannot add text");
		ABANDON_EDIT()
		ok = 0;
	}
	if (ok)
		RecordOperation(pageNo, operation.String());
	return ok != 0;
}


bool
Document::AddShape(int pageNo, ShapeType type, fz_point from, fz_point to, uint32 rgb, float width, bool hasFill,
	uint32 fill)
{
	if (FixedStore())
		return StoreAddShape(pageNo, type, from, to, rgb, width, hasFill, fill);
	if (!CanDrawAnnotations() || pageNo < 1 || pageNo > fPageCount)
		return false;

	const char* names[] = { B_TRANSLATE("Add rectangle"), B_TRANSLATE("Add ellipse"), B_TRANSLATE("Add line"),
		B_TRANSLATE("Add arrow") };
	BString operation(names[type]);
	static const int types[] = { PDF_ANNOT_SQUARE, PDF_ANNOT_CIRCLE, PDF_ANNOT_LINE, PDF_ANNOT_LINE };
	fz_rect rect = fz_make_rect(fminf(from.x, to.x), fminf(from.y, to.y), fmaxf(from.x, to.x),
		fmaxf(from.y, to.y));
	float color[3];
	ColorFloats(rgb, color);
	const char* author = getenv("USER");
	char annotationId[48];
	WebAnnotation::NewId(annotationId, sizeof(annotationId));
	DocumentLocker locker(this);
	fz_page* page = NULL;
	int ok = 0;
	int began = 0;

	fz_var(page);
	fz_try(fContext) {
		page = fz_load_page(fContext, fDocument, pageNo - 1);
		pdf_page* pdfPage = pdf_page_from_fz_page(fContext, page);
		BEGIN_EDIT(operation.String())
		pdf_annot* annot = pdf_create_annot(fContext, pdfPage, (enum pdf_annot_type)types[type]);
		pdf_set_annot_name(fContext, annot, annotationId);
		if (type == kShapeLine || type == kShapeArrow) {
			pdf_set_annot_line(fContext, annot, from, to);
			if (type == kShapeArrow)
				pdf_set_annot_line_ending_styles(fContext, annot, PDF_ANNOT_LE_NONE, PDF_ANNOT_LE_OPEN_ARROW);
		} else
			pdf_set_annot_rect(fContext, annot, rect);
		pdf_set_annot_color(fContext, annot, 3, color);
		pdf_set_annot_border_width(fContext, annot, width > 0 ? width : kShapeLineWidth);
		if (hasFill && (type == kShapeRectangle || type == kShapeEllipse)) {
			float fillColor[3];
			ColorFloats(fill, fillColor);
			pdf_set_annot_interior_color(fContext, annot, 3, fillColor);
		}
		if (author != NULL && author[0] != '\0')
			pdf_set_annot_author(fContext, annot, author);
		pdf_set_annot_creation_date(fContext, annot, (int64_t)time(NULL));
		pdf_set_annot_modification_date(fContext, annot, (int64_t)time(NULL));
		pdf_update_annot(fContext, annot);
		END_EDIT()
		ok = 1;
	}
	fz_always(fContext) {
		fz_drop_page(fContext, page);
	}
	fz_catch(fContext) {
		LogError(fContext, "cannot add a shape");
		ABANDON_EDIT()
		ok = 0;
	}
	if (ok)
		RecordOperation(pageNo, operation.String());
	return ok != 0;
}


bool
Document::AddInk(int pageNo, const fz_point* points, int count, uint32 rgb, float width)
{
	if (FixedStore())
		return StoreAddInk(pageNo, points, count, rgb, width);
	if (!CanDrawAnnotations() || pageNo < 1 || pageNo > fPageCount || count < 2)
		return false;

	BString operation(B_TRANSLATE("Add drawing"));
	std::vector<fz_point> stroke(points, points + count);
	float color[3];
	ColorFloats(rgb, color);
	const char* author = getenv("USER");
	char annotationId[48];
	WebAnnotation::NewId(annotationId, sizeof(annotationId));
	DocumentLocker locker(this);
	fz_page* page = NULL;
	int ok = 0;
	int began = 0;

	fz_var(page);
	fz_try(fContext) {
		page = fz_load_page(fContext, fDocument, pageNo - 1);
		pdf_page* pdfPage = pdf_page_from_fz_page(fContext, page);
		BEGIN_EDIT(operation.String())
		pdf_annot* annot = pdf_create_annot(fContext, pdfPage, PDF_ANNOT_INK);
		pdf_set_annot_name(fContext, annot, annotationId);
		pdf_add_annot_ink_list(fContext, annot, count, &stroke[0]);
		pdf_set_annot_color(fContext, annot, 3, color);
		pdf_set_annot_border_width(fContext, annot, width > 0 ? width : kShapeLineWidth);
		if (author != NULL && author[0] != '\0')
			pdf_set_annot_author(fContext, annot, author);
		pdf_set_annot_creation_date(fContext, annot, (int64_t)time(NULL));
		pdf_set_annot_modification_date(fContext, annot, (int64_t)time(NULL));
		pdf_update_annot(fContext, annot);
		END_EDIT()
		ok = 1;
	}
	fz_always(fContext) {
		fz_drop_page(fContext, page);
	}
	fz_catch(fContext) {
		LogError(fContext, "cannot add a drawing");
		ABANDON_EDIT()
		ok = 0;
	}
	if (ok)
		RecordOperation(pageNo, operation.String());
	return ok != 0;
}


static fz_point
MapPoint(fz_point p, fz_rect from, fz_rect to, float scaleX, float scaleY)
{
	return fz_make_point(to.x0 + (p.x - from.x0) * scaleX, to.y0 + (p.y - from.y0) * scaleY);
}


bool
Document::SetAnnotationBounds(int pageNo, int index, fz_rect bounds, bool resize)
{
	if (FixedStore())
		return StoreSetBounds(pageNo, index, bounds, resize);
	if (!CanDrawAnnotations() || pageNo < 1 || pageNo > fPageCount)
		return false;

	BString operation(resize ? B_TRANSLATE("Resize annotation") : B_TRANSLATE("Move annotation"));
	std::vector<fz_point> points(kMaxInkPoints);
	std::vector<int> strokeSizes(kMaxInkStrokes);
	DocumentLocker locker(this);
	fz_page* page = NULL;
	int ok = 0;
	int began = 0;

	fz_var(page);
	fz_try(fContext) {
		page = fz_load_page(fContext, fDocument, pageNo - 1);
		pdf_page* pdfPage = pdf_page_from_fz_page(fContext, page);
		pdf_annot* annot = FindAnnotation(fContext, pdfPage, index);
		if (annot != NULL) {
			fz_rect old = pdf_bound_annot(fContext, annot);
			int type = pdf_annot_type(fContext, annot);
			float oldWidth = old.x1 - old.x0, oldHeight = old.y1 - old.y0;
			// a note keeps its size, and an extent of nothing (a straight line) cannot be scaled
			float scaleX = 1, scaleY = 1;
			if (type != PDF_ANNOT_TEXT) {
				if (oldWidth > 0.01f)
					scaleX = (bounds.x1 - bounds.x0) / oldWidth;
				if (oldHeight > 0.01f)
					scaleY = (bounds.y1 - bounds.y0) / oldHeight;
			}

			BEGIN_EDIT(operation.String())
			if (type == PDF_ANNOT_LINE) {
				fz_point a, b;
				pdf_annot_line(fContext, annot, &a, &b);
				pdf_set_annot_line(fContext, annot, MapPoint(a, old, bounds, scaleX, scaleY),
					MapPoint(b, old, bounds, scaleX, scaleY));
			} else if (type == PDF_ANNOT_INK) {
				int strokes = pdf_annot_ink_list_count(fContext, annot);
				int total = 0;
				if (strokes > kMaxInkStrokes)
					fz_throw(fContext, FZ_ERROR_ARGUMENT, "too many strokes");
				for (int i = 0; i < strokes; i++) {
					int n = pdf_annot_ink_list_stroke_count(fContext, annot, i);
					if (total + n > kMaxInkPoints)
						fz_throw(fContext, FZ_ERROR_ARGUMENT, "too many points");
					strokeSizes[i] = n;
					for (int k = 0; k < n; k++)
						points[total++] = MapPoint(pdf_annot_ink_list_stroke_vertex(fContext, annot, i, k), old,
							bounds, scaleX, scaleY);
				}
				pdf_clear_annot_ink_list(fContext, annot);
				int at = 0;
				for (int i = 0; i < strokes; i++) {
					pdf_add_annot_ink_list_stroke(fContext, annot);
					for (int k = 0; k < strokeSizes[i]; k++)
						pdf_add_annot_ink_list_stroke_vertex(fContext, annot, points[at++]);
				}
			} else {
				fz_rect rect = pdf_annot_rect(fContext, annot);
				fz_point corner0 = MapPoint(fz_make_point(rect.x0, rect.y0), old, bounds, scaleX, scaleY);
				fz_point corner1 = MapPoint(fz_make_point(rect.x1, rect.y1), old, bounds, scaleX, scaleY);
				pdf_set_annot_rect(fContext, annot, fz_make_rect(corner0.x, corner0.y, corner1.x, corner1.y));
			}
			pdf_set_annot_modification_date(fContext, annot, (int64_t)time(NULL));
			pdf_update_annot(fContext, annot);
			END_EDIT()
			ok = 1;
		}
	}
	fz_always(fContext) {
		fz_drop_page(fContext, page);
	}
	fz_catch(fContext) {
		LogError(fContext, "cannot move or resize an annotation");
		ABANDON_EDIT()
		ok = 0;
	}
	if (ok)
		RecordOperation(pageNo, operation.String());
	return ok != 0;
}


bool
Document::FindAnnotationById(const char* id, int* _page, int* _index)
{
	if (id == NULL || id[0] == '\0')
		return false;
	if (UsesStore()) {
		DocumentLocker locker(this);
		ResolveStore();
		for (size_t i = 0; i < fStore.size(); i++) {
			if (fStore[i].id != id || fResolved[i].empty())
				continue;
			int page = fResolved[i][0].page;
			std::vector<DocAnnotation> onPage;
			StoreAnnotationsOnPage(page, onPage);
			for (size_t k = 0; k < onPage.size(); k++) {
				if (onPage[k].id == id) {
					*_page = page;
					*_index = onPage[k].index;
					return true;
				}
			}
		}
		return false;
	}
	if (!fIsPDF)
		return false;

	DocumentLocker locker(this);
	for (int pageNo = 1; pageNo <= fPageCount; pageNo++) {
		fz_page* page = NULL;
		int found = -1;

		fz_var(page);
		fz_try(fContext) {
			page = fz_load_page(fContext, fDocument, pageNo - 1);
			pdf_page* pdfPage = pdf_page_from_fz_page(fContext, page);
			int n = 0;
			for (pdf_annot* annot = pdf_first_annot(fContext, pdfPage); annot != NULL && found < 0;
					annot = pdf_next_annot(fContext, annot)) {
				if (!IsListedAnnotation(pdf_annot_type(fContext, annot)))
					continue;
				const char* name = pdf_annot_name(fContext, annot);
				if (name != NULL && strcmp(name, id) == 0)
					found = n;
				n++;
			}
		}
		fz_always(fContext) {
			fz_drop_page(fContext, page);
		}
		fz_catch(fContext) {
			found = -1;
		}

		if (found >= 0) {
			*_page = pageNo;
			*_index = found;
			return true;
		}
	}
	return false;
}


// does the page have annotations that are listed, without loading the page: looks at the subtypes in the page
// dictionary (most /Annots are only links)
static bool
PageHasListedAnnotation(fz_context* context, pdf_document* pdf, int pageIndex)
{
	int found = 0;
	fz_var(found);
	fz_try(context) {
		pdf_obj* annots = pdf_dict_get(context, pdf_lookup_page_obj(context, pdf, pageIndex), PDF_NAME(Annots));
		int count = pdf_array_len(context, annots);
		for (int i = 0; i < count && !found; i++) {
			pdf_obj* subtype = pdf_dict_get(context, pdf_array_get(context, annots, i), PDF_NAME(Subtype));
			if (subtype != PDF_NAME(Link) && subtype != PDF_NAME(Popup) && subtype != PDF_NAME(Widget))
				found = 1;
		}
	}
	fz_catch(context) {
		found = 0;
	}
	return found != 0;
}


// a line of the text that is under a mark, for the list
static BString
Excerpt(const DocAnnotation& annotation, const char* text)
{
	BString result;
	if (annotation.isMarkup)
		result = text;
	else
		result = annotation.contents;
	// one line of reasonable length
	result.ReplaceAll("\r", " ");
	result.ReplaceAll("\n", " ");
	while (result.FindFirst("  ") >= 0)
		result.ReplaceAll("  ", " ");
	result.Trim();
	if (result.CountChars() > 90) {
		result.TruncateChars(90);
		result << "\xe2\x80\xa6";	// an ellipsis
	}
	return result;
}


// the annotations of one page with the text they cover; holds the lock for this page only
void
Document::ListPage(int pageNo, std::vector<DocAnnotationEntry>& entries)
{
	DocumentLocker locker(this);
	if (UsesStore()) {
		if (!StorePageHasParts(pageNo))
			return;
		// the marks of a book are known with the words, no page is needed
		std::vector<DocAnnotation> marks;
		StoreAnnotationsOnPage(pageNo, marks);
		for (size_t i = 0; i < marks.size(); i++) {
			if (marks[i].continued)
				continue;
			DocAnnotationEntry entry;
			entry.page = pageNo;
			entry.annotation = marks[i];
			entry.excerpt = Excerpt(marks[i], marks[i].quote.String());
			entries.push_back(entry);
		}
		return;
	} else if (!fIsPDF || !PageHasListedAnnotation(fContext, pdf_specifics(fContext, fDocument), pageNo - 1))
		return;

	std::vector<DocAnnotation> annotations;
	fz_page* page = NULL;
	fz_stext_page* text = NULL;
	std::vector<BString> excerpts;

	fz_var(page);
	fz_var(text);
	int loaded = 0;
	fz_try(fContext) {
		page = fz_load_page(fContext, fDocument, pageNo - 1);
		loaded = 1;
	}
	fz_catch(fContext) {
		loaded = 0;
	}
	if (!loaded)
		return;

	LoadAnnotations(pageNo, page, annotations);

	for (size_t i = 0; i < annotations.size(); i++) {
		const DocAnnotation& annotation = annotations[i];
		if (annotation.continued)
			continue;	// listed with its first part
		char* copied = NULL;
		if (annotation.isMarkup && !annotation.quads.empty() && !UsesStore()) {
			const fz_quad& first = annotation.quads.front();
			const fz_quad& last = annotation.quads.back();
			fz_point a = fz_make_point(first.ul.x + 0.5f, (first.ul.y + first.ll.y) / 2);
			fz_point b = fz_make_point(last.ur.x - 0.5f, (last.ur.y + last.lr.y) / 2);
			fz_var(copied);
			fz_try(fContext) {
				if (text == NULL)
					text = fz_new_stext_page_from_page(fContext, page, NULL);
				copied = fz_copy_selection(fContext, text, a, b, 0);
			}
			fz_catch(fContext) {
				copied = NULL;
			}
		}
		DocAnnotationEntry entry;
		entry.page = pageNo;
		entry.annotation = annotation;
		entry.excerpt = Excerpt(annotation, UsesStore() ? annotation.quote.String() : copied != NULL ? copied : "");
		fz_free(fContext, copied);
		entries.push_back(entry);
	}

	fz_drop_stext_page(fContext, text);
	fz_drop_page(fContext, page);
}


bool
Document::ListAnnotations(std::vector<DocAnnotationEntry>& entries, const volatile bool* cancel)
{
	if (!fIsPDF && !UsesStore())
		return false;

	for (int pageNo = 1; pageNo <= fPageCount; pageNo++) {
		if (cancel != NULL && *cancel)
			return false;
		ListPage(pageNo, entries);
	}
	return true;
}


bool
Document::ListAnnotationsOnPage(int pageNo, std::vector<DocAnnotationEntry>& entries)
{
	if ((!fIsPDF && !UsesStore()) || pageNo < 1 || pageNo > fPageCount)
		return false;

	ListPage(pageNo, entries);
	return true;
}


// Remembers an edit that has been made: what it was and on which page, to offer it for undo.
void
Document::RecordOperation(int page, const char* name)
{
	int steps = 0;
	int position = 0;
	fz_try(fContext) {
		position = pdf_undoredo_state(fContext, pdf_specifics(fContext, fDocument), &steps);
	}
	fz_catch(fContext) {
		position = 0;
	}

	if (position <= 0) {
		// no journal entry, so there is nothing to undo; the document is changed all the same
		fModified = true;
		return;
	}

	HistoryEntry entry;
	entry.name = name;
	entry.page = page;
	fHistory.resize(position - 1);	// a new edit ends what could be redone
	fHistory.push_back(entry);
	fHistoryPosition = position;
	fModified = fHistoryPosition != fSavedPosition;
}


BString
Document::UndoLabel() const
{
	if (UsesStore())
		return fStoreUndo.empty() ? BString() : fStoreUndo.back().name;
	return fHistoryPosition > 0 ? fHistory[fHistoryPosition - 1].name : BString();
}


BString
Document::RedoLabel() const
{
	if (UsesStore())
		return fStoreRedo.empty() ? BString() : fStoreRedo.back().name;
	return fHistoryPosition < (int)fHistory.size() ? fHistory[fHistoryPosition].name : BString();
}


// Takes back the last edit; returns the page (1-based) it was on, 0 if there is nothing to undo.
int
Document::Undo()
{
	if (!CanUndo())
		return 0;
	if (UsesStore())
		return StoreUndoRedo(true);

	DocumentLocker locker(this);
	int ok = 0;
	fz_try(fContext) {
		pdf_undo(fContext, pdf_specifics(fContext, fDocument));
		ok = 1;
	}
	fz_catch(fContext) {
		LogError(fContext, "cannot undo");
	}
	if (!ok)
		return 0;

	fHistoryPosition--;
	fModified = fHistoryPosition != fSavedPosition;
	return fHistory[fHistoryPosition].page;
}


int
Document::Redo()
{
	if (!CanRedo())
		return 0;
	if (UsesStore())
		return StoreUndoRedo(false);

	DocumentLocker locker(this);
	int ok = 0;
	fz_try(fContext) {
		pdf_redo(fContext, pdf_specifics(fContext, fDocument));
		ok = 1;
	}
	fz_catch(fContext) {
		LogError(fContext, "cannot redo");
	}
	if (!ok)
		return 0;

	fHistoryPosition++;
	fModified = fHistoryPosition != fSavedPosition;
	return fHistory[fHistoryPosition - 1].page;
}


// what has been written to disk is not undone any more (the file is loaded again after it changed)
void
Document::ForgetHistory()
{
	fz_try(fContext) {
		pdf_document* pdf = pdf_specifics(fContext, fDocument);
		if (pdf->journal != NULL)
			pdf_discard_journal(fContext, pdf->journal);
	}
	fz_catch(fContext) {
	}
	fHistory.clear();
	fHistoryPosition = 0;
	fSavedPosition = 0;
}


// how many annotations that are listed a page has, from its dictionary (no page is loaded)
static int
CountListedAnnotations(fz_context* context, pdf_document* pdf, int pageIndex)
{
	int count = 0;
	fz_var(count);
	fz_try(context) {
		pdf_obj* annots = pdf_dict_get(context, pdf_lookup_page_obj(context, pdf, pageIndex), PDF_NAME(Annots));
		int length = pdf_array_len(context, annots);
		for (int i = 0; i < length; i++) {
			pdf_obj* subtype = pdf_dict_get(context, pdf_array_get(context, annots, i), PDF_NAME(Subtype));
			if (subtype != PDF_NAME(Link) && subtype != PDF_NAME(Popup) && subtype != PDF_NAME(Widget))
				count++;
		}
	}
	fz_catch(context) {
	}
	return count;
}


bool
Document::PageMayHaveAnnotations(int page)
{
	if (!fIsPDF || page < 1 || page > fPageCount)
		return false;
	DocumentLocker locker(this);
	return PageHasListedAnnotation(fContext, pdf_specifics(fContext, fDocument), page - 1);
}


int
Document::AnnotationCount()
{
	DocumentLocker locker(this);
	if (UsesStore())
		return (int)fStore.size();
	if (!fIsPDF)
		return 0;

	pdf_document* pdf = pdf_specifics(fContext, fDocument);
	int count = 0;
	for (int i = 0; i < fPageCount; i++)
		count += CountListedAnnotations(fContext, pdf, i);
	return count;
}


void
Document::SyncAnnotationCount(const char* path)
{
	if (!fIsPDF && !UsesStore())
		return;

	BNode node(path);
	if (node.InitCheck() != B_OK)
		return;

	int32 count = AnnotationCount();
	int32 value = 0;
	bool present = node.ReadAttr("SEN:annotationCount", B_INT32_TYPE, 0, &value, sizeof(value)) == sizeof(value);
	if (count > 0 && (!present || value != count))
		node.WriteAttr("SEN:annotationCount", B_INT32_TYPE, 0, &count, sizeof(count));
	else if (count == 0 && present)
		node.RemoveAttr("SEN:annotationCount");
}


BString
ForeignAnnotationKey(int page, const DocAnnotation& a)
{
	char key[160];
	snprintf(key, sizeof(key), "%d|%d|%.0f,%.0f,%.0f,%.0f|", page, a.type, a.rect.x0, a.rect.y0, a.rect.x1, a.rect.y1);
	BString result(key);
	result.Append(a.contents.String(), a.contents.Length() < 48 ? a.contents.Length() : 48);
	return result;
}


int
Document::UpgradeAnnotationIds()
{
	if (!fIsPDF)
		return 0;

	// the identifiers that were given before, with the keys of their annotations
	std::map<BString, BString> given;
	{
		BNode node(fPath.String());
		attr_info info;
		if (node.InitCheck() == B_OK && node.GetAttrInfo("SEN:annotations", &info) == B_OK && info.size > 0
			&& info.size < 16 * 1024 * 1024) {
			char* buffer = new char[info.size];
			BMessage archive;
			if (node.ReadAttr("SEN:annotations", info.type, 0, buffer, info.size) == info.size
				&& archive.Unflatten(buffer) == B_OK) {
				BMessage item;
				for (int32 i = 0; archive.FindMessage("oa:Annotation", i, &item) == B_OK; i++) {
					BString key, id;
					if (item.FindString("sen:key", &key) == B_OK && item.FindString("id", &id) == B_OK)
						given[key] = WebAnnotation::IdentifierKey(id.String());
				}
			}
			delete[] buffer;
		}
	}

	DocumentLocker locker(this);
	int named = 0, total = 0;
	for (int pageNo = 1; pageNo <= fPageCount; pageNo++) {
		if (!PageMayHaveAnnotations(pageNo))
			continue;
		fz_page* page = NULL;
		fz_var(page);
		fz_try(fContext) {
			page = fz_load_page(fContext, fDocument, pageNo - 1);
			pdf_page* pdfPage = pdf_page_from_fz_page(fContext, page);
			for (pdf_annot* annot = pdf_first_annot(fContext, pdfPage); annot != NULL;
					annot = pdf_next_annot(fContext, annot)) {
				int type = pdf_annot_type(fContext, annot);
				if (!IsListedAnnotation(type))
					continue;
				total++;
				const char* nm = pdf_annot_name(fContext, annot);
				if (nm != NULL && nm[0] != '\0')
					continue;
				DocAnnotation probe;
				probe.type = type;
				probe.rect = pdf_bound_annot(fContext, annot);
				const char* contents = pdf_annot_contents(fContext, annot);
				probe.contents = contents != NULL ? contents : "";
				BString key = ForeignAnnotationKey(pageNo, probe);
				std::map<BString, BString>::iterator found = given.find(key);
				char id[48];
				if (found != given.end())
					strlcpy(id, found->second.String(), sizeof(id));
				else
					WebAnnotation::NewId(id, sizeof(id));
				pdf_set_annot_name(fContext, annot, id);
				named++;
			}
		}
		fz_catch(fContext) {
			LogError(fContext, "cannot name the annotations of a page");
		}
		fz_drop_page(fContext, page);
	}

	// the description in the attribute (only if there is something to say, or it is not up to date)
	if (total > 0 || !given.empty())
		WriteWebAnnotations(fPath.String());
	return named;
}


// no lock, the window asks this all the time while a page is rendered
bool
Document::HasUnsavedChanges()
{
	return fModified;
}


bool
Document::CanSave()
{
	return fCanSave;
}


bool
Document::Save()
{
	if (UsesStore()) {
		DocumentLocker locker(this);
		if (!WriteStore(fPath.String()))
			return false;
		fModified = false;
		fStoreSavedDepth = fStoreUndo.size();
		SyncAnnotationCount(fPath.String());
		return true;
	}
	if (!fIsPDF)
		return false;

	DocumentLocker locker(this);
	int ok = 0;
	fz_try(fContext) {
		pdf_write_options options = pdf_default_write_options;
		options.do_incremental = 1;
		pdf_save_document(fContext, pdf_specifics(fContext, fDocument), fPath.String(), &options);
		ok = 1;
	}
	fz_catch(fContext) {
		LogError(fContext, "cannot save document");
		ok = 0;
	}
	if (ok) {
		fModified = false;
		ForgetHistory();
		SyncAnnotationCount(fPath.String());
		WriteWebAnnotations(fPath.String());
	}
	return ok != 0;
}


// copies all attributes (bookmarks and position of Toji, the type, ...) from one file to the other
void
CopyAttributes(const char* from, const char* to)
{
	BNode source(from), target(to);
	if (source.InitCheck() != B_OK || target.InitCheck() != B_OK)
		return;

	char name[B_ATTR_NAME_LENGTH];
	source.RewindAttrs();
	while (source.GetNextAttrName(name) == B_OK) {
		attr_info info;
		// SYS:PACKAGE says which package the original belongs to, the copy does not
		if (strncmp(name, "SYS:PACKAGE", 11) == 0)
			continue;
		if (source.GetAttrInfo(name, &info) != B_OK || info.size > 1024 * 1024)
			continue;
		char* buffer = new char[info.size > 0 ? info.size : 1];
		ssize_t size = source.ReadAttr(name, info.type, 0, buffer, info.size);
		if (size >= 0)
			target.WriteAttr(name, info.type, 0, buffer, size);
		delete[] buffer;
	}
}


bool
Document::SaveCopy(const char* path)
{
	if (UsesStore())
		return fPath != path && StoreSaveCopy(path);
	if (!fIsPDF || fPath == path)
		return false;

	DocumentLocker locker(this);
	int ok = 0;
	fz_try(fContext) {
		pdf_write_options options = pdf_default_write_options;
		pdf_save_document(fContext, pdf_specifics(fContext, fDocument), path, &options);
		ok = 1;
	}
	fz_catch(fContext) {
		LogError(fContext, "cannot save a copy");
		ok = 0;
	}
	if (ok) {
		CopyAttributes(fPath.String(), path);
		fModified = false;
		ForgetHistory();
		SyncAnnotationCount(path);
		WriteWebAnnotations(path);
	}
	return ok != 0;
}

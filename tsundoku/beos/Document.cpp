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

#include "Document.h"

#include <math.h>
#include <time.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <File.h>
#include <Catalog.h>
#include <Node.h>
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


static void
LogError(fz_context* context, const char* what)
{
	fprintf(stderr, "Tsundoku: %s: %s\n", what, fz_caught_message(context));
}


Document::OpenResult
Document::Open(const char* path, const char* password, Document** _document)
{
	pthread_once(&sMutexesInitialized, InitMutexes);

	fz_context* context = fz_new_context(NULL, &sLocks, kStoreSize);
	if (context == NULL)
		return kFailed;

	fz_document* document = NULL;
	bool needsPassword = false;
	bool failed = false;

	fz_var(document);
	fz_try(context) {
		fz_register_document_handlers(context);
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
		fz_drop_document(context, document);
		fz_drop_context(context);
		return failed ? kFailed : kNeedsPassword;
	}

	Document* result = new Document(context, document, path);
	if (result->fPageCount <= 0) {
		delete result;
		return kFailed;
	}
	*_document = result;
	return kOpened;
}


Document::Document(fz_context* context, fz_document* document, const char* path)
	:
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
	fSavedPosition(0)
{
	int pages = 0;
	int isPDF = 0;
	int encrypted = 0;
	int canSave = 0;

	fz_try(fContext) {
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
}


Document::~Document()
{
	fLock.Lock();
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


bool
Document::PageBounds(int page, fz_rect* bounds)
{
	if (page < 1 || page > fPageCount)
		return false;

	DocumentLocker locker(this);
	int index = page - 1;
	if (!fBoundsKnown[index]) {
		fz_rect rect = fz_empty_rect;
		fz_page* fzPage = NULL;
		bool ok = true;

		fz_var(fzPage);
		fz_var(rect);
		fz_try(fContext) {
			fzPage = fz_load_page(fContext, fDocument, index);
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

	if (page >= 1 && page <= fPageCount) {
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


// the annotations that are shown in the list: all but links, popups (belong to a note) and form fields
static bool
IsListedAnnotation(int type)
{
	return type != PDF_ANNOT_LINK && type != PDF_ANNOT_POPUP && type != PDF_ANNOT_WIDGET;
}


bool
Document::CanEditAnnotations()
{
	return fIsPDF && CanAnnotate();
}


bool
Document::LoadAnnotations(fz_page* page, std::vector<DocAnnotation>& annotations)
{
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
		char contents[1024], author[128];
		contents[0] = author[0] = '\0';
		int ok = 0;
		pdf_annot* next = NULL;
		int colorCount = 0;
		float colorValue[4] = { 0, 0, 0, 0 };

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
			entry.rect = rect;
			entry.quads.assign(quads.begin(), quads.begin() + quadCount);
			entry.contents = contents;
			entry.author = author;
			entry.isMarkup = type == PDF_ANNOT_HIGHLIGHT || type == PDF_ANNOT_UNDERLINE
				|| type == PDF_ANNOT_STRIKE_OUT || type == PDF_ANNOT_SQUIGGLY;
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

	static const int types[] = { PDF_ANNOT_HIGHLIGHT, PDF_ANNOT_UNDERLINE, PDF_ANNOT_STRIKE_OUT,
		PDF_ANNOT_SQUIGGLY };
	const char* names[] = { B_TRANSLATE("Add highlight"), B_TRANSLATE("Add underline"),
		B_TRANSLATE("Add strike out"), B_TRANSLATE("Add squiggly line") };
	BString operation(names[type]);

	const char* author = getenv("USER");
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
Document::SetAnnotationColor(int pageNo, int index, uint32 rgb)
{
	if (!CanEditAnnotations() || pageNo < 1 || pageNo > fPageCount)
		return false;

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
	return fHistoryPosition > 0 ? fHistory[fHistoryPosition - 1].name : BString();
}


BString
Document::RedoLabel() const
{
	return fHistoryPosition < (int)fHistory.size() ? fHistory[fHistoryPosition].name : BString();
}


// Takes back the last edit; returns the page (1-based) it was on, 0 if there is nothing to undo.
int
Document::Undo()
{
	if (!CanUndo())
		return 0;

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
	}
	return ok != 0;
}


// copies all attributes (bookmarks and position of Tsundoku, the type, ...) from one file to the other
static void
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
	}
	return ok != 0;
}

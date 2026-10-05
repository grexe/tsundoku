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


// Annotations drawn on a page of a comic book: notes, text, rectangles, ellipses, lines, arrows and drawings. A comic
// book is an archive of images, it has neither text to mark nor a place to keep annotations, so they are kept in an
// attribute of the file (DocumentStore.cpp, the store) like the marks of a book and told by the page and a place on
// it, in fractions of the page. They are drawn into the rendering of the page, as the marks of a book.
//
// Note: fz_try() uses setjmp()/longjmp(), so no C++ objects with destructors may be created or destroyed inside
// of a fz_try() block.

#include "Document.h"
#include "WebAnnotation.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <Catalog.h>

extern "C" {
#include <mupdf/pdf.h>
}

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "Document"



// The size of things that are the same size on every page: a page of 600 by 800 points (like the pages of most PDF
// files) is 1, the pages of a comic book are as large as the images are.
static float
PageScale(const fz_rect& bounds)
{
	float area = (bounds.x1 - bounds.x0) * (bounds.y1 - bounds.y0);
	return area > 0 ? sqrtf(area / (600.0f * 800.0f)) : 1;
}


static fz_point
ToFraction(const fz_rect& bounds, fz_point point)
{
	return fz_make_point((point.x - bounds.x0) / (bounds.x1 - bounds.x0),
		(point.y - bounds.y0) / (bounds.y1 - bounds.y0));
}


static fz_point
FromFraction(const fz_rect& bounds, fz_point point)
{
	return fz_make_point(bounds.x0 + point.x * (bounds.x1 - bounds.x0), bounds.y0 + point.y * (bounds.y1 - bounds.y0));
}


static fz_rect
RectFromFraction(const fz_rect& bounds, const fz_rect& box)
{
	fz_point a = FromFraction(bounds, fz_make_point(box.x0, box.y0));
	fz_point b = FromFraction(bounds, fz_make_point(box.x1, box.y1));
	return fz_make_rect(a.x, a.y, b.x, b.y);
}


// moves a box into the page, if it is not too large for it
static void
KeepInPage(fz_rect* box)
{
	float width = box->x1 - box->x0, height = box->y1 - box->y0;
	if (box->x1 > 1)
		box->x0 = fmaxf(0, 1 - width);
	if (box->y1 > 1)
		box->y0 = fmaxf(0, 1 - height);
	box->x1 = box->x0 + width;
	box->y1 = box->y0 + height;
}


static fz_rect
BoxOf(const std::vector<std::vector<fz_point> >& paths)
{
	fz_rect box = fz_make_rect(2, 2, -1, -1);
	for (size_t i = 0; i < paths.size(); i++) {
		for (size_t k = 0; k < paths[i].size(); k++) {
			box.x0 = fminf(box.x0, paths[i][k].x);
			box.x1 = fmaxf(box.x1, paths[i][k].x);
			box.y0 = fminf(box.y0, paths[i][k].y);
			box.y1 = fmaxf(box.y1, paths[i][k].y);
		}
	}
	return box;
}


///////////////////////////////////////////////////////////////////////////
// Adding

bool
Document::StoreAddDrawn(int pageNo, StoredAnnotation* a, const char* operation)
{
	char id[48];
	WebAnnotation::NewId(id, sizeof(id));
	a->id = id;
	const char* author = getenv("USER");
	a->author = author != NULL ? author : "";
	a->created = (int64)time(NULL);
	a->page = pageNo;

	PushStoreUndo(operation, pageNo);
	fStore.push_back(*a);
	// where it is, is known: on the page
	StoredPart part;
	part.page = pageNo;
	fResolved.push_back(std::vector<StoredPart>(1, part));
	fResolvedKnown.push_back(1);
	return true;
}


bool
Document::StoreAddNote(int pageNo, fz_point where, const char* text)
{
	if (!CanDrawAnnotations() || pageNo < 1 || pageNo > fPageCount)
		return false;

	DocumentLocker locker(this);
	fz_rect bounds;
	if (!PageBounds(pageNo, &bounds))
		return false;
	float size = 20 * PageScale(bounds);
	StoredAnnotation a;
	a.kind = kAnnotNote;
	a.color = 0xffeb3b;
	a.contents = text != NULL ? text : "";
	fz_point at = ToFraction(bounds, where);
	a.box = fz_make_rect(at.x, at.y, at.x + size / (bounds.x1 - bounds.x0), at.y + size / (bounds.y1 - bounds.y0));
	KeepInPage(&a.box);
	return StoreAddDrawn(pageNo, &a, B_TRANSLATE("Add note"));
}


bool
Document::StoreAddFreeText(int pageNo, fz_point where, const char* text)
{
	if (!CanDrawAnnotations() || pageNo < 1 || pageNo > fPageCount || text == NULL)
		return false;

	DocumentLocker locker(this);
	fz_rect bounds;
	if (!PageBounds(pageNo, &bounds))
		return false;
	// the box is as wide as a paragraph and as high as its lines, a line is about 34 characters
	const float kFontSize = 12;
	int lines = 1, column = 0;
	for (const char* p = text; *p != '\0'; p++) {
		if (*p == '\n' || ++column > 34) {
			lines++;
			column = 0;
		}
	}
	float scale = PageScale(bounds);
	StoredAnnotation a;
	a.kind = kAnnotText;
	a.color = 0x000000;
	a.contents = text;
	fz_point at = ToFraction(bounds, where);
	a.box = fz_make_rect(at.x, at.y, at.x + 220 * scale / (bounds.x1 - bounds.x0),
		at.y + (lines * (kFontSize + 3) + 8) * scale / (bounds.y1 - bounds.y0));
	KeepInPage(&a.box);
	return StoreAddDrawn(pageNo, &a, B_TRANSLATE("Add text"));
}


bool
Document::StoreAddShape(int pageNo, ShapeType type, fz_point from, fz_point to, uint32 rgb)
{
	if (!CanDrawAnnotations() || pageNo < 1 || pageNo > fPageCount)
		return false;

	DocumentLocker locker(this);
	fz_rect bounds;
	if (!PageBounds(pageNo, &bounds))
		return false;
	const char* names[] = { B_TRANSLATE("Add rectangle"), B_TRANSLATE("Add ellipse"), B_TRANSLATE("Add line"),
		B_TRANSLATE("Add arrow") };
	fz_point a0 = ToFraction(bounds, from), a1 = ToFraction(bounds, to);

	StoredAnnotation a;
	a.color = rgb & 0xffffff;
	a.box = fz_make_rect(fminf(a0.x, a1.x), fminf(a0.y, a1.y), fmaxf(a0.x, a1.x), fmaxf(a0.y, a1.y));
	switch (type) {
		case kShapeEllipse:
			a.kind = kAnnotEllipse;
			break;
		case kShapeLine:
		case kShapeArrow: {
			a.kind = kAnnotLine;
			a.arrow = type == kShapeArrow;
			std::vector<fz_point> path;
			path.push_back(a0);
			path.push_back(a1);
			a.paths.push_back(path);
			break;
		}
		default:
			a.kind = kAnnotRectangle;
			break;
	}
	return StoreAddDrawn(pageNo, &a, names[type]);
}


bool
Document::StoreAddInk(int pageNo, const fz_point* points, int count, uint32 rgb)
{
	if (!CanDrawAnnotations() || pageNo < 1 || pageNo > fPageCount || count < 2)
		return false;

	DocumentLocker locker(this);
	fz_rect bounds;
	if (!PageBounds(pageNo, &bounds))
		return false;
	StoredAnnotation a;
	a.kind = kAnnotInk;
	a.color = rgb & 0xffffff;
	std::vector<fz_point> stroke;
	for (int i = 0; i < count; i++)
		stroke.push_back(ToFraction(bounds, points[i]));
	a.paths.push_back(stroke);
	a.box = BoxOf(a.paths);
	return StoreAddDrawn(pageNo, &a, B_TRANSLATE("Add drawing"));
}


///////////////////////////////////////////////////////////////////////////
// Moving and resizing

// Fits the annotation into the new bounds; a note only follows the corner.
bool
Document::StoreSetBounds(int pageNo, int index, fz_rect bounds, bool resize)
{
	if (!CanDrawAnnotations() || pageNo < 1 || pageNo > fPageCount)
		return false;

	DocumentLocker locker(this);
	int at;
	if (!StoreIndexFor(pageNo, index, &at) || fStore[at].page != pageNo)
		return false;
	fz_rect page;
	if (!PageBounds(pageNo, &page))
		return false;

	StoredAnnotation next = fStore[at];
	fz_point a = ToFraction(page, fz_make_point(bounds.x0, bounds.y0));
	fz_point b = ToFraction(page, fz_make_point(bounds.x1, bounds.y1));
	fz_rect box = fz_make_rect(fminf(a.x, b.x), fminf(a.y, b.y), fmaxf(a.x, b.x), fmaxf(a.y, b.y));
	const fz_rect old = next.box;
	if (next.kind == kAnnotNote) {
		float width = old.x1 - old.x0, height = old.y1 - old.y0;
		box = fz_make_rect(box.x0, box.y0, box.x0 + width, box.y0 + height);
	}

	float oldWidth = old.x1 - old.x0, oldHeight = old.y1 - old.y0;
	float newWidth = box.x1 - box.x0, newHeight = box.y1 - box.y0;
	for (size_t i = 0; i < next.paths.size(); i++) {
		for (size_t k = 0; k < next.paths[i].size(); k++) {
			fz_point& p = next.paths[i][k];
			p.x = box.x0 + (oldWidth > 0 ? (p.x - old.x0) / oldWidth * newWidth : 0);
			p.y = box.y0 + (oldHeight > 0 ? (p.y - old.y0) / oldHeight * newHeight : 0);
		}
	}
	next.box = box;

	PushStoreUndo(resize ? B_TRANSLATE("Resize annotation") : B_TRANSLATE("Move annotation"), pageNo);
	fStore[at] = next;
	return true;
}


///////////////////////////////////////////////////////////////////////////
// For the list on the page and the handles

void
Document::StoreDrawnOnPage(const StoredAnnotation& a, int index, DocAnnotation* entry)
{
	fz_rect bounds = fz_make_rect(0, 0, 1, 1);
	PageBounds(a.page, &bounds);

	static const int types[] = { PDF_ANNOT_UNKNOWN, PDF_ANNOT_UNKNOWN, PDF_ANNOT_TEXT, PDF_ANNOT_FREE_TEXT,
		PDF_ANNOT_SQUARE, PDF_ANNOT_CIRCLE, PDF_ANNOT_LINE, PDF_ANNOT_INK };
	entry->type = types[a.kind >= 0 && a.kind <= kAnnotInk ? a.kind : 0];
	entry->index = index;
	switch (a.kind) {
		case kAnnotNote:		entry->label = B_TRANSLATE("Note"); break;
		case kAnnotText:		entry->label = B_TRANSLATE("Text"); break;
		case kAnnotEllipse:		entry->label = B_TRANSLATE("Ellipse"); break;
		case kAnnotLine:		entry->label = a.arrow ? B_TRANSLATE("Arrow") : B_TRANSLATE("Line"); break;
		case kAnnotInk:			entry->label = B_TRANSLATE("Drawing"); break;
		default:				entry->label = B_TRANSLATE("Rectangle"); break;
	}
	entry->id = a.id;
	entry->rect = RectFromFraction(bounds, a.box);
	entry->contents = a.contents;
	entry->author = a.author;
	entry->isMarkup = false;
	entry->kind = a.kind;
	entry->isFreeText = a.kind == kAnnotText;
	entry->hasColor = true;
	entry->color = a.color;
	entry->continued = false;
	for (size_t i = 0; i < a.paths.size(); i++) {
		std::vector<fz_point> path;
		for (size_t k = 0; k < a.paths[i].size(); k++)
			path.push_back(FromFraction(bounds, a.paths[i][k]));
		entry->paths.push_back(path);
	}
}


///////////////////////////////////////////////////////////////////////////
// Drawing

static void
Fill(fz_context* context, fz_device* device, fz_matrix ctm, fz_path* path, const float rgb[3], float alpha)
{
	fz_fill_path(context, device, path, 0, ctm, fz_device_rgb(context), rgb, alpha, fz_default_color_params);
}


static void
Stroke(fz_context* context, fz_device* device, fz_matrix ctm, fz_path* path, fz_stroke_state* stroke,
	const float rgb[3])
{
	fz_stroke_path(context, device, path, stroke, ctm, fz_device_rgb(context), rgb, 1, fz_default_color_params);
}


static void
RectPath(fz_context* context, fz_path* path, const fz_rect& r)
{
	fz_moveto(context, path, r.x0, r.y0);
	fz_lineto(context, path, r.x1, r.y0);
	fz_lineto(context, path, r.x1, r.y1);
	fz_lineto(context, path, r.x0, r.y1);
	fz_closepath(context, path);
}


static void
EllipsePath(fz_context* context, fz_path* path, const fz_rect& r)
{
	const float kappa = 0.5522847f;
	float cx = (r.x0 + r.x1) / 2, cy = (r.y0 + r.y1) / 2, rx = (r.x1 - r.x0) / 2, ry = (r.y1 - r.y0) / 2;
	fz_moveto(context, path, cx + rx, cy);
	fz_curveto(context, path, cx + rx, cy + kappa * ry, cx + kappa * rx, cy + ry, cx, cy + ry);
	fz_curveto(context, path, cx - kappa * rx, cy + ry, cx - rx, cy + kappa * ry, cx - rx, cy);
	fz_curveto(context, path, cx - rx, cy - kappa * ry, cx - kappa * rx, cy - ry, cx, cy - ry);
	fz_curveto(context, path, cx + kappa * rx, cy - ry, cx + rx, cy - kappa * ry, cx + rx, cy);
	fz_closepath(context, path);
}


// The width of a text in a font of the size.
static float
TextWidth(fz_context* context, fz_font* font, const char* text, size_t length, float size)
{
	float width = 0;
	const char* end = text + length;
	while (text < end) {
		int rune;
		text += fz_chartorune(&rune, text);
		width += fz_advance_glyph(context, font, fz_encode_character(context, font, rune), 0) * size;
	}
	return width;
}


// Writes the text in the box, broken into lines at the spaces.
static void
DrawText(fz_context* context, fz_device* device, fz_matrix ctm, fz_font* font, const fz_rect& box, float size,
	const char* text, const float rgb[3])
{
	fz_text* run = fz_new_text(context);
	fz_try(context) {
		float padding = size * 0.35f;
		float maxWidth = box.x1 - box.x0 - 2 * padding;
		float y = box.y0 + padding + size;
		const char* p = text;
		while (*p != '\0' && y < box.y1 + size * 4) {
			const char* lineStart = p;
			const char* lastSpace = NULL;
			const char* q = p;
			float width = 0;
			while (*q != '\0' && *q != '\n') {
				int rune;
				int bytes = fz_chartorune(&rune, q);
				float advance = fz_advance_glyph(context, font, fz_encode_character(context, font, rune), 0) * size;
				if (width + advance > maxWidth && q > lineStart)
					break;
				if (rune == ' ')
					lastSpace = q;
				width += advance;
				q += bytes;
			}
			const char* end = q;
			if (*q != '\0' && *q != '\n' && lastSpace != NULL && lastSpace > lineStart)
				end = lastSpace;

			char line[1024];
			size_t length = (size_t)(end - lineStart);
			if (length > sizeof(line) - 1)
				length = sizeof(line) - 1;
			memcpy(line, lineStart, length);
			line[length] = '\0';
			fz_matrix trm = fz_make_matrix(size, 0, 0, -size, box.x0 + padding, y);
			fz_show_string(context, run, font, trm, line, 0, 0, FZ_BIDI_LTR, FZ_LANG_UNSET);

			p = end;
			if (*p == '\n' || *p == ' ')
				p++;
			y += size * 1.2f;
		}
		fz_fill_text(context, device, run, ctm, fz_device_rgb(context), rgb, 1, fz_default_color_params);
	}
	fz_always(context) {
		fz_drop_text(context, run);
	}
	fz_catch(context) {
		fz_rethrow(context);
	}
}


void
Document::PaintDrawn(const StoredAnnotation& a, fz_device* device, fz_matrix ctm, const fz_rect& bounds)
{
	fz_context* context = fContext;
	float rgb[3] = { ((a.color >> 16) & 0xff) / 255.0f, ((a.color >> 8) & 0xff) / 255.0f, (a.color & 0xff) / 255.0f };
	float scale = PageScale(bounds);
	float lineWidth = 2 * scale;
	fz_rect box = RectFromFraction(bounds, a.box);

	fz_path* path = NULL;
	fz_stroke_state* stroke = NULL;
	fz_font* font = NULL;
	fz_var(path);
	fz_var(stroke);
	fz_var(font);
	fz_try(context) {
		path = fz_new_path(context);
		stroke = fz_new_stroke_state(context);
		stroke->linewidth = lineWidth;
		stroke->start_cap = stroke->end_cap = FZ_LINECAP_ROUND;
		stroke->linejoin = FZ_LINEJOIN_ROUND;

		switch (a.kind) {
			case kAnnotNote: {
				// a small page with lines of text, in the color of the note
				RectPath(context, path, box);
				Fill(context, device, ctm, path, rgb, 1);
				const float dark[3] = { 0.25f, 0.25f, 0.25f };
				stroke->linewidth = fmaxf(1, scale * 0.8f);
				Stroke(context, device, ctm, path, stroke, dark);
				fz_drop_path(context, path);
				path = fz_new_path(context);
				float width = box.x1 - box.x0, height = box.y1 - box.y0;
				for (int i = 0; i < 3; i++) {
					float y = box.y0 + height * (0.28f + 0.22f * i);
					fz_moveto(context, path, box.x0 + width * 0.2f, y);
					fz_lineto(context, path, box.x1 - width * (i == 2 ? 0.45f : 0.2f), y);
				}
				Stroke(context, device, ctm, path, stroke, dark);
				break;
			}
			case kAnnotText: {
				// the text on a light ground, to be read over the drawing
				const float white[3] = { 1, 1, 1 };
				RectPath(context, path, box);
				Fill(context, device, ctm, path, white, 0.85f);
				font = fz_new_base14_font(context, "Helvetica");
				DrawText(context, device, ctm, font, box, 12 * scale, a.contents.String(), rgb);
				break;
			}
			case kAnnotEllipse:
				EllipsePath(context, path, box);
				Stroke(context, device, ctm, path, stroke, rgb);
				break;
			case kAnnotLine:
			case kAnnotInk:
				for (size_t i = 0; i < a.paths.size(); i++) {
					if (a.paths[i].size() < 2)
						continue;
					fz_drop_path(context, path);
					path = fz_new_path(context);
					fz_point first = FromFraction(bounds, a.paths[i][0]);
					fz_moveto(context, path, first.x, first.y);
					for (size_t k = 1; k < a.paths[i].size(); k++) {
						fz_point p = FromFraction(bounds, a.paths[i][k]);
						fz_lineto(context, path, p.x, p.y);
					}
					Stroke(context, device, ctm, path, stroke, rgb);

					if (a.kind == kAnnotLine && a.arrow && a.paths[i].size() >= 2) {
						// the head: two lines back from the end
						fz_point from = FromFraction(bounds, a.paths[i][a.paths[i].size() - 2]);
						fz_point to = FromFraction(bounds, a.paths[i].back());
						float dx = to.x - from.x, dy = to.y - from.y;
						float length = sqrtf(dx * dx + dy * dy);
						if (length > 0) {
							float head = fminf(length * 0.5f, 14 * scale);
							float ux = dx / length, uy = dy / length;
							fz_drop_path(context, path);
							path = fz_new_path(context);
							fz_moveto(context, path, to.x - head * (ux * 0.866f - uy * 0.5f),
								to.y - head * (uy * 0.866f + ux * 0.5f));
							fz_lineto(context, path, to.x, to.y);
							fz_lineto(context, path, to.x - head * (ux * 0.866f + uy * 0.5f),
								to.y - head * (uy * 0.866f - ux * 0.5f));
							Stroke(context, device, ctm, path, stroke, rgb);
						}
					}
				}
				break;
			default:
				RectPath(context, path, box);
				Stroke(context, device, ctm, path, stroke, rgb);
				break;
		}
	}
	fz_always(context) {
		fz_drop_font(context, font);
		fz_drop_stroke_state(context, stroke);
		fz_drop_path(context, path);
	}
	fz_catch(context) {
		fprintf(stderr, "Toji: cannot draw an annotation: %s\n", fz_caught_message(context));
	}
}

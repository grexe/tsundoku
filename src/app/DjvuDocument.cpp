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

// DjVu as a document of MuPDF. The pages are drawn by DjVuLibre at the size that is asked for and put into the page as an image;
// the hidden text layer (the words with their boxes) is put into it as invisible text, which is what MuPDF's text extraction
// expects of a scan that has been through OCR. The outline, the links and the metadata are those of the file.
//
// DjVuLibre keeps what it reports about a page (text, links, outline, metadata) as s-expressions that its own garbage collector
// frees. On Haiku that is broken in a way that has to be worked around (it was found by crashes in the garbage collector, in the
// locale code of libroot and at the end of threads): miniexp.cpp keeps the key of its thread-specific data in a static
// pthread_key_t that is created through a static pthread_once_t, which is zero. Haiku's PTHREAD_ONCE_INIT is -1, so the
// creation never happens, the key stays 0, and every thread that makes an s-expression stores its data under the key 0 of the
// process, which belongs to someone else (the ICU locale data of libroot, in Tsundoku). So the value of the key 0 is kept
// and put back around every call that makes s-expressions (KeyGuard); the garbage collector is switched off (its list of
// the threads would be wrong); and each of these things is asked for only once and kept in structures of our own, so
// that the s-expressions are never needed again.
//
// Note: fz_try() uses setjmp()/longjmp(), so no C++ objects with destructors may be created or destroyed inside
// of a fz_try() block.

#include "DjvuDocument.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <pthread.h>
#include <unistd.h>

#include <map>
#include <string>
#include <utility>
#include <vector>

#include <libdjvu/ddjvuapi.h>
#include <libdjvu/miniexp.h>

namespace Djvu {

namespace {

const int kDefaultDpi = 300;
const int kWaitMilliseconds = 60000;			// for what DjVuLibre decodes
const long kMaxPixels = 80L * 1000 * 1000;		// of a page that is drawn: 8000 by 10000


struct PageInfo {
	int width, height;		// in pixels, as they are shown (turned if the page is)
	int storedWidth, storedHeight;	// as the file has them
	int dpi;
	int rotation;			// quarter turns that the file asks for
	int known;
};


// A box in the coordinates of DjVu: pixels of the page, origin at the bottom left
struct Box {
	int x0, y0, x1, y1;
};

struct Word {
	Box box;
	std::string text;
};

struct Link {
	Box box;
	std::string url;
};

struct PageData {
	bool textKnown, linksKnown;
	std::vector<Word> words;
	std::vector<Link> links;
	PageData() : textKnown(false), linksKnown(false) {}
};

struct OutlineItem {
	int depth;
	std::string title, url;
};


// everything that was asked of DjVuLibre
struct Cache {
	std::map<int, PageData> pages;
	bool outlineKnown;
	std::vector<OutlineItem> outline;
	bool metadataKnown;
	std::vector<std::pair<std::string, std::string> > metadata;
	Cache() : outlineKnown(false), metadataKnown(false) {}
};


struct DjvuDoc {
	fz_document super;
	ddjvu_context_t* ddjvu;
	ddjvu_document_t* document;
	ddjvu_format_t* format;		// 24 bit RGB, top row first
	int pageCount;
	PageInfo* infos;
	Cache* cache;
};


struct DjvuPage {
	fz_page super;
	int number;				// from 0
	float width, height;		// in points
	int rotation;			// quarter turns, counter-clockwise
	int storedWidth, storedHeight;	// of the page as the file has it, in pixels
	int dpi;
};


// Keeps the thread-specific value of the key 0 while DjVuLibre makes s-expressions, see the note at the top.
class KeyGuard {
public:
	KeyGuard() : fSaved(pthread_getspecific(0)) {}
	~KeyGuard() { pthread_setspecific(0, fSaved); }

private:
	void* fSaved;
};


// what DjVuLibre has to say: errors are told, the rest is for it to know
void
Pump(DjvuDoc* doc)
{
	ddjvu_message_t* message;
	while ((message = ddjvu_message_peek(doc->ddjvu)) != NULL) {
		if (message->m_any.tag == DDJVU_ERROR)
			fprintf(stderr, "DjVu: %s\n", message->m_error.message != NULL ? message->m_error.message : "error");
		ddjvu_message_pop(doc->ddjvu);
	}
}


// waits for something that DjVuLibre does in threads of its own, for a while
template <class Done>
bool
Wait(DjvuDoc* doc, Done done)
{
	for (int waited = 0; waited < kWaitMilliseconds; waited++) {
		Pump(doc);
		if (done())
			return true;
		usleep(1000);
	}
	return false;
}


struct DocumentDone {
	DjvuDoc* doc;
	bool operator()() const { return ddjvu_document_decoding_done(doc->document); }
};


struct PageDone {
	ddjvu_page_t* page;
	bool operator()() const { return ddjvu_page_decoding_done(page); }
};


float
PointsPerPixel(int dpi)
{
	return 72.0f / (dpi > 0 ? dpi : kDefaultDpi);
}


// size and resolution of a page, found once
bool
GetInfo(DjvuDoc* doc, int number, PageInfo* info)
{
	if (doc->infos[number].known) {
		*info = doc->infos[number];
		return true;
	}
	ddjvu_pageinfo_t raw;
	ddjvu_status_t status = DDJVU_JOB_NOTSTARTED;
	for (int waited = 0; waited < kWaitMilliseconds; waited++) {
		Pump(doc);
		status = ddjvu_document_get_pageinfo(doc->document, number, &raw);
		if (status != DDJVU_JOB_STARTED && status != DDJVU_JOB_NOTSTARTED)
			break;
		usleep(1000);
	}
	if (status != DDJVU_JOB_OK || raw.width <= 0 || raw.height <= 0)
		return false;

	PageInfo result;
	result.rotation = raw.rotation & 3;
	// the size is that of the page as it is shown (DjVuLibre has turned it already if the page is turned by a quarter); the
	// page as the file has it is the other way round then
	bool turned = (result.rotation & 1) != 0;
	result.width = raw.width;
	result.height = raw.height;
	result.storedWidth = turned ? raw.height : raw.width;
	result.storedHeight = turned ? raw.width : raw.height;
	result.dpi = raw.dpi > 0 ? raw.dpi : kDefaultDpi;
	result.known = 1;
	doc->infos[number] = result;
	*info = result;
	return true;
}


///////////////////////////////////////////////////////////////////////////
// What DjVuLibre reports, as structures of ours (no MuPDF in here, so no long jumps)

// The zones of the text: (type x0 y0 x1 y1 child...), a child is another zone or the text of this one.
void
CollectWords(miniexp_t zone, std::vector<Word>* words)
{
	if (!miniexp_consp(zone) || !miniexp_symbolp(miniexp_car(zone)))
		return;
	Box box;
	int* values[4] = { &box.x0, &box.y0, &box.x1, &box.y1 };
	miniexp_t rest = miniexp_cdr(zone);
	for (int i = 0; i < 4; i++) {
		if (!miniexp_consp(rest) || !miniexp_numberp(miniexp_car(rest)))
			return;
		*values[i] = miniexp_to_int(miniexp_car(rest));
		rest = miniexp_cdr(rest);
	}
	for (; miniexp_consp(rest); rest = miniexp_cdr(rest)) {
		miniexp_t child = miniexp_car(rest);
		if (miniexp_stringp(child)) {
			const char* text = miniexp_to_str(child);
			if (text != NULL && text[0] != '\0') {
				Word word;
				word.box = box;
				word.text = text;
				words->push_back(word);
			}
		} else
			CollectWords(child, words);
	}
}


// the first string or the text of (url "href" "target"), the way that a link names where it goes
const char*
UrlOf(miniexp_t expr)
{
	if (miniexp_stringp(expr))
		return miniexp_to_str(expr);
	if (miniexp_consp(expr) && miniexp_cdr(expr) != miniexp_nil && miniexp_stringp(miniexp_cadr(expr)))
		return miniexp_to_str(miniexp_cadr(expr));
	return NULL;
}


// the box of a map area, (rect x y w h), (oval x y w h) or (poly x y x y ...)
bool
AreaBox(miniexp_t area, Box* box)
{
	if (!miniexp_consp(area) || !miniexp_symbolp(miniexp_car(area)))
		return false;
	int values[64];
	int count = 0;
	for (miniexp_t rest = miniexp_cdr(area); miniexp_consp(rest) && count < 64; rest = miniexp_cdr(rest)) {
		if (!miniexp_numberp(miniexp_car(rest)))
			return false;
		values[count++] = miniexp_to_int(miniexp_car(rest));
	}
	if (count < 4)
		return false;
	const char* kind = miniexp_to_name(miniexp_car(area));
	if (kind != NULL && strcmp(kind, "poly") != 0) {
		box->x0 = values[0];
		box->y0 = values[1];
		box->x1 = values[0] + values[2];
		box->y1 = values[1] + values[3];
		return true;
	}
	box->x0 = box->x1 = values[0];
	box->y0 = box->y1 = values[1];
	for (int i = 0; i + 1 < count; i += 2) {
		box->x0 = values[i] < box->x0 ? values[i] : box->x0;
		box->x1 = values[i] > box->x1 ? values[i] : box->x1;
		box->y0 = values[i + 1] < box->y0 ? values[i + 1] : box->y0;
		box->y1 = values[i + 1] > box->y1 ? values[i + 1] : box->y1;
	}
	return true;
}


// (bookmarks ("title" "#page" child...) ...)
void
CollectOutline(miniexp_t entries, int depth, std::vector<OutlineItem>* items)
{
	for (; miniexp_consp(entries); entries = miniexp_cdr(entries)) {
		miniexp_t entry = miniexp_car(entries);
		if (!miniexp_consp(entry) || !miniexp_stringp(miniexp_car(entry)))
			continue;
		miniexp_t rest = miniexp_cdr(entry);
		OutlineItem item;
		item.depth = depth;
		item.title = miniexp_to_str(miniexp_car(entry));
		if (miniexp_consp(rest) && miniexp_stringp(miniexp_car(rest)))
			item.url = miniexp_to_str(miniexp_car(rest));
		items->push_back(item);
		if (miniexp_consp(rest))
			CollectOutline(miniexp_cdr(rest), depth + 1, items);
	}
}


// The words of a page, asked for once.
const std::vector<Word>&
WordsOf(DjvuDoc* doc, int number)
{
	KeyGuard guard;
	PageData& data = doc->cache->pages[number];
	if (data.textKnown)
		return data.words;
	data.textKnown = true;

	miniexp_t text = miniexp_nil;
	for (int waited = 0; waited < kWaitMilliseconds; waited++) {
		Pump(doc);
		text = ddjvu_document_get_pagetext(doc->document, number, "word");
		if (text != miniexp_dummy)
			break;
		usleep(1000);
	}
	if (text != miniexp_dummy)
		CollectWords(text, &data.words);
	return data.words;
}


// The links of a page, asked for once: (maparea url comment area border...)
const std::vector<Link>&
LinksOf(DjvuDoc* doc, int number)
{
	KeyGuard guard;
	PageData& data = doc->cache->pages[number];
	if (data.linksKnown)
		return data.links;
	data.linksKnown = true;

	miniexp_t annotations = miniexp_nil;
	for (int waited = 0; waited < kWaitMilliseconds; waited++) {
		Pump(doc);
		annotations = ddjvu_document_get_pageanno(doc->document, number);
		if (annotations != miniexp_dummy)
			break;
		usleep(1000);
	}
	if (annotations == miniexp_dummy || !miniexp_consp(annotations))
		return data.links;

	miniexp_t* areas = ddjvu_anno_get_hyperlinks(annotations);
	for (int i = 0; areas != NULL && areas[i] != miniexp_nil; i++) {
		miniexp_t rest = miniexp_cdr(areas[i]);
		const char* url = miniexp_consp(rest) ? UrlOf(miniexp_car(rest)) : NULL;
		rest = miniexp_cdr(rest);
		rest = miniexp_consp(rest) ? miniexp_cdr(rest) : miniexp_nil;	// the comment
		Link link;
		if (url == NULL || url[0] == '\0' || !miniexp_consp(rest) || !AreaBox(miniexp_car(rest), &link.box))
			continue;
		link.url = url;
		data.links.push_back(link);
	}
	free(areas);
	return data.links;
}


const std::vector<OutlineItem>&
OutlineOf(DjvuDoc* doc)
{
	KeyGuard guard;
	Cache* cache = doc->cache;
	if (cache->outlineKnown)
		return cache->outline;
	cache->outlineKnown = true;

	miniexp_t outline = miniexp_nil;
	for (int waited = 0; waited < kWaitMilliseconds; waited++) {
		Pump(doc);
		outline = ddjvu_document_get_outline(doc->document);
		if (outline != miniexp_dummy)
			break;
		usleep(1000);
	}
	if (outline != miniexp_dummy && miniexp_consp(outline))
		CollectOutline(miniexp_cdr(outline), 0, &cache->outline);	// (bookmarks entry...)
	return cache->outline;
}


// the metadata of the document, as the PDF info has them (Title, Author, ...), asked for once
const std::vector<std::pair<std::string, std::string> >&
MetadataOf(DjvuDoc* doc)
{
	KeyGuard guard;
	Cache* cache = doc->cache;
	if (cache->metadataKnown)
		return cache->metadata;
	cache->metadataKnown = true;

	miniexp_t annotations = ddjvu_document_get_anno(doc->document, 1);
	if (annotations == miniexp_dummy || !miniexp_consp(annotations))
		return cache->metadata;
	miniexp_t* keys = ddjvu_anno_get_metadata_keys(annotations);
	for (int i = 0; keys != NULL && keys[i] != miniexp_nil; i++) {
		const char* name = miniexp_to_name(keys[i]);
		const char* value = ddjvu_anno_get_metadata(annotations, keys[i]);
		if (name != NULL && value != NULL)
			cache->metadata.push_back(std::make_pair(std::string(name), std::string(value)));
	}
	free(keys);
	return cache->metadata;
}


///////////////////////////////////////////////////////////////////////////
// The text layer

// the width of a string in a font of a size
float
StringWidth(fz_context* context, fz_font* font, const char* text, float size)
{
	float width = 0;
	while (*text != '\0') {
		int rune;
		text += fz_chartorune(&rune, text);
		width += fz_advance_glyph(context, font, fz_encode_character(context, font, rune), 0) * size;
	}
	return width;
}


// A box of the text, which is in the coordinates of the page as the file has it (origin at the bottom left, in pixels), as it is
// on the page as it is shown: turned by the quarter turns, counter-clockwise, that the file asks for. (Links are given in the
// coordinates of the page as it is shown, only the text is not: ddjvu_page_get_initial_rotation() says so.)
Box
TurnBox(const Box& box, int rotation, int storedWidth, int storedHeight)
{
	// the corners, turned: a quarter turn counter-clockwise takes (x, y) to (height - y, x)
	int xs[2] = { box.x0, box.x1 }, ys[2] = { box.y0, box.y1 };
	int minX = 0, minY = 0, maxX = 0, maxY = 0;
	bool first = true;
	for (int i = 0; i < 2; i++) {
		for (int k = 0; k < 2; k++) {
			int x = xs[i], y = ys[k], tx, ty;
			switch (rotation & 3) {
				case 1:		tx = storedHeight - y; ty = x; break;
				case 2:		tx = storedWidth - x; ty = storedHeight - y; break;
				case 3:		tx = y; ty = storedWidth - x; break;
				default:	tx = x; ty = y; break;
			}
			if (first || tx < minX) minX = tx;
			if (first || tx > maxX) maxX = tx;
			if (first || ty < minY) minY = ty;
			if (first || ty > maxY) maxY = ty;
			first = false;
		}
	}
	Box result = { minX, minY, maxX, maxY };
	return result;
}


// One piece of text in its box (on the page as it is shown), stretched to the box.
void
AddText(fz_context* context, fz_text* run, fz_font* font, const Box& box, const std::string& text, float scale,
	float pageHeight)
{
	float left = box.x0 * scale, right = box.x1 * scale;
	float top = pageHeight - box.y1 * scale, bottom = pageHeight - box.y0 * scale;
	float height = bottom - top, width = right - left;
	if (height < 0.5f || width < 0.5f)
		return;

	float ascender = fz_font_ascender(context, font), descender = fz_font_descender(context, font);
	float em = ascender - descender;
	float size = em > 0 ? height / em : height;
	float natural = StringWidth(context, font, text.c_str(), size);
	float stretch = natural > 0 ? width / natural : 1;
	if (stretch < 0.1f)
		stretch = 0.1f;
	else if (stretch > 10)
		stretch = 10;
	fz_matrix matrix = fz_make_matrix(size * stretch, 0, 0, -size, left, top + size * ascender);
	fz_show_string(context, run, font, matrix, text.c_str(), 0, 0, FZ_BIDI_LTR, FZ_LANG_UNSET);
}


// the hidden text as invisible text on the page
void
RunText(fz_context* context, DjvuDoc* doc, DjvuPage* page, fz_device* device, fz_matrix ctm)
{
	const std::vector<Word>& words = WordsOf(doc, page->number);
	if (words.empty())
		return;

	float scale = PointsPerPixel(page->dpi);
	fz_text* run = fz_new_text(context);
	fz_font* font = NULL;
	fz_var(font);
	fz_try(context) {
		font = fz_new_base14_font(context, "Helvetica");
		for (size_t i = 0; i < words.size(); i++) {
			Box box = TurnBox(words[i].box, page->rotation, page->storedWidth, page->storedHeight);
			AddText(context, run, font, box, words[i].text, scale, page->height);
		}
		fz_ignore_text(context, device, run, ctm);
	}
	fz_always(context) {
		fz_drop_font(context, font);
		fz_drop_text(context, run);
	}
	fz_catch(context) {
		fz_rethrow(context);
	}
}


///////////////////////////////////////////////////////////////////////////
// Pages

fz_rect
BoundPage(fz_context*, fz_page* page_, fz_box_type)
{
	DjvuPage* page = (DjvuPage*)page_;
	return fz_make_rect(0, 0, page->width, page->height);
}


void
RunPage(fz_context* context, fz_page* page_, fz_device* device, fz_matrix ctm, fz_cookie* cookie)
{
	DjvuPage* page = (DjvuPage*)page_;
	DjvuDoc* doc = (DjvuDoc*)page->super.doc;
	if (cookie != NULL && cookie->abort)
		return;

	// the size that the picture has when it is drawn, in pixels along the sides of the page
	float scaleX = sqrtf(ctm.a * ctm.a + ctm.b * ctm.b), scaleY = sqrtf(ctm.c * ctm.c + ctm.d * ctm.d);
	float width = page->width * scaleX, height = page->height * scaleY;
	if (width < 1)
		width = 1;
	if (height < 1)
		height = 1;
	if (width * height > (float)kMaxPixels) {
		float shrink = sqrtf((float)kMaxPixels / (width * height));
		width *= shrink;
		height *= shrink;
	}
	int pixelsWide = (int)ceilf(width), pixelsHigh = (int)ceilf(height);

	ddjvu_page_t* djvu = ddjvu_page_create_by_pageno(doc->document, page->number);
	fz_pixmap* pixmap = NULL;
	fz_image* image = NULL;
	fz_var(pixmap);
	fz_var(image);
	fz_try(context) {
		if (djvu == NULL)
			fz_throw(context, FZ_ERROR_FORMAT, "cannot decode page %d", page->number + 1);
		PageDone done = { djvu };
		if (!Wait(doc, done) || ddjvu_page_decoding_error(djvu))
			fz_throw(context, FZ_ERROR_FORMAT, "cannot decode page %d", page->number + 1);

		pixmap = fz_new_pixmap(context, fz_device_rgb(context), pixelsWide, pixelsHigh, NULL, 0);
		fz_clear_pixmap_with_value(context, pixmap, 0xff);
		ddjvu_rect_t rect;
		rect.x = 0;
		rect.y = 0;
		rect.w = (unsigned int)pixelsWide;
		rect.h = (unsigned int)pixelsHigh;
		ddjvu_page_render(djvu, DDJVU_RENDER_COLOR, &rect, &rect, doc->format,
			(unsigned long)fz_pixmap_stride(context, pixmap), (char*)fz_pixmap_samples(context, pixmap));

		image = fz_new_image_from_pixmap(context, pixmap, NULL);
		fz_fill_image(context, device, image, fz_concat(fz_scale(page->width, page->height), ctm), 1,
			fz_default_color_params);
		RunText(context, doc, page, device, ctm);
	}
	fz_always(context) {
		fz_drop_image(context, image);
		fz_drop_pixmap(context, pixmap);
		if (djvu != NULL)
			ddjvu_page_release(djvu);
	}
	fz_catch(context) {
		fz_report_error(context);
		fz_warn(context, "cannot draw page %d, leaving it blank", page->number + 1);
	}
}


void
DropPage(fz_context*, fz_page*)
{
}


// The page that a link goes to: #5 is the fifth page, other targets are the names of pages in the file; -1 if it is none.
int
PageOfTarget(DjvuDoc* doc, const char* target)
{
	if (target == NULL)
		return -1;
	if (target[0] == '#')
		target++;
	char* end;
	long number = strtol(target, &end, 10);
	if (end != target && *end == '\0')
		return number >= 1 && number <= doc->pageCount ? (int)number - 1 : -1;
	int page = ddjvu_document_search_pageno(doc->document, target);
	return page >= 0 && page < doc->pageCount ? page : -1;
}


fz_link*
LoadLinks(fz_context* context, fz_page* page_)
{
	DjvuPage* page = (DjvuPage*)page_;
	DjvuDoc* doc = (DjvuDoc*)page->super.doc;
	const std::vector<Link>& links = LinksOf(doc, page->number);
	float scale = PointsPerPixel(page->dpi);

	fz_link* first = NULL;
	fz_link* last = NULL;
	fz_var(first);
	fz_var(last);
	fz_try(context) {
		for (size_t i = 0; i < links.size(); i++) {
			const Box& box = links[i].box;
			// the coordinates of a link are those of the page as it is drawn, origin at the bottom left
			fz_rect rect = fz_make_rect(box.x0 * scale, page->height - box.y1 * scale, box.x1 * scale,
				page->height - box.y0 * scale);
			fz_link* link = fz_new_link_of_size(context, sizeof(fz_link), rect, links[i].url.c_str());
			if (last != NULL)
				last->next = link;
			else
				first = link;
			last = link;
		}
	}
	fz_catch(context) {
		fz_drop_link(context, first);
		fz_rethrow(context);
	}
	return first;
}


fz_page*
LoadPage(fz_context* context, fz_document* doc_, int, int number)
{
	DjvuDoc* doc = (DjvuDoc*)doc_;
	if (number < 0 || number >= doc->pageCount)
		fz_throw(context, FZ_ERROR_ARGUMENT, "invalid page number %d", number);
	PageInfo info;
	if (!GetInfo(doc, number, &info))
		fz_throw(context, FZ_ERROR_FORMAT, "cannot read the size of page %d", number + 1);

	DjvuPage* page = fz_new_derived_page(context, DjvuPage, doc_);
	page->super.bound_page = BoundPage;
	page->super.run_page_contents = RunPage;
	page->super.load_links = LoadLinks;
	page->super.drop_page = DropPage;
	page->number = number;
	page->rotation = info.rotation;
	page->storedWidth = info.storedWidth;
	page->storedHeight = info.storedHeight;
	page->dpi = info.dpi;
	page->width = info.width * PointsPerPixel(info.dpi);
	page->height = info.height * PointsPerPixel(info.dpi);
	return &page->super;
}


///////////////////////////////////////////////////////////////////////////
// The document

int
CountPages(fz_context*, fz_document* doc_, int)
{
	return ((DjvuDoc*)doc_)->pageCount;
}


void
DropDocument(fz_context* context, fz_document* doc_)
{
	DjvuDoc* doc = (DjvuDoc*)doc_;
	if (doc->format != NULL)
		ddjvu_format_release(doc->format);
	if (doc->document != NULL)
		ddjvu_document_release(doc->document);
	if (doc->ddjvu != NULL)
		ddjvu_context_release(doc->ddjvu);
	delete doc->cache;
	fz_free(context, doc->infos);
}


fz_outline*
LoadOutline(fz_context* context, fz_document* doc_)
{
	DjvuDoc* doc = (DjvuDoc*)doc_;
	const std::vector<OutlineItem>& items = OutlineOf(doc);
	if (items.empty())
		return NULL;

	// the items are in the order of the tree, with their depth: the first of a level is the child of the one above it
	std::vector<fz_outline*> lastAt;
	fz_outline* first = NULL;
	fz_var(first);
	fz_try(context) {
		for (size_t i = 0; i < items.size(); i++) {
			const OutlineItem& item = items[i];
			size_t depth = (size_t)item.depth;
			if (depth > lastAt.size())
				depth = lastAt.size();	// a level that was skipped
			fz_outline* outline = fz_new_outline(context);
			if (depth == 0) {
				if (!lastAt.empty())
					lastAt[0]->next = outline;
				else
					first = outline;
			} else if (lastAt.size() > depth) {
				lastAt[depth]->next = outline;
			} else
				lastAt[depth - 1]->down = outline;
			lastAt.resize(depth + 1);
			lastAt[depth] = outline;

			outline->title = fz_strdup(context, item.title.c_str());
			outline->is_open = 1;
			outline->page = fz_make_location(-1, -1);
			int page = PageOfTarget(doc, item.url.c_str());
			if (page >= 0)
				outline->page = fz_make_location(0, page);
			else if (!item.url.empty())
				outline->uri = fz_strdup(context, item.url.c_str());
		}
	}
	fz_catch(context) {
		fz_drop_outline(context, first);
		fz_rethrow(context);
	}
	return first;
}


fz_link_dest
ResolveLinkDest(fz_context*, fz_document* doc_, const char* uri)
{
	int page = PageOfTarget((DjvuDoc*)doc_, uri);
	if (page < 0)
		return fz_make_link_dest_none();
	return fz_make_link_dest_xyz(0, page, NAN, NAN, 0);
}


int
LookupMetadata(fz_context*, fz_document* doc_, const char* key, char* buffer, size_t size)
{
	DjvuDoc* doc = (DjvuDoc*)doc_;
	if (strcmp(key, FZ_META_FORMAT) == 0) {
		snprintf(buffer, size, "DjVu");
		return 1 + (int)strlen("DjVu");
	}
	if (strncmp(key, FZ_META_INFO, strlen(FZ_META_INFO)) != 0)
		return -1;
	const char* name = key + strlen(FZ_META_INFO);

	const std::vector<std::pair<std::string, std::string> >& metadata = MetadataOf(doc);
	for (size_t i = 0; i < metadata.size(); i++) {
		if (strcasecmp(metadata[i].first.c_str(), name) == 0 && size > 0) {
			snprintf(buffer, size, "%s", metadata[i].second.c_str());
			return 1 + (int)strlen(buffer);
		}
	}
	return -1;
}

}	// namespace


bool
IsDjvuFile(const char* path)
{
	FILE* file = fopen(path, "rb");
	if (file == NULL)
		return false;
	unsigned char head[16];
	size_t got = fread(head, 1, sizeof(head), file);
	fclose(file);
	// the mark of the format: AT&TFORM, a length, then DJVU (a page) or DJVM (a document with pages)
	return got == sizeof(head) && memcmp(head, "AT&TFORM", 8) == 0
		&& (memcmp(head + 12, "DJVU", 4) == 0 || memcmp(head + 12, "DJVM", 4) == 0);
}


fz_document*
Open(fz_context* context, const char* path)
{
	// see the note at the top: the garbage collector of the s-expressions is not run (once is enough)
	static bool sCollectorOff = false;
	if (!sCollectorOff) {
		minilisp_acquire_gc_lock(0);
		sCollectorOff = true;
	}

	DjvuDoc* doc = fz_new_derived_document(context, DjvuDoc);
	doc->super.drop_document = DropDocument;
	doc->super.count_pages = CountPages;
	doc->super.load_page = LoadPage;
	doc->super.load_outline = LoadOutline;
	doc->super.resolve_link_dest = ResolveLinkDest;
	doc->super.lookup_metadata = LookupMetadata;
	doc->cache = new Cache();

	fz_try(context) {
		doc->ddjvu = ddjvu_context_create("Tsundoku");
		if (doc->ddjvu == NULL)
			fz_throw(context, FZ_ERROR_SYSTEM, "cannot start DjVuLibre");
		// decoded pages are kept for a while: the same page is drawn again at every zoom
		ddjvu_cache_set_size(doc->ddjvu, 128UL * 1024 * 1024);
		doc->document = ddjvu_document_create_by_filename(doc->ddjvu, path, 0);
		if (doc->document == NULL)
			fz_throw(context, FZ_ERROR_FORMAT, "cannot open the DjVu file");
		DocumentDone done = { doc };
		if (!Wait(doc, done) || ddjvu_document_decoding_error(doc->document))
			fz_throw(context, FZ_ERROR_FORMAT, "cannot read the DjVu file");

		doc->pageCount = ddjvu_document_get_pagenum(doc->document);
		if (doc->pageCount <= 0)
			fz_throw(context, FZ_ERROR_FORMAT, "the DjVu file has no pages");
		doc->infos = (PageInfo*)fz_calloc(context, doc->pageCount, sizeof(PageInfo));
		doc->format = ddjvu_format_create(DDJVU_FORMAT_RGB24, 0, NULL);
		if (doc->format == NULL)
			fz_throw(context, FZ_ERROR_SYSTEM, "cannot make a format for the pages");
		ddjvu_format_set_row_order(doc->format, 1);
	}
	fz_catch(context) {
		fz_drop_document(context, &doc->super);
		fz_rethrow(context);
	}
	return &doc->super;
}

}	// namespace Djvu

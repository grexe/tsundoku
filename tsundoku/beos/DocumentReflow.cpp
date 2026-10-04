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


// Reflowable documents (EPUB): the layout, and the annotations that are kept in an attribute of the file.
//
// Note: fz_try() uses setjmp()/longjmp(), so no C++ objects with destructors may be created or destroyed inside
// of a fz_try() block.

#include "Document.h"
#include "EpubCfi.h"
#include "EpubInfo.h"
#include "Globals.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <map>

#include <Catalog.h>
#include <File.h>
#include <Message.h>
#include <fs_attr.h>

extern "C" {
#include <mupdf/pdf.h>
}

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "Document"

void NewAnnotationId(char* id, size_t size);		// Document.cpp
void CopyAttributes(const char* from, const char* to);

const float Document::kReflowWidth = 432;		// 6 x 9 inches
const float Document::kReflowHeight = 648;
const float Document::kDefaultTextSize = 12;
const float Document::kMinTextSize = 6;
const float Document::kMaxTextSize = 40;

// The annotations of a book are kept in this attribute of its file (a standard message, which other applications
// can use for the same purpose). Nothing in the ontologies is made for it, so it has the prefix of SEN, whose standard
// it is; the names of the first versions are still read.
static const char* kStoreAttribute = "SEN:annotations";
static const char* const kLegacyStoreAttributes[] = { "META:annotations", "tsundoku:annotations", NULL };
static const int32 kStoreVersion = 1;
static const int kMaxUndo = 100;
static const int kMaxQuoteLength = 4000;


static void
LogReflowError(fz_context* context, const char* what)
{
	fprintf(stderr, "Tsundoku: %s: %s\n", what, fz_caught_message(context));
}


///////////////////////////////////////////////////////////////////////////
// Layout

bool
Document::Layout(float textSize)
{
	if (!fReflowable)
		return true;

	if (textSize < kMinTextSize)
		textSize = kMinTextSize;
	if (textSize > kMaxTextSize)
		textSize = kMaxTextSize;

	fAbortLayout = false;
	float previous = fTextSize;
	int chapters = 0;
	{
		DocumentLocker locker(this);
		fz_try(fContext) {
			fz_layout_document(fContext, fDocument, kReflowWidth, kReflowHeight, textSize);
			chapters = fz_count_chapters(fContext, fDocument);
		}
		fz_catch(fContext) {
			LogReflowError(fContext, "cannot lay out the document");
			chapters = 0;
		}
	}

	// the chapters are laid out one by one (that is what counting their pages does), the lock is let go between them
	bool aborted = false;
	for (int chapter = 0; chapter < chapters; chapter++) {
		if (fAbortLayout) {
			aborted = true;
			break;
		}
		DocumentLocker locker(this);
		fz_try(fContext) {
			fz_count_chapter_pages(fContext, fDocument, chapter);
		}
		fz_catch(fContext) {
			LogReflowError(fContext, "cannot lay out a chapter");
		}
	}

	DocumentLocker locker(this);
	if (aborted) {
		// as it was (the chapters are laid out again when they are needed)
		fz_try(fContext) {
			fz_layout_document(fContext, fDocument, kReflowWidth, kReflowHeight, previous);
		}
		fz_catch(fContext) {
		}
		return false;
	}

	int pages = 0;
	fz_try(fContext) {
		pages = fz_count_pages(fContext, fDocument);
	}
	fz_catch(fContext) {
		LogReflowError(fContext, "cannot count the pages");
		pages = 0;
	}
	if (pages <= 0)
		return true;

	fTextSize = textSize;
	fPageCount = pages;
	fz_rect empty = fz_empty_rect;
	fBounds.assign(pages, empty);
	fBoundsKnown.assign(pages, false);
	// the marks are somewhere else on the new pages
	fResolvedKnown.assign(fStore.size(), 0);
	fOutlineCached = false;
	return true;
}


int
Document::ChangeTextSize(float textSize, int currentPage)
{
	if (!fReflowable)
		return currentPage;

	// the page stays the page at its beginning: a bookmark is where the page starts in the text
	// (if the reader has not moved since the last change the same place is used again, so that a larger text
	// and then a smaller one lead back to the same page)
	fz_bookmark bookmark = 0;
	int marked = 0;
	fz_var(marked);
	{
		DocumentLocker locker(this);
		if (fKeptPage != 0 && fKeptPage == currentPage) {
			bookmark = fKeptBookmark;
			marked = 1;
		} else {
			fz_try(fContext) {
				bookmark = fz_make_bookmark(fContext, fDocument,
					fz_location_from_page_number(fContext, fDocument, currentPage - 1));
				marked = 1;
			}
			fz_catch(fContext) {
				marked = 0;
			}
		}
	}

	if (!Layout(textSize))
		return currentPage;

	DocumentLocker locker(this);
	int page = currentPage;
	if (marked) {
		fz_try(fContext) {
			fz_location location = fz_lookup_bookmark(fContext, fDocument, bookmark);
			page = fz_page_number_from_location(fContext, fDocument, location) + 1;
		}
		fz_catch(fContext) {
			page = currentPage;
		}
	}
	if (page < 1)
		page = 1;
	if (page > fPageCount)
		page = fPageCount;
	fKeptBookmark = bookmark;
	fKeptPage = marked ? page : 0;
	return page;
}


///////////////////////////////////////////////////////////////////////////
// Chapters

int
Document::ChapterCount()
{
	DocumentLocker locker(this);
	int count = 1;
	fz_try(fContext) {
		count = fz_count_chapters(fContext, fDocument);
	}
	fz_catch(fContext) {
		count = 1;
	}
	return count < 1 ? 1 : count;
}


int
Document::ChapterOfPage(int page)
{
	DocumentLocker locker(this);
	int chapter = 0;
	fz_try(fContext) {
		chapter = fz_location_from_page_number(fContext, fDocument, page - 1).chapter;
	}
	fz_catch(fContext) {
		chapter = 0;
	}
	return chapter;
}


int
Document::ChapterFirstPage(int chapter)
{
	DocumentLocker locker(this);
	int page = 1;
	fz_try(fContext) {
		page = fz_page_number_from_location(fContext, fDocument, fz_make_location(chapter, 0)) + 1;
	}
	fz_catch(fContext) {
		page = 1;
	}
	return page;
}


int
Document::ChapterPageCount(int chapter)
{
	DocumentLocker locker(this);
	int count = 0;
	fz_try(fContext) {
		count = fz_count_chapter_pages(fContext, fDocument, chapter);
	}
	fz_catch(fContext) {
		count = 0;
	}
	return count;
}


std::vector<BString>
Document::ChapterTitles()
{
	int chapters = ChapterCount();
	std::vector<BString> titles(chapters);
	std::vector<int> levels(chapters, 1000);

	std::vector<DocOutlineEntry> entries;
	LoadOutline(entries);
	for (size_t i = 0; i < entries.size(); i++) {
		if (entries[i].page < 1 || entries[i].title.Length() == 0)
			continue;
		int chapter = ChapterOfPage(entries[i].page);
		if (chapter >= 0 && chapter < chapters && entries[i].level < levels[chapter]) {
			levels[chapter] = entries[i].level;
			titles[chapter] = entries[i].title;
		}
	}
	return titles;
}


///////////////////////////////////////////////////////////////////////////
// The attribute

void
Document::LoadStore()
{
	fStore.clear();
	fStoreUndo.clear();
	fStoreRedo.clear();
	fStoreSavedDepth = 0;
	fResolved.clear();
	fResolvedKnown.clear();

	BNode node(fPath.String());
	attr_info info;
	const char* attribute = kStoreAttribute;
	if (node.InitCheck() != B_OK)
		return;
	if (node.GetAttrInfo(attribute, &info) != B_OK) {
		attribute = NULL;
		for (int i = 0; kLegacyStoreAttributes[i] != NULL && attribute == NULL; i++) {
			if (node.GetAttrInfo(kLegacyStoreAttributes[i], &info) == B_OK)
				attribute = kLegacyStoreAttributes[i];
		}
		if (attribute == NULL)
			return;
	}
	if (info.size <= 0 || info.size > 16 * 1024 * 1024)
		return;

	char* buffer = new char[info.size];
	ssize_t size = node.ReadAttr(attribute, info.type, 0, buffer, info.size);
	BMessage archive;
	if (size == info.size && archive.Unflatten(buffer) == B_OK) {
		BMessage item;
		for (int32 i = 0; archive.FindMessage("annotation", i, &item) == B_OK; i++) {
			StoredAnnotation a;
			int32 markup = 0;
			uint32 color = 0xffeb3b;
			int64 created = 0;
			item.FindString("id", &a.id);
			item.FindInt32("markup", &markup);
			item.FindUInt32("color", &color);
			item.FindString("contents", &a.contents);
			item.FindString("author", &a.author);
			item.FindInt64("created", &created);
			item.FindInt32("chapter", &a.chapter);
			item.FindFloat("fraction", &a.fraction);
			item.FindFloat("ypos", &a.ypos);
			item.FindString("quote", &a.quote);
			item.FindString("cfi", &a.cfi);
			a.markup = markup;
			a.color = color;
			a.created = created;
			if (a.id.Length() == 0 || a.quote.Length() == 0)
				continue;
			fStore.push_back(a);
		}
	}
	delete[] buffer;
}


// Writes the annotations into the attribute of the file (and takes the attribute away if there are none).
bool
Document::WriteStore(const char* path)
{
	BNode node(path);
	if (node.InitCheck() != B_OK)
		return false;

	for (int i = 0; kLegacyStoreAttributes[i] != NULL; i++)
		node.RemoveAttr(kLegacyStoreAttributes[i]);
	if (fStore.empty()) {
		node.RemoveAttr(kStoreAttribute);
		return true;
	}

	BMessage archive;
	archive.AddInt32("version", kStoreVersion);
	for (size_t i = 0; i < fStore.size(); i++) {
		const StoredAnnotation& a = fStore[i];
		BMessage item;
		item.AddString("id", a.id);
		item.AddInt32("markup", a.markup);
		item.AddUInt32("color", a.color);
		item.AddString("contents", a.contents);
		item.AddString("author", a.author);
		item.AddInt64("created", a.created);
		item.AddInt32("chapter", a.chapter);
		item.AddFloat("fraction", a.fraction);
		item.AddFloat("ypos", a.ypos);
		item.AddString("quote", a.quote);
		item.AddString("cfi", a.cfi);
		archive.AddMessage("annotation", &item);
	}

	ssize_t size = archive.FlattenedSize();
	char* buffer = new char[size];
	bool ok = archive.Flatten(buffer, size) == B_OK
		&& node.WriteAttr(kStoreAttribute, B_MESSAGE_TYPE, 0, buffer, size) == size;
	delete[] buffer;
	return ok;
}


bool
Document::StoreSaveCopy(const char* path)
{
	{
		BFile in(fPath.String(), B_READ_ONLY);
		BFile out(path, B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
		if (in.InitCheck() != B_OK || out.InitCheck() != B_OK)
			return false;
		char buffer[65536];
		ssize_t got;
		while ((got = in.Read(buffer, sizeof(buffer))) > 0) {
			if (out.Write(buffer, got) != got)
				return false;
		}
		if (got < 0)
			return false;
	}
	// the attributes of the original (position, bookmarks, type), then the marks as they are now
	CopyAttributes(fPath.String(), path);

	DocumentLocker locker(this);
	if (!WriteStore(path))
		return false;
	fModified = false;
	fStoreSavedDepth = fStoreUndo.size();
	SyncAnnotationCount(path);
	return true;
}


///////////////////////////////////////////////////////////////////////////
// Finding the text again

struct QuoteHits {
	std::vector<std::vector<fz_quad> >* hits;
};


static int
CollectQuoteHit(fz_context*, void* data, int numQuads, fz_quad* quads, int, int)
{
	if (numQuads > 0) {
		QuoteHits* collected = (QuoteHits*)data;
		collected->hits->push_back(std::vector<fz_quad>(quads, quads + numQuads));
	}
	return 0;
}


// the text of a page (0-based) for the search, kept for the next annotation
static fz_stext_page*
TextOfPage(Document* document, std::map<int, fz_stext_page*>& cache, int page)
{
	std::map<int, fz_stext_page*>::iterator found = cache.find(page);
	if (found != cache.end())
		return found->second;

	fz_context* context = document->Context();
	fz_stext_page* text = NULL;
	fz_var(text);
	fz_try(context) {
		text = fz_new_stext_page_from_page_number(context, document->Doc(), page, NULL);
	}
	fz_catch(context) {
		text = NULL;
	}
	cache[page] = text;
	return text;
}


static bool
HitsOnPage(Document* document, fz_stext_page* text, const BString& needle,
	std::vector<std::vector<fz_quad> >* hits)
{
	if (text == NULL)
		return false;
	QuoteHits collected = { hits };
	fz_context* context = document->Context();
	int ok = 0;
	fz_try(context) {
		fz_match_stext_page_cb(context, text, needle.String(), CollectQuoteHit, &collected, FZ_SEARCH_EXACT);
		ok = 1;
	}
	fz_catch(context) {
		ok = 0;
	}
	return ok != 0 && !hits->empty();
}


// Looks for the text on the pages of a chapter, beginning with the page where it was and going out from there.
// firstPage is the 0-based number of the first page of the chapter, count the number of its pages.
// The first and the last character of a page that is not white space (false if there is none), as the middle of
// its box.
static bool
PageEnds(fz_stext_page* text, fz_point* first, fz_point* last)
{
	bool found = false;
	for (fz_stext_block* block = text->first_block; block != NULL; block = block->next) {
		if (block->type != FZ_STEXT_BLOCK_TEXT)
			continue;
		for (fz_stext_line* line = block->u.t.first_line; line != NULL; line = line->next) {
			for (fz_stext_char* ch = line->first_char; ch != NULL; ch = ch->next) {
				if (ch->c == ' ' || ch->c == '\n' || ch->c == '\t' || ch->c == 0xa0)
					continue;
				fz_rect box = fz_rect_from_quad(ch->quad);
				fz_point middle = fz_make_point((box.x0 + box.x1) / 2, (box.y0 + box.y1) / 2);
				if (!found)
					*first = middle;
				*last = middle;
				found = true;
			}
		}
	}
	return found;
}


static bool
Contains(const fz_quad& quad, fz_point point)
{
	fz_rect box = fz_rect_from_quad(quad);
	return point.x >= box.x0 - 1 && point.x <= box.x1 + 1 && point.y >= box.y0 - 1 && point.y <= box.y1 + 1;
}


// Is the hit at the beginning of the page (the first character of the page is in it) or at its end?
static bool
HitFitsBreak(Document*, fz_stext_page* text, const std::vector<fz_quad>& hit, bool startsPage, bool endsPage)
{
	fz_point first, last;
	if (!PageEnds(text, &first, &last))
		return false;
	if (startsPage && !Contains(hit.front(), first))
		return false;
	if (endsPage && !Contains(hit.back(), last))
		return false;
	return true;
}


static bool
FindQuote(Document* document, std::map<int, fz_stext_page*>& cache, int firstPage, int count, int hint,
	const BString& quote, float ypos, int low, int high, bool startsPage, bool endsPage, int* foundIndex,
	std::vector<fz_quad>* quads)
{
	// (only the pages from low to high are looked at)
	for (int distance = 0; distance < count; distance++) {
		for (int side = 0; side < (distance == 0 ? 1 : 2); side++) {
			int index = hint + (side == 0 ? distance : -distance);
			if (index < low || index > high || index < 0 || index >= count)
				continue;

			std::vector<std::vector<fz_quad> > hits;
			fz_stext_page* text = TextOfPage(document, cache, firstPage + index);
			if (!HitsOnPage(document, text, quote, &hits))
				continue;
			if (startsPage || endsPage) {
				// only a hit at the page break is the one that is meant
				for (int k = (int)hits.size() - 1; k >= 0; k--) {
					if (!HitFitsBreak(document, text, hits[k], startsPage, endsPage))
						hits.erase(hits.begin() + k);
				}
				if (hits.empty())
					continue;
			}

			// of several equal texts on the page, the one that was at the same height
			size_t best = 0;
			if (index == hint && hits.size() > 1) {
				fz_rect bounds;
				if (document->PageBounds(firstPage + index + 1, &bounds) && bounds.y1 > bounds.y0) {
					float bestDistance = 1e9f;
					for (size_t k = 0; k < hits.size(); k++) {
						float at = (hits[k][0].ul.y - bounds.y0) / (bounds.y1 - bounds.y0);
						if (fabsf(at - ypos) < bestDistance) {
							bestDistance = fabsf(at - ypos);
							best = k;
						}
					}
				}
			}
			*foundIndex = index;
			*quads = hits[best];
			return true;
		}
	}
	return false;
}


// Where an annotation is now, into parts; false if the words are not found.
static bool
ResolveByWords(Document* document, std::map<int, fz_stext_page*>& cache, const StoredAnnotation& a,
	std::vector<StoredPart>* parts)
{
	fz_context* context = document->Context();
	int firstPage = 0, count = 0;
	fz_var(count);
	fz_try(context) {
		count = fz_count_chapter_pages(context, document->Doc(), a.chapter);
		firstPage = fz_page_number_from_location(context, document->Doc(), fz_make_location(a.chapter, 0));
	}
	fz_catch(context) {
		count = 0;
	}

	if (count <= 0)
		return false;

	int hint = (int)(a.fraction * count);
	if (hint < 0)
		hint = 0;
	if (hint >= count)
		hint = count - 1;
	// the words on one page
	int index = 0;
	std::vector<fz_quad> quads;
	if (FindQuote(document, cache, firstPage, count, hint, a.quote, a.ypos, 0, count - 1, false, false, &index,
			&quads)) {
		StoredPart part;
		part.page = firstPage + index + 1;
		part.quads = quads;
		parts->push_back(part);
		return true;
	}

	// or over a page break: the first words end a page and the others begin the next one. Where the break is,
	// is found by trying the places between the words.
	std::vector<BString> words;
	int32 start = 0;
	while (start < a.quote.Length()) {
		int32 space = a.quote.FindFirst(' ', start);
		if (space < 0)
			space = a.quote.Length();
		BString word;
		a.quote.CopyInto(word, start, space - start);
		if (word.Length() > 0)
			words.push_back(word);
		start = space + 1;
	}
	int low = hint - 2 < 0 ? 0 : hint - 2;
	int high = hint + 2 >= count ? count - 1 : hint + 2;
	for (size_t at = 1; at < words.size(); at++) {
		BString head, tail;
		for (size_t i = 0; i < words.size(); i++) {
			BString& into = i < at ? head : tail;
			if (into.Length() > 0)
				into << " ";
			into << words[i];
		}
		int headIndex = 0, tailIndex = 0;
		std::vector<fz_quad> headQuads, tailQuads;
		if (!FindQuote(document, cache, firstPage, count, hint, head, a.ypos, low, high, false, true, &headIndex,
				&headQuads))
			continue;
		if (headIndex + 1 >= count
			|| !FindQuote(document, cache, firstPage, count, headIndex + 1, tail, 0, headIndex + 1, headIndex + 1,
				true, false, &tailIndex, &tailQuads))
			continue;
		StoredPart one, two;
		one.page = firstPage + headIndex + 1;
		one.quads = headQuads;
		two.page = firstPage + tailIndex + 1;
		two.quads = tailQuads;
		parts->push_back(one);
		parts->push_back(two);
		return true;
	}
	return false;
}


// Where an annotation is now: by its words and where they were; if they are not there (the book is another
// version), by its CFI, which names the chapter by the id of its itemref and leads to the words that are there now.
// If nothing is found, there is one part without quads, on the page where it should be.
static void
ResolveAnnotation(Document* document, std::map<int, fz_stext_page*>& cache, const StoredAnnotation& a,
	std::vector<StoredPart>* parts)
{
	if (ResolveByWords(document, cache, a, parts))
		return;

	if (!a.cfi.IsEmpty() && document->Epub() != NULL) {
		StoredAnnotation moved = a;
		int spine = 0;
		BString words;
		float fraction = 0;
		if (EpubCfi::Resolve(document->Path(), *document->Epub(), a.cfi.String(), &spine, &words, &fraction)
			&& !words.IsEmpty()) {
			moved.chapter = spine;
			moved.fraction = fraction;
			moved.quote = words;
			parts->clear();
			if (ResolveByWords(document, cache, moved, parts))
				return;
		}
	}

	parts->clear();
	StoredPart orphan;
	orphan.page = 1;
	fz_context* context = document->Context();
	int firstPage = 0, count = 0;
	fz_var(count);
	fz_try(context) {
		count = fz_count_chapter_pages(context, document->Doc(), a.chapter);
		firstPage = fz_page_number_from_location(context, document->Doc(), fz_make_location(a.chapter, 0));
	}
	fz_catch(context) {
		count = 0;
	}
	if (count > 0) {
		int hint = (int)(a.fraction * count);
		if (hint < 0)
			hint = 0;
		if (hint >= count)
			hint = count - 1;
		orphan.page = firstPage + hint + 1;
	}
	parts->push_back(orphan);
}


// Finds the places of the marks that are not known yet (all of them after a new layout, none after most edits).
void
Document::ResolveStore()
{
	DocumentLocker locker(this);
	fResolved.resize(fStore.size());
	fResolvedKnown.resize(fStore.size(), 0);

	bool any = false;
	for (size_t i = 0; i < fStore.size(); i++) {
		if (!fResolvedKnown[i]) {
			any = true;
			break;
		}
	}
	if (!any)
		return;

	TimingMark("resolve: starts");
	std::map<int, fz_stext_page*> cache;
	for (size_t i = 0; i < fStore.size(); i++) {
		if (fResolvedKnown[i])
			continue;
		fResolved[i].clear();
		ResolveAnnotation(this, cache, fStore[i], &fResolved[i]);
		fResolvedKnown[i] = 1;
	}

	for (std::map<int, fz_stext_page*>::iterator it = cache.begin(); it != cache.end(); ++it) {
		if (it->second != NULL)
			fz_drop_stext_page(fContext, it->second);
	}
	TimingMark("resolve: done");
}


bool
Document::StorePageHasParts(int pageNo)
{
	DocumentLocker locker(this);
	ResolveStore();
	for (size_t i = 0; i < fResolved.size(); i++) {
		for (size_t k = 0; k < fResolved[i].size(); k++) {
			if (fResolved[i][k].page == pageNo)
				return true;
		}
	}
	return false;
}


static const char*
MarkupLabel(int markup)
{
	switch (markup) {
		case kMarkupUnderline:	return B_TRANSLATE("Underline");
		case kMarkupStrikeOut:	return B_TRANSLATE("Strike out");
		case kMarkupSquiggly:	return B_TRANSLATE("Squiggly line");
		default:				return B_TRANSLATE("Highlight");
	}
}


void
Document::StoreAnnotationsOnPage(int pageNo, std::vector<DocAnnotation>& annotations)
{
	DocumentLocker locker(this);
	ResolveStore();
	int index = 0;
	for (size_t i = 0; i < fStore.size(); i++) {
		const StoredAnnotation& a = fStore[i];
		for (size_t k = 0; k < fResolved[i].size(); k++) {
			const StoredPart& part = fResolved[i][k];
			if (part.page != pageNo)
				continue;

			DocAnnotation entry;
			static const int types[] = { PDF_ANNOT_HIGHLIGHT, PDF_ANNOT_UNDERLINE, PDF_ANNOT_STRIKE_OUT,
				PDF_ANNOT_SQUIGGLY };
			entry.type = types[a.markup >= 0 && a.markup <= kMarkupSquiggly ? a.markup : 0];
			entry.index = index++;
			entry.label = MarkupLabel(a.markup);
			entry.id = a.id;
			entry.rect = fz_empty_rect;
			for (size_t q = 0; q < part.quads.size(); q++)
				entry.rect = fz_union_rect(entry.rect, fz_rect_from_quad(part.quads[q]));
			if (part.quads.empty())
				entry.rect = fz_make_rect(0, 0, 0, 0);
			entry.quads = part.quads;
			entry.contents = a.contents;
			entry.author = a.author;
			entry.isMarkup = true;
			entry.kind = kAnnotMarkup;
			entry.isFreeText = false;
			entry.hasColor = true;
			entry.color = a.color;
			entry.quote = a.quote;
			entry.cfi = a.cfi;
			entry.continued = k > 0;
			annotations.push_back(entry);
		}
	}
}


bool
Document::StoreIndexFor(int pageNo, int index, int* storeIndex)
{
	ResolveStore();
	int n = 0;
	for (size_t i = 0; i < fStore.size(); i++) {
		for (size_t k = 0; k < fResolved[i].size(); k++) {
			if (fResolved[i][k].page != pageNo)
				continue;
			if (n++ == index) {
				*storeIndex = (int)i;
				return true;
			}
		}
	}
	return false;
}


///////////////////////////////////////////////////////////////////////////
// Anchors: places in a book that stay where they are when the pages change

void
TextAnchor::Archive(BMessage* into) const
{
	into->AddInt32("chapter", chapter);
	into->AddFloat("fraction", fraction);
	into->AddFloat("ypos", ypos);
	into->AddString("quote", quote);
	if (!cfi.IsEmpty())
		into->AddString("cfi", cfi);
}


bool
TextAnchor::Unarchive(const BMessage* from)
{
	if (from == NULL || from->FindString("quote", &quote) != B_OK || quote.IsEmpty())
		return false;
	from->FindInt32("chapter", &chapter);
	from->FindFloat("fraction", &fraction);
	from->FindFloat("ypos", &ypos);
	cfi = "";
	from->FindString("cfi", &cfi);
	return true;
}


// The anchor of the beginning of a page: its first words.
bool
Document::MakeAnchor(int page, TextAnchor* anchor)
{
	if (!fReflowable || page < 1 || page > fPageCount)
		return false;

	DocumentLocker locker(this);
	std::map<int, fz_stext_page*> cache;
	fz_stext_page* text = TextOfPage(this, cache, page - 1);
	char* copied = NULL;
	if (text != NULL) {
		fz_var(copied);
		fz_try(fContext) {
			copied = fz_copy_selection(fContext, text, fz_make_point(-10000, -10000), fz_make_point(10000, 10000),
				0);
		}
		fz_catch(fContext) {
			copied = NULL;
		}
	}
	BString words(copied != NULL ? copied : "");
	fz_free(fContext, copied);
	if (text != NULL)
		fz_drop_stext_page(fContext, text);

	words.ReplaceAll("\r", " ");
	words.ReplaceAll("\n", " ");
	while (words.FindFirst("  ") >= 0)
		words.ReplaceAll("  ", " ");
	words.Trim();
	// about a line, ending at a word
	if (words.Length() > 80) {
		int32 at = words.FindLast(' ', 80);
		words.Truncate(at > 20 ? at : 80);
	}
	if (words.IsEmpty())
		return false;

	int chapter = 0, inChapter = 0, chapterPages = 1;
	fz_try(fContext) {
		fz_location location = fz_location_from_page_number(fContext, fDocument, page - 1);
		chapter = location.chapter;
		inChapter = location.page;
		chapterPages = fz_count_chapter_pages(fContext, fDocument, chapter);
	}
	fz_catch(fContext) {
		return false;
	}
	if (chapterPages < 1)
		chapterPages = 1;

	anchor->chapter = chapter;
	anchor->fraction = (inChapter + 0.5f) / chapterPages;
	anchor->ypos = 0;
	anchor->quote = words;
	anchor->cfi = "";
	if (fEpub != NULL)
		EpubCfi::Create(fPath.String(), *fEpub, chapter, words.String(), anchor->fraction, &anchor->cfi);
	return true;
}


// The page of an anchor: where its words are now, or if they are not found where its chapter is.
int
Document::PageOfAnchor(const TextAnchor& anchor)
{
	if (!fReflowable)
		return 0;

	DocumentLocker locker(this);
	StoredAnnotation mark;
	mark.chapter = anchor.chapter;
	mark.fraction = anchor.fraction;
	mark.ypos = anchor.ypos;
	mark.quote = anchor.quote;
	mark.cfi = anchor.cfi;

	std::map<int, fz_stext_page*> cache;
	std::vector<StoredPart> parts;
	ResolveAnnotation(this, cache, mark, &parts);
	for (std::map<int, fz_stext_page*>::iterator it = cache.begin(); it != cache.end(); ++it) {
		if (it->second != NULL)
			fz_drop_stext_page(fContext, it->second);
	}
	return parts.empty() ? 0 : parts[0].page;
}


///////////////////////////////////////////////////////////////////////////
// Changes

void
Document::PushStoreUndo(const char* name, int page)
{
	fResolved.resize(fStore.size());
	fResolvedKnown.resize(fStore.size(), 0);
	StoreState state;
	state.name = name;
	state.page = page;
	state.annotations = fStore;
	state.resolved = fResolved;
	state.known = fResolvedKnown;
	fStoreUndo.push_back(state);
	if ((int)fStoreUndo.size() > kMaxUndo) {
		fStoreUndo.erase(fStoreUndo.begin());
		if (fStoreSavedDepth > 0)
			fStoreSavedDepth--;
	}
	fStoreRedo.clear();
	fModified = true;
}


bool
Document::StoreAddMarkup(int pageNo, MarkupType type, const fz_quad* quads, int count, const float color[3])
{
	DocumentLocker locker(this);

	// the words that are marked
	std::map<int, fz_stext_page*> cache;
	fz_stext_page* text = TextOfPage(this, cache, pageNo - 1);
	char* copied = NULL;
	if (text != NULL) {
		const fz_quad& first = quads[0];
		const fz_quad& last = quads[count - 1];
		fz_point a = fz_make_point(first.ul.x + 0.5f, (first.ul.y + first.ll.y) / 2);
		fz_point b = fz_make_point(last.ur.x - 0.5f, (last.ur.y + last.lr.y) / 2);
		fz_var(copied);
		fz_try(fContext) {
			copied = fz_copy_selection(fContext, text, a, b, 0);
		}
		fz_catch(fContext) {
			copied = NULL;
		}
	}
	BString quote(copied != NULL ? copied : "");
	fz_free(fContext, copied);
	if (text != NULL)
		fz_drop_stext_page(fContext, text);

	quote.ReplaceAll("\r", " ");
	quote.ReplaceAll("\n", " ");
	while (quote.FindFirst("  ") >= 0)
		quote.ReplaceAll("  ", " ");
	quote.Trim();
	if (quote.Length() == 0)
		return false;
	if (quote.Length() > kMaxQuoteLength)
		quote.Truncate(kMaxQuoteLength);

	StoredAnnotation a;
	char id[48];
	NewAnnotationId(id, sizeof(id));
	a.id = id;
	a.markup = type;
	a.color = ((uint32)(color[0] * 255 + 0.5f) << 16) | ((uint32)(color[1] * 255 + 0.5f) << 8)
		| (uint32)(color[2] * 255 + 0.5f);
	const char* author = getenv("USER");
	a.author = author != NULL ? author : "";
	a.created = (int64)time(NULL);
	a.quote = quote;

	int chapter = 0, inChapter = 0, chapterPages = 1;
	fz_try(fContext) {
		fz_location location = fz_location_from_page_number(fContext, fDocument, pageNo - 1);
		chapter = location.chapter;
		inChapter = location.page;
		chapterPages = fz_count_chapter_pages(fContext, fDocument, chapter);
	}
	fz_catch(fContext) {
		LogReflowError(fContext, "cannot find the chapter");
		return false;
	}
	if (chapterPages < 1)
		chapterPages = 1;
	a.chapter = chapter;
	a.fraction = (inChapter + 0.5f) / chapterPages;
	fz_rect bounds;
	a.ypos = 0;
	if (PageBounds(pageNo, &bounds) && bounds.y1 > bounds.y0)
		a.ypos = (quads[0].ul.y - bounds.y0) / (bounds.y1 - bounds.y0);

	// where it is in the book, in a form that other programs know
	if (fEpub != NULL)
		EpubCfi::Create(fPath.String(), *fEpub, chapter, quote.String(), a.fraction, &a.cfi);

	const char* names[] = { B_TRANSLATE("Add highlight"), B_TRANSLATE("Add underline"),
		B_TRANSLATE("Add strike out"), B_TRANSLATE("Add squiggly line") };
	PushStoreUndo(names[type], pageNo);
	fStore.push_back(a);
	// where it is, is known: the quads that were given
	StoredPart part;
	part.page = pageNo;
	part.quads.assign(quads, quads + count);
	fResolved.push_back(std::vector<StoredPart>(1, part));
	fResolvedKnown.push_back(1);
	return true;
}


bool
Document::StoreDelete(int pageNo, int index)
{
	DocumentLocker locker(this);
	int at;
	if (!StoreIndexFor(pageNo, index, &at))
		return false;
	PushStoreUndo(B_TRANSLATE("Delete annotation"), pageNo);
	fStore.erase(fStore.begin() + at);
	fResolved.erase(fResolved.begin() + at);
	fResolvedKnown.erase(fResolvedKnown.begin() + at);
	return true;
}


bool
Document::StoreSetContents(int pageNo, int index, const char* text)
{
	DocumentLocker locker(this);
	int at;
	if (!StoreIndexFor(pageNo, index, &at))
		return false;
	PushStoreUndo(B_TRANSLATE("Change note"), pageNo);
	fStore[at].contents = text != NULL ? text : "";
	return true;
}


bool
Document::StoreSetColor(int pageNo, int index, uint32 rgb)
{
	DocumentLocker locker(this);
	int at;
	if (!StoreIndexFor(pageNo, index, &at))
		return false;
	PushStoreUndo(B_TRANSLATE("Change color"), pageNo);
	fStore[at].color = rgb & 0xffffff;
	return true;
}


int
Document::StoreUndoRedo(bool undo)
{
	DocumentLocker locker(this);
	std::vector<StoreState>& from = undo ? fStoreUndo : fStoreRedo;
	std::vector<StoreState>& to = undo ? fStoreRedo : fStoreUndo;
	if (from.empty())
		return 0;

	StoreState state = from.back();
	from.pop_back();
	StoreState current;
	current.name = state.name;
	current.page = state.page;
	current.annotations = fStore;
	current.resolved = fResolved;
	current.known = fResolvedKnown;
	to.push_back(current);

	fStore = state.annotations;
	fResolved = state.resolved;
	fResolvedKnown = state.known;
	fModified = fStoreUndo.size() != fStoreSavedDepth;
	return state.page;
}


///////////////////////////////////////////////////////////////////////////
// Drawing: the marks go into the rendering of the page

static void
PaintQuad(fz_context* context, fz_device* device, fz_matrix ctm, const fz_quad& quad, int markup,
	const float rgb[3])
{
	fz_path* path = NULL;
	fz_stroke_state* stroke = NULL;
	fz_var(path);
	fz_var(stroke);
	fz_try(context) {
		path = fz_new_path(context);
		if (markup == kMarkupHighlight) {
			fz_moveto(context, path, quad.ul.x, quad.ul.y);
			fz_lineto(context, path, quad.ur.x, quad.ur.y);
			fz_lineto(context, path, quad.lr.x, quad.lr.y);
			fz_lineto(context, path, quad.ll.x, quad.ll.y);
			fz_closepath(context, path);
			// the text shows through, as with a marker
			fz_rect area = fz_transform_rect(fz_rect_from_quad(quad), ctm);
			fz_begin_group(context, device, area, NULL, 0, 0, FZ_BLEND_MULTIPLY, 1);
			fz_fill_path(context, device, path, 0, ctm, fz_device_rgb(context), rgb, 1, fz_default_color_params);
			fz_end_group(context, device);
		} else {
			float height = fabsf(quad.ll.y - quad.ul.y);
			if (markup == kMarkupStrikeOut) {
				fz_moveto(context, path, (quad.ul.x + quad.ll.x) / 2, (quad.ul.y + quad.ll.y) / 2);
				fz_lineto(context, path, (quad.ur.x + quad.lr.x) / 2, (quad.ur.y + quad.lr.y) / 2);
			} else {
				// a little above the lower edge of the line
				fz_moveto(context, path, quad.ll.x, quad.ll.y - height * 0.12f);
				fz_lineto(context, path, quad.lr.x, quad.lr.y - height * 0.12f);
			}
			stroke = fz_new_stroke_state(context);
			stroke->linewidth = fmaxf(0.7f, height * 0.06f);
			fz_stroke_path(context, device, path, stroke, ctm, fz_device_rgb(context), rgb, 1,
				fz_default_color_params);
		}
	}
	fz_always(context) {
		fz_drop_stroke_state(context, stroke);
		fz_drop_path(context, path);
	}
	fz_catch(context) {
		LogReflowError(context, "cannot draw a mark");
	}
}


void
Document::PaintStoredAnnotations(int pageNo, fz_device* device, fz_matrix ctm)
{
	if (!UsesStore())
		return;

	DocumentLocker locker(this);
	ResolveStore();
	for (size_t i = 0; i < fStore.size(); i++) {
		const StoredAnnotation& a = fStore[i];
		float rgb[3] = { ((a.color >> 16) & 0xff) / 255.0f, ((a.color >> 8) & 0xff) / 255.0f,
			(a.color & 0xff) / 255.0f };
		for (size_t k = 0; k < fResolved[i].size(); k++) {
			const StoredPart& part = fResolved[i][k];
			if (part.page != pageNo)
				continue;
			for (size_t q = 0; q < part.quads.size(); q++)
				PaintQuad(fContext, device, ctm, part.quads[q], a.markup, rgb);
		}
	}
}

/*
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
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

// Reflowable documents (EPUB): the layout of the pages for a text size, and the chapters.
//
// Note: fz_try() uses setjmp()/longjmp(), so no C++ objects with destructors may be created or destroyed inside
// of a fz_try() block.

#include "Document.h"

#include <stdio.h>

#include <Catalog.h>
#include <String.h>

extern "C" {
#include <mupdf/pdf.h>
}

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "Document"

const float Document::kReflowWidth = 432;		// 6 x 9 inches
const float Document::kReflowHeight = 648;
const float Document::kDefaultTextSize = 12;
const float Document::kMinTextSize = 6;
const float Document::kMaxTextSize = 40;


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

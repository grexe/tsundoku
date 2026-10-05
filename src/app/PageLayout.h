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

#ifndef _PAGE_LAYOUT_H_
#define _PAGE_LAYOUT_H_

#include <vector>

#include <Rect.h>

#include "Document.h"

// How the pages of a document are arranged in the view. Each is a preset of a grid, see PageLayout::SetFlow().
enum PageFlow {
	kFlowSingle = 0,     // one page at a time
	kFlowDouble,         // two pages side by side, like the pages of a book (the first one stands alone, on the right)
	kFlowContinuous,     // all pages one below the other, scrolling from one to the next
	kFlowFourFold        // four pages at a time, two by two (not offered in the user interface yet)
};

// Where the pages are in the canvas that the view scrolls over, for a size of the pages (their zoom and rotation).
// It knows nothing about rendering: the view asks which pages it needs to show and where each one goes. Pages that
// are not like each other in size are fine; those that have not been measured yet count like the first one.
//
// The arrangement is a grid. A view of the document (a "spread") has columns x rows cells that are filled with
// consecutive pages. The title page can be left alone: then the first spread is page 1 only (in the cell at the
// right end of the first row, as in a book) and the others follow from page 2. What is shown is one spread, or, if
// the flow is continuous, all spreads below each other. A new arrangement is a new line in the table of presets
// in PageLayout.cpp: a number of columns and rows and whether it scrolls. Continuous arrangements with more than
// one column work with the same code.
class PageLayout {
public:
	PageLayout();

	void     SetFlow(PageFlow flow);
	PageFlow Flow() const { return fFlow; }
	bool     IsContinuous() const { return fContinuous; }
	// the title page is a spread of its own (only matters if a spread has more than one page)
	void     SetFirstPageAlone(bool alone);
	bool     FirstPageAlone() const { return fFirstAlone; }
	// Read from the right to the left, like a manga: the first page of a spread is the one on the right, and the title
	// page alone is on the left.
	void     SetRightToLeft(bool rightToLeft);
	bool     RightToLeft() const { return fRightToLeft; }
	// Read from the top to the bottom, like a webtoon (a strip as high as the story is long, cut into pages): in the continuous flow
	// the pages are one against the next without a gap.
	void     SetTopToBottom(bool topToBottom);
	bool     TopToBottom() const { return fTopToBottom; }

	// the document and the size of its pages in pixels, anything measured before is forgotten
	void     SetPages(Document* document, int pageCount, float dpi, int rotation);

	// The page that a request for this page leads to: a spread starts with its first page (the first spread is
	// page 1 even if it has blank cells), with one page per spread it is the page itself.
	int      NormalizePage(int page) const;
	// The first page of the spread after the one that has the page, and of the spread before it (the page itself if there
	// is none): where a step forward or backward goes. A spread has the pages of the cells, but a page that is wider than
	// high in a comic book (a double page) is a spread of its own, and the page before it too if it has no partner.
	int      NextSpreadPage(int page) const;
	int      PreviousSpreadPage(int page) const;

	// Arranges the pages around the current one: the spread or the whole document. After that the canvas
	// size and the rectangles are valid.
	void     Arrange(int current);
	float    CanvasWidth() const { return fCanvasWidth; }
	float    CanvasHeight() const { return fCanvasHeight; }

	// The rectangle of a page in the canvas, empty if the page is not part of the arrangement.
	BRect    PageRect(int page) const;

	// The pages that the view needs when it shows the canvas between top and bottom: those of the spread, or the
	// pages that are inside, with some more around them.
	void     NeededPages(float top, float bottom, float margin, std::vector<int>& pages) const;

	// the page that is at a height in the canvas (continuous flow), the first or the last one outside
	int      PageAt(float y) const;
	// the height of the top of a page in the canvas
	float    PageTop(int page) const;

	// the size of the page in pixels
	void     PageSize(int page, float* width, float* height) const;

	static const float kPageGap;     // between the rows of pages
	static const float kSpreadGap;   // between the columns of a spread

private:
	void     Measure(int page) const;
	// the spread that has the page (0 for the first), and the first page of a spread
	int      SpreadOf(int page) const;
	int      FirstPageOfSpread(int spread) const;
	// how many pages the spread has
	int      PagesInSpread(int spread) const;
	// whether a page is a double page that has a spread to itself (the pages of a comic book that are wider than high)
	bool     IsWide(int page) const;
	// the spreads as far as they are needed; a spread starts at a page that is not known before the ones before it are
	void     ExtendSpreads(int upToPage) const;
	void     ForgetSpreads();

	PageFlow  fFlow;
	int       fColumns, fRows;
	bool      fContinuous;
	bool      fFirstAlone;
	bool      fRightToLeft;
	bool      fTopToBottom;
	float     fPageGap;       // between the pages of the continuous flow
	bool      fWideAlone;     // pages that are wider than high have a spread to themselves (comic books, two columns)
	Document* fDocument;
	int       fPageCount;
	float     fDpi;
	int       fRotation;
	// sizes in pixels, 0 if not measured
	mutable std::vector<float> fWidths, fHeights;
	mutable float fReferenceWidth, fReferenceHeight;
	// the first page of each spread, as far as they are known, and the first page that is not in one yet
	mutable std::vector<int> fSpreads;
	mutable int fNextStart;

	float     fCanvasWidth, fCanvasHeight;
	int       fCurrent;
	// a spread: the pages and where they go
	std::vector<int>   fShownPages;
	std::vector<BRect> fShownRects;
	// continuous: the tops of all pages and their left
	std::vector<float> fTops;
};

#endif

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

#include "PageLayout.h"

#include <math.h>

#include <algorithm>

const float PageLayout::kPageGap = 8;
const float PageLayout::kSpreadGap = 4;


// The presets: pages per spread as columns and rows, and whether the spreads follow each other in one scrolling
// view. Here is where a new way to arrange pages goes.
static const struct {
	int  columns;
	int  rows;
	bool continuous;
} kPresets[] = {
	{ 1, 1, false },   // kFlowSingle
	{ 2, 1, false },   // kFlowDouble: the pages of a book
	{ 1, 1, true },    // kFlowContinuous
	{ 2, 2, false }    // kFlowFourFold
};


PageLayout::PageLayout()
	:
	fFlow(kFlowSingle),
	fColumns(1),
	fRows(1),
	fContinuous(false),
	fFirstAlone(true),
	fRightToLeft(false),
	fWideAlone(false),
	fDocument(NULL),
	fPageCount(0),
	fDpi(72),
	fRotation(0),
	fReferenceWidth(100),
	fReferenceHeight(100),
	fNextStart(1),
	fCanvasWidth(100),
	fCanvasHeight(100),
	fCurrent(1)
{
}


void
PageLayout::SetFlow(PageFlow flow)
{
	if ((int)flow < 0 || (int)flow >= (int)(sizeof(kPresets) / sizeof(kPresets[0])))
		flow = kFlowSingle;
	fFlow = flow;
	fTops.clear();
	fShownPages.clear();
	fShownRects.clear();
	fColumns = kPresets[flow].columns;
	fRows = kPresets[flow].rows;
	fContinuous = kPresets[flow].continuous;
	ForgetSpreads();
}


void
PageLayout::SetPages(Document* document, int pageCount, float dpi, int rotation)
{
	// the spreads do not depend on the size of the pages, they stay if it is the same document
	if (document != fDocument || pageCount != fPageCount)
		ForgetSpreads();
	fDocument = document;
	fPageCount = pageCount;
	fDpi = dpi;
	fRotation = rotation;
	fWideAlone = document != NULL && document->IsComic();
	fWidths.assign(pageCount + 1, 0);
	fHeights.assign(pageCount + 1, 0);
	fReferenceWidth = fReferenceHeight = 0;
	fTops.clear();
}


// measures a page, once
void
PageLayout::Measure(int page) const
{
	if (page < 1 || page > fPageCount || fWidths[page] > 0)
		return;

	fz_matrix matrix;
	int width = 0, height = 0;
	if (fDocument != NULL && fDocument->PageMatrix(page, fDpi, fRotation, &matrix, &width, &height)) {
		fWidths[page] = width;
		fHeights[page] = height;
		if (fReferenceWidth <= 0) {
			fReferenceWidth = width;
			fReferenceHeight = height;
		}
	}
}


void
PageLayout::PageSize(int page, float* width, float* height) const
{
	if (fReferenceWidth <= 0)
		Measure(1);
	Measure(page);
	if (page >= 1 && page <= fPageCount && fWidths[page] > 0) {
		*width = fWidths[page];
		*height = fHeights[page];
	} else {
		*width = fReferenceWidth > 0 ? fReferenceWidth : 100;
		*height = fReferenceHeight > 0 ? fReferenceHeight : 100;
	}
}


void
PageLayout::SetFirstPageAlone(bool alone)
{
	if (alone != fFirstAlone)
		ForgetSpreads();
	fFirstAlone = alone;
}


void
PageLayout::SetRightToLeft(bool rightToLeft)
{
	fRightToLeft = rightToLeft;
	fShownPages.clear();
	fShownRects.clear();
}


void
PageLayout::ForgetSpreads()
{
	fSpreads.clear();
	fNextStart = 1;
	fTops.clear();
	fShownPages.clear();
	fShownRects.clear();
}


// a double page: wider than high as the page itself is (not turned by the reader), or marked as one in the file
bool
PageLayout::IsWide(int page) const
{
	return fWideAlone && fColumns == 2 && fRows == 1 && fDocument != NULL && fDocument->IsWidePage(page);
}


// The spreads: with the title page alone it is the first one, and the others have as many pages as the cells; a double
// page is alone, and so is a page that is followed by one (it has no partner then). Only the pages that are needed are
// looked at, so a comic book is not read from end to end to show its first page.
void
PageLayout::ExtendSpreads(int upToPage) const
{
	int perSpread = fColumns * fRows;
	if (fSpreads.empty() && fNextStart == 1 && fFirstAlone && perSpread > 1 && fPageCount >= 1) {
		fSpreads.push_back(1);
		fNextStart = 2;
	}
	while (fNextStart <= upToPage && fNextStart <= fPageCount) {
		int start = fNextStart;
		fSpreads.push_back(start);
		if (fColumns == 2 && fRows == 1) {
			if (IsWide(start) || start + 1 > fPageCount || IsWide(start + 1))
				fNextStart = start + 1;
			else
				fNextStart = start + 2;
		} else
			fNextStart = start + perSpread;
	}
}


int
PageLayout::SpreadOf(int page) const
{
	ExtendSpreads(page);
	int low = 0, high = (int)fSpreads.size() - 1;
	if (high < 0)
		return 0;
	while (low < high) {
		int middle = (low + high + 1) / 2;
		if (fSpreads[middle] <= page)
			low = middle;
		else
			high = middle - 1;
	}
	return low;
}


int
PageLayout::FirstPageOfSpread(int spread) const
{
	while ((int)fSpreads.size() <= spread && fNextStart <= fPageCount)
		ExtendSpreads(fNextStart);
	if (spread < 0)
		spread = 0;
	if (spread >= (int)fSpreads.size())
		return fSpreads.empty() ? 1 : fPageCount + 1;	// there is none
	return fSpreads[spread];
}


int
PageLayout::PagesInSpread(int spread) const
{
	return FirstPageOfSpread(spread + 1) - FirstPageOfSpread(spread);
}


int
PageLayout::NextSpreadPage(int page) const
{
	if (page < 1)
		return 1;
	if (fContinuous)
		return page < fPageCount ? page + 1 : fPageCount;
	int next = FirstPageOfSpread(SpreadOf(page) + 1);
	return next <= fPageCount ? next : FirstPageOfSpread(SpreadOf(page));
}


int
PageLayout::PreviousSpreadPage(int page) const
{
	if (fContinuous)
		return page > 1 ? page - 1 : 1;
	int spread = SpreadOf(page);
	return spread > 0 ? FirstPageOfSpread(spread - 1) : FirstPageOfSpread(0);
}


int
PageLayout::NormalizePage(int page) const
{
	if (page > fPageCount)
		page = fPageCount;
	if (page < 1)
		page = 1;
	if (fContinuous)
		return page;
	return FirstPageOfSpread(SpreadOf(page));
}


void
PageLayout::Arrange(int current)
{
	fCurrent = NormalizePage(current);
	fShownPages.clear();
	fShownRects.clear();

	if (fContinuous) {
		float widest = 0;
		fTops.assign(fPageCount + 2, 0);
		float y = 0;
		for (int page = 1; page <= fPageCount; page++) {
			float width, height;
			PageSize(page, &width, &height);
			fTops[page] = y;
			y += height + kPageGap;
			widest = fmaxf(widest, width);
		}
		fTops[fPageCount + 1] = y;
		fCanvasWidth = widest;
		fCanvasHeight = fmaxf(1, y - kPageGap);
		return;
	}

	// a spread: the cells are filled row by row, a column is as wide and a row as high as its widest and highest page
	int cells = fColumns * fRows;
	int spread = SpreadOf(fCurrent);
	int first = FirstPageOfSpread(spread);
	int count = PagesInSpread(spread);
	std::vector<int> pages(cells, 0);
	if (fColumns == 2 && fRows == 1) {
		if (count == 1 && IsWide(first)) {
			// a double page is all there is: its size is the size of the canvas
			float width, height;
			PageSize(first, &width, &height);
			fCanvasWidth = width;
			fCanvasHeight = height;
			fShownPages.push_back(first);
			fShownRects.push_back(BRect(0, 0, width, height));
			return;
		}
		if (count >= 2) {
			pages[fRightToLeft ? 1 : 0] = first;
			pages[fRightToLeft ? 0 : 1] = first + 1;
		} else {
			// a page alone: the title page is on the right in a book (on the left if it is read the other way), the
			// last one on the left
			bool title = fFirstAlone && spread == 0;
			pages[title != fRightToLeft ? 1 : 0] = first;
		}
	} else {
		for (int cell = 0; cell < cells; cell++) {
			// the title page alone sits in the cell at the right end of the first row
			int page;
			if (fFirstAlone && spread == 0)
				page = cell == fColumns - 1 ? 1 : 0;
			else
				page = first + cell;
			pages[cell] = page;
		}
		if (fRightToLeft && fColumns > 1) {
			for (int row = 0; row < fRows; row++)
				std::reverse(pages.begin() + row * fColumns, pages.begin() + (row + 1) * fColumns);
		}
	}

	std::vector<float> columnWidth(fColumns, 0), rowHeight(fRows, 0);
	float anyWidth = 0, anyHeight = 0;
	for (int cell = 0; cell < cells; cell++) {
		int page = pages[cell];
		if (page < 1 || page > fPageCount) {
			pages[cell] = 0;
			continue;
		}
		float width, height;
		PageSize(page, &width, &height);
		columnWidth[cell % fColumns] = fmaxf(columnWidth[cell % fColumns], width);
		rowHeight[cell / fColumns] = fmaxf(rowHeight[cell / fColumns], height);
		anyWidth = fmaxf(anyWidth, width);
		anyHeight = fmaxf(anyHeight, height);
	}
	// a column or a row of blank cells is as large as the pages
	for (int column = 0; column < fColumns; column++) {
		if (columnWidth[column] <= 0)
			columnWidth[column] = anyWidth > 0 ? anyWidth : 100;
	}
	for (int row = 0; row < fRows; row++) {
		if (rowHeight[row] <= 0)
			rowHeight[row] = anyHeight > 0 ? anyHeight : 100;
	}

	std::vector<float> columnLeft(fColumns, 0), rowTop(fRows, 0);
	float x = 0;
	for (int column = 0; column < fColumns; column++) {
		columnLeft[column] = x;
		x += columnWidth[column] + kSpreadGap;
	}
	float y = 0;
	for (int row = 0; row < fRows; row++) {
		rowTop[row] = y;
		y += rowHeight[row] + kPageGap;
	}
	fCanvasWidth = x - kSpreadGap;
	fCanvasHeight = y - kPageGap;

	for (int cell = 0; cell < cells; cell++) {
		if (pages[cell] == 0)
			continue;
		int column = cell % fColumns, row = cell / fColumns;
		float width, height;
		PageSize(pages[cell], &width, &height);
		// the pages of the left half lie against the middle, those of the right half too, like a book
		float left = columnLeft[column];
		if (fColumns > 1) {
			if (column < fColumns / 2)
				left += columnWidth[column] - width;
			else if (column * 2 + 1 == fColumns)
				left += floorf((columnWidth[column] - width) / 2);
		}
		fShownPages.push_back(pages[cell]);
		fShownRects.push_back(BRect(left, rowTop[row], left + width, rowTop[row] + height));
	}
}


BRect
PageLayout::PageRect(int page) const
{
	if (fContinuous) {
		if (page < 1 || page > fPageCount || (int)fTops.size() != fPageCount + 2)
			return BRect();
		float width, height;
		PageSize(page, &width, &height);
		float left = floorf((fCanvasWidth - width) / 2);
		return BRect(left, fTops[page], left + width, fTops[page] + height);
	}

	for (size_t i = 0; i < fShownPages.size(); i++) {
		if (fShownPages[i] == page)
			return fShownRects[i];
	}
	return BRect();
}


void
PageLayout::NeededPages(float top, float bottom, float margin, std::vector<int>& pages) const
{
	pages.clear();
	if (fContinuous) {
		if ((int)fTops.size() != fPageCount + 2) {
			// not arranged yet
			pages.push_back(fCurrent);
			return;
		}
		int first = PageAt(top - margin), last = PageAt(bottom + margin);
		for (int page = first; page <= last; page++)
			pages.push_back(page);
		return;
	}
	pages = fShownPages;
}


// binary search in the tops of the pages
int
PageLayout::PageAt(float y) const
{
	if (!fContinuous || fPageCount < 1 || (int)fTops.size() != fPageCount + 2)
		return fCurrent;
	if (y <= 0)
		return 1;
	int low = 1, high = fPageCount;
	while (low < high) {
		int middle = (low + high + 1) / 2;
		if (fTops[middle] <= y)
			low = middle;
		else
			high = middle - 1;
	}
	return low;
}


float
PageLayout::PageTop(int page) const
{
	if (!fContinuous || page < 1 || page > fPageCount || (int)fTops.size() != fPageCount + 2)
		return 0;
	return fTops[page];
}

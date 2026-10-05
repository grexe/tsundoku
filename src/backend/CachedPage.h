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
#ifndef _CACHED_PAGE_H
#define _CACHED_PAGE_H

#include <vector>

#include <Bitmap.h>
#include <Point.h>
#include <Rect.h>
#include <SupportDefs.h>

#include "Document.h"

class PageRenderer;

// The rendered page that is shown: the bitmap and what is needed to interact with it (links, text).
//
// Page space is the coordinate system of MuPDF for the page (points, y points down, rotation of the page
// applied). The bitmap is a transformation of it, see Matrix().
class CachedPage {
public:
	CachedPage();
	virtual ~CachedPage();

	enum State {
		EMPTY = 0,     // Page
		WAITING = 1,   // Waits to be rendered
		RENDERING = 2, // Is rendering
		READY = 3      // Has been rendered
	};

	enum State GetState() const { return mState; }

	BBitmap* GetBitmap() const { return mBitmap; }
	int32 GetWidth() const     { return mWidth; }
	int32 GetHeight() const    { return mHeight; }

	// maps page space to pixels of the bitmap
	const fz_matrix& Matrix() const { return mMatrix; }

	fz_point  DevToPage(BPoint dev) const;
	BPoint    PageToDev(fz_point page) const;
	// the bounding box in the bitmap
	BRect     PageToDev(fz_rect page) const;
	BRect     PageToDev(fz_quad quad) const;

	// The link at the position (page space), NULL if there is none. Valid until the page is rendered again.
	const DocLink* FindLink(fz_point point) const;

	// The annotation at the position (page space), NULL if there is none. Valid until the page is rendered again.
	// The tolerance (page space) is how far from a line or border a click still counts; with movable only the
	// annotations that can be moved on the page are looked at, not the marks on text.
	const DocAnnotation* FindAnnotation(fz_point point, float tolerance = 3, bool movable = false) const;
	// the annotation with the index
	const DocAnnotation* AnnotationAt(int index) const;
	// pixels of the bitmap per unit of page space
	float Scale() const;

	// Structured text of the page for selecting and copying; NULL as long as the page is rendering.
	// The caller holds the lock of the document while it uses it.
	fz_stext_page* Text() const { return mState == READY ? mText : NULL; }
	Document* GetDocument() const { return mDocument; }

	friend class PageRenderer;
	friend class PDFView;

	// forgets the text and links (they belong to the document), keeps the bitmap
	void MakeEmpty();

protected:
	void SetState(enum State state) { mState = state; }
	void SetBitmap(BBitmap* bitmap, int32 width, int32 height);
	void SetBitmapSize(int32 width, int32 height) { mWidth = width; mHeight = height; }
	void SetMatrix(const fz_matrix& matrix);

	enum State mState;
	BBitmap* mBitmap;
	int32 mWidth, mHeight;
	fz_matrix mMatrix, mInverse;
	Document* mDocument;
	fz_stext_page* mText;
	std::vector<DocLink> mLinks;
	std::vector<DocAnnotation> mAnnotations;

public:
	// where the text of the last search is on this page (page space), see PDFView::UpdateFindQuads()
	std::vector<fz_quad> mFindQuads;
};

#endif

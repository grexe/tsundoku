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
#include "CachedPage.h"

#include <math.h>

CachedPage::CachedPage()
	:
	mState(EMPTY),
	mBitmap(NULL),
	mWidth(0),
	mHeight(0),
	mMatrix(fz_identity),
	mInverse(fz_identity),
	mDocument(NULL),
	mText(NULL)
{
}


CachedPage::~CachedPage()
{
	MakeEmpty();
	delete mBitmap;
}


void
CachedPage::SetMatrix(const fz_matrix& matrix)
{
	mMatrix = matrix;
	mInverse = fz_invert_matrix(matrix);
}


fz_point
CachedPage::DevToPage(BPoint dev) const
{
	return fz_transform_point(fz_make_point(dev.x, dev.y), mInverse);
}


BPoint
CachedPage::PageToDev(fz_point page) const
{
	fz_point p = fz_transform_point(page, mMatrix);
	return BPoint(p.x, p.y);
}


BRect
CachedPage::PageToDev(fz_rect page) const
{
	fz_rect r = fz_transform_rect(page, mMatrix);
	return BRect(floorf(r.x0), floorf(r.y0), ceilf(r.x1), ceilf(r.y1));
}


BRect
CachedPage::PageToDev(fz_quad quad) const
{
	return PageToDev(fz_rect_from_quad(quad));
}


const DocLink*
CachedPage::FindLink(fz_point point) const
{
	if (mState != READY)
		return NULL;

	for (size_t i = 0; i < mLinks.size(); i++) {
		const fz_rect& r = mLinks[i].rect;
		if (point.x >= r.x0 && point.x < r.x1 && point.y >= r.y0 && point.y < r.y1)
			return &mLinks[i];
	}
	return NULL;
}


const DocAnnotation*
CachedPage::FindAnnotation(fz_point point) const
{
	if (mState != READY)
		return NULL;

	// the last one is on top
	for (int i = (int)mAnnotations.size() - 1; i >= 0; i--) {
		const DocAnnotation& annotation = mAnnotations[i];
		if (annotation.isMarkup && !annotation.quads.empty()) {
			for (size_t j = 0; j < annotation.quads.size(); j++) {
				if (fz_is_point_inside_quad(point, annotation.quads[j]))
					return &annotation;
			}
		} else if (point.x >= annotation.rect.x0 && point.x < annotation.rect.x1
				&& point.y >= annotation.rect.y0 && point.y < annotation.rect.y1) {
			return &annotation;
		}
	}
	return NULL;
}


void
CachedPage::SetBitmap(BBitmap* bitmap, int32 width, int32 height)
{
	mBitmap = bitmap;
	mWidth = width;
	mHeight = height;
}


void
CachedPage::MakeEmpty()
{
	mLinks.clear();
	mAnnotations.clear();
	if (mText != NULL && mDocument != NULL) {
		DocumentLocker locker(mDocument);
		fz_drop_stext_page(mDocument->Context(), mText);
	}
	mText = NULL;
	// don't delete mBitmap, it is reused
}

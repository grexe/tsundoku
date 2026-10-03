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


static float
DistanceToSegment(fz_point p, fz_point a, fz_point b)
{
	float dx = b.x - a.x, dy = b.y - a.y;
	float length2 = dx * dx + dy * dy;
	float t = length2 > 0 ? ((p.x - a.x) * dx + (p.y - a.y) * dy) / length2 : 0;
	t = fmaxf(0, fminf(1, t));
	float x = a.x + t * dx - p.x, y = a.y + t * dy - p.y;
	return sqrtf(x * x + y * y);
}


static bool
InRect(fz_point p, const fz_rect& r, float margin)
{
	return p.x >= r.x0 - margin && p.x < r.x1 + margin && p.y >= r.y0 - margin && p.y < r.y1 + margin;
}


// is the point on the annotation, with the tolerance around lines and borders
static bool
HitsAnnotation(const DocAnnotation& annotation, fz_point point, float tolerance)
{
	const fz_rect& rect = annotation.rect;
	switch (annotation.kind) {
		case kAnnotMarkup:
			for (size_t i = 0; i < annotation.quads.size(); i++) {
				if (fz_is_point_inside_quad(point, annotation.quads[i]))
					return true;
			}
			return annotation.quads.empty() && InRect(point, rect, 0);

		case kAnnotRectangle:
			// only the border, so that what is inside can still be clicked
			return InRect(point, rect, tolerance)
				&& !(point.x > rect.x0 + tolerance && point.x < rect.x1 - tolerance
					&& point.y > rect.y0 + tolerance && point.y < rect.y1 - tolerance);

		case kAnnotEllipse: {
			float a = (rect.x1 - rect.x0) / 2, b = (rect.y1 - rect.y0) / 2;
			if (a < 1 || b < 1)
				return InRect(point, rect, tolerance);
			float x = (point.x - (rect.x0 + a)) / a, y = (point.y - (rect.y0 + b)) / b;
			return fabsf(sqrtf(x * x + y * y) - 1) * fminf(a, b) <= tolerance;
		}

		case kAnnotLine:
		case kAnnotInk:
			for (size_t i = 0; i < annotation.paths.size(); i++) {
				const std::vector<fz_point>& path = annotation.paths[i];
				if (path.size() == 1 && DistanceToSegment(point, path[0], path[0]) <= tolerance)
					return true;
				for (size_t k = 1; k < path.size(); k++) {
					if (DistanceToSegment(point, path[k - 1], path[k]) <= tolerance)
						return true;
				}
			}
			return false;

		default:
			return InRect(point, rect, 0);
	}
}


const DocAnnotation*
CachedPage::FindAnnotation(fz_point point, float tolerance, bool movable) const
{
	if (mState != READY)
		return NULL;

	// the last one is on top
	for (int i = (int)mAnnotations.size() - 1; i >= 0; i--) {
		const DocAnnotation& annotation = mAnnotations[i];
		if (movable && (annotation.kind == kAnnotMarkup || annotation.kind == kAnnotOther))
			continue;
		if (HitsAnnotation(annotation, point, tolerance))
			return &annotation;
	}
	return NULL;
}


const DocAnnotation*
CachedPage::AnnotationAt(int index) const
{
	if (mState != READY)
		return NULL;
	for (size_t i = 0; i < mAnnotations.size(); i++) {
		if (mAnnotations[i].index == index)
			return &mAnnotations[i];
	}
	return NULL;
}


float
CachedPage::Scale() const
{
	float scale = sqrtf(fabsf(mMatrix.a * mMatrix.d - mMatrix.b * mMatrix.c));
	return scale > 0 ? scale : 1;
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
	mFindQuads.clear();
	if (mText != NULL && mDocument != NULL) {
		DocumentLocker locker(mDocument);
		fz_drop_stext_page(mDocument->Context(), mText);
	}
	mText = NULL;
	// don't delete mBitmap, it is reused
}

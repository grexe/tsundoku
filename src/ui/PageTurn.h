/*
 * SPDX-License-Identifier: AGPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Gregor B. Rosenauer & Claude
 */
#ifndef _PAGE_TURN_H_
#define _PAGE_TURN_H_

#include <Bitmap.h>
#include <Rect.h>
#include <SupportDefs.h>
#include <View.h>

// The picture of a page that turns, for the fancy mode: one leaf of the book is lifted at its outer edge and rolled over to the
// spine, and shows the page that comes under it. It is made of what the view shows before and after (two bitmaps of the size of the
// view) with a few dozen strips of the leaf, which the drawing of the view squeezes and shades: no pixels are touched here.
namespace PageTurn {

struct Geometry {
	BRect area;			// the page or the pages (a spread) in the bitmaps
	bool  spread;		// the leaf is one of two pages and the spine is in the middle, else the leaf is the page
	bool  rightToLeft;	// the leaf is the left one of the spread (a manga), the spine of a page is on its right
};

// the time of a turn
static const bigtime_t kDuration = 600000;

// one picture of the turn from "before" to "after" at the progress of 0 (nothing is turned) to 1 (done), drawn into the view
// (whose bounds are those of the bitmaps, which has to be locked)
void Draw(BView* view, const BBitmap* before, const BBitmap* after, const Geometry& geometry, float progress);

// the progress of a time from 0 to 1, slow at the start and at the end
float Ease(float time);

}	// namespace PageTurn

#endif

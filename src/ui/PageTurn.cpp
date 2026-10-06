/*
 * SPDX-License-Identifier: AGPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Gregor B. Rosenauer & Claude
 */
#include "PageTurn.h"

#include <math.h>

#include <Region.h>

namespace PageTurn {

static const float kPi = 3.14159265f;
static const float kStep = 3.0f;			// the width of a strip of the leaf
static const rgb_color kPaper = { 234, 230, 220, 255 };		// the back of a leaf: darker than the pages, to be seen on them


float
Ease(float time)
{
	if (time <= 0)
		return 0;
	if (time >= 1)
		return 1;
	return time * time * (3 - 2 * time);
}


// where a point of the leaf goes: its distance from the spine (c) is its place before the turn; the leaf is flat up to the fold (f),
// rolled over a cylinder of the radius r from there, and flat again, back side up, beyond it. The angle is 0 on the flat front,
// and pi on the back.
static void
Place(float c, float f, float r, float* d, float* angle)
{
	if (c <= f) {
		*d = c;
		*angle = 0;
	} else if (c <= f + kPi * r) {
		float theta = (c - f) / r;
		*d = f + r * sinf(theta);
		*angle = theta;
	} else {
		*d = f - (c - f - kPi * r);
		*angle = kPi;
	}
}


static void
Shade(BView* view, BRect rect, uint8 red, uint8 green, uint8 blue, float alpha)
{
	if (alpha < 1)
		return;
	view->SetDrawingMode(B_OP_ALPHA);
	view->SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_OVERLAY);
	view->SetHighColor(red, green, blue, (uint8)fminf(alpha, 255));
	view->FillRect(rect);
	view->SetDrawingMode(B_OP_COPY);
}


void
Draw(BView* view, const BBitmap* before, const BBitmap* after, const Geometry& g, float progress)
{
	BRect bounds = view->Bounds();
	if (progress < 0)
		progress = 0;
	if (progress > 1)
		progress = 1;

	view->SetDrawingMode(B_OP_COPY);
	view->DrawBitmap(before, BPoint(0, 0));

	const float s = g.rightToLeft ? -1.0f : 1.0f;
	const float width = g.area.Width() + 1;
	const float w = g.spread ? width / 2 : width;
	const float spine = g.spread ? g.area.left + w : (g.rightToLeft ? g.area.right + 1 : g.area.left);

	// the side of the leaf already shows the page that comes (in a spread: the other side stays as it is)
	BRect leafSide = bounds;
	if (g.spread) {
		if (s > 0)
			leafSide.left = spine;
		else
			leafSide.right = spine - 1;
	}
	view->DrawBitmap(after, leafSide, leafSide);

	// the fold goes from the outer edge to the spine; the roll gets thinner at the end
	const float f = w * (1 - progress);
	const float r0 = fmaxf(10.0f, w * 0.10f);
	const float r = fmaxf(0.5f, r0 * fminf(1.0f, f / (0.35f * w)));

	BRegion clip(g.area);
	view->ConstrainClippingRegion(&clip);

	// the shadow of the leaf on the page below, beyond the roll
	if (f > 1) {
		float edge = spine + s * (f + r);
		for (int i = 0; i < 28; i++) {
			float x = edge + s * i;
			Shade(view, BRect(x, g.area.top, x, g.area.bottom), 0, 0, 0, 70.0f * (1 - i / 28.0f) * (1 - i / 28.0f));
		}
	}

	const float height = g.area.Height() + 1;
	const float top = g.area.top, bottom = g.area.bottom;
	const float rollEnd = fminf(f + kPi * r, w);	// where the roll ends, in the distance from the spine

	// the part that is not turned yet: one piece (it is not squeezed)
	if (f > 0) {
		float edge = spine + s * fminf(f, w);
		BRect part(fminf(spine, edge), top, fmaxf(spine, edge) - 1, bottom);
		if (part.Width() >= 1)
			view->DrawBitmap(before, part, part);
	}

	// the roll: strips, squeezed and shaded
	for (float c = fmaxf(f, 0); c < rollEnd; c += kStep) {
		float c1 = fminf(c + kStep, rollEnd);
		float d0, a0, d1, a1;
		Place(c, f, r, &d0, &a0);
		Place(c1, f, r, &d1, &a1);
		float x0 = spine + s * d0, x1 = spine + s * d1;
		float destLeft = fminf(x0, x1), destRight = fmaxf(x0, x1);
		if (destRight - destLeft < 0.5f)
			continue;
		float angle = (a0 + a1) / 2;
		// the roll lifts the paper a little: the strips shrink a bit above and below
		float lift = height * 0.03f * (1 - cosf(angle)) / 2;
		BRect dest(destLeft, top + lift, destRight - 0.01f, bottom - lift);

		if (angle < kPi / 2) {
			float sx0 = spine + s * c, sx1 = spine + s * c1;
			BRect source(floorf(fminf(sx0, sx1)), top, ceilf(fmaxf(sx0, sx1)) - 1, bottom);
			view->DrawBitmap(before, source, dest, B_FILTER_BITMAP_BILINEAR);
			Shade(view, dest, 0, 0, 0, 105 * sinf(angle));
		} else if (g.spread) {
			// the back of the leaf is the page that comes, from the other side of the spine
			float sx0 = spine - s * c, sx1 = spine - s * c1;
			BRect source(floorf(fminf(sx0, sx1)), top, ceilf(fmaxf(sx0, sx1)) - 1, bottom);
			view->DrawBitmap(after, source, dest, B_FILTER_BITMAP_BILINEAR);
			Shade(view, dest, 255, 255, 255, 95);
			Shade(view, dest, 0, 0, 0, 105 * sinf(angle));
		} else {
			// the back of the page, with the page showing through, from behind
			view->SetHighColor(kPaper);
			view->FillRect(dest);
			float sx0 = spine + s * c, sx1 = spine + s * c1;
			BRect source(floorf(fminf(sx0, sx1)), top, ceilf(fmaxf(sx0, sx1)) - 1, bottom);
			view->SetDrawingMode(B_OP_ALPHA);
			view->SetBlendingMode(B_CONSTANT_ALPHA, B_ALPHA_OVERLAY);
			view->SetHighColor(0, 0, 0, 60);
			view->DrawBitmap(before, source, dest);
			view->SetDrawingMode(B_OP_COPY);
			Shade(view, dest, 0, 0, 0, 120 * sinf(angle));
		}
	}

	// the back, flat again beyond the roll: one piece
	if (rollEnd < w) {
		float lift = height * 0.03f;
		float dLeft = spine + s * (2 * f + kPi * r - w), dRight = spine + s * f;
		BRect dest(fminf(dLeft, dRight), top + lift, fmaxf(dLeft, dRight) - 1, bottom - lift);
		if (dest.Width() >= 1) {
			if (g.spread) {
				float sLeft = spine - s * w, sRight = spine - s * rollEnd;
				BRect source(fminf(sLeft, sRight), top, fmaxf(sLeft, sRight) - 1, bottom);
				view->DrawBitmap(after, source, dest);
				Shade(view, dest, 255, 255, 255, 95);
			} else {
				view->SetHighColor(kPaper);
				view->FillRect(dest);
			}
			// its edge, and its shadow on what lies below, toward the spine
			float endX = s > 0 ? dest.left : dest.right;
			Shade(view, BRect(endX, top + lift, endX, bottom - lift), 0, 0, 0, 110);
			for (int i = 1; i <= 16; i++) {
				float x = endX - s * i;
				float k = 1 - i / 17.0f;
				Shade(view, BRect(x, top + lift, x, bottom - lift), 0, 0, 0, 80 * k * k);
			}
		}
	}

	view->ConstrainClippingRegion(NULL);
}

}	// namespace PageTurn

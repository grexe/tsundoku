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

#include "SidebarTabView.h"

#include <Window.h>

static const float kIconSize = 18;


// a tab that shows an icon instead of its label
class IconTab : public BTab {
public:
	IconTab(BBitmap* icon)
		:
		BTab(),
		fIcon(icon)
	{
	}

	virtual ~IconTab()
	{
		delete fIcon;
	}

	virtual void DrawLabel(BView* owner, BRect frame)
	{
		if (fIcon == NULL)
			return;
		BRect bounds = fIcon->Bounds();
		BPoint at(frame.left + (frame.Width() - bounds.Width()) / 2,
			frame.top + (frame.Height() - bounds.Height()) / 2);
		drawing_mode mode = owner->DrawingMode();
		owner->SetDrawingMode(B_OP_ALPHA);
		owner->SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_OVERLAY);
		owner->DrawBitmap(fIcon, BPoint(floorf(at.x), floorf(at.y)));
		owner->SetDrawingMode(mode);
	}

private:
	BBitmap* fIcon;
};


SidebarTabView::SidebarTabView(const char* name)
	:
	BTabView(name),
	fTipTab(-1)
{
	SetTabHeight(ceilf(kIconSize + 10));
}


void
SidebarTabView::AddPanel(BView* view, const char* label, BBitmap* icon)
{
	BTab* tab = new IconTab(icon);
	AddTab(view, tab);
	tab->SetLabel(label);	// the tooltip
}


BRect
SidebarTabView::TabFrame(int32 index) const
{
	if (index < 0 || index >= CountTabs())
		return BRect();
	const float width = ceilf(kIconSize * 2);
	const float offset = 4;
	return BRect(offset + index * width, 0, offset + (index + 1) * width, TabHeight());
}


void
SidebarTabView::Select(int32 index)
{
	BTabView::Select(index);
	if (Window() != NULL && index >= 0 && index < CountTabs()) {
		BMessage selected(kPanelSelected);
		selected.AddInt32("panel", index);
		Window()->PostMessage(&selected);
	}
}


void
SidebarTabView::MouseMoved(BPoint where, uint32 transit, const BMessage* dragMessage)
{
	BTabView::MouseMoved(where, transit, dragMessage);

	int32 tab = -1;
	if (transit != B_EXITED_VIEW) {
		for (int32 i = 0; i < CountTabs(); i++) {
			if (TabFrame(i).Contains(where)) {
				tab = i;
				break;
			}
		}
	}
	if (tab != fTipTab) {
		fTipTab = tab;
		if (tab >= 0)
			SetToolTip(TabAt(tab)->Label());
		else
			SetToolTip((const char*)NULL);
	}
}

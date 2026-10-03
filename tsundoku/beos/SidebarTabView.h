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

#ifndef _SIDEBAR_TAB_VIEW_H_
#define _SIDEBAR_TAB_VIEW_H_

#include <Bitmap.h>
#include <TabView.h>

// The switcher of the sidebar: tabs with only an icon, the tooltip tells what each is. Choosing one (by the mouse
// or the keyboard) also sends kPanelSelected with "panel" to the window.
class SidebarTabView : public BTabView {
public:
	enum {
		kPanelSelected = 'sbpn'
	};

	SidebarTabView(const char* name);

	// adds a panel with its tab, the view of the panel is the first to be shown
	void AddPanel(BView* view, const char* label, BBitmap* icon);

	virtual BRect TabFrame(int32 index) const;
	virtual void Select(int32 index);
	virtual void MouseMoved(BPoint where, uint32 transit, const BMessage* dragMessage);

private:
	int32 fTipTab;
};

#endif

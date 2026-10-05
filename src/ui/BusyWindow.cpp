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


#include "BusyWindow.h"

#include <LayoutBuilder.h>
#include <StringView.h>

#include <private/shared/BarberPole.h>


BusyWindow::BusyWindow(BWindow* over, const char* text)
	:
	BWindow(BRect(0, 0, 10, 10), "busy", B_BORDERED_WINDOW_LOOK, B_FLOATING_SUBSET_WINDOW_FEEL,
		B_NOT_CLOSABLE | B_NOT_RESIZABLE | B_NOT_ZOOMABLE | B_NOT_MOVABLE | B_AVOID_FOCUS
		| B_AUTO_UPDATE_SIZE_LIMITS | B_NOT_MINIMIZABLE),
	fOver(over)
{
	fPole = new BarberPole("pole");
	fPole->SetExplicitMinSize(BSize(220, 12));
	fPole->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, 12));

	BStringView* label = new BStringView("label", text);
	label->SetAlignment(B_ALIGN_CENTER);

	BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_DEFAULT_SPACING)
		.SetInsets(B_USE_WINDOW_INSETS)
		.Add(label)
		.Add(fPole);

	if (fOver != NULL)
		AddToSubset(fOver);
}


void
BusyWindow::Appear()
{
	if (!Lock())
		return;
	bool hidden = IsHidden();
	if (hidden) {
		// in the middle of the window it is over
		if (fOver != NULL && fOver->Lock()) {
			BRect frame = fOver->Frame();
			fOver->Unlock();
			ResizeToPreferred();
			MoveTo(frame.left + (frame.Width() - Frame().Width()) / 2,
				frame.top + (frame.Height() - Frame().Height()) / 2);
		}
		Show();
	}
	fPole->Start();
	Unlock();
}


void
BusyWindow::Disappear()
{
	// (called from the thread of another window)
	if (!Lock())
		return;
	fPole->Stop();
	if (!IsHidden())
		Hide();
	Unlock();
}

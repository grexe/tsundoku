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

#include "NoteWindow.h"

#include <Button.h>
#include <Catalog.h>
#include <LayoutBuilder.h>
#include <ScrollView.h>
#include <TextView.h>

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "NoteWindow"

static const uint32 kOK = 'nwok';


NoteWindow::NoteWindow(BWindow* parent, const BMessenger& target, const BMessage& message, const char* text)
	:
	BWindow(BRect(0, 0, 360, 220), B_TRANSLATE("Note"), B_FLOATING_WINDOW_LOOK, B_NORMAL_WINDOW_FEEL,
		B_ASYNCHRONOUS_CONTROLS | B_AUTO_UPDATE_SIZE_LIMITS | B_CLOSE_ON_ESCAPE),
	fTarget(target),
	fMessage(message)
{
	fText = new BTextView("text");
	fText->SetWordWrap(true);
	fText->SetText(text);
	BScrollView* scroll = new BScrollView("scroll", fText, 0, false, true, B_FANCY_BORDER);
	scroll->SetExplicitMinSize(BSize(320, 140));

	BButton* ok = new BButton("ok", B_TRANSLATE("OK"), new BMessage(kOK));
	BButton* cancel = new BButton("cancel", B_TRANSLATE("Cancel"), new BMessage(B_QUIT_REQUESTED));

	BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_DEFAULT_SPACING)
		.SetInsets(B_USE_WINDOW_INSETS)
		.Add(scroll)
		.AddGroup(B_HORIZONTAL)
			.AddGlue()
			.Add(cancel)
			.Add(ok)
		.End()
	.End();

	ok->MakeDefault(true);
	if (parent != NULL)
		CenterIn(parent->Frame());
	else
		CenterOnScreen();
	fText->MakeFocus();
	fText->SelectAll();
	Show();
}


void
NoteWindow::MessageReceived(BMessage* message)
{
	if (message->what == kOK) {
		BMessage result(fMessage);
		result.AddString("text", fText->Text());
		fTarget.SendMessage(&result);
		PostMessage(B_QUIT_REQUESTED);
		return;
	}
	BWindow::MessageReceived(message);
}

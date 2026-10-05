/*
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Toji: a universal document reader for Haiku, extended for SEN.
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

#include "AttachmentsView.h"

#include <stdio.h>

#include <Alert.h>
#include <Button.h>
#include <Catalog.h>
#include <Directory.h>
#include <LayoutBuilder.h>
#include <ListView.h>
#include <Path.h>
#include <ScrollView.h>
#include <StringItem.h>
#include <Window.h>

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "AttachmentsView"

static const uint32 kSaveMsg = 'atsv';
static const uint32 kSelectionMsg = 'atsl';


AttachmentsView::AttachmentsView(Document* document)
	:
	BView("attachments", B_WILL_DRAW | B_FRAME_EVENTS),
	fDocument(document),
	fPanel(NULL),
	fSaving(-1)
{
	fList = new BListView("attachmentsList", B_SINGLE_SELECTION_LIST);
	fList->SetSelectionMessage(new BMessage(kSelectionMsg));
	fList->SetInvocationMessage(new BMessage(kSaveMsg));
	fSave = new BButton("save", B_TRANSLATE("Save" B_UTF8_ELLIPSIS), new BMessage(kSaveMsg));
	fSave->SetEnabled(false);

	BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_SMALL_SPACING)
		.SetInsets(0, 0, 0, B_USE_SMALL_SPACING)
		.Add(new BScrollView("attachmentsScroll", fList, B_FRAME_EVENTS, false, true, B_FANCY_BORDER))
		.AddGroup(B_HORIZONTAL)
			.SetInsets(B_USE_SMALL_SPACING, 0, B_USE_SMALL_SPACING, 0)
			.AddGlue()
			.Add(fSave)
		.End()
	.End();

	Fill();
}


AttachmentsView::~AttachmentsView()
{
	delete fPanel;
}


void
AttachmentsView::AttachedToWindow()
{
	BView::AttachedToWindow();
	fList->SetTarget(this);
	fSave->SetTarget(this);
}


void
AttachmentsView::SetDocument(Document* document)
{
	fDocument = document;
	Fill();
}


static BString
SizeString(int64 size)
{
	char buffer[64];
	if (size < 0)
		return BString();
	if (size < 1024)
		snprintf(buffer, sizeof(buffer), "%lld B", (long long)size);
	else if (size < 1024 * 1024)
		snprintf(buffer, sizeof(buffer), "%.1f KB", size / 1024.0);
	else
		snprintf(buffer, sizeof(buffer), "%.1f MB", size / (1024.0 * 1024.0));
	return BString(buffer);
}


void
AttachmentsView::Fill()
{
	for (int32 i = fList->CountItems() - 1; i >= 0; i--)
		delete fList->RemoveItem(i);

	fAttachments.clear();
	if (fDocument != NULL)
		fDocument->LoadAttachments(fAttachments);

	for (size_t i = 0; i < fAttachments.size(); i++) {
		BString label = fAttachments[i].name;
		BString size = SizeString(fAttachments[i].size);
		if (size.Length() > 0)
			label << "  (" << size << ")";
		fList->AddItem(new BStringItem(label.String()));
	}
	Update();
}


void
AttachmentsView::Update()
{
	fSave->SetEnabled(fList->CurrentSelection() >= 0);
}


void
AttachmentsView::SaveSelected()
{
	int32 index = fList->CurrentSelection();
	if (index < 0 || index >= (int32)fAttachments.size())
		return;

	fSaving = index;
	if (fPanel == NULL) {
		BMessenger target(this);
		fPanel = new BFilePanel(B_SAVE_PANEL, &target, NULL, B_FILE_NODE, false);
	}
	fPanel->SetSaveText(fAttachments[index].name.String());
	fPanel->Show();
}


void
AttachmentsView::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kSelectionMsg:
			Update();
			break;
		case kSaveMsg:
			SaveSelected();
			break;
		case B_SAVE_REQUESTED: {
			entry_ref directory;
			const char* name;
			if (fSaving < 0 || message->FindRef("directory", &directory) != B_OK
				|| message->FindString("name", &name) != B_OK)
				break;
			BPath path(&directory);
			path.Append(name);
			if (fDocument == NULL || !fDocument->SaveAttachment(fSaving, path.Path())) {
				BAlert* alert = new BAlert("Error", B_TRANSLATE("The file could not be saved."),
					B_TRANSLATE("OK"), NULL, NULL, B_WIDTH_AS_USUAL, B_STOP_ALERT);
				alert->Go(NULL);
			}
			fSaving = -1;
			break;
		}
		default:
			BView::MessageReceived(message);
	}
}

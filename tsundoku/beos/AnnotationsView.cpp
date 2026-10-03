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

#include "AnnotationsView.h"

#include <stdio.h>

#include <Catalog.h>
#include <LayoutBuilder.h>
#include <ListView.h>
#include <ScrollView.h>
#include <StringItem.h>
#include <StringView.h>
#include <Window.h>

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "AnnotationsView"

static const uint32 kScanDone = 'anSD';
static const uint32 kChosen = 'anCh';


AnnotationsView::AnnotationsView(Document* document, uint32 chosenMessage)
	:
	BView("annotations", B_WILL_DRAW | B_FRAME_EVENTS),
	fDocument(document),
	fChosenMessage(chosenMessage),
	fThread(-1),
	fCancel(false),
	fLock("annotations"),
	fCount(0)
{
	fList = new BListView("annotationsList", B_SINGLE_SELECTION_LIST);
	fList->SetSelectionMessage(new BMessage(kChosen));
	fStatus = new BStringView("annotationsStatus", "");
	fStatus->SetAlignment(B_ALIGN_CENTER);

	BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_SMALL_SPACING)
		.SetInsets(0, 0, 0, B_USE_SMALL_SPACING)
		.Add(new BScrollView("annotationsScroll", fList, B_FRAME_EVENTS, false, true, B_FANCY_BORDER))
		.Add(fStatus)
	.End();
}


AnnotationsView::~AnnotationsView()
{
	Stop();
}


void
AnnotationsView::AttachedToWindow()
{
	BView::AttachedToWindow();
	fMessenger = BMessenger(this);
	fList->SetTarget(this);
	Refresh();
}


void
AnnotationsView::Stop()
{
	if (fThread >= 0) {
		fCancel = true;
		status_t result;
		wait_for_thread(fThread, &result);
		fThread = -1;
	}
}


void
AnnotationsView::SetDocument(Document* document)
{
	Stop();
	fDocument = document;
	Refresh();
}


void
AnnotationsView::Refresh()
{
	Stop();
	if (fDocument == NULL || !fMessenger.IsValid())
		return;

	fCancel = false;
	fThread = spawn_thread(ScanThread, "annotations scan", B_LOW_PRIORITY, this);
	if (fThread >= 0)
		resume_thread(fThread);
}


int32
AnnotationsView::ScanThread(void* data)
{
	((AnnotationsView*)data)->Scan();
	return 0;
}


void
AnnotationsView::Scan()
{
	std::vector<DocAnnotationEntry> entries;
	if (fDocument->ListAnnotations(entries, &fCancel) && !fCancel) {
		fLock.Lock();
		fPending.swap(entries);
		fLock.Unlock();
		fMessenger.SendMessage(kScanDone);
	}
}


void
AnnotationsView::Fill()
{
	fLock.Lock();
	fEntries.swap(fPending);
	fLock.Unlock();

	// what was chosen stays chosen
	for (int32 i = fList->CountItems() - 1; i >= 0; i--)
		delete fList->RemoveItem(i);

	for (size_t i = 0; i < fEntries.size(); i++) {
		const DocAnnotationEntry& entry = fEntries[i];
		BString label;
		label << entry.page << "   " << entry.annotation.label;
		if (entry.excerpt.Length() > 0)
			label << ":  " << entry.excerpt;
		fList->AddItem(new BStringItem(label.String()));
	}

	fCount = (int)fEntries.size();
	BString status;
	if (fCount == 0)
		status = B_TRANSLATE("No annotations");
	else if (fCount == 1)
		status = B_TRANSLATE("1 annotation");
	else
		status << fCount << " " << B_TRANSLATE("annotations");
	fStatus->SetText(status.String());
}


void
AnnotationsView::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kScanDone:
			Fill();
			break;
		case kChosen: {
			int32 index = fList->CurrentSelection();
			if (index >= 0 && index < (int32)fEntries.size() && Window() != NULL) {
				BMessage chosen(fChosenMessage);
				chosen.AddInt32("page", fEntries[index].page);
				chosen.AddInt32("index", fEntries[index].annotation.index);
				Window()->PostMessage(&chosen);
			}
			break;
		}
		default:
			BView::MessageReceived(message);
	}
}

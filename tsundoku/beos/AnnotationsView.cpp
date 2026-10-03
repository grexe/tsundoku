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
#include <ColumnListView.h>
#include <ColumnTypes.h>
#include <LayoutBuilder.h>
#include <StringView.h>
#include <Window.h>

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "AnnotationsView"

static const uint32 kScanDone = 'anSD';
static const uint32 kChosen = 'anCh';


// a line of the list that knows which annotation it stands for (the order of the lines changes with the sorting)
class AnnotationRow : public BRow {
public:
	AnnotationRow(int page, int index)
		:
		BRow(),
		fPage(page),
		fIndex(index)
	{
	}

	int Page() const { return fPage; }
	int Index() const { return fIndex; }

private:
	int fPage, fIndex;
};


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
	// the standard list with columns that can be sorted and resized, but not moved or taken away
	fList = new BColumnListView("annotationsList", B_NAVIGABLE, B_FANCY_BORDER, true);
	fList->SetColumnFlags(B_ALLOW_COLUMN_RESIZE);
	fList->SetSortingEnabled(true);
	fList->SetSelectionMode(B_SINGLE_SELECTION_LIST);
	fList->SetSelectionMessage(new BMessage(kChosen));

	fPageColumn = new BIntegerColumn(B_TRANSLATE("Page"), 76, 64, 140, B_ALIGN_RIGHT);
	fTypeColumn = new BStringColumn(B_TRANSLATE("Type"), 90, 40, 300, B_TRUNCATE_END);
	fExcerptColumn = new BStringColumn(B_TRANSLATE("Excerpt"), 320, 60, 4000, B_TRUNCATE_MIDDLE);
	fList->AddColumn(fPageColumn, 0);
	fList->AddColumn(fTypeColumn, 1);
	fList->AddColumn(fExcerptColumn, 2);
	fList->SetSortColumn(fPageColumn, false, true);

	fStatus = new BStringView("annotationsStatus", "");
	fStatus->SetAlignment(B_ALIGN_CENTER);

	BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_SMALL_SPACING)
		.SetInsets(0, 0, 0, B_USE_SMALL_SPACING)
		.Add(fList)
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
	std::vector<DocAnnotationEntry> entries;
	fLock.Lock();
	entries.swap(fPending);
	fLock.Unlock();

	fList->Clear();
	for (size_t i = 0; i < entries.size(); i++) {
		const DocAnnotationEntry& entry = entries[i];
		AnnotationRow* row = new AnnotationRow(entry.page, entry.annotation.index);
		row->SetField(new BIntegerField(entry.page), 0);
		row->SetField(new BStringField(entry.annotation.label.String()), 1);
		row->SetField(new BStringField(entry.excerpt.String()), 2);
		fList->AddRow(row);
	}
	fCount = (int)entries.size();

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
			AnnotationRow* row = dynamic_cast<AnnotationRow*>(fList->CurrentSelection());
			if (row != NULL && Window() != NULL) {
				BMessage chosen(fChosenMessage);
				chosen.AddInt32("page", row->Page());
				chosen.AddInt32("index", row->Index());
				Window()->PostMessage(&chosen);
			}
			break;
		}
		default:
			BView::MessageReceived(message);
	}
}


#ifdef TSUNDOKU_TESTING
void
AnnotationsView::TestChoose(int index)
{
	fList->DeselectAll();
	fList->SetFocusRow(index, true);
}
#endif

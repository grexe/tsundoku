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
#include <Font.h>
#include <ListItem.h>
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


// the widths of the first two columns, shared by the header and the items; the last column gets what is left
struct AnnotationColumns {
	float page;
	float type;
};

static const float kCellPadding = 6;


// a line of the list: page, type and excerpt, the excerpt is shortened in the middle to what fits
class AnnotationItem : public BListItem {
public:
	AnnotationItem(int page, const BString& type, const BString& excerpt, const AnnotationColumns* columns)
		:
		BListItem(),
		fPage(),
		fType(type),
		fExcerpt(excerpt),
		fColumns(columns)
	{
		fPage << page;
	}

	virtual void Update(BView* owner, const BFont* font)
	{
		font_height height;
		font->GetHeight(&height);
		SetHeight(ceilf(height.ascent + height.descent + height.leading) + 4);
	}

	virtual void DrawItem(BView* owner, BRect frame, bool complete = false)
	{
		rgb_color background = IsSelected() ? ui_color(B_LIST_SELECTED_BACKGROUND_COLOR)
			: ui_color(B_LIST_BACKGROUND_COLOR);
		rgb_color text = IsSelected() ? ui_color(B_LIST_SELECTED_ITEM_TEXT_COLOR)
			: ui_color(B_LIST_ITEM_TEXT_COLOR);
		owner->SetHighColor(background);
		owner->FillRect(frame);
		owner->SetHighColor(text);
		owner->SetLowColor(background);

		BFont font;
		owner->GetFont(&font);
		font_height height;
		font.GetHeight(&height);
		float baseline = frame.top + (frame.Height() - (height.ascent + height.descent)) / 2 + height.ascent;

		float x = frame.left + kCellPadding;
		owner->DrawString(fPage.String(), BPoint(x, baseline));
		x = frame.left + fColumns->page;
		BString type(fType);
		font.TruncateString(&type, B_TRUNCATE_END, fColumns->type - kCellPadding);
		owner->DrawString(type.String(), BPoint(x, baseline));
		x = frame.left + fColumns->page + fColumns->type;
		BString excerpt(fExcerpt);
		font.TruncateString(&excerpt, B_TRUNCATE_MIDDLE, frame.right - x - kCellPadding);
		owner->DrawString(excerpt.String(), BPoint(x, baseline));
	}

private:
	BString fPage, fType, fExcerpt;
	const AnnotationColumns* fColumns;
};


// the titles of the columns
class AnnotationHeader : public BView {
public:
	AnnotationHeader(const AnnotationColumns* columns)
		:
		BView("annotationsHeader", B_WILL_DRAW | B_FRAME_EVENTS),
		fColumns(columns)
	{
		SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
	}

	virtual void GetPreferredSize(float* width, float* height)
	{
		font_height fontHeight;
		GetFontHeight(&fontHeight);
		*width = 100;
		*height = ceilf(fontHeight.ascent + fontHeight.descent) + 6;
	}

	virtual BSize MinSize()
	{
		float width, height;
		GetPreferredSize(&width, &height);
		return BSize(0, height);
	}

	virtual BSize MaxSize()
	{
		float width, height;
		GetPreferredSize(&width, &height);
		return BSize(B_SIZE_UNLIMITED, height);
	}

	virtual void Draw(BRect updateRect)
	{
		BRect bounds(Bounds());
		SetHighColor(ui_color(B_PANEL_TEXT_COLOR));
		font_height fontHeight;
		GetFontHeight(&fontHeight);
		float baseline = (bounds.Height() - (fontHeight.ascent + fontHeight.descent)) / 2 + fontHeight.ascent;
		// the list has a border of 2 pixels
		float left = 2;
		DrawString(B_TRANSLATE("Page"), BPoint(left + kCellPadding, baseline));
		DrawString(B_TRANSLATE("Type"), BPoint(left + fColumns->page, baseline));
		DrawString(B_TRANSLATE("Excerpt"), BPoint(left + fColumns->page + fColumns->type, baseline));
		SetHighColor(tint_color(ui_color(B_PANEL_BACKGROUND_COLOR), B_DARKEN_2_TINT));
		StrokeLine(BPoint(bounds.left, bounds.bottom), BPoint(bounds.right, bounds.bottom));
	}

private:
	const AnnotationColumns* fColumns;
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
	fColumns = new AnnotationColumns();
	fColumns->page = 40;
	fColumns->type = 90;
	fList = new BListView("annotationsList", B_SINGLE_SELECTION_LIST);
	fList->SetSelectionMessage(new BMessage(kChosen));
	fStatus = new BStringView("annotationsStatus", "");
	fStatus->SetAlignment(B_ALIGN_CENTER);

	BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_SMALL_SPACING)
		.SetInsets(0, 0, 0, B_USE_SMALL_SPACING)
		.Add(new AnnotationHeader(fColumns))
		.Add(new BScrollView("annotationsScroll", fList, B_FRAME_EVENTS, false, true, B_FANCY_BORDER))
		.Add(fStatus)
	.End();
}


AnnotationsView::~AnnotationsView()
{
	Stop();
	// the items point to the columns
	for (int32 i = fList->CountItems() - 1; i >= 0; i--)
		delete fList->RemoveItem(i);
	delete fColumns;
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

	// the first columns are as wide as their widest cell
	BFont font;
	fList->GetFont(&font);
	float widestPage = font.StringWidth(B_TRANSLATE("Page")), widestType = font.StringWidth(B_TRANSLATE("Type"));
	for (size_t i = 0; i < fEntries.size(); i++) {
		BString page;
		page << fEntries[i].page;
		widestPage = max_c(widestPage, font.StringWidth(page.String()));
		widestType = max_c(widestType, font.StringWidth(fEntries[i].annotation.label.String()));
	}
	fColumns->page = ceilf(widestPage) + 2 * kCellPadding;
	fColumns->type = ceilf(widestType) + 2 * kCellPadding;

	for (size_t i = 0; i < fEntries.size(); i++) {
		const DocAnnotationEntry& entry = fEntries[i];
		fList->AddItem(new AnnotationItem(entry.page, entry.annotation.label, entry.excerpt, fColumns));
	}
	if (Window() != NULL && Window()->LockLooper()) {
		// the header uses the widths too
		Invalidate();
		Window()->UnlockLooper();
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

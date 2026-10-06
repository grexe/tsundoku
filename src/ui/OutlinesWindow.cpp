/*
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * BePDF: The PDF reader for Haiku.
 * 	 Copyright (C) 1997 Benoit Triquet.
 * 	 Copyright (C) 1998-2000 Hubert Figuiere.
 * 	 Copyright (C) 2000-2011 Michael Pfeiffer.
 * 	 Copyright (C) 2013 waddlesplash.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <stdio.h>
// BeOS
#include <locale/Catalog.h>
#include <Button.h>
#include <InterfaceDefs.h>
#include <LayoutBuilder.h>
#include <ListItem.h>
#include <OutlineListView.h>
#include <ScrollView.h>
#include <TextControl.h>
#include <Window.h>
// BePDF
#include "Globals.h"
#include "LayoutUtils.h"
#include "OutlinesWindow.h"

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "OutlinesWindow"

// Implementation of OutlineStyle

OutlineStyle::OutlineStyle(const BFont* font, rgb_color color)
: mFont(font)
, mColor(color)
{
}

// Implementation of OutlineStyle

OutlineStyleList::OutlineStyleList() {
	mFonts[PLAIN_STYLE] = *be_plain_font;
	mFonts[BOLD_STYLE] = *be_plain_font;
	mFonts[BOLD_STYLE].SetFace(B_BOLD_FACE);
	mFonts[ITALIC_STYLE] = *be_plain_font;
	mFonts[ITALIC_STYLE].SetFace(B_ITALIC_FACE);
	mFonts[BOLD_ITALIC_STYLE] = *be_plain_font;
	mFonts[BOLD_ITALIC_STYLE].SetFace(B_BOLD_FACE | B_ITALIC_FACE);
}

OutlineStyleList::~OutlineStyleList() {
	const int32 n = mList.CountItems();
	for (int32 i = 0; i < n; i ++) {
		OutlineStyle* style = (OutlineStyle*)mList.ItemAt(i);
		delete style;
	}
	mList.MakeEmpty();
}

const BFont* OutlineStyleList::GetFont(int style) const {
	return &mFonts[style];
}

OutlineStyle* OutlineStyleList::GetStyle(int style, rgb_color color) {
	const int32 n = mList.CountItems();
	for (int32 i = 0; i < n; i ++) {
		OutlineStyle* os = (OutlineStyle*)mList.ItemAt(i);
		if (os->GetFont() == GetFont(style) && memcmp(os->GetColor(), &color, sizeof(color)) == 0) {
			return os;
		}
	}
	OutlineStyle* os = new OutlineStyle(GetFont(style), color);
	mList.AddItem(os);
	return os;
}

OutlineStyle* OutlineStyleList::GetDefaultStyle() {
	rgb_color black={0, 0, 0, 0};
	return GetStyle(PLAIN_STYLE, black);
}

// Implementation of OutlineListItem

OutlineListItem::OutlineListItem(const char *string, uint32 level, bool expanded, OutlineStyle* style) :
	BListItem(level, expanded),
	mString(string),
	mType(linkUndefined),
	mPageNum(0),
	mStyle(style),
	mResolvedPage(0),
	mAnchor(NULL)
{
	mDest.page = 0;
	mDest.x = mDest.y = 0;
	mDest.hasPosition = false;
}

OutlineListItem::~OutlineListItem() {
	delete mAnchor;
}


void OutlineListItem::SetAnchor(const BMessage* anchor) {
	delete mAnchor;
	mAnchor = anchor != NULL ? new BMessage(*anchor) : NULL;
}


void OutlineListItem::DrawItem(BView* owner, BRect frame, bool complete)
{
	rgb_color color;

	owner->PushState();
	// select background color
	if (IsSelected()) {
		color = ui_color(B_LIST_SELECTED_BACKGROUND_COLOR);
	} else {
		color = ui_color(B_LIST_BACKGROUND_COLOR);
	}
	// fill background
	owner->SetHighColor(color);
	owner->FillRect(frame);
	// set font color
	if (IsEnabled()) {
		owner->SetHighColor(*mStyle->GetColor());
	} else {
		owner->SetHighColor(*mStyle->GetColor());
	}
	// set background color
	owner->SetLowColor(color);
	// display text
	owner->MovePenTo(frame.left+4, frame.bottom-2);
	owner->SetFont(mStyle->GetFont());
	owner->DrawString(mString.String());

	owner->PopState();
}

void OutlineListItem::SetDest(int page, float x, float y, bool hasPosition) {
	if (mType == linkUndefined) {
		mType = linkDest;
		mDest.page = page;
		mDest.x = x;
		mDest.y = y;
		mDest.hasPosition = hasPosition;
		mResolvedPage = page;
	}
}

void OutlineListItem::SetLink(const char* link) {
	if (mType == linkUndefined) {
		mType = linkString;
		mLink = link;
	}
}

void OutlineListItem::SetPageNum(int pageNum) {
	if (mType == linkUndefined) {
		mType = linkPageNum;
		mPageNum = pageNum;
	}
}

// Implementation of OutlinesView
void OutlinesView::ReadOutlines(const std::vector<DocOutlineEntry>& entries) {
	std::vector<OutlineListItem*> items;
	for (size_t i = 0; i < entries.size(); i++) {
		const DocOutlineEntry& entry = entries[i];

		// end string at first newline character
		BString title(entry.title);
		int32 newline = title.FindFirst('\n');
		if (newline >= 0)
			title.Truncate(newline);

		rgb_color color = {0, 0, 0, 0};
		if (entry.hasColor) {
			color.red = entry.red;
			color.green = entry.green;
			color.blue = entry.blue;
		}
		int style = OutlineStyleList::PLAIN_STYLE;
		if (entry.bold)
			style |= OutlineStyleList::BOLD_STYLE;
		if (entry.italic)
			style |= OutlineStyleList::ITALIC_STYLE;

		OutlineListItem *item = new OutlineListItem(
			title.Length() > 0 ? title.String() : B_TRANSLATE("No title"),
			(uint32)entry.level + 1, entry.open, mOutlineStyleList.GetStyle(style, color));
		if (entry.page > 0)
			item->SetDest(entry.page, entry.x, entry.y, entry.hasPosition);
		else if (entry.uri.Length() > 0)
			item->SetLink(entry.uri.String());
		mList->AddItem(item);
		items.push_back(item);
	}

	// expanded argument of OutlineListItem constructor does not work! Children first.
	for (size_t i = entries.size(); i > 0; i--) {
		OutlineListItem *item = items[i - 1];
		if (mList->CountItemsUnder(item, true) == 0)
			continue;
		if (entries[i - 1].open)
			mList->Expand(item);
		else
			mList->Collapse(item);
	}
}

OutlinesView::OutlinesView(Document *document, BMessage *bookmarks,
	GlobalSettings *settings, BLooper *looper, uint32 flags)
	:
	BScrollView("BookmarksScroll", NULL, 0, true, true),
	mLooper(looper),
	mList(NULL),
	mDocument(NULL),
	mBookmarks(NULL),
	mNeedsUpdate(true),
	mUserDefined(NULL),
	mEmptyUserBM(NULL),
	mHasDocumentOutline(false),
	mCurrentPage(0)
{
	SetTarget(mList = new BOutlineListView("", B_SINGLE_SELECTION_LIST));
	mEmptyUserBM = new OutlineListItem(B_TRANSLATE("<empty>"), 1, true,
		GetDefaultStyle());
	SetDocument(document, bookmarks);
}

OutlinesView::~OutlinesView() {
	if (mLooper) {
		BMessage msg(QUIT_NOTIFY);
		mLooper->PostMessage(&msg);
	}
	if (mList) {
		mList->RemoveItem(mEmptyUserBM);
		delete mEmptyUserBM;
		MakeEmpty(mList);
	}
}

void OutlinesView::AttachedToWindow() {
	mList->SetSelectionMessage(new BMessage('Outl'));
	mList->SetTarget(this);
}

void OutlinesView::SetDocument(Document *document, BMessage *bookmarks) {
	if (mDocument != document) {
		mDocument    = document;
		mBookmarks   = bookmarks;
		mNeedsUpdate = true;
		mHasDocumentOutline = false;
		mCurrentPage = 0;
		InitUserBookmarks(true);
	}
}

void OutlinesView::Reload(BMessage *bookmarks) {
	mBookmarks   = bookmarks;
	mNeedsUpdate = true;
	mHasDocumentOutline = false;
	InitUserBookmarks(true);
}

void OutlinesView::Activate() {
	if (mNeedsUpdate) {
		mNeedsUpdate = false;
		mList->RemoveItem(mEmptyUserBM); // keep mEmptyUserBM
		MakeEmpty(mList);
		mList->AddItem(new OutlineListItem(B_TRANSLATE("Document"), 0, true, GetDefaultStyle()));
		std::vector<DocOutlineEntry> entries;
		if (mDocument != NULL && mDocument->LoadOutline(entries))
			ReadOutlines(entries);
		mHasDocumentOutline = mList->CountItems() > 1;
		if (!mHasDocumentOutline) {
			mList->AddItem(new OutlineListItem(B_TRANSLATE("<empty>"), 1, true, GetDefaultStyle()));
		}
		mUserDefined = new OutlineListItem(B_TRANSLATE("User defined"), 0, true, GetDefaultStyle());
		mList->AddItem(mUserDefined);
		InitUserBookmarks(false);
		SelectPage(mCurrentPage);
	}
}

bool OutlinesView::HasUserBookmarks() {
	for (int32 i = 0; i < mList->CountItemsUnder(mUserDefined, true); i++) {
		OutlineListItem *item = (OutlineListItem*)mList->ItemUnderAt(mUserDefined, true, i);
		if (item != NULL && item->isPageNum()) {
			return true;
		}
	}
	return false;
}

bool OutlinesView::HasEntries() {
	Activate();
	return mHasDocumentOutline || HasUserBookmarks();
}

void OutlinesView::SelectPage(int pageNum) {
	mCurrentPage = pageNum;
	if (mNeedsUpdate || !mHasDocumentOutline || pageNum <= 0) {
		return;
	}

	// the entry of the user's choice stays if it points to this very page, e.g. after clicking it
	int32 selected = mList->CurrentSelection(0);
	if (selected >= 0) {
		OutlineListItem *item = (OutlineListItem*)mList->ItemAt(selected);
		if (item != NULL && item->GetResolvedPage() == pageNum) {
			return;
		}
	}

	// otherwise the page belongs to the entry that starts closest before it (the last one
	// if several start on the same page). Outlines are not necessarily ordered by page.
	OutlineListItem *chapter = NULL;
	int bestPage = 0;
	int32 end = mList->FullListIndexOf(mUserDefined);
	for (int32 i = 1; i < end; i++) {
		OutlineListItem *item = (OutlineListItem*)mList->FullListItemAt(i);
		int itemPage = item->GetResolvedPage();
		if (itemPage > 0 && itemPage <= pageNum && itemPage >= bestPage) {
			chapter = item;
			bestPage = itemPage;
		}
	}
	if (chapter == NULL) {
		// before the first entry: none is the place of the page (and the one of another page does not stay selected)
		if (mList->CurrentSelection(0) >= 0) {
			mList->SetSelectionMessage(NULL);
			mList->DeselectAll();
			mList->SetSelectionMessage(new BMessage('Outl'));
		}
		return;
	}
	if (chapter->IsSelected()) {
		return;
	}

	// make sure the entry is visible
	for (BListItem *parent = mList->Superitem(chapter); parent != NULL;
			parent = mList->Superitem(parent)) {
		if (!parent->IsExpanded()) {
			mList->Expand(parent);
		}
	}

	// selecting would send the selection message and navigate to the entry's destination
	mList->SetSelectionMessage(NULL);
	mList->Select(mList->IndexOf(chapter));
	mList->SetSelectionMessage(new BMessage('Outl'));
	mList->ScrollToSelection();
}


// handling of user bookmarks
void OutlinesView::InitUserBookmarks(bool initOnly) {
	mBookmark.Clear();
	if (mBookmarks == NULL || mBookmarks->IsEmpty()) {
		if (!initOnly) {
			mList->AddItem(mEmptyUserBM);
		}
	} else {
		BString label;
		int32   pageNum, i = 0;
		while (B_OK == mBookmarks->FindString("l", i, &label) &&
		       B_OK == mBookmarks->FindInt32 ("p", i, &pageNum)) {
			// a bookmark in a book knows its place in the text: the page it is on now
			BMessage anchorMessage;
			TextAnchor anchor;
			bool anchored = mDocument != NULL && mDocument->IsReflowable()
				&& mBookmarks->FindMessage("a", i, &anchorMessage) == B_OK && anchor.Unarchive(&anchorMessage);
			if (anchored) {
				int page = mDocument->PageOfAnchor(anchor);
				if (page > 0)
					pageNum = page;
			}
	    	mBookmark.Set(pageNum, true);
		    if (!initOnly) {
		    	OutlineListItem *item = new OutlineListItem(label.String(), 1, true, GetDefaultStyle());
		    	item->SetPageNum(pageNum);
		    	if (anchored)
		    		item->SetAnchor(&anchorMessage);
				mList->AddItem(item);
			}
			i ++;
		}
	}
}

static BListItem* store_bookmarks(BListItem *i, void *d) {
	OutlineListItem *item = (OutlineListItem*)i;
	BMessage    *bm   = (BMessage*)d;
	if (item->isPageNum()) {
		bm->AddString("l", item->Text());
		bm->AddInt32 ("p", item->getPageNum());
		// (the array stays as long as the others; BePDF does not read it)
		BMessage none;
		bm->AddMessage("a", item->Anchor() != NULL ? item->Anchor() : &none);
	}
	return NULL;
}

bool OutlinesView::GetBookmarks(BMessage *bm) {
	if (mList == NULL) {
		return false;
	}
	mList->EachItemUnder(mUserDefined, true, store_bookmarks, bm);
	return true;
}

static BListItem *find_bookmark(BListItem *i, void *d) {
	OutlineListItem *item = (OutlineListItem*)i;
	int pageNum       = *(int*)d;
	if (item->isPageNum() && item->getPageNum() == pageNum) return i;
	return NULL;
}

OutlineListItem *OutlinesView::FindUserBookmark(int pageNum) {
	if (mList == NULL) {
		return NULL;
	}
	return (OutlineListItem*)mList->EachItemUnder(mUserDefined, true, find_bookmark, &pageNum);
}

bool OutlinesView::HasUserBookmark(int pageNum) {
	return mBookmark.IsSet(pageNum);
}

bool OutlinesView::IsUserBMSelected() {
	if (mNeedsUpdate) return false;
	int i = mList->CurrentSelection(0);
	if (i >= 0) {
		OutlineListItem *item = (OutlineListItem*)mList->ItemAt(i);
		return item->isPageNum();
	}
	return false;
}

void OutlinesView::AddUserBookmark(int pageNum, const char *label, const BMessage* anchor) {
	RemoveUserBookmark(pageNum);
	if (mList->CountItemsUnder(mUserDefined, true) == 1) {
		mList->RemoveItem(mEmptyUserBM);
	}
	OutlineListItem* item;
	int32 i = 0;
	int32 index = mList->FullListIndexOf(mUserDefined)+1;
	item = (OutlineListItem*)mList->ItemUnderAt(mUserDefined, true, i);
	while(item != NULL) {
		if (item->isPageNum() && item->getPageNum() > pageNum) {
			// insert new OutlineListItem before item
			break;
		}
		i ++;
		item = (OutlineListItem*)mList->ItemUnderAt(mUserDefined, true, i);
	}
	index += i;
	OutlineListItem *n = new OutlineListItem(label, 1, true, GetDefaultStyle());
	n->SetPageNum(pageNum);
	if (anchor != NULL && !anchor->IsEmpty())
		n->SetAnchor(anchor);
	mList->AddItem(n, index);
	mBookmark.Set(pageNum, true);
}

void OutlinesView::RemoveUserBookmark(int pageNum) {
	Activate();
	OutlineListItem* item;
	int32 i = 0;
	item = (OutlineListItem*)mList->ItemUnderAt(mUserDefined, true, i);
	while(item != NULL) {
		if (item->isPageNum() && item->getPageNum() == pageNum) {
			// remove item
			mList->RemoveItem(item);
			delete item;
			mBookmark.Set(pageNum, false);
			break;
		}
		i ++;
		item = (OutlineListItem*)mList->ItemUnderAt(mUserDefined, true, i);
	}
	if (mList->CountItemsUnder(mUserDefined, true) == 0) {
		mList->AddItem(mEmptyUserBM);
	}
}

const char *OutlinesView::GetUserBMLabel(int pageNum) {
	if (mNeedsUpdate) return NULL;
	OutlineListItem *item = FindUserBookmark(pageNum);
	if (item) return item->Text();
	return NULL;
}

void OutlinesView::MessageReceived(BMessage *msg) {
	if (msg->what == 'Outl') {
		// get first selected item
		int32 selected = mList->CurrentSelection(0);
		if (selected >= 0) {
			bool msgSent = false;
			OutlineListItem *item = (OutlineListItem*)mList->ItemAt(selected);
			if (item) {
				if (item->isDest() || item->isPageNum()) {
					BMessage msg(PAGE_NOTIFY);
					msg.AddInt32("page", item->isDest() ? item->getDestPage() : item->getPageNum());
					if (item->isDest() && item->hasDestPosition()) {
						msg.AddFloat("x", item->getDestX());
						msg.AddFloat("y", item->getDestY());
					}
					mLooper->PostMessage(&msg);
					msgSent = true;
				}
				if (!msgSent) {
					// notify window that state has changed
					BMessage msg(STATE_CHANGE_NOTIFY);
					mLooper->PostMessage(&msg);
				}
			}
		}
	} else {
		BView::MessageReceived(msg);
	}
}


// BookmarkWindow

BookmarkWindow::BookmarkWindow(int pageNum, const char* title, BRect aRect, BLooper *looper)
	: BWindow(aRect, B_TRANSLATE("Edit title for bookmark"),
		B_TITLED_WINDOW_LOOK,
		B_MODAL_APP_WINDOW_FEEL,
		B_NOT_ZOOMABLE | B_AUTO_UPDATE_SIZE_LIMITS) {
	mLooper  = looper;
	mPageNum = pageNum;

	AddCommonFilter(new EscapeMessageFilter(this, B_QUIT_REQUESTED));

	// center window

	aRect.OffsetBy(aRect.Width() / 2, aRect.Height() / 2);
	float width = 480, height = 45;
	aRect.SetRightBottom(BPoint(aRect.left + width, aRect.top + height));
	aRect.OffsetBy(-aRect.Width() / 2, -aRect.Height() / 2);
	ResizeTo(width, height);
	if (BWindow* parent = dynamic_cast<BWindow*>(looper))
		CenterIn(parent->Frame());
	else
		CenterOnScreen();

	mTitle = new BTextControl("mTitle", "", title, NULL);
	mTitle->SetExplicitMinSize(BSize(340, B_SIZE_UNSET));	// room for "7.3 Comic books (p11)"

	BButton *button = new BButton("button", B_TRANSLATE("OK"), new BMessage('OK'));

	BLayoutBuilder::Group<>(this, B_HORIZONTAL)
		.SetInsets(B_USE_WINDOW_INSETS)
		.Add(mTitle)
		.Add(button);

	SetDefaultButton(button);

	mTitle->MakeFocus();
	Show();
}


bool BookmarkWindow::QuitRequested() {
	return true;
}

void BookmarkWindow::MessageReceived(BMessage *msg) {
	switch (msg->what) {
	case 'OK': {
		// post message to application

		BMessage msg(BOOKMARK_ENTERED_NOTIFY);
		msg.AddString("label", mTitle->Text());
		msg.AddInt32("pageNum", mPageNum);
		mLooper->PostMessage(&msg, NULL);
		Quit();
		break; }
	default:
		BWindow::MessageReceived(msg);
	}
}


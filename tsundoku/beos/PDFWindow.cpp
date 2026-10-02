/*
 * BePDF: The PDF reader for Haiku.
 * 	 Copyright (C) 1997 Benoit Triquet.
 * 	 Copyright (C) 1998-2000 Hubert Figuiere.
 * 	 Copyright (C) 2000-2011 Michael Pfeiffer.
 * 	 Copyright (C) 2013-2016 waddlesplash.
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

#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>

// BeOS
#include <locale/Catalog.h>
#include <be/app/Roster.h>
#include <be/app/MessageQueue.h>
#include <be/interface/Alert.h>
#include <be/interface/Bitmap.h>
#include <be/interface/Button.h>
#include <be/interface/MenuBar.h>
#include <be/interface/MenuItem.h>
#include <be/interface/MenuField.h>
#include <be/interface/Screen.h>
#include <be/interface/ScrollView.h>
#include <be/interface/ScrollBar.h>
#include <be/interface/StringView.h>
#include <be/interface/TabView.h>
#include <be/interface/View.h>
#include <be/storage/Path.h>
#include <be/storage/Directory.h>
#include <be/storage/Entry.h>
#include <be/storage/NodeMonitor.h>
#include <be/support/Beep.h>
#include <be/support/Debug.h>
#include <LayoutBuilder.h>

// BePDF
#include "Globals.h"
#include "Application.h"
#include "EntryMenuItem.h"
#include "FileInfoWindow.h"
#include "FindTextWindow.h"
#include "LayoutUtils.h"
#include "OutlinesWindow.h"
#include "PageRenderer.h"
#include "PasswordWindow.h"
#include "PDFView.h"
#include "PDFWindow.h"
#include "PreferencesWindow.h"
#include "PrintSettingsWindow.h"
#include "ResourceLoader.h"
#include "StatusBar.h"
#include "TraceWindow.h"

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "PDFWindow"


char * PDFWindow::PAGE_MSG_LABEL = "page";

// Implementation of RecentDocumentsMenu

RecentDocumentsMenu::RecentDocumentsMenu(const char *title, uint32 what, menu_layout layout)
	: BMenu(title, layout)
	, fWhat(what)
{
}

bool
RecentDocumentsMenu::AddDynamicItem(add_state s)
{
	if (s != B_INITIAL_ADD)
		return false;

	BMenuItem *item;
	BMessage list, *msg;
	entry_ref ref;
	char name[B_FILE_NAME_LENGTH];

	while ((item = RemoveItem((int32)0)) != NULL) {
		delete item;
	}

	be_roster->GetRecentDocuments(&list, 20, NULL, BEPDF_APP_SIG);
	for (int i = 0; list.FindRef("refs", i, &ref) == B_OK; i++) {
		BEntry entry(&ref);
		if (entry.Exists() && entry.GetName(name) == B_OK) {
			msg = new BMessage(fWhat);
			msg->AddRef("refs", &ref);
			item =  new EntryMenuItem(&ref, name, msg, 0, 0);
			AddItem(item);
			if (fWhat == B_REFS_RECEIVED) {
				item->SetTarget(be_app, NULL);
			}
		}
	}

	return false;
}




///////////////////////////////////////////////////////////
/*
	Check errors that may happen
*/
PDFWindow::PDFWindow(entry_ref* ref, BRect frame, const char *ownerPassword,
	const char *userPassword, bool *encrypted)
	: BWindow(frame, "PDF", B_DOCUMENT_WINDOW, 0)
{
	mMainView = NULL;
	mPagesView = NULL;
	mPageNumberItem = NULL;
	mPrintSettings = NULL;
	mTotalPageNumberItem = NULL;
	mFindWindow = NULL;
	mPreferencesItem = NULL;
	mFileInfoItem =  NULL;
	mFindInProgress = false;

	mZoomMenu = mRotationMenu = NULL;
	mLayerView = NULL;

	mOWMessenger = NULL;
	mFIWMessenger = NULL;
	mPSWMessenger = NULL;

	mPrintSettingsWindowOpen = false;

	mShowLeftPanel = true;
	mOutlineAutoCollapsed = false;
	mFullScreen = false;

	mPendingMask = 0;


	AddHandler(&mEntryChangedMonitor);
	mEntryChangedMonitor.SetEntryChangedListener(this);

	SetUpViews(ref, ownerPassword, userPassword, encrypted);

	GlobalSettings *settings = gApp->GetSettings();
	int32 ws = settings->GetWorkspace();
	if (settings->GetOpenInWorkspace() && ws >= 1 && ws <= count_workspaces()) {
		SetWorkspaces(1 << (ws - 1));
	}
	mCurrentWorkspace = Workspaces();

	if (mMainView != NULL) {
		mMainView->Redraw();
		InitAfterOpen();
	}

	SetSizeLimits(938, 10000, 131, 10000);
	FitToScreen();
}


///////////////////////////////////////////////////////////
// Makes sure that the window, with its border and title tab, is not larger than the screen and lies on it, for
// the default frame as well as for a frame stored for another screen.
void PDFWindow::FitToScreen()
{
	BScreen screen(this);
	if (!screen.IsValid())
		return;

	const float margin = 10;
	BRect screenFrame = screen.Frame();
	BRect frame = Frame();
	BRect decorator = DecoratorFrame();
	float borderLeft = frame.left - decorator.left, borderTop = frame.top - decorator.top;
	float borderRight = decorator.right - frame.right, borderBottom = decorator.bottom - frame.bottom;

	float maxWidth = screenFrame.Width() - 2 * margin - borderLeft - borderRight;
	float maxHeight = screenFrame.Height() - 2 * margin - borderTop - borderBottom;

	// the minimum size must not stop the window from fitting
	float minWidth, maxWidthLimit, minHeight, maxHeightLimit;
	GetSizeLimits(&minWidth, &maxWidthLimit, &minHeight, &maxHeightLimit);
	SetSizeLimits(min_c(minWidth, maxWidth), maxWidthLimit, min_c(minHeight, maxHeight), maxHeightLimit);

	float width = min_c(frame.Width(), maxWidth);
	float height = min_c(frame.Height(), maxHeight);
	if (width != frame.Width() || height != frame.Height())
		ResizeTo(width, height);

	float left = frame.left, top = frame.top;
	left = max_c(left, screenFrame.left + margin + borderLeft);
	left = min_c(left, screenFrame.right - margin - borderRight - width);
	top = max_c(top, screenFrame.top + margin + borderTop);
	top = min_c(top, screenFrame.bottom - margin - borderBottom - height);
	if (left != frame.left || top != frame.top)
		MoveTo(left, top);
}



///////////////////////////////////////////////////////////
PDFWindow::~PDFWindow()
{
	RemoveHandler(&mEntryChangedMonitor);

	if (mPagesView) {
		MakeEmpty(mPagesView);
	}
}

void PDFWindow::SetTotalPageNumber(int pages) {
	const char *fmt = B_TRANSLATE("of %d");
	int len = strlen(fmt) + 30;
	char *label = new char[len];
	snprintf (label, len, fmt, pages);
	mTotalPageNumberItem->SetText(label);
	delete label;
}

void PDFWindow::InitAfterOpen() {
	GlobalSettings *s = gApp->GetSettings();
	if (Lock()) {
		// set page number text
		SetTotalPageNumber(mMainView->GetNumPages());

		// set window frame
		if (s->GetRestoreWindowFrame()) {
			float left, top;
			mFileAttributes.GetLeftTop(left, top);
			mMainView->ScrollTo(left, top);
		}

		// set page number list
		if (mMainView->GetDocument()->Lock()->LockWithTimeout(0) == B_OK) {
			UpdatePageList();
			mMainView->GetDocument()->Lock()->Unlock();
		} else {
			FillPageList();
			SetPending(UPDATE_PAGE_LIST_PENDING);
		}
		// select page number
		mPagesView->Select(mFileAttributes.GetPage()-1);
		mPagesView->ScrollToSelection();
		if (s->GetRestorePageNumber()) {
			SetZoom(s->GetZoom());
			SetRotation(s->GetRotation());
		}
		Unlock();
	}
}


void PDFWindow::FillPageList() {
	BList list;
	for (int32 i = 0; i < mMainView->GetNumPages(); i++) {
		char pageNo[20];
		sprintf(pageNo, "%5.0d", (int)(i+1));
		list.AddItem(new BStringItem(pageNo));
	}

	MakeEmpty(mPagesView);
	mPagesView->AddList(&list);
}


void PDFWindow::UpdatePageList() {
	Document* document = mMainView->GetDocument();
	int pages = document->PageCount();
	// the labels need every page to be loaded, which takes a while for really large documents
	if (pages > 2000)
		return;

	BList list;
	bool hasLabels = false;
	for (int i = 1; i <= pages; i++) {
		BString label = document->PageLabel(i);
		BString number;
		number << i;
		if (label.Length() > 0 && label != number)
			hasLabels = true;
		list.AddItem(new BStringItem(label.Length() > 0 ? label.String() : number.String()));
	}

	if (hasLabels) {
		MakeEmpty(mPagesView);
		mPagesView->AddList(&list);
	} else {
		for (int32 i = list.CountItems() - 1; i >= 0; i--)
			delete (BStringItem*)list.ItemAt(i);
	}
}

bool PDFWindow::SetPendingIfLocked(uint32 mask) {
	if (mMainView->GetDocument()->Lock()->LockWithTimeout(0) == B_OK) {
		mMainView->GetDocument()->Lock()->Unlock();
		return false;
	} else {
		// could not lock, schedule action later
		SetPending(mask);
		return true;
	}
}

void PDFWindow::HandlePendingActions(bool ok) {
	if (IsPending(UPDATE_PAGE_LIST_PENDING))
		UpdatePageList();

	if (ok) {
		BMessage msg;
		if (IsPending(UPDATE_OUTLINE_LIST_PENDING)) {
			msg.what = SHOW_BOOKMARKS_CMD;
			MessageReceived(&msg);
		}
		if (IsPending(FILE_INFO_PENDING)) {
			msg.what = FILE_INFO_CMD;
			MessageReceived(&msg);
		}
		if (IsPending(PRINT_SETTINGS_PENDING)) {
			msg.what = PRINT_SETTINGS_CMD;
			MessageReceived(&msg);
		}
	}
	ClearPending();
}

///////////////////////////////////////////////////////////
void PDFWindow::StoreFileAttributes() {
	// store file settings
	if (mMainView && Lock()) {
		entry_ref cur_ref;
		if (mCurrentFile.InitCheck() == B_OK) {
			mCurrentFile.GetRef(&cur_ref);
			mFileAttributes.Write(&cur_ref, gApp->GetSettings());
		}
		Unlock();
	}
}

///////////////////////////////////////////////////////////
bool PDFWindow::QuitRequested() {
	gApp->WindowClosed();
	mMainView->WaitForPage(true);
	StoreFileAttributes();
	be_app->PostMessage(B_QUIT_REQUESTED);
	return true;
}

///////////////////////////////////////////////////////////
void PDFWindow::CleanUpBeforeLoad() {
}


///////////////////////////////////////////////////////////
bool PDFWindow::IsCurrentFile(entry_ref *ref) const {
	entry_ref r;
	mCurrentFile.GetRef(&r);
	return r == *ref;
}

///////////////////////////////////////////////////////////
bool PDFWindow::LoadFile(entry_ref *ref, const char *ownerPassword, const char *userPassword, bool *encrypted) {
	if (mMainView != NULL) {
		StoreFileAttributes();
		CleanUpBeforeLoad();
		// load new file
		if (mMainView->LoadFile(ref, &mFileAttributes, ownerPassword, userPassword, false, encrypted)) {
			mEntryChangedMonitor.StartWatching(ref);
			be_roster->AddToRecentDocuments(ref, BEPDF_APP_SIG);
			mCurrentFile.SetTo(ref);
			InitAfterOpen();
			return true;
		}
	}
	return false;
}

///////////////////////////////////////////////////////////
void PDFWindow::Reload(void) {
    BMessage m(B_REFS_RECEIVED);
    entry_ref ref;
    mCurrentFile.GetRef(&ref);
    m.AddRef("refs", &ref);
	be_app->PostMessage(&m);
}

///////////////////////////////////////////////////////////
void PDFWindow::EntryChanged() {
	Reload();
}

///////////////////////////////////////////////////////////
bool PDFWindow::CancelCommand(BMessage* msg) {
	// This is a work around:
	// This commands aren't allowed in fullscreen mode, otherwise
	// the windows opened by this commands would be behind the
	// main window and would not block the main window.
	if (mFullScreen) {
		switch(msg->what) {
			case OPEN_FILE_CMD:
			case RELOAD_FILE_CMD:
			case PAGESETUP_FILE_CMD:
			case ABOUT_APP_CMD:
			case FIND_CMD:
			case FIND_NEXT_CMD:
			case PREFERENCES_FILE_CMD:
			case FILE_INFO_CMD:
			case PRINT_SETTINGS_CMD:
				beep();
				return true;
		}
	}
	return false;
}

///////////////////////////////////////////////////////////
bool PDFWindow::ActivateWindow(BMessenger *messenger) {
	if (messenger && messenger->LockTarget()) {
		BLooper *looper;
		messenger->Target(&looper);
		((BWindow*)looper)->Activate(true);
		looper->Unlock();
		return true;
	} else {
		return false;
	}
}

///////////////////////////////////////////////////////////
bool PDFWindow::CanClose()
{
	return true;
}

void PDFWindow::UpdateInputEnabler()
{
	if (mMainView) {
		Document* doc = mMainView->GetDocument();
		int num_pages = mMainView->GetNumPages();
		int page = mMainView->Page();
		bool b = num_pages > 1 && page != 1;

		fMenuBar->FindItem(FIRST_PAGE_CMD)->SetEnabled(b);
		mToolBar->SetActionEnabled(FIRST_PAGE_CMD, b);
		fMenuBar->FindItem(PREVIOUS_PAGE_CMD)->SetEnabled(b);
		mToolBar->SetActionEnabled(PREVIOUS_N_PAGE_CMD, b);

		b = num_pages > 1 && page != num_pages;
		fMenuBar->FindItem(LAST_PAGE_CMD)->SetEnabled(b);
		mToolBar->SetActionEnabled(LAST_PAGE_CMD, b);
		fMenuBar->FindItem(NEXT_PAGE_CMD)->SetEnabled(b);
		mToolBar->SetActionEnabled(NEXT_PAGE_CMD, b);
		mToolBar->SetActionEnabled(NEXT_N_PAGE_CMD, b);

		mPageNumberItem->SetEnabled(num_pages > 1);

		mToolBar->SetActionEnabled(HISTORY_FORWARD_CMD, mMainView->CanGoForward());
		mToolBar->SetActionEnabled(HISTORY_BACK_CMD, mMainView->CanGoBack());

		int32 dpi = mMainView->GetZoomDPI();
		mToolBar->SetActionEnabled(ZOOM_IN_CMD, dpi != ZOOM_DPI_MAX);
		mToolBar->SetActionEnabled(ZOOM_OUT_CMD, dpi != ZOOM_DPI_MIN);

		mToolBar->SetActionEnabled(FIND_NEXT_CMD, mFindText.Length() > 0);

		int active = mLayerView->CardLayout()->VisibleIndex();
		mToolBar->SetActionPressed(SHOW_PAGE_LIST_CMD, mShowLeftPanel && active == PAGE_LIST_PANEL);
		mToolBar->SetActionPressed(SHOW_BOOKMARKS_CMD, mShowLeftPanel && active == BOOKMARKS_PANEL);
		mToolBar->SetActionPressed(FULL_SCREEN_CMD, mFullScreen);

		fMenuBar->FindItem(SHOW_PAGE_LIST_CMD)
			->SetMarked(mShowLeftPanel && active == PAGE_LIST_PANEL);
		fMenuBar->FindItem(SHOW_BOOKMARKS_CMD)
			->SetMarked(mShowLeftPanel && active == BOOKMARKS_PANEL);
		fMenuBar->FindItem(HIDE_LEFT_PANEL_CMD)->SetEnabled(mShowLeftPanel);

		fMenuBar->FindItem(OPEN_FILE_CMD)->SetEnabled(!mFullScreen);
		mToolBar->SetActionEnabled(OPEN_FILE_CMD, !mFullScreen);
		fMenuBar->FindItem(RELOAD_FILE_CMD)->SetEnabled(!mFullScreen);
		mToolBar->SetActionEnabled(RELOAD_FILE_CMD, !mFullScreen);
		fMenuBar->FindItem(PRINT_SETTINGS_CMD)
			->SetEnabled(!mFullScreen && !mPrintSettingsWindowOpen && doc->CanPrint());
		mToolBar->SetActionEnabled(PRINT_SETTINGS_CMD,
			!mFullScreen && !mPrintSettingsWindowOpen && doc->CanPrint());

		// PDF security settings
		bool okToCopy = doc->CanCopy();
		fMenuBar->FindItem(COPY_SELECTION_CMD)->SetEnabled(okToCopy);
		fMenuBar->FindItem(SELECT_ALL_CMD)->SetEnabled(okToCopy);
		fMenuBar->FindItem(SELECT_NONE_CMD)->SetEnabled(okToCopy);

		bool hasUserBookmark = mOutlinesView->HasUserBookmark(page);
		bool selected    = hasUserBookmark && mOutlinesView->IsUserBMSelected();
		fMenuBar->FindItem(ADD_USER_BOOKMARK_CMD)->SetEnabled(!hasUserBookmark);
		fMenuBar->FindItem(EDIT_USER_BOOKMARK_CMD)->SetEnabled(selected);
		fMenuBar->FindItem(DELETE_USER_BOOKMARK_CMD)->SetEnabled(selected);
	}
}


void PDFWindow::AddItem(BMenu *subMenu, const char *label, uint32 cmd, bool marked, char shortcut, uint32 modifiers) {
	BMenuItem *item = new BMenuItem(label, new BMessage(cmd), shortcut, modifiers);
	item->SetMarked(marked);
	subMenu->AddItem(item);
}


void PDFWindow::UpdateWindowsMenu() {
/*
	BMenuItem *item;
	while ((item = mWindowsMenu->RemoveItem((int32)0)) != NULL) delete item;
	BList list;
	be_roster->GetAppList(BEPDF_APP_SIG, &list);
	entry_ref ref;
	const int n = list.CountItems();

	for (int i = n-1; i >= 0; i --) {
		team_id who = (team_id)list.ItemAt(i);
		char s[256];
		sprintf(s, "BePDF %d", who);
		mWindowsMenu->AddItem(new BMenuItem(s, NULL));
	}
*/
}


BMenuBar* PDFWindow::BuildMenu()
{
	BString label;
	GlobalSettings* settings = gApp->GetSettings();
	int16 zoom = settings->GetZoom();
	float rotation = settings->GetRotation();

	BMenuBar* menuBar = new BMenuBar("mainBar");
	BLayoutBuilder::Menu<>(menuBar)
		.AddMenu(B_TRANSLATE("File"))
			.AddItem(mOpenMenu = new RecentDocumentsMenu(
				B_TRANSLATE("Open" B_UTF8_ELLIPSIS), B_REFS_RECEIVED))
			.AddItem(mNewMenu  = new RecentDocumentsMenu(
				B_TRANSLATE("Open in new window" B_UTF8_ELLIPSIS),
				OPEN_IN_NEW_WINDOW_CMD))
			.AddItem(B_TRANSLATE("Reload"), RELOAD_FILE_CMD, 'R')
			.AddItem(mFileInfoItem = new BMenuItem(B_TRANSLATE("File info" B_UTF8_ELLIPSIS),
				new BMessage(FILE_INFO_CMD), 'I'))
			.AddSeparator()
			.AddItem(B_TRANSLATE("Page setup" B_UTF8_ELLIPSIS), PAGESETUP_FILE_CMD, 'S')
			.AddItem(B_TRANSLATE("Print" B_UTF8_ELLIPSIS), PRINT_SETTINGS_CMD, 'P')
			.AddSeparator()
			.AddItem(B_TRANSLATE("Close"), CLOSE_FILE_CMD, 'W')
			.AddItem(B_TRANSLATE("Quit"), QUIT_APP_CMD, 'Q')
		.End()

		.AddMenu(B_TRANSLATE("Edit"))
			.AddItem(B_TRANSLATE("Copy selection"), COPY_SELECTION_CMD, 'C')
			.AddSeparator()
			.AddItem(B_TRANSLATE("Select all"), SELECT_ALL_CMD, 'A')
			.AddItem(B_TRANSLATE("Select none"), SELECT_NONE_CMD, 'A', B_SHIFT_KEY)
			.AddSeparator()
			.AddItem(mPreferencesItem = new BMenuItem(B_TRANSLATE("Preferences" B_UTF8_ELLIPSIS),
										new BMessage(PREFERENCES_FILE_CMD), ','))
		.End()

		.AddMenu(B_TRANSLATE("View"))
			.AddItem(B_TRANSLATE("Show bookmarks"), SHOW_BOOKMARKS_CMD, 'B')
			.AddItem(B_TRANSLATE("Show page list"), SHOW_PAGE_LIST_CMD, 'L')
			.AddItem(B_TRANSLATE("Hide side bar"), HIDE_LEFT_PANEL_CMD, 'H')
			.AddSeparator()
			.AddItem(mFullScreenItem = new BMenuItem(B_TRANSLATE("Fullscreen"),
													new BMessage(FULL_SCREEN_CMD), B_RETURN))
			.AddSeparator()
			.AddItem(B_TRANSLATE("Fit to page width"), (FIT_TO_PAGE_WIDTH_CMD), '/')
			.AddItem(B_TRANSLATE("Fit to page"), (FIT_TO_PAGE_CMD), '*')
			.AddSeparator()
			.AddItem(B_TRANSLATE("Zoom in"), (ZOOM_IN_CMD), '+')
			.AddItem(B_TRANSLATE("Zoom out"), (ZOOM_OUT_CMD), '-')
			.AddSeparator()

			.AddMenu(mZoomMenu = new BMenu(B_TRANSLATE("Zoom")))
				.AddItem("25%", SET_ZOOM_VALUE_CMD, MIN_ZOOM == zoom)
				.AddItem("33%", SET_ZOOM_VALUE_CMD, MIN_ZOOM + 1 == zoom)
				.AddItem("50%", SET_ZOOM_VALUE_CMD, MIN_ZOOM + 2 == zoom)
				.AddItem("66%", SET_ZOOM_VALUE_CMD, MIN_ZOOM + 3 == zoom)
				.AddItem("75%", SET_ZOOM_VALUE_CMD, MIN_ZOOM + 4 == zoom)
				.AddItem("100%", SET_ZOOM_VALUE_CMD, MIN_ZOOM + 5 == zoom)
				.AddItem("125%", SET_ZOOM_VALUE_CMD, MIN_ZOOM + 6 == zoom)
				.AddItem("150%", SET_ZOOM_VALUE_CMD, MIN_ZOOM + 7 == zoom)
				.AddItem("175%", SET_ZOOM_VALUE_CMD, MIN_ZOOM + 8 == zoom)
				.AddItem("200%", SET_ZOOM_VALUE_CMD, MIN_ZOOM + 9 == zoom)
				.AddItem("300%", SET_ZOOM_VALUE_CMD, MIN_ZOOM + 10 == zoom)
			.End()

			.AddSeparator()

			.AddMenu(mRotationMenu = new BMenu(B_TRANSLATE("Rotation")))
				.AddItem("0°", SET_ROTATE_VALUE_CMD, rotation == 0)
				.AddItem("90°", SET_ROTATE_VALUE_CMD, rotation == 90)
				.AddItem("180°", SET_ROTATE_VALUE_CMD, rotation == 180)
				.AddItem("270°", SET_ROTATE_VALUE_CMD, rotation == 270)
			.End()

			.AddSeparator()
			.AddItem(B_TRANSLATE("Show error messages"), SHOW_TRACER_CMD, 'M')
		.End()

		.AddMenu(B_TRANSLATE("Search"))
			.AddItem(B_TRANSLATE("Find" B_UTF8_ELLIPSIS), FIND_CMD, 'F')
			.AddItem(B_TRANSLATE("Find next" B_UTF8_ELLIPSIS), new BMessage(FIND_NEXT_CMD), 'G')
		.End()

		.AddMenu(B_TRANSLATE("Page"))
			.AddItem(B_TRANSLATE("First"), FIRST_PAGE_CMD)
			.AddItem(B_TRANSLATE("Previous"), PREVIOUS_PAGE_CMD)
			.AddItem(B_TRANSLATE("Jump to page"), GOTO_PAGE_MENU_CMD, 'J')
			.AddItem(B_TRANSLATE("Next"), NEXT_PAGE_CMD)
			.AddItem(B_TRANSLATE("Last"), LAST_PAGE_CMD)
			.AddSeparator()
			.AddItem(B_TRANSLATE("Back"), HISTORY_BACK_CMD, B_LEFT_ARROW)
			.AddItem(B_TRANSLATE("Forward"), HISTORY_FORWARD_CMD, B_RIGHT_ARROW)
		.End()

		.AddMenu(B_TRANSLATE("Bookmark"))
			.AddItem(B_TRANSLATE("Add"), ADD_USER_BOOKMARK_CMD)
			.AddItem(B_TRANSLATE("Delete"), DELETE_USER_BOOKMARK_CMD)
			.AddItem(B_TRANSLATE("Edit"), EDIT_USER_BOOKMARK_CMD)
		.End()

		.AddMenu(B_TRANSLATE("Help"))
			.AddItem(B_TRANSLATE("Show help" B_UTF8_ELLIPSIS), HELP_CMD)
			.AddItem(B_TRANSLATE("Online help" B_UTF8_ELLIPSIS), ONLINE_HELP_CMD)
			.AddSeparator()
			.AddItem(B_TRANSLATE("Visit homepage" B_UTF8_ELLIPSIS), HOME_PAGE_CMD)
			.AddItem(B_TRANSLATE("Issue tracker" B_UTF8_ELLIPSIS), BUG_REPORT_CMD)
			.AddSeparator()
			.AddItem(B_TRANSLATE("About Tsundoku" B_UTF8_ELLIPSIS), ABOUT_APP_CMD)
		.End();

		mZoomMenu->SetRadioMode (true);
		if (zoom < MIN_ZOOM) SetZoom(zoom);

		mRotationMenu->SetRadioMode(true);

//		menuBar->AddItem ( mWindowsMenu = new BMenu(B_TRANSLATE("Window")) );
		UpdateWindowsMenu();

	mOpenMenu->Superitem()->SetTrigger('O');
	mOpenMenu->Superitem()->SetMessage(new BMessage(OPEN_FILE_CMD));
	mOpenMenu->Superitem()->SetShortcut('O', 0);

	mNewMenu->Superitem()->SetTrigger('N');
	mNewMenu->Superitem()->SetMessage(new BMessage(NEW_WINDOW_CMD));
	mNewMenu->Superitem()->SetShortcut('N', 0);

	return menuBar;
}


BToolBar* PDFWindow::BuildToolBar()
{
	mToolBar = new BToolBar;
	mToolBar->SetName("toolbar");
	mToolBar->SetResizingMode(B_FOLLOW_TOP | B_FOLLOW_LEFT_RIGHT);
	mToolBar->SetFlags(B_WILL_DRAW | B_FRAME_EVENTS);

	mToolBar->AddAction(OPEN_FILE_CMD, this, LoadVectorIcon("OPEN_FILE"),
		B_TRANSLATE("Open file"));
	mToolBar->AddAction(RELOAD_FILE_CMD, this, LoadVectorIcon("RELOAD_FILE"),
		B_TRANSLATE("Reload file"));
	mToolBar->AddAction(PRINT_SETTINGS_CMD, this, LoadVectorIcon("PRINT"),
		B_TRANSLATE("Print"));

	mToolBar->AddSeparator();

	mToolBar->AddAction(SHOW_BOOKMARKS_CMD, this, LoadVectorIcon("BOOKMARKS"),
		B_TRANSLATE("Show bookmarks"), NULL, true);
	mToolBar->AddAction(SHOW_PAGE_LIST_CMD, this,
		LoadVectorIcon("SHOW_PAGE_LIST"), B_TRANSLATE("Show page list"), NULL,
		true);
	// mToolBar->AddAction(HIDE_LEFT_PANEL_CMD, this,
	//	LoadVectorIcon("HIDE_PAGE_LIST"), B_TRANSLATE("Hide page list"),
	//	NULL, true);

	mToolBar->AddSeparator();

	mToolBar->AddAction(FULL_SCREEN_CMD, this,
		LoadVectorIcon("FULL_SCREEN"), B_TRANSLATE("Fullscreen mode"),
		NULL, true);

	mToolBar->AddSeparator();

	mToolBar->AddAction(FIRST_PAGE_CMD, this, LoadVectorIcon("FIRST"),
		B_TRANSLATE("Go to start of document"));
	mToolBar->AddAction(PREVIOUS_N_PAGE_CMD, this,
		LoadVectorIcon("PREVIOUS_N"),
		B_TRANSLATE("Go back 10 pages"));
	mToolBar->AddAction(PREVIOUS_PAGE_CMD, this, LoadVectorIcon("PREVIOUS"),
		B_TRANSLATE("Go to previous page"));
	mToolBar->AddAction(NEXT_PAGE_CMD, this, LoadVectorIcon("NEXT"),
		B_TRANSLATE("Go to next page"));
	mToolBar->AddAction(NEXT_N_PAGE_CMD, this, LoadVectorIcon("NEXT_N"),
		B_TRANSLATE("Go forward 10 pages"));
	mToolBar->AddAction(LAST_PAGE_CMD, this, LoadVectorIcon("LAST"),
		B_TRANSLATE("Go to end of document"));

	mToolBar->AddSeparator();

	mToolBar->AddAction(HISTORY_BACK_CMD, this, LoadVectorIcon("BACK"),
		B_TRANSLATE("Back in page history list"));
	mToolBar->AddAction(HISTORY_FORWARD_CMD, this, LoadVectorIcon("FORWARD"),
		B_TRANSLATE("Forward in page history list"));

	mToolBar->AddSeparator();

	// Add "go to page number" TextControl
	mPageNumberItem	= new BTextControl("goto_page",
		"", "", new BMessage(GOTO_PAGE_CMD));
	mPageNumberItem->SetExplicitMaxSize(BSize(50, 25));
	mPageNumberItem->SetAlignment(B_ALIGN_CENTER, B_ALIGN_CENTER);
	mPageNumberItem->SetTarget(this);
	mPageNumberItem->TextView()->DisallowChar(B_ESCAPE);

	BTextView *t = mPageNumberItem->TextView();
	BFont font(be_plain_font);
	t->GetFontAndColor(0, &font);
	font.SetSize(10);
	t->SetFontAndColor(0, 1000, &font, B_FONT_SIZE);
	mToolBar->AddView(mPageNumberItem);

	// display total number of pages
	mTotalPageNumberItem = new BStringView("total_num_of_pages", "");
	mTotalPageNumberItem->SetAlignment(B_ALIGN_CENTER);
	mTotalPageNumberItem->SetFontSize(10);
	mToolBar->AddView(mTotalPageNumberItem);

	mToolBar->AddSeparator();

	mToolBar->AddAction(FIT_TO_PAGE_WIDTH_CMD, this,
		LoadVectorIcon("FIT_TO_PAGE_WIDTH"),
		B_TRANSLATE("Fit to page width"));
	mToolBar->AddAction(FIT_TO_PAGE_CMD, this, LoadVectorIcon("FIT_TO_PAGE"),
		B_TRANSLATE("Fit to page"));

	mToolBar->AddSeparator();

	mToolBar->AddAction(ROTATE_CLOCKWISE_CMD, this,
		LoadVectorIcon("ROTATE_CLOCKWISE"), B_TRANSLATE("Rotate clockwise"));
	mToolBar->AddAction(ROTATE_ANTI_CLOCKWISE_CMD, this,
		LoadVectorIcon("ROTATE_ANTI_CLOCKWISE"),
		B_TRANSLATE("Rotate counter-clockwise"));
	mToolBar->AddAction(ZOOM_IN_CMD, this, LoadVectorIcon("ZOOM_IN"),
		B_TRANSLATE("Zoom in"));
	mToolBar->AddAction(ZOOM_OUT_CMD, this, LoadVectorIcon("ZOOM_OUT"),
		B_TRANSLATE("Zoom out"));

	mToolBar->AddSeparator();

	mToolBar->AddAction(FIND_CMD, this, LoadVectorIcon("FIND"),
		B_TRANSLATE("Find"));
	mToolBar->AddAction(FIND_NEXT_CMD, this, LoadVectorIcon("FIND_NEXT"),
		B_TRANSLATE("Find next"));
	mToolBar->AddGlue();
	return mToolBar;
}


BCardView* PDFWindow::BuildLeftPanel()
{
	BCardView* layerView = new BCardView("layers");

	// PageList
	mOutlinesView = new OutlinesView(mMainView->GetDocument(),
		mFileAttributes.GetBookmarks(), gApp->GetSettings(),
		this, B_FRAME_EVENTS);

	// LayerView contains the page numbers
	mPagesView = new BListView("pagesList", B_SINGLE_SELECTION_LIST,
		B_WILL_DRAW | B_NAVIGABLE | B_FRAME_EVENTS);
	mPagesView->SetSelectionMessage(new BMessage(PAGE_SELECTED_CMD));

	BView *pageView = new BScrollView("pageScrollView", mPagesView,
		B_FRAME_EVENTS, true, true, B_FANCY_BORDER);

	layerView->CardLayout()->AddView(mOutlinesView);
	layerView->CardLayout()->AddView(pageView);

	return layerView;
}


void PDFWindow::SetUpViews(entry_ref* ref,
	const char *ownerPassword, const char *userPassword, bool *encrypted)
{
	fMenuBar = BuildMenu();
	BuildToolBar();

	mMainView = new PDFView(ref, &mFileAttributes, "mainView",
		B_WILL_DRAW | B_NAVIGABLE | B_FRAME_EVENTS,
		ownerPassword, userPassword, encrypted);

	mCurrentFile.SetTo(ref);
	if (!mMainView->IsOk()) {
		delete mMainView;
		mMainView = NULL;
		return; // ERROR!
	}
	mEntryChangedMonitor.StartWatching(ref);

	fMainContainer = new BView("ScrollContainer", 0);
	BScrollView* mainScrollView = new BScrollView("scrollView",
		mMainView, 0, true, true, B_FANCY_BORDER);
	mainScrollView->SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
	mainScrollView->SetExplicitMinSize(BSize(0, 0));

	BLayoutBuilder::Group<>(fMainContainer, B_VERTICAL, 0)
		.SetInsets(0, 0, -1, -1)
		.Add(mainScrollView)
	.End();
	fMainContainer->SetExplicitMinSize(BSize(0, 0));

	// left view of SplitView is a LayerView
	mLayerView = BuildLeftPanel();

	// SplitView
	mSplitView = new BSplitView(B_HORIZONTAL);
	// the outline needs room for chapter titles (the divider can still be dragged)
	mLayerView->SetExplicitMinSize(
		BSize(be_plain_font->StringWidth("M") * 20, B_SIZE_UNSET));
	mSplitView->AddChild(mLayerView, 3);
	mSplitView->AddChild(fMainContainer, 10);
	mSplitView->SetInsets(0);
	mSplitView->SetSpacing(4);

	BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
		.SetInsets(0, 0, -1, -1)
		.Add(fMenuBar)
		.Add(mToolBar)
		.Add(mSplitView)
	.End();

	SetTotalPageNumber(mMainView->GetNumPages());

    GlobalSettings *s = gApp->GetSettings();

	// show or hide panel that is stored in settings
	// the annotation and attachment panels of older versions do not exist any more
	ShowLeftPanel(s->GetLeftPanel() == PAGE_LIST_PANEL ? PAGE_LIST_PANEL : BOOKMARKS_PANEL);
	if (!s->GetShowLeftPanel()) {
		// hide panel
		ToggleLeftPanel();
	}
	CollapseOutlinePanelIfEmpty();

	// set focus to PDFView, so it receives mouse and keyboard events
	mMainView->MakeFocus();
}


void PDFWindow::SetZoom(int16 zoom)
{
	BMenuItem *item;
	gApp->GetSettings()->SetZoom(zoom);
	if (zoom >= MIN_ZOOM) {
		item = mZoomMenu->ItemAt(zoom - MIN_ZOOM);
		if (item != NULL)
			item->SetMarked(true);
	} else {
		item = mZoomMenu->FindItem(CUSTOM_ZOOM_FACTOR_MSG);
		if (item != NULL) {
			mZoomMenu->RemoveItem(item);
			delete item;
		}
		char label[256];
		sprintf(label, B_TRANSLATE("Custom zoom factor (%d%%)"), -zoom * 100 / 72);
		BMessage *msg = new BMessage(CUSTOM_ZOOM_FACTOR_MSG);
		msg->AddInt16("zoom", zoom);
		item = new BMenuItem(label, msg, 0);
		mZoomMenu->AddItem(item);
		item->SetMarked(true);
	}
}
///////////////////////////////////////////////////////////
void PDFWindow::SetRotation(float rotation) {
int16 i;
	if (rotation <= 45) i = 0;
	else if (rotation <= 90.0+45) i = 1;
	else if (rotation <= 180.0+45) i = 2;
	else if (rotation <= 270.0+45) i = 3;
	else i = 0;
	BMenuItem *item = mRotationMenu->ItemAt(i);
	item->SetMarked(true);
}

void PDFWindow::NewDoc(Document *doc) {
	mOutlinesView->SetDocument(doc, mFileAttributes.GetBookmarks());
	ActivateOutlines();
	CollapseOutlinePanelIfEmpty();

	if (mFIWMessenger && mFIWMessenger->LockTarget()) {
		BLooper *looper;
		FileInfoWindow *w = (FileInfoWindow*)mFIWMessenger->Target(&looper);
		w->Refresh(&mCurrentFile, doc);
		looper->Unlock();
	}
	if (mPSWMessenger && mPSWMessenger->LockTarget()) {
		BLooper *looper;
		PrintSettingsWindow *w = (PrintSettingsWindow*)mPSWMessenger->Target(&looper);
		w->Refresh(doc);
		looper->Unlock();
	}
}
///////////////////////////////////////////////////////////
void PDFWindow::NewPage(int page) {
	UpdateInputEnabler();
}
///////////////////////////////////////////////////////////
void PDFWindow::FrameMoved(BPoint p) {
	if (!mFullScreen) {
		gApp->GetSettings()->SetWindowPosition(p);
	}
}
///////////////////////////////////////////////////////////
void PDFWindow::FrameResized (float width, float height)
{
	if (!mFullScreen) {
		gApp->GetSettings()->SetWindowSize(width, height);
	}
}


void
PDFWindow::SetZoomSize(float w, float h)
{
	// TODO / FIXME
}

///////////////////////////////////////////////////////////
// update page list and page number item
void
PDFWindow::SetPage(int32 page) {
	char pageStr [64];
    if (page <= 0) page = 1;
    if (page > mPagesView->CountItems()) {
        page = mPagesView->CountItems();
    }
	snprintf (pageStr, sizeof (pageStr), "%d" B_PRId32, page);
	mPageNumberItem->SetText (pageStr);
	mPagesView->Select(page-1);
	mPagesView->ScrollToSelection();
	mOutlinesView->SelectPage(page);
}


void
PDFWindow::MessageReceived(BMessage* message)
{
	int32 page;
	const char *text;

	if (CancelCommand(message))
		return;

	switch (message->what) {
	case OPEN_FILE_CMD:
		mMainView->WaitForPage();
		gApp->OpenFilePanel();
		break;
	case NEW_WINDOW_CMD:
		be_roster->Launch(BEPDF_APP_SIG, 0, (char**)NULL);
		break;
	case OPEN_IN_NEW_WINDOW_CMD: {
		BMessage m(B_REFS_RECEIVED);
		entry_ref r;
		if (message->FindRef("refs", 0, &r) == B_OK) {
			BEntry entry(&r);
			BPath path;
			entry.GetPath(&path);
			OpenPDF(path.Path());
		}
		}
		break;
	case RELOAD_FILE_CMD:
		Reload();
		break;
	case CLOSE_FILE_CMD:
		mMainView->WaitForPage(true);
		PostMessage(B_QUIT_REQUESTED);
		break;
	case QUIT_APP_CMD:
	    gApp->Notify(BepdfApplication::NOTIFY_QUIT_MSG);
		break;
	case PAGESETUP_FILE_CMD:
		mMainView->PageSetup ();
		break;
	case ABOUT_APP_CMD:
		be_app->PostMessage(B_ABOUT_REQUESTED);
		break;
	case COPY_SELECTION_CMD: mMainView->CopySelection();
		break;
	case SELECT_ALL_CMD: mMainView->SelectAll();
		break;
	case SELECT_NONE_CMD: mMainView->SelectNone();
		break;
	case FIRST_PAGE_CMD:
		mMainView->MoveToPage(1);
		break;
	case PREVIOUS_N_PAGE_CMD:
		mMainView->MoveToPage(mMainView->Page() - 10);
		break;
	case NEXT_N_PAGE_CMD:
		mMainView->MoveToPage(mMainView->Page() + 10);
		break;
	case PREVIOUS_PAGE_CMD:
		if (B_SHIFT_KEY & modifiers()) {
			mMainView->ScrollVertical (false, 0.95);
		} else {
			page = mMainView->Page();
			mMainView->MoveToPage (page - 1);
		}
		break;
	case NEXT_PAGE_CMD:
		if (B_SHIFT_KEY & modifiers()) {
			mMainView->ScrollVertical (true, 0.95);
		} else {
			page = mMainView->Page();
			mMainView->MoveToPage (page + 1);
		}
		break;
	case LAST_PAGE_CMD:
		mMainView->MoveToPage (mMainView->GetNumPages());
		break;
	case GOTO_PAGE_CMD: {
        status_t result;
        BTextControl * control;
        BControl * ptr;

        result = message->FindPointer ("source", (void **)&ptr);
        if (result == B_OK) {
            control = dynamic_cast <BTextControl *> (ptr);
            if (result == B_OK && control != NULL) {
                const char *txt = control->Text ();
                page = atoi (txt);
            }
        } else {    // may come from external source over page parameter
            result = message->FindInt32("page", &page);
            if (result == B_OK)
                mMainView->WaitForPage();
        }

        if (result == B_OK) {
            LockLooper();
            mMainView->MoveToPage (page);
            UnlockLooper();
            mMainView->MakeFocus();
        }
		break;
    }
	case PAGE_SELECTED_CMD:
		page = mPagesView->CurrentSelection(0) + 1;
		mMainView->MoveToPage(page);
		break;
	case GOTO_PAGE_MENU_CMD:
		mPageNumberItem->MakeFocus();
		break;
	case SET_ZOOM_VALUE_CMD: {
			status_t err;
			BMenuItem * item;
			BMenu * menu;
			BArchivable * ptr;
			int32 idx;

			err = message->FindPointer ("source", (void **)&ptr);
			item = dynamic_cast <BMenuItem *> (ptr);
			if (err == B_OK && item != NULL) {
				menu = item->Menu();
				if (menu == NULL) {
					// ERROR
				} else {
					idx = menu->IndexOf(item);
					if (idx > MAX_ZOOM) {
						idx = MAX_ZOOM;
					}
					SetZoom (idx);
					mMainView->SetZoom(idx);
				}
			}
		}
		break;
	case ZOOM_IN_CMD:
	case ZOOM_OUT_CMD:
		mMainView->Zoom(message->what == ZOOM_IN_CMD);
		break;
	case FIT_TO_PAGE_WIDTH_CMD:
		mMainView->FitToPageWidth();
		break;
	case FIT_TO_PAGE_CMD:
		mMainView->FitToPage();
		break;
	case SET_ROTATE_VALUE_CMD: {
			status_t err;
			BMenuItem * item;
			BMenu * menu;
			BArchivable * ptr;
			int32 idx;

			err = message->FindPointer ("source", (void **)&ptr);
			item = dynamic_cast <BMenuItem *>(ptr);
			if (err == B_OK && item != NULL) {
				menu = item->Menu();
				if (menu == NULL) {
					// ERROR
				} else {
					idx = menu->IndexOf (item);
					mMainView->SetRotation (idx * 90);
				}
			}
		}
		break;
	case ROTATE_CLOCKWISE_CMD: mMainView->RotateClockwise();
		break;
	case ROTATE_ANTI_CLOCKWISE_CMD: mMainView->RotateAntiClockwise();
		break;
	case HISTORY_BACK_CMD:
		mMainView->Back ();
		break;
	case HISTORY_FORWARD_CMD:
		mMainView->Forward ();
		break;

	case FIND_CMD:
		mMainView->WaitForPage();
		if (Lock()) {
			mFindWindow = new FindTextWindow(gApp->GetSettings(), mFindText.String(), this);
			Unlock();
		}
		break;
	case FIND_NEXT_CMD:
		mMainView->WaitForPage();
		if (Lock()) {
			mFindWindow = new FindTextWindow(gApp->GetSettings(), mFindText.String(), this);
			Unlock();
			mFindWindow->PostMessage('Find');
		}
		break;
/*	case KEYBOARD_SHORTCUTS_CMD: {
			BAlert *info = new BAlert("Info",
				"Keyboard Shortcuts:\n\n"
				"Space - scroll forward on a page\n"
				"Backspace - scroll backwards on page\n"
				"Cursor Arrow Keys - scroll incrementally in the direction of the cursor key\n"
				"Page Up - skip to the previous page\n"
				"Page Down - skip to the next page\n"
				"Home - return to the beginning of the document\n"
				"End - advance to the end of the document\n"
				"ALT+B - return to the previously viewed page within the document"
				, "OK");
			info->Go();
		}
		break;*/
	case HELP_CMD:
		OpenHelp();
		break;
	case ONLINE_HELP_CMD:
		LaunchHTMLBrowser("http://haikuarchives.github.io/BePDF/English/table_of_contents.html");
		break;
	case HOME_PAGE_CMD:
		LaunchHTMLBrowser("https://github.com/grexe/tsundoku");
		break;
	case BUG_REPORT_CMD:
		LaunchHTMLBrowser("https://github.com/grexe/tsundoku/issues/");
		break;
	case PREFERENCES_FILE_CMD:
		mPreferencesItem->SetEnabled(false);
		new PreferencesWindow(gApp->GetSettings(), this);
		break;
	case FILE_INFO_CMD:
		if (SetPendingIfLocked(FILE_INFO_PENDING)) return;
		if (!ActivateWindow(mFIWMessenger)) {
			FileInfoWindow *w;
			mMainView->WaitForPage();
			w = new FileInfoWindow(gApp->GetSettings(), &mCurrentFile, mMainView->GetDocument(), this);
			mFIWMessenger = new BMessenger(w);
		}
		break;
	case PRINT_SETTINGS_CMD: {
			if (SetPendingIfLocked(PRINT_SETTINGS_PENDING)) return;
			PrintSettingsWindow *w;
			mPrintSettingsWindowOpen = true;
			UpdateInputEnabler();
			w = new PrintSettingsWindow(mMainView->GetDocument(), gApp->GetSettings(), this);
			mPSWMessenger = new BMessenger(w);
		}
		break;
	case SHOW_BOOKMARKS_CMD:
		if (mShowLeftPanel && mLayerView->CardLayout()->VisibleIndex() == BOOKMARKS_PANEL)
			HideLeftPanel();
		else
			ShowLeftPanel(BOOKMARKS_PANEL);
		break;
	case SHOW_PAGE_LIST_CMD:
		if (mShowLeftPanel && mLayerView->CardLayout()->VisibleIndex() == PAGE_LIST_PANEL)
			HideLeftPanel();
		else
			ShowLeftPanel(PAGE_LIST_PANEL);
		break;
	case HIDE_LEFT_PANEL_CMD:
		HideLeftPanel();
		break;
	case FULL_SCREEN_CMD: OnFullScreen();
		break;
	case ADD_USER_BOOKMARK_CMD: AddUserBookmark();
		break;
	case DELETE_USER_BOOKMARK_CMD: DeleteUserBookmark();
		break;
	case EDIT_USER_BOOKMARK_CMD: EditUserBookmark();
		break;
	case SHOW_TRACER_CMD: OutputTracer::ShowWindow(gApp->GetSettings());
		break;
	case CUSTOM_ZOOM_FACTOR_MSG: {
		int16 zoom;
		if (message->FindInt16("zoom", &zoom) == B_OK) {
			SetZoom(zoom);
			mMainView->SetZoom(zoom);
		}
		}
		break;

	// Find Text Window
	case FindTextWindow::FIND_START_NOTIFY_MSG: {
			bool ignoreCase;
			bool backward;
			mFindInProgress = true;
			mFindState = (uint32) FindTextWindow::FIND_STOP_NOTIFY_MSG;
			message->FindString("text", &text);
			message->FindBool("ignoreCase", &ignoreCase);
			message->FindBool("backward", &backward);
			mFindText.SetTo(text);
			mMainView->Find(text, ignoreCase, backward, mFindWindow);
			break;
		}
	case FindTextWindow::FIND_STOP_NOTIFY_MSG:
	case FindTextWindow::FIND_ABORT_NOTIFY_MSG:
		if (mFindInProgress) {
			mFindState = message->what;
			mMainView->StopFind();
		} else {
			mFindWindow->PostMessage(message->what);
			if (message->what == (uint32)FindTextWindow::FIND_ABORT_NOTIFY_MSG) {
				mFindWindow->PostMessage(FindTextWindow::FIND_QUIT_REQUESTED_MSG);
			}
		}
		UpdateInputEnabler();
		break;
	case FindTextWindow::TEXT_FOUND_NOTIFY_MSG:
	case FindTextWindow::TEXT_NOT_FOUND_NOTIFY_MSG:
		mFindInProgress = false;
		mFindWindow->PostMessage(mFindState);
		if (mFindState == (uint32)FindTextWindow::FIND_ABORT_NOTIFY_MSG) {
			mFindWindow->PostMessage(FindTextWindow::FIND_QUIT_REQUESTED_MSG);
		}
		break;

	// Page Renderer
	case PageRenderer::FINISH_MSG: {
			thread_id id;
			BBitmap *bitmap;
			PageRenderer::GetParameter(message, &id, &bitmap);
			mMainView->PostRedraw(id, bitmap);
			HandlePendingActions(message->what == PageRenderer::FINISH_MSG);
		}
		break;
	case PageRenderer::ABORT_MSG: {
			thread_id id; BBitmap *bitmap;
			PageRenderer::GetParameter(message, &id, &bitmap);
			mMainView->RedrawAborted(id, bitmap);
			HandlePendingActions(false);
		}
		break;

	// Preferences Window
	case PreferencesWindow::RESTART_DOC_NOTIFY:
		mMainView->WaitForPage(true);
		mMainView->RestartDoc();
		break;
	case PreferencesWindow::CHANGE_NOTIFY: {
		int16 kind, which, index;
			if (PreferencesWindow::DecodeMessage(message, kind, which, index)) {
				switch (kind) {
				case PreferencesWindow::DISPLAY:
					switch (which) {
					case PreferencesWindow::DISPLAY_FILLED_SELECTION:
						mMainView->SetFilledSelection(index == 0);
						break;
					}
				}
			}
		}
		break;
	case PreferencesWindow::QUIT_NOTIFY: mPreferencesItem->SetEnabled(true);
		break;
	case PreferencesWindow::UPDATE_NOTIFY:
		mMainView->UpdateSettings(gApp->GetSettings());
		break;

	// File Info Window
	case FileInfoWindow::QUIT_NOTIFY:
		mFileInfoItem->SetEnabled(true);
		delete mFIWMessenger;
		mFIWMessenger = NULL;
		break;
	// Print Settings Window
	case PrintSettingsWindow::QUIT_NOTIFY:
		// mPrintSettingsItem->SetEnabled(true);
		mPrintSettingsWindowOpen = false;
		UpdateInputEnabler();
		delete mPSWMessenger;
		mPSWMessenger = NULL;
		break;
	case PrintSettingsWindow::PRINT_NOTIFY:
			mMainView->WaitForPage();
			mMainView->Print ();
		break;

	// Outlines View (TODO simplify, BMessenger not needed any more)
	case OutlinesView::PAGE_NOTIFY: {
			int32 page;
			if (message->FindInt32("page", &page) == B_OK) {
				float x, y;
				if (message->FindFloat("x", &x) == B_OK && message->FindFloat("y", &y) == B_OK)
					mMainView->GotoPosition(page, x, y);
				else
					mMainView->MoveToPage(page);
				UpdateInputEnabler();
			}
		}
		break;
	case OutlinesView::QUIT_NOTIFY:
		delete mOWMessenger; mOWMessenger = NULL;
		break;
	case OutlinesView::STATE_CHANGE_NOTIFY:
		UpdateInputEnabler();
		break;
	case BookmarkWindow::BOOKMARK_ENTERED_NOTIFY:
		{
		BString label;
		int32  pageNum;
			if (message->FindString("label", &label) == B_OK &&
			    message->FindInt32("pageNum", &pageNum) == B_OK) {
				mOutlinesView->AddUserBookmark(pageNum, label.String());
				SaveUserBookmarks();
				UpdateInputEnabler();
			}
		}
		break;

#ifdef TSUNDOKU_TESTING
	case 'TSTX': {
			BString cmd;
			message->FindString("cmd", &cmd);
			{
				FILE* out = fopen("/tmp/ts_test.out", "a");
				if (out != NULL) {
					char* type; type_code t; int32 c;
					fprintf(out, "TSTX cmd=[%s]", cmd.String());
					for (int32 i = 0; message->GetInfo(B_ANY_TYPE, i, &type, &t, &c) == B_OK; i++)
						fprintf(out, " %s(%.4s)", type, (char*)&t);
					fputc('\n', out);
					fclose(out);
				}
			}
			if (cmd == "bigwindow") {
				// a frame that does not fit the screen, to see that FitToScreen() makes it fit
				SetSizeLimits(100, 10000, 100, 10000);
				ResizeTo(3000, 2000);
				MoveTo(1500, 900);
				FitToScreen();
				BRect frame = Frame(), decorator = DecoratorFrame(), screen = BScreen(this).Frame();
				FILE* out = fopen("/tmp/ts_test.out", "a");
				if (out != NULL) {
					fprintf(out, "bigwindow: frame %g,%g-%g,%g decorator %g,%g-%g,%g screen %g,%g-%g,%g\n", frame.left,
						frame.top, frame.right, frame.bottom, decorator.left, decorator.top, decorator.right,
						decorator.bottom, screen.left, screen.top, screen.right, screen.bottom);
					fclose(out);
				}
			} else if (cmd == "find") {
				// as the find window does it
				if (mFindWindow == NULL)
					mFindWindow = new FindTextWindow(gApp->GetSettings(), "", this);
				BString text;
				message->FindString("text", &text);
				int32 ignoreCase = 1, backward = 0;
				message->FindInt32("ignoreCase", &ignoreCase);
				message->FindInt32("backward", &backward);
				BMessage start(FindTextWindow::FIND_START_NOTIFY_MSG);
				start.AddString("text", text);
				start.AddBool("ignoreCase", ignoreCase != 0);
				start.AddBool("backward", backward != 0);
				MessageReceived(&start);
			} else if (cmd.StartsWith("do_")) {
				// any command of the window by name
				static const struct { const char* name; uint32 what; } commands[] = {
					{ "fileinfo", FILE_INFO_CMD }, { "preferences", PREFERENCES_FILE_CMD },
					{ "printsettings", PRINT_SETTINGS_CMD }, { "rotate", ROTATE_CLOCKWISE_CMD },
					{ "fitwidth", FIT_TO_PAGE_WIDTH_CMD }, { "fitpage", FIT_TO_PAGE_CMD },
					{ "back", HISTORY_BACK_CMD }, { "forward", HISTORY_FORWARD_CMD },
					{ "pagelist", SHOW_PAGE_LIST_CMD }, { "bookmarks", SHOW_BOOKMARKS_CMD },
					{ "zoomin", ZOOM_IN_CMD }, { "zoomout", ZOOM_OUT_CMD }, { "next", NEXT_PAGE_CMD },
					{ "previous", PREVIOUS_PAGE_CMD }, { "last", LAST_PAGE_CMD }, { "first", FIRST_PAGE_CMD },
					{ "copy", COPY_SELECTION_CMD }, { "selectall", SELECT_ALL_CMD }, { "addbookmark", ADD_USER_BOOKMARK_CMD },
					{ NULL, 0 }
				};
				BString name(cmd.String() + 3);
				for (int i = 0; commands[i].name != NULL; i++) {
					if (name == commands[i].name) {
						FILE* out = fopen("/tmp/ts_test.out", "a");
						if (out != NULL) {
							fprintf(out, "command %s\n", name.String());
							fclose(out);
						}
						BMessage m(commands[i].what);
						MessageReceived(&m);
					}
				}
			} else
				mMainView->TestCommand(message);
		}
		break;
#endif
	default:
		BWindow::MessageReceived(message);
	}
}


void
PDFWindow::OpenPDF(const char* file)
{
	char *argv[2] = { (char*)file, NULL };
	be_roster->Launch(BEPDF_APP_SIG, 1, argv);
}


bool
PDFWindow::OpenPDFHelp(const char* name)
{
	BPath path(*gApp->GetAppPath());
	path.Append("docs");
	path.Append(name);
	BEntry entry(path.Path());
	if (entry.InitCheck() == B_OK && entry.Exists()) {
		OpenPDF(path.Path());
		return true;
	}
	return false;
}


void
PDFWindow::OpenHelp()
{
	// Tsundoku does not ship a manual of its own yet, so fall back to the online one of BePDF
	if (!OpenPDFHelp(B_TRANSLATE_COMMENT("English.pdf",
			"Replace with the PDF name of the help document, if there is one for your language.")))
		LaunchHTMLBrowser("http://haikuarchives.github.io/BePDF/English/table_of_contents.html");
}


void
PDFWindow::LaunchHTMLBrowser(const char *path)
{
	char *argv[2] = {(char*)path, NULL};
	be_roster->Launch("text/html", 1, argv);
}


void
PDFWindow::LaunchInHome(const char *rel_path) {
	BPath path(*gApp->GetAppPath());
	path.Append(rel_path);
	Launch(path.Path());
}

bool
PDFWindow::FindFile(BPath* path) {
	if (path->InitCheck() != B_OK) return false;
	BString leaf(path->Leaf());
	if (leaf.Length() == 0) {
		path->SetTo("/");
		return true;
	}
	if (path->GetParent(path) != B_OK) return false;
	if (FindFile(path)) {
		BPath p(path->Path());
		path->Append(leaf.String());
		BEntry entry(path->Path());
		if (entry.Exists()) return true;

		*path = p;
		entry.SetTo(p.Path());
		BDirectory dir(&entry);
		char name[B_FILE_NAME_LENGTH];
		while (dir.GetNextEntry(&entry) == B_OK) {
			entry.GetName(name);
			if (leaf.ICompare(name) == 0) {
				path->Append(name);
				return true;
			}
		}
	}
	return false;
}

bool
PDFWindow::GetEntryRef(const char* file, entry_ref* ref) {
	BEntry entry(file);
	BPath path(file);
	if (!entry.Exists() && FindFile(&path)) {
		entry.SetTo(path.Path());
	}
	if (entry.Exists()) {
		entry.GetRef(ref); return true;
	}
	return false;
}


void
PDFWindow::Launch(const char *file)
{
	entry_ref r;
	if (GetEntryRef(file, &r)) {
		be_roster->Launch(&r);
	}
}

void
PDFWindow::OpenInWindow(const char *file)
{
	entry_ref r;
	if (GetEntryRef(file, &r)) {
		BMessage msg(B_REFS_RECEIVED);
		msg.AddRef("refs", &r);
		be_app->PostMessage(&msg);
	}
}


// #pragma mark - Left Panel


void
PDFWindow::ActivateOutlines()
{
	// mMainView->WaitForPage();
	if (mLayerView->CardLayout()->VisibleIndex() == BOOKMARKS_PANEL &&
		mShowLeftPanel) {
		mMainView->WaitForPage();
		mOutlinesView->Activate();
	}
}


void
PDFWindow::CollapseOutlinePanelIfEmpty()
{
	if (mLayerView->CardLayout()->VisibleIndex() != BOOKMARKS_PANEL)
		return;
	// hidden by the user: leave it alone
	if (!mShowLeftPanel && !mOutlineAutoCollapsed)
		return;

	mMainView->WaitForPage();
	bool empty = !mOutlinesView->HasEntries();
	if (empty == !mShowLeftPanel)
		return;

	// not a user choice, so don't store it in the settings: the panel should be
	// back for the next document that has bookmarks.
	mShowLeftPanel = !empty;
	mOutlineAutoCollapsed = empty;
	mSplitView->SetItemCollapsed(0, empty);
	if (empty)
		mSplitView->SetFlags((~B_NAVIGABLE) & mSplitView->Flags());
	else
		mSplitView->SetFlags(B_NAVIGABLE | mSplitView->Flags());
	UpdateInputEnabler();
	mMainView->Resize();
}


void
PDFWindow::ShowLeftPanel(int panel)
{
	if (!mShowLeftPanel) {
		ToggleLeftPanel();
	}
	if (mLayerView->CardLayout()->VisibleIndex() != panel) {
		gApp->GetSettings()->SetLeftPanel(panel);
		mLayerView->CardLayout()->SetVisibleItem(panel);
	}
	if (panel == BOOKMARKS_PANEL) {
		ActivateOutlines();
	}	
	UpdateInputEnabler();
}


void
PDFWindow::HideLeftPanel()
{
	if (mShowLeftPanel) {
		ToggleLeftPanel();
	}
}


void
PDFWindow::ToggleLeftPanel()
{
	mOutlineAutoCollapsed = false;
	mShowLeftPanel = !mShowLeftPanel;
	mSplitView->SetItemCollapsed(0, !mShowLeftPanel);
	gApp->GetSettings()->SetShowLeftPanel(mShowLeftPanel);
	if (mShowLeftPanel) {
		ActivateOutlines();
		mSplitView->SetFlags(B_NAVIGABLE | mSplitView->Flags());
	} else {
		mSplitView->SetFlags((~B_NAVIGABLE) & mSplitView->Flags());
	}
	UpdateInputEnabler();
	mMainView->Resize();
}


void
PDFWindow::OnFullScreen()
{
	bool quasiFullScreenMode = gApp->GetSettings()->GetQuasiFullscreenMode();
	mFullScreen = !mFullScreen;
	BRect frame;
	if (mFullScreen) {
		mWindowFrame = Frame();
		frame = gScreen->Frame();
		if (quasiFullScreenMode) {
			frame.OffsetBy(0, -fMenuBar->Bounds().Height());
			frame.bottom += fMenuBar->Bounds().Height();
		} else {
			HideLeftPanel();
			BRect bounds = mMainView->Parent()->ConvertToScreen(mMainView->Frame());
			frame.bottom += mWindowFrame.IntegerHeight() - bounds.IntegerHeight();
			frame.right += mWindowFrame.IntegerWidth() - bounds.IntegerWidth();
			frame.OffsetBy(-bounds.left+mWindowFrame.left, -bounds.top+mWindowFrame.top);
		}
		mFullScreenItem->SetMarked(true);
		SetFeel(B_FLOATING_ALL_WINDOW_FEEL);
		SetFlags(Flags() | B_NOT_RESIZABLE | B_NOT_MOVABLE);
		Activate(true);
	} else {
		SetFeel(B_NORMAL_WINDOW_FEEL);
		SetWorkspaces(B_CURRENT_WORKSPACE);
		SetFlags(Flags() & ~(B_NOT_RESIZABLE | B_NOT_MOVABLE));
		frame = mWindowFrame;
		mFullScreenItem->SetMarked(false);
	}
	MoveTo(frame.left, frame.top);
	ResizeTo(frame.Width(), frame.Height());

	UpdateInputEnabler();
}

//~ this is very restrictive: it assumes that the window is only set in one workspace
void PDFWindow::WorkspaceActivated(int32 workspace, bool active) {
#ifdef MORE_DEBUG
	fprintf(stderr, "%s %d %s %d %d\n",
		mFullScreen ? "fullscreen" : "window",
		workspace,
		active ? "active" : "not active",
		mCurrentWorkspace, Workspaces());
#endif
	if (mFullScreen) {
		if (mCurrentWorkspace == 1 << workspace) {
			SetFeel(B_FLOATING_ALL_WINDOW_FEEL);
		} else {
			SetFeel(B_NORMAL_WINDOW_FEEL);
			SetWorkspaces(mCurrentWorkspace);
		}
	} else if (active) {
		mCurrentWorkspace = 1 << workspace;
	}
}

// #pragma mark - User-defined bookmarks

void PDFWindow::AddUserBookmark()
{
	char buffer[256];
	sprintf(buffer, B_TRANSLATE("Page %d"), mMainView->Page());
	new BookmarkWindow(mMainView->Page(), buffer, BRect(30, 30, 300, 200), this);
}

void PDFWindow::DeleteUserBookmark() {
	mOutlinesView->RemoveUserBookmark(mMainView->Page());
	SaveUserBookmarks();
	UpdateInputEnabler();
}

void PDFWindow::EditUserBookmark() {
	const char *label = mOutlinesView->GetUserBMLabel(mMainView->Page());
	if (label) {
		new BookmarkWindow(mMainView->Page(), label, BRect(30, 30, 300, 200), this);
	} else {
		// should not reach here
	}
}

void PDFWindow::SaveUserBookmarks()
{
	if (mCurrentFile.InitCheck() != B_OK)
		return;
	
	BMessage bm;
	if (!mOutlinesView->GetBookmarks(&bm))
		return;
	
	entry_ref cur_ref;
	mCurrentFile.GetRef(&cur_ref);
	
	mFileAttributes.SetBookmarks(&bm);
	mFileAttributes.Write(&cur_ref, gApp->GetSettings());
}

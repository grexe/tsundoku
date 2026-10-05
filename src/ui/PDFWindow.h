/*
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

#ifndef _PDF_WINDOW_
#define _PDF_WINDOW_


#include <be/interface/ListView.h>
#include <be/interface/OutlineListView.h>
#include <be/interface/StringItem.h>
#include <vector>
#include <be/interface/PictureButton.h>
#include <be/interface/TextControl.h>
#include <be/storage/Entry.h>
#include <be/storage/Path.h>
#include <private/shared/ToolBar.h>
#include <SplitView.h>
#include <CardView.h>

// BePDF
#include "EntryChangedMonitor.h"
#include "FindTextWindow.h"
#include "PDFView.h"
#include "ToolTip.h"

class AnnotationsView;
class SidebarTabView;
class AttachmentsView;
class BFilePanel;
class OutlinesView;

class RecentDocumentsMenu : public BMenu
{
public:
	RecentDocumentsMenu(const char *title, uint32 what, menu_layout layout = B_ITEMS_IN_COLUMN);
	bool AddDynamicItem(add_state s);

private:
	void UpdateRecentDocumentsMenu();
	uint32 fWhat;
};


// An entry of the page list: a page, or a chapter (which stands for its first page)
class PageListItem : public BStringItem {
public:
	PageListItem(const char* label, uint32 level, int page, bool isChapter)
		:
		BStringItem(label, level, false),
		fPage(page),
		fIsChapter(isChapter)
	{
	}

	int  Page() const { return fPage; }
	bool IsChapter() const { return fIsChapter; }

private:
	int  fPage;
	bool fIsChapter;
};


class PDFWindow
	: public BWindow
	, public EntryChangedListener
{
public:
	enum {
		// File
		OPEN_FILE_CMD = 'PDFW',
		OPEN_IN_NEW_WINDOW_CMD,
		NEW_WINDOW_CMD,
		CLOSE_FILE_CMD,
		RELOAD_FILE_CMD,
		SAVE_FILE_CMD,
		UNDO_CMD,
		ADD_ANNOTATION_CMD,
		REDO_CMD,
		SAVE_AS_FILE_CMD,
		ANNOTATE_HIGHLIGHT_CMD,
		ANNOTATE_UNDERLINE_CMD,
		ANNOTATE_STRIKEOUT_CMD,
		SAVE_FILE_AS_CMD,
		QUIT_APP_CMD,
		ABOUT_APP_CMD,
		KEYBOARD_SHORTCUTS_CMD,
		TYPE3_FONT_RENDERER_CMD,
		PREFERENCES_FILE_CMD,
		FILE_INFO_CMD,
			// Printing
			PAGESETUP_FILE_CMD,
			PRINT_SETTINGS_CMD,
			// PRINT_FILE_CMD,

		// Edit
		COPY_SELECTION_CMD,
		SELECT_ALL_CMD,
		SELECT_NONE_CMD,
		COPY_LINK_CMD,

		// Zoom
		SET_ZOOM_VALUE_CMD,
		SET_CUSTOM_ZOOM_FACTOR_CMD,
		ZOOM_IN_CMD,
		ZOOM_OUT_CMD,
		FIT_TO_PAGE_WIDTH_CMD,
		FIT_TO_PAGE_CMD,
		FLOW_SINGLE_CMD,
		FLOW_DOUBLE_CMD,
		FLOW_CONTINUOUS_CMD,
		TITLE_PAGE_ALONE_CMD,
		RIGHT_TO_LEFT_CMD,
		TOP_TO_BOTTOM_CMD,
		TEXT_LARGER_CMD,
		TEXT_SMALLER_CMD,

		// Page
		FIRST_PAGE_CMD,
		NEXT_N_PAGE_CMD,
		NEXT_PAGE_CMD,
		PREVIOUS_PAGE_CMD,
		PREVIOUS_N_PAGE_CMD,
		LAST_PAGE_CMD,
		GOTO_PAGE_CMD,
		SHOW_TARGET_CMD,
		GOTO_PAGE_MENU_CMD,
		HISTORY_BACK_CMD,
		HISTORY_FORWARD_CMD,
		PAGE_SELECTED_CMD,

		// Rotation
		SET_ROTATE_VALUE_CMD,
		ROTATE_CLOCKWISE_CMD,
		ROTATE_ANTI_CLOCKWISE_CMD,
		SHOW_TRACER_CMD,

		// Search
		FIND_CMD,
		FIND_NEXT_CMD,
		FIND_PREVIOUS_CMD,

		// User defined bookmarks
		ADD_USER_BOOKMARK_CMD,
		DELETE_USER_BOOKMARK_CMD,
		EDIT_USER_BOOKMARK_CMD,

		// Help
		HELP_CMD,
		ONLINE_HELP_CMD,
		BUG_REPORT_CMD,
		HOME_PAGE_CMD,
		CHECK_FOR_UPDATE_CMD,
		// Type 1 Font Renderer
		T1_OFF,
		T1_ON,
		T1_AA,
		T1_AA_HIGH,
		// Freetype Font Renderer
		FT_OFF,
		FT_ON,
		FT_AA,
		// show/hide Page List
		SHOW_BOOKMARKS_CMD,
		SHOW_PAGE_LIST_CMD,
		SHOW_ATTACHMENTS_CMD,
		SHOW_ANNOTATIONS_CMD,
		SHOW_ANNOTATION_CMD,   // "page" and "index", or "id": goes to an annotation and selects it
		HIDE_LEFT_PANEL_CMD,
		// full screen
		FULL_SCREEN_CMD,
	};

	enum {
		TOOLBAR_HEIGHT = 30,
		TOOLBAR_WIDTH = 30
	};

	// active view in left panel
	enum {
		BOOKMARKS_PANEL = 0,
		PAGE_LIST_PANEL,
		ATTACHMENTS_PANEL,
		ANNOTATIONS_PANEL,
	};

	// pending mask
	enum {
		UPDATE_PAGE_LIST_PENDING =    1 << 0,
		UPDATE_OUTLINE_LIST_PENDING = 1 << 1,
		FILE_INFO_PENDING =           1 << 2,
		PRINT_SETTINGS_PENDING =      1 << 3,
	};

private:
	BEntry         mCurrentFile;
	FileAttributes mFileAttributes;
	EntryChangedMonitor mEntryChangedMonitor;

	BToolBar		*mToolBar;
	BTextControl   *mPageNumberItem;
	BStringView    *mTotalPageNumberItem;
	BSplitView*		mSplitView;
	PDFView*		mMainView;
	BHandler*		mScripting;	// the document for the scripting suite (Scripting.h)
	BView*			fMainContainer;
	SidebarTabView*	mLayerView;
	BOutlineListView *mPagesView;
	std::vector<PageListItem*> mPageItems;      // by page (0-based), owned by the list
	std::vector<PageListItem*> mChapterItems;   // by chapter, empty if the document is not in chapters
	OutlinesView   *mOutlinesView;
	AttachmentsView *mAttachmentsView;
	AnnotationsView *mAnnotationsView;
	BFilePanel     *mSavePanel;
	bool           mCloseAfterSave;	// the copy is written because the window is closed, then it closes
	bool           mChangesKept;	// the changes are in a copy, closing needs no question

	BMessage       *mPrintSettings;
	FindTextWindow *mFindWindow;
	BMenuBar*		fMenuBar;
	BMenuItem      *mPreferencesItem, *mFileInfoItem, // *mPrintSettingsItem,
	               *mFullScreenItem;
	BMenu          *mOpenMenu, *mNewMenu, *mWindowsMenu;

	uint32         mFindState;
	BString        mFindText;
	bool           mFindInProgress;

	BMenu          *mZoomMenu,
	               *mRotationMenu;
	BMessenger     *mOWMessenger;  // outlines window messenger
	BMessenger     *mFIWMessenger; // file info window messenger
	BMessenger     *mPSWMessenger; // printing settings window messenger
	bool           mPrintSettingsWindowOpen;

	bool           mShowLeftPanel;
	// panel was collapsed because there are no bookmarks, not by the user's choice
	bool           mOutlineAutoCollapsed;

	BRect          mWindowFrame;
	bool           mFullScreen;
	int32          mCurrentWorkspace;

	uint32         mPendingMask;

public:
	PDFWindow (entry_ref* ref, BRect frame, const char *ownerPassword,
		const char *userPassword, bool *encrypted);
	virtual ~PDFWindow();

	virtual bool QuitRequested();
	virtual	bool CanClose();

	// scripting (Scripting.h)
	virtual status_t GetSupportedSuites(BMessage* data);
	virtual BHandler* ResolveSpecifier(BMessage* message, int32 index, BMessage* specifier, int32 what,
		const char* property);
	PDFView* View() { return mMainView; }
	OutlinesView* BookmarkList() { return mOutlinesView; }
	// the bookmarks were changed from outside the sidebar
	void BookmarksChanged() { SaveUserBookmarks(); }
	bool IsOk();
	BMenuBar* BuildMenu();
	BToolBar* BuildToolBar();
	SidebarTabView* BuildLeftPanel();
	void SetUpViews (entry_ref * ref, const char *ownerPassword, const char *userPassword, bool *encrypted);
	void CleanUpBeforeLoad();
	bool IsCurrentFile(entry_ref* ref) const;
	bool LoadFile(entry_ref *ref, const char *ownerPassword, const char *userPassword, bool *encrypted);
	void Reload(void);
	void EntryChanged();
	void StoreFileAttributes();
	FileAttributes *GetFileAttributes() { return &mFileAttributes; };

	virtual void FrameMoved (BPoint p);
	virtual void FrameResized (float width, float height);
	virtual void MessageReceived (BMessage * message);
	void SetZoomSize (float w, float h);
	void SetZoom(int16 zoom);
	void SetRotation(float rotation);
	void SetPage(int32 page);
	// keeps the window on the screen
	void FitToScreen();

	static void OpenPDF(const char* file);
	static bool OpenPDFHelp(const char* name);
	static void OpenHelp();
	static void LaunchHTMLBrowser(const char *file);
	static void LaunchInHome(const char *rel_path);
	static bool FindFile(BPath* path);
	static bool GetEntryRef(const char* file, entry_ref* ref);
	static void Launch(const char *file);
	static void OpenInWindow(const char* file);

	// hook function
	void NewDoc(Document *doc);
	void NewPage(int page);

	enum {
		PAGE_CHANGE_NOTIFY_MSG = 'Page',	/*message of page change notification*/
		CUSTOM_ZOOM_FACTOR_MSG = 'CuZo'
	};

	static char * PAGE_MSG_LABEL;

	const BEntry* CurrentFile() const { return &mCurrentFile; }
	void UpdateInputEnabler();
	// the annotations of the document have changed: the list of them is read again
	// a page (or all of them, when 0) has other annotations
	void AnnotationsChanged(int page = 0);
	// a reflowable document has other pages now: the lists and the numbers are made anew
	void TextSizeChanged();

	void UpdateWindowsMenu();

	void ClearPending()          { mPendingMask = 0; }
	bool IsPending(uint32 mask)  { return (mPendingMask & mask) != 0; }
	void SetPending(uint32 mask) { mPendingMask |= mask; }
	bool SetPendingIfLocked(uint32 mask);

	void FillPageList();
	void UpdatePageList();
	// selects the page in the page list (and opens its chapter)
	void SelectInPageList(int page);
	// asks what to do with unsaved changes; false if the user wants to keep working on the document
	bool ConfirmDiscardChanges(bool closing = false);
	void SaveDocument();
	void SaveDocumentAs();
	void SaveCopyTo(const char* path);
	void HandlePendingActions(bool ok);


protected:
	bool CancelCommand(BMessage* msg);
	void Find(const char *s);
	void AddItem(BMenu *subMenu, const char *label, uint32 cmd, bool marked, char shortcut = 0, uint32 modifiers = 0);

	void ActivateOutlines();
	// after loading a document: collapse the bookmarks panel if there are neither
	// document nor user bookmarks, bring it back if the new document has some
	void CollapseOutlinePanelIfEmpty();

	bool ActivateWindow(BMessenger *messenger);
	void WorkspaceActivated(int32 workspace, bool active);
	void SetTotalPageNumber(int pages);
	void InitAfterOpen();

	void ToggleLeftPanel(); // show / hide panel
	void ShowLeftPanel(int panel);
	void HideLeftPanel();

	void OnFullScreen();

	// User defined bookmarks
	void AddUserBookmark();
	void DeleteUserBookmark();
	void EditUserBookmark();
	void SaveUserBookmarks();
};


//////////////////////////////////////////////////////////////////
inline bool PDFWindow::IsOk()
{
	if (mMainView != NULL) {
		return mMainView->IsOk();
	}
	else {
		return false;
	}
}




//////////////////////////////////////////////////////////////////


#endif

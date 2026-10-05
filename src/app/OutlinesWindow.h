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

#ifndef OUTLINES_WINDOW_H
#define OUTLINES_WINDOW_H

// BeOS
#include <Font.h>
#include <Looper.h>
#include <List.h>
#include <SupportDefs.h>

#include <vector>

#include "Document.h"
#include "LayoutUtils.h" // for Bitset
#include "Settings.h"

class OutlineStyle {
	const BFont* mFont;
	rgb_color mColor;

public:
	OutlineStyle(const BFont* font, rgb_color color);
	const BFont* GetFont() const { return mFont; }
	const rgb_color* GetColor() const { return &mColor; }
};

class OutlineStyleList {
	BList mList; // of OutlineStyle
	BFont mFonts[4];

	static void Initialize();
public:
	OutlineStyleList();
	~OutlineStyleList();

	enum {
		PLAIN_STYLE,
		BOLD_STYLE,
		ITALIC_STYLE,
		BOLD_ITALIC_STYLE
	};

	const BFont* GetFont(int style) const;
	OutlineStyle* GetStyle(int style, rgb_color color);
	OutlineStyle* GetDefaultStyle();
};

class OutlineListItem : public BListItem {
	BString mString;
	enum {
		linkDest,       // an entry of the outline of the document: a page and maybe a position
		linkPageNum,    // a bookmark of the user
		linkString,     // somewhere else
		linkUndefined
	} mType;
	BString       mLink;
	struct {
		int   page;
		float x, y;
		bool  hasPosition;
	} mDest;
	int           mPageNum;
	OutlineStyle *mStyle;
	int           mResolvedPage;     // page this entry points to, 0 if unknown
	BMessage*     mAnchor;           // a bookmark in a book: its place in the text (TextAnchor), NULL if none

public:
	OutlineListItem(const char *string, uint32 level, bool expanded, OutlineStyle* style);
	virtual ~OutlineListItem();
	const char* Text() const { return mString.String(); }
	void SetStyle(OutlineStyle* style) { mStyle = style; }
	void SetDest(int page, float x, float y, bool hasPosition);
	void SetLink(const char* link);
	void SetPageNum(int pageNum);
	// the place in the text that a bookmark in a book stands for, it is found again when the pages change
	void SetAnchor(const BMessage* anchor);
	const BMessage* Anchor() const { return mAnchor; }

	void DrawItem(BView* owner, BRect frame, bool complete);

	bool isDest() const        { return mType == linkDest; }
	bool isString() const      { return mType == linkString; }
	bool isPageNum() const     { return mType == linkPageNum; }
	int getDestPage() const    { return mDest.page; }
	float getDestX() const     { return mDest.x; }
	float getDestY() const     { return mDest.y; }
	bool hasDestPosition() const { return mDest.hasPosition; }
	const char* getString() const { return mLink.String(); }
	int getPageNum() const     { return mPageNum; }
	void SetResolvedPage(int pageNum) { mResolvedPage = pageNum; }
	int GetResolvedPage() const       { return mResolvedPage; }
};

class OutlinesView : public BScrollView {
	BLooper          *mLooper;
	OutlineStyleList  mOutlineStyleList;
	BOutlineListView *mList;
	Document         *mDocument;
	BMessage         *mBookmarks;    // archived bookmarks
	bool              mNeedsUpdate;
	OutlineListItem  *mUserDefined;
	OutlineListItem  *mEmptyUserBM;  // cached value
	Bitset            mBookmark;
	bool              mHasDocumentOutline;
	int               mCurrentPage;  // last page requested via SelectPage(), 0 if none

	void ReadOutlines(const std::vector<DocOutlineEntry>& entries);
	bool HasUserBookmarks();
	OutlineListItem* FindUserBookmark(int pageNum);
	void InsertUserBookmark(int pageNum, const char *label);
	void InitUserBookmarks(bool initOnly);
	OutlineStyle* GetDefaultStyle() { return mOutlineStyleList.GetDefaultStyle(); }

public:
	// message sent to mLooper has this fields:
	enum {
		// what                          attribute(s):
		PAGE_NOTIFY         = 'OWPg', // "page", and "x" and "y" (page space) if the entry has a position
		QUIT_NOTIFY         = 'ORQt',
		STATE_CHANGE_NOTIFY = 'OWCg'
	};

	OutlinesView(Document *document, BMessage *bookmarks, GlobalSettings *settings, BLooper *looper, uint32 flags);
	~OutlinesView();
	void AttachedToWindow();
	void MessageReceived(BMessage *msg);

	void SetDocument(Document *document, BMessage* bookmarks);
	// the pages of the document have changed (a book with another text size): the entries are made again, the
	// bookmarks of a book find their places in the text
	void Reload(BMessage* bookmarks);
	bool HasUserBookmark(int pageNum);
	bool IsUserBMSelected();
	const char *GetUserBMLabel(int pageNum);
	void AddUserBookmark(int pageNum, const char *label, const BMessage* anchor = NULL);
	void RemoveUserBookmark(int pageNum);
	// fills BMessage with bookmarks to be stored in FileAttributes
	bool GetBookmarks(BMessage *bookmarks);

	void Activate();

	// selects and scrolls to the outline entry (chapter) the given page belongs to,
	// without navigating to it.
	void SelectPage(int pageNum);
	// true if the document has an outline or the user has defined bookmarks.
	bool HasEntries();
};

class BTextControl;

class BookmarkWindow : public BWindow {
public:
	BookmarkWindow(int pageNum, const char* title, BRect rect, BLooper *looper);
	void MessageReceived(BMessage *msg);
	bool QuitRequested();

	enum {
		BOOKMARK_ENTERED_NOTIFY = 'BMEt'
	};
protected:
	BLooper      *mLooper;
	BTextControl *mTitle;
	int           mPageNum;
};


#endif

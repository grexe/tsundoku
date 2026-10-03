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


#ifndef _PDFVIEW_H_
#define _PDFVIEW_H_

#include <vector>

#include <be/interface/Bitmap.h>
#include <be/interface/Menu.h>
#include <be/interface/View.h>
#include <String.h>

#include "Document.h"
#include "History.h"
#include "FindTextWindow.h"
#include "PageLayout.h"
#include "PageRenderer.h"
#include "Settings.h"

class PDFWindow;
class CachedPage;
class FileAttributes;

#define MIN_ZOOM	0
#define MAX_ZOOM	10

#define ZOOM_DPI_MIN  29
// 360 / 72 = 500%
#define ZOOM_DPI_MAX 360

inline float RealSize (float x, float zoomDPI)
{
	return zoomDPI / 72 * x;
}

// A page that is shown: what is rendered of it, the thread that renders it and where it is.
struct PageSlot {
	PageSlot();
	~PageSlot();

	int           number;       // the page, 0 if the slot is free
	CachedPage*   page;
	PageRenderer* renderer;
	thread_id     rendererId;   // -1 if it is not rendering
	bool          rendering;
	BPoint        origin;       // top left of the page in the coordinates of the view
};

class PDFView
	: public BView
{
private:
	bool mLoading;
	Document * mDoc;
	bool mOk;
	int mZoom;

	// What is shown is a number of slots (one page, two, or those that are in view of a long run of pages), laid out
	// by mLayout. One of them is the active page: all that works with a page (selecting, links, annotations, tools)
	// works with it, and the mouse makes the page it is on the active one. mPage, mBitmap, mWidth, mHeight, mLeft
	// and mTop are those of the active page.
	PageLayout mLayout;
	std::vector<PageSlot*> mSlots;       // in use, in the order of the pages
	std::vector<PageSlot*> mFreeSlots;   // to be used again, with the memory of their bitmaps
	PageSlot* mActive;
	int mInteractionPage;                // the page that the selections and the like belong to
	float mCanvasLeft, mCanvasTop;       // where the pages start in the view if they are smaller than the view
	float mCanvasWidth, mCanvasHeight;   // what the view scrolls over
	BBitmap * mBitmap;
	CachedPage *mPage;
	int mCurrentPage;                    // the page of the page box: the page, the left page of a spread, the page at the top
	float mRotation;
	BString *mOwnerPassword;
	BString *mUserPassword;

	color_space mColorSpace;

	bool mInvertVerticalScrolling;

	BString *mTitle;
	float mLeft, mTop;	// position of page inside the view
	float mWidth, mHeight;		//document width and height
	const DocLink *mLink;      // link under the mouse
	int mNoteTip;              // the annotation (index + 1) whose note is shown as a tooltip, 0 for none
	bool mSelectKeyDown;       // the key for selecting was down when the cursor was set last
	bool mReadOnlyWarned;      // the user knows that changes cannot be saved to the file itself
	BMessageRunner* mModifierRunner;  // watches the keys for the cursor
	History mHistory;
	enum {
		kNotInHistory, kInHistory
	} mNavigationState;

	BCursor *mViewCursor;
	enum mouse_action {
		NO_ACTION,
		MOVE_ACTION,
		SELECT_ACTION,
		DND_ACTION,
		ZOOM_ACTION,
		SECONDARY_ACTION, // a click opens the menu, moving starts to drag the selection
		TOOL_ACTION,      // drawing a shape (see PlacementTool)
		ANNOT_ACTION      // moving or resizing the selected annotation
	} mMouseAction;
	BPoint mMousePosition;
	BPoint mSecondaryStart;

public:
	// What the next click or drag on the page creates; after that the tool is gone, so there is no mode to
	// leave. Escape cancels it.
	enum PlacementTool {
		kToolNone,
		kToolNote,
		kToolFreeText,
		kToolRectangle,
		kToolEllipse,
		kToolLine,
		kToolArrow,
		kToolInk
	};

private:
	PlacementTool mTool;
	BPoint mToolStart, mToolEnd;       // in the bitmap
	std::vector<BPoint> mToolPoints;   // the path of a drawing
	BCursor* mToolCursor;

	// The annotation that is selected by a click: it has handles, can be moved and resized, and deleted with the
	// Delete key. The index is that of DocAnnotation, -1 for none.
	enum {
		kHandleNone = -1,
		kHandleMove = 0,
		kHandleNorthWest, kHandleNorth, kHandleNorthEast, kHandleEast,
		kHandleSouthEast, kHandleSouth, kHandleSouthWest, kHandleWest,
		kHandleCount
	};
	int mAnnotationIndex;
	int mAnnotationHandle;          // which one is being dragged
	BPoint mAnnotationDragStart;    // in the bitmap
	BRect mAnnotationOriginal;      // in the bitmap, as it was when the drag began
	BRect mAnnotationPreview;       // where it will be
	BCursor* mHandleCursors[kHandleCount];
	bool mDragStarted;

	float mMouseWheelDY;
	enum {
		MOUSE_WHEEL_THRESHHOLD = 2
	};

	bool mRendering;                     // the active page is rendered

	enum {
		NOT_SELECTED = 0,
		DO_SELECTION = 1,
		SELECTED = 2
	} mSelected;
	// What is selected: text that follows the flow of the text between two points, a rectangle (also for
	// copying an image), or the places where text has been found.
	enum {
		kSelectText,
		kSelectArea
	} mSelectionKind;
	bool mFilledSelection;

	// text selection: end points in page space and the area they cover (page space)
	fz_point mTextStart, mTextEnd;
	std::vector<fz_quad> mQuads;
	// area selection (and zoom to selection): in coordinates of the bitmap
	BPoint mSelectionStart;
	BRect mSelection;

	BMessage * mPrintSettings;

	// find: where the last hit was, to go on after it
	bool mStopFindThread;
	int mFindPage, mFindIndex;
	BString mFindNeedle;
	bool mFindCaseSensitive;
	// all hits of the search on the shown page (page space), shown until cleared
	bool mFindHighlight;
	// the page that was last started to render, a selection stays as long as it is the same
	int mRenderedPage;

	BPoint CorrectMousePos(const BPoint point);
	void OnMouseWheelChanged(BMessage *msg);

	PDFWindow* GetPDFWindow();

	// selection of text
	void StartTextSelection(BPoint point);
	void ExtendTextSelection(BPoint point);
	bool SelectTextAt(BPoint point, int mode);
	void UpdateQuads(bool invalidate);
	bool InTextSelection(BPoint point);
	bool InSelection(BPoint point);
	BRect SelectionBounds();

public:
	PDFView(entry_ref* ref, FileAttributes *fileAttributs,
		const char *name, uint32 flags, const char *ownerPassword,
		const char *userPassword, bool *encrypted);
	virtual ~PDFView();

	void SetPassword(const char *owner, const char *user);

	void EndDoc();

	void UpdatePanelDirectory(BPath* path);
	void MakeTitleString(BPath* path);

	bool OpenFile(entry_ref *ref, const char *ownerPassword, const char *userPassword, bool *encrypted);
	bool LoadFile(entry_ref *ref, FileAttributes *fileAttributs, const char *ownerPassword, const char *userPassword, bool init, bool *encrypted);
	void SetViewCursor(BCursor *cursor, bool sync = true);
	void LoadFileSettings(entry_ref* ref, FileAttributes *fileAttributes, float& left, float& top);

	void RestoreWindowFrame(BWindow* w);

	bool InPage(BPoint p);  // NOT USED
	BPoint LimitToPage(BPoint p);

	void DrawPage(BRect updateRect);
	void DrawBackground(BRect updateRect);
	void DrawSelection(BRect updateRect);
	void DrawFindHits(BRect updateRect);
	void UpdateFindQuadsOfAll();
	void ClearFindHighlights();
	virtual	void Draw (BRect updateRect);

	virtual void FrameResized (float width, float height);
	virtual void AttachedToWindow ();

	void SkipMouseMoveMsgs();
	virtual void KeyDown (const char * bytes, int32 numBytes);
	void SetAction(mouse_action action);

	uint32 GetButtons();
	void BeginSelection(BPoint point, bool rectangle);
	virtual void MouseDown (BPoint point);
	void ScrollIfOutside (BPoint point);
	void ResizeSelection (BPoint point);
	void InitViewCursor(uint32 transit);
	virtual void MouseMoved (BPoint point, uint32 transit, const BMessage *msg);
	virtual void MouseUp (BPoint point);
	virtual void ScrollTo (BPoint point);
	void ScrollTo(float x, float y);
	virtual void MessageReceived(BMessage *msg);
	const DocLink* OnLink(BPoint p);
	const DocAnnotation* OnAnnotation(BPoint p);
	void LinkToString(const DocLink* link, BString* string);
	void ShowPopUpMenu(BPoint point, const DocLink* link, const DocAnnotation* annotation);
	// shows the changed annotations, on another page if the change was there
	void AnnotationsChanged(int page = 0);

	// slots, layout and the active page
	PageSlot* NewSlot();
	void ReleaseSlot(PageSlot* slot);
	void SetSlotsDocument(Document* document);
	PageSlot* SlotForPage(int page) const;
	PageSlot* SlotAt(BPoint point) const;
	void SetActiveRaw(PageSlot* slot);   // only changes what the members stand for
	void ActivateSlot(PageSlot* slot);   // the page of a click: its selections and what is of another page end
	int ActivePage() const { return mActive != NULL && mActive->number > 0 ? mActive->number : mCurrentPage; }
	bool PageShown(int page) const { return SlotForPage(page) != NULL; }
	void Relayout();                     // sizes, canvas, places of the slots, scroll bars
	void SyncSlots();                    // slots for the pages the layout needs
	void StartRender(PageSlot* slot);
	void UpdateVisibleSlots();           // after scrolling in a continuous flow
	void NotifyPageChanged();
	void ScrollToPage(int page, bool top);
	void UpdateFindQuads(PageSlot* slot);

	// Makes the slot the one the members stand for as long as it lives (hovering over another page, drawing it).
	class SlotScope {
	public:
		SlotScope(PDFView* view, PageSlot* slot);
		~SlotScope();
	private:
		PDFView* fView;
		PageSlot* fSaved;
	};
	friend class SlotScope;
	void BeginTool(BPoint point);
	void FinishTool(BPoint point);
	void CancelTool();
	void AskForText(PlacementTool tool, fz_point position);
	const DocAnnotation* SelectedAnnotation() const;
	BRect AnnotationDeviceRect(const DocAnnotation* annotation) const;
	int HandleAt(const DocAnnotation* annotation, BPoint point) const;
	void SelectAnnotation(int index);
	bool BeginAnnotationDrag(BPoint point);
	void FinishAnnotationDrag();
	void DrawAnnotationSelection();
	void DrawToolPreview();
	BRect ToolBounds() const;
	// before the first change of a read-only file: tells the user, false if the user does not want to go on
	bool ConfirmEditable();
	void CopyText(BString *str);
	bool IsOk() { return mOk; }

	void SetPage (int page);

	void MoveToPage (int page, bool top = true);
	int Page()      { return mCurrentPage; } ;

	// how the pages are arranged: one, a spread, or all of them below each other
	void SetFlow(PageFlow flow);
	// the title page is shown alone, also when two pages or more are shown at a time
	void SetTitlePageAlone(bool alone);
	bool TitlePageAlone() const { return mLayout.FirstPageAlone(); }
	PageFlow Flow() const { return mLayout.Flow(); }
	// a step to the next or the previous page (a spread in the double flow)
	void NextPage();
	void PreviousPage();

	// history
	void BeginHistoryNavigation();
	void EndHistoryNavigation();
	void RecordHistory();
	void RecordHistory(entry_ref ref, const char* owner, const char* user);
	void RestoreHistory();
	void Back();
	void Forward();
	bool CanGoBack()    { return mHistory.CanGoBack(); }
	bool CanGoForward() { return mHistory.CanGoForward(); }

	void SetZoom ( int zoom );
	void Zoom(bool zoomIn);
	void FitToPageWidth();
	void FitToPage();

	int16 GetZoomDPI() const;
	void SetRotation ( float rot );
	void RotateClockwise();
	void RotateAntiClockwise();
	void Redraw();
	void PostRedraw(thread_id id, BBitmap *bitmap);
	void RedrawAborted(thread_id id, BBitmap *bitmap);
	void WaitForPage(bool abort = false);
	// Rerender this page
	void RestartDoc();

	// called when size of window changes
	void Resize();
	void CenterPage();
	void FixScrollbars ();

	int GetNumPages() 		    { return mDoc->PageCount(); };

	status_t PageSetup();
	void Print();
	void SetPrintingDpi(int dpi);

	// follows a link; the position is for a link inside of the document
	bool HandleLink(BPoint point);
	bool IsLinkToDocument(const DocLink* link, BString* path);
	// goes to the page and scrolls to the position (page space, may be NaN)
	void GotoPosition(int page, float x, float y);
	void DisplayLink(BPoint point);

	void Find(const char *s, bool ignoreCase, bool backward, FindTextWindow *findWindow);
	void StopFind();

	void SelectionChanged();
	// Selects the text found by a search (from start to end in page space of the current page), scrolls to it.
	void SelectFound(fz_point start, fz_point end);
	void CopySelection();
	void SelectAll();
	void SelectNone();
	bool HasTextSelection() const { return mSelected == SELECTED && mSelectionKind == kSelectText; }
	// marks the selected text in the document, rgb is 0xRRGGBB
	bool AnnotateSelection(MarkupType type, uint32 rgb);
	void Undo();
	void Redo();
	// Prepares to create an annotation. A note or text goes to the position if there is one (page space), a
	// shape is drawn with the next drag.
	void SetTool(PlacementTool tool, const fz_point* position = NULL);
	bool HasTool() const { return mTool != kToolNone; }
	bool HasAnnotationSelected() const { return SelectedAnnotation() != NULL; }
	void DeleteSelectedAnnotation();
	// goes to the annotation (page and index as in DocAnnotation) and shows it: a mark on text is selected, the
	// others get their handles. Runs in the thread of the window.
	void ShowAnnotation(int page, int index);
	// finds a quoted passage, selects it and shows it (see PDFSearch.cpp)
	bool ShowQuote(const char* quote, int page, bool annotate);
	void SetFilledSelection(bool filled);

	// caller must delete returned string object
	BString *GetSelectedText();
	void SendDragMessage(uint32 protocol);
	void SendDataMessage(BMessage *msg);

	void ScrollVertical(bool down, float by);
	void ScrollHorizontal(bool right, float by);

	void SetColorSpace(color_space colorSpace);

	void SetInvertVerticalScrolling(bool reverse) { mInvertVerticalScrolling = reverse; }

	Document* GetDocument() { return mDoc; }
	CachedPage* GetPage() { return mPage; }
	bool HasSelection() { return mSelected == SELECTED; }

	void UpdateSettings(GlobalSettings* settings);

	friend class PrintView;
	friend class FindThread;

#ifdef TSUNDOKU_TESTING
	// drives the view without mouse and keyboard, see PDFView.cpp
	void TestCommand(BMessage* message);
#endif
};

#endif

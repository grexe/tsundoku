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


#include <stdarg.h>
#include <stdio.h>
#include <math.h>

// BeOS
#include <locale/Catalog.h>

#include <be/app/Application.h>
#include <be/app/Clipboard.h>
#include <be/app/Looper.h>
#include <be/app/MessageRunner.h>
#include <be/app/MessageQueue.h>
#include <be/app/Roster.h>

#include <be/interface/Button.h>
#include <be/interface/ScrollBar.h>
#include <be/interface/PrintJob.h>
#include <be/interface/Alert.h>
#include <be/interface/StringView.h>
#include <be/interface/PopUpMenu.h>
#include <be/interface/MenuItem.h>

#include <be/storage/Path.h>
#include <be/storage/Entry.h>
#include <be/storage/Directory.h>
#include <be/storage/File.h>
#include <be/storage/NodeInfo.h>
#include <be/translation/BitmapStream.h>
#include <be/translation/TranslatorFormats.h>
#include <be/translation/TranslationUtils.h>
#include <be/translation/TranslatorRoster.h>
#include <be/support/String.h>
#include <be/support/Debug.h>
#include <be/support/Beep.h>

// BePDF
#include "Globals.h"
#include "Application.h"
#include "CachedPage.h"
#include "FileInfoWindow.h"
#include "FindTextWindow.h"
#include "NoteWindow.h"
#include "PageRenderer.h"
#include "PDFWindow.h"
#include "PDFView.h"
#include "PrintingProgressWindow.h"
#include "ResourceLoader.h"
#include "StatusWindow.h"

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "PDFView"


// zoom factor is 1.2 (similar to DVI magsteps)
static const int kZoomDPI[MAX_ZOOM - MIN_ZOOM + 1] = {
	18, 24, 36, 48, 54,
	72,
	90, 108, 127, 144, 216
};

#define OPEN_FILE_MSG                  'open'
#define COPY_LINK_MSG                  'cplk'
#define COPY_SELECTION_MSG             'cpsl'
#define SELECT_ALL_MSG                 'slal'
#define ANNOTATE_MSG                   'anno'
#define DELETE_ANNOTATION_MSG          'dlan'
#define EDIT_NOTE_MSG                  'ednt'
#define NOTE_ENTERED_MSG               'ntnt'
#define CHANGE_COLOR_MSG               'chcl'
#define MODIFIERS_POLL_MSG             'mdfy'

static bool SelectModifierDown();

// the colors offered for marking text
static const struct { const char* name; uint32 rgb; } kMarkerColors[] = {
	{ B_TRANSLATE_MARK("Yellow"), 0xffeb3b }, { B_TRANSLATE_MARK("Green"), 0x96e678 },
	{ B_TRANSLATE_MARK("Blue"), 0x78beff }, { B_TRANSLATE_MARK("Pink"), 0xff96c8 },
	{ B_TRANSLATE_MARK("Orange"), 0xffb95a }, { B_TRANSLATE_MARK("Red"), 0xe53935 }
};
static const int kMarkerColorCount = sizeof(kMarkerColors) / sizeof(kMarkerColors[0]);

// the color around the page, a little darker than the panels of the system
static rgb_color
DesktopColor()
{
	return tint_color(ui_color(B_PANEL_BACKGROUND_COLOR), B_DARKEN_3_TINT);
}

static const float kGap = 6;

// A menu item with a box of a color in front of the label, to the right of the check mark.
class ColorMenuItem : public BMenuItem {
public:
	ColorMenuItem(const char* label, uint32 rgb, BMessage* message)
		:
		BMenuItem(label, message),
		fColor(rgb)
	{
	}

	virtual void GetContentSize(float* width, float* height)
	{
		BMenuItem::GetContentSize(width, height);
		*width += BoxWidth() + kGap;
	}

	virtual void DrawContent()
	{
		BMenu* menu = Menu();
		BPoint origin = menu->PenLocation();
		font_height fontHeight;
		menu->GetFontHeight(&fontHeight);
		float height = ceilf(fontHeight.ascent + fontHeight.descent);

		BRect box(origin.x, origin.y + 1, origin.x + BoxWidth() - 1, origin.y + height - 2);
		rgb_color color = { (uint8)(fColor >> 16), (uint8)(fColor >> 8), (uint8)fColor, 255 };
		rgb_color saved = menu->HighColor();
		menu->SetHighColor(color);
		menu->FillRect(box);
		menu->SetHighColor(tint_color(ui_color(B_MENU_BACKGROUND_COLOR), B_DARKEN_3_TINT));
		menu->StrokeRect(box);
		menu->SetHighColor(saved);

		menu->MovePenTo(origin.x + BoxWidth() + kGap, origin.y);
		BMenuItem::DrawContent();
	}

private:
	float BoxWidth() const
	{
		return ceilf(be_plain_font->Size() * 1.6f);
	}

	uint32 fColor;
};

// the colors as a submenu, the one that is the current one has a check mark; the messages are copies of
// the template with the color added
static BMenu*
BuildColorMenu(const char* title, const BMessage& message, BHandler* target, bool hasCurrent, uint32 current)
{
	BMenu* menu = new BMenu(title);
	bool known = false;
	for (int c = 0; c < kMarkerColorCount; c++) {
		BMessage* copy = new BMessage(message);
		copy->AddInt32("color", kMarkerColors[c].rgb);
		ColorMenuItem* item = new ColorMenuItem(B_TRANSLATE_NOCOLLECT(kMarkerColors[c].name),
			kMarkerColors[c].rgb, copy);
		item->SetTarget(target);
		if (hasCurrent && current == kMarkerColors[c].rgb) {
			item->SetMarked(true);
			known = true;
		}
		menu->AddItem(item);
	}
	if (hasCurrent && !known) {
		// a color from another program: shown, so that it is clear what the mark has now
		ColorMenuItem* item = new ColorMenuItem(B_TRANSLATE("Current"), current, new BMessage(message));
		item->SetMarked(true);
		item->SetEnabled(false);
		menu->AddItem(item, 0);
		menu->AddItem(new BSeparatorItem(), 1);
	}
	return menu;
}

// more quads than a page can have lines of text
static const int kMaxQuads = 8192;

///////////////////////////////////////////////////////////////////////////
PDFView::PDFView (entry_ref* ref, FileAttributes *fileAttributes,
	const char *name, uint32 flags, const char *ownerPassword,
	const char *userPassword, bool *encrypted)
	: BView(name, flags)
{
	GlobalSettings *settings = gApp->GetSettings();
	SetViewColor(B_TRANSPARENT_COLOR);
	// init member variables
	mDoc = NULL;
	mLoading = false;
	mOk = false;
	mZoom = settings->GetZoom();
	mBitmap = NULL;
	mPage = new CachedPage();
	mCurrentPage = 0;
	mRotation = settings->GetRotation(); // 0.0f;
	mOwnerPassword = mUserPassword = NULL;
	SetPassword(ownerPassword, userPassword);

	mColorSpace = B_RGB32;

	mInvertVerticalScrolling = settings->GetInvertVerticalScrolling();

	mTitle = NULL;
	mLeft = mTop = 0;
	mWidth = 100; mHeight = 100;
	mLink = NULL;
	mNoteTip = 0;
	mSelectKeyDown = false;
	mReadOnlyWarned = false;
	mModifierRunner = NULL;
	mNavigationState = kNotInHistory;

	mViewCursor = NULL;
	mMouseAction = NO_ACTION;
	mMousePosition.Set(0, 0);
	mDragStarted = false;

	mMouseWheelDY = 0;

	mRendererID = -1;
	mRendering = false;

	mSelected = NOT_SELECTED;
	mSelectionKind = kSelectText;
	mFilledSelection = settings->GetFilledSelection();
	mTextStart = mTextEnd = fz_make_point(0, 0);

	mPrintSettings = NULL;
	mStopFindThread = false;
	mFindPage = 0;
	mFindIndex = -1;
	mFindCaseSensitive = false;
	mFindHighlight = false;
	mRenderedPage = 0;

	if (LoadFile(ref, fileAttributes, ownerPassword, userPassword, true, encrypted)) {
		SetViewCursor(gApp->handCursor, true);
		mOk = true;
	}
}

PDFWindow*
PDFView::GetPDFWindow() {
	return dynamic_cast<PDFWindow*>(Window());
}

///////////////////////////////////////////////////////////////////////////
void PDFView::SetPassword(const char* ownerPassword, const char* userPassword) {
	delete mOwnerPassword; mOwnerPassword = ownerPassword ? new BString(ownerPassword) : NULL;
	delete mUserPassword; mUserPassword = userPassword ? new BString(userPassword) : NULL;
}

///////////////////////////////////////////////////////////////////////////
void
PDFView::EndDoc() {
	mSelected = NOT_SELECTED;
	mQuads.clear();
}

///////////////////////////////////////////////////////////////////////////
void
PDFView::UpdatePanelDirectory(BPath* path) {
	BPath directory;
	if (strcmp(path->Path(), gApp->DefaultPDF()->Path()) != 0 &&
		B_OK == path->GetParent(&directory)) {
		// don't set path to default pdf file
		gApp->GetSettings()->SetPanelDirectory(directory.Path());
	}
}

///////////////////////////////////////////////////////////////////////////
void
PDFView::MakeTitleString(BPath* path) {
	delete mTitle;
	mTitle = new BString("Tsundoku: ");

	BString title = mDoc->Metadata(FZ_META_INFO_TITLE);
	if (title.Length() > 0)
		*mTitle << title << " (" << path->Leaf() << ")";
	else
		*mTitle << path->Leaf();
	if (!mDoc->IsWritable())
		*mTitle << " " << B_TRANSLATE("(read-only)");
}

///////////////////////////////////////////////////////////////////////////
bool
PDFView::OpenFile(entry_ref *ref, const char *ownerPassword, const char *userPassword, bool *encrypted) {
	BEntry entry (ref, true);
    if (!entry.Exists()) {
        return false;
    }
	BPath path;
	entry.GetPath (&path);

	// MuPDF knows one password and tries it as user and as owner password
	const char* password = userPassword != NULL && userPassword[0] != '\0' ? userPassword : ownerPassword;

	Document* newDoc = NULL;
	Document::OpenResult result = Document::Open(path.Path(), password, &newDoc);
	*encrypted = result == Document::kNeedsPassword;
	if (result != Document::kOpened)
		return false;

	UpdatePanelDirectory(&path);

	// the page cache refers to the previous document
	mPageRenderer.SetDocument(NULL);
	mPage->MakeEmpty();
	delete mDoc;
	mDoc = newDoc;
	mPageRenderer.SetDocument(mDoc);
	MakeTitleString(&path);
	return true;
}

///////////////////////////////////////////////////////////////////////////
void
PDFView::LoadFileSettings(entry_ref* ref, FileAttributes* fileAttributes, float& left, float& top) {
	GlobalSettings *s = gApp->GetSettings();
	if (fileAttributes->Read(ref, s) && s->GetRestorePageNumber()) {
		mCurrentPage = fileAttributes->GetPage();
		if (mCurrentPage > mDoc->PageCount()) {
			mCurrentPage = mDoc->PageCount();
		}
		if (mCurrentPage < 1) {
			mCurrentPage = 1;
		}
		mZoom = s->GetZoom();
		mRotation = s->GetRotation();
		fileAttributes->GetLeftTop(left, top);
	} else {
		left = top = 0;
		mCurrentPage = 1;
		fileAttributes->SetPage(mCurrentPage);
		fileAttributes->SetLeftTop(left, top);
	}
}

///////////////////////////////////////////////////////////////////////////
void
PDFView::RestoreWindowFrame(BWindow* w) {
	GlobalSettings* s = gApp->GetSettings();
	if (s->GetRestoreWindowFrame()) {
		// restore window position and size
		w->MoveTo(s->GetWindowPosition());
		float width, height;
		s->GetWindowSize(width, height);
		w->ResizeTo(width, height);
	}
}

///////////////////////////////////////////////////////////////////////////
bool
PDFView::LoadFile(entry_ref *ref, FileAttributes *fileAttributes, const char *ownerPassword, const char *userPassword, bool init, bool *encrypted) {
	BString s(B_TRANSLATE("Tsundoku reading file: "));
	s += ref->name;
	ShowLoadProgressStatusWindow statusWindow(s.String());
	EndDoc();

	SetPassword(ownerPassword, userPassword);
	WaitForPage(true);

	// We use the application thread to load a file.
	// To keep the window responsive while loading, we unlock the window lock
	// and have to ensure that the window thread does not access data
	// that is being loaded (Draw() just fills the entire view with a background color).
	mLoading = true;
	bool isLocked = Window()->IsLocked();
	if (isLocked) {
		Invalidate();
		Window()->Unlock();
	}
	bool opened = OpenFile(ref, ownerPassword, userPassword, encrypted);
	if (isLocked) Window()->Lock();
	mLoading = false;
	if (!opened) {
		// show previous document
		if (Window()->Lock()) {
			Invalidate();
			Window()->Unlock();
		}
		return false;
	}
	BepdfApplication::UpdateFileAttributes(mDoc, ref);

	float left, top;
	LoadFileSettings(ref, fileAttributes, left, top);

	RecordHistory(*ref, ownerPassword, userPassword);

	PDFWindow *w = GetPDFWindow();
	if (w && !init && w->Lock()) {
		RestoreWindowFrame(w);
		w->FitToScreen();
		w->NewDoc(mDoc);
		w->SetTitle (mTitle->String());
		mRenderedPage = 0;
		mFindHighlight = false;
		Redraw();
		ScrollTo(left, top);
		w->Unlock();
	}

	return true;
}

///////////////////////////////////////////////////////////////////////////
PDFView::~PDFView()
{
	mPageRenderer.SetDocument(NULL);
	delete mPage;	// refers to the document
	delete mDoc;
	delete mModifierRunner;
	delete mTitle;
	delete mOwnerPassword;
	delete mUserPassword;
}

///////////////////////////////////////////////////////////////////////////
void PDFView::MessageReceived(BMessage *msg) {
	BString string;
	switch (msg->what) {
	case B_SIMPLE_DATA: {
			entry_ref ref;
			if (B_OK == msg->FindRef("refs", 0, &ref)) {
				be_app->RefsReceived(msg);
				return;
			}
		}
		break;
	case B_COPY_TARGET:
		SendDataMessage(msg);
		break;
	case B_MOUSE_WHEEL_CHANGED:
		OnMouseWheelChanged(msg);
		break;
	case COPY_LINK_MSG:
		if (B_OK == msg->FindString("link", &string)) {
			CopyText(&string);
		}
		break;
	case COPY_SELECTION_MSG:
		CopySelection();
		break;
	case SELECT_ALL_MSG:
		SelectAll();
		break;
	case MODIFIERS_POLL_MSG: {
		// the cursor shows the selecting mode as soon as the key is down
		bool down = SelectModifierDown();
		if (down != mSelectKeyDown && Window() != NULL && Window()->IsActive()) {
			BPoint point;
			uint32 buttons;
			GetMouse(&point, &buttons, false);
			if (buttons == 0 && Bounds().Contains(point))
				DisplayLink(point);
			mSelectKeyDown = down;
		}
		break;
	}
	case ANNOTATE_MSG: {
		int32 type = kMarkupHighlight, rgb = 0xffeb3b;
		msg->FindInt32("type", &type);
		msg->FindInt32("color", &rgb);
		AnnotateSelection((MarkupType)type, (uint32)rgb);
		break;
	}
	case CHANGE_COLOR_MSG: {
		int32 page = 0, index = -1, rgb = 0;
		msg->FindInt32("page", &page);
		msg->FindInt32("index", &index);
		msg->FindInt32("color", &rgb);
		if (ConfirmEditable() && mDoc->SetAnnotationColor(page, index, (uint32)rgb))
			AnnotationsChanged();
		break;
	}
	case DELETE_ANNOTATION_MSG: {
		int32 page = 0, index = -1;
		msg->FindInt32("page", &page);
		msg->FindInt32("index", &index);
		if (ConfirmEditable() && mDoc->DeleteAnnotation(page, index))
			AnnotationsChanged();
		break;
	}
	case EDIT_NOTE_MSG: {
		BMessage entered(NOTE_ENTERED_MSG);
		int32 page = 0, index = -1;
		msg->FindInt32("page", &page);
		msg->FindInt32("index", &index);
		entered.AddInt32("page", page);
		entered.AddInt32("index", index);
		const char* text = "";
		msg->FindString("text", &text);
		if (ConfirmEditable())
			new NoteWindow(Window(), BMessenger(this), entered, text);
		break;
	}
	case NOTE_ENTERED_MSG: {
		int32 page = 0, index = -1;
		const char* text = "";
		msg->FindInt32("page", &page);
		msg->FindInt32("index", &index);
		msg->FindString("text", &text);
		if (mDoc->SetAnnotationContents(page, index, text))
			AnnotationsChanged();
		break;
	}
	case OPEN_FILE_MSG:
		if (B_OK == msg->FindString("file", &string)) {
			PDFWindow::Launch(string.String());
		}
		break;
	default:
		BView::MessageReceived(msg);
	}
}

///////////////////////////////////////////////////////////////////////////
bool
PDFView::InPage(BPoint p) {
	return p.x >= 0.0 && p.x < mWidth && p.y >= 0.0 && p.y < mHeight;
}

///////////////////////////////////////////////////////////////////////////
BPoint
PDFView::LimitToPage(BPoint p) {
	if (p.x < 0) p.x = 0.0;
	else if (p.x > mWidth - 1) p.x = mWidth - 1;

	if (p.y < 0) p.y = 0.0;
	else if (p.y > mHeight - 1) p.y = mHeight - 1;
	return p;
}

///////////////////////////////////////////////////////////////////////////
void
PDFView::OnMouseWheelChanged(BMessage *msg) {
	float dy, dx;
	if (msg->FindFloat("be:wheel_delta_y", &dy) == B_OK && dy != 0.0) {
		bool down = dy > 0;
		// as the guidelines say: Command zooms (Control as well, there is no font size to change), Option
		// scrolls a full page; Shift goes to the next or previous page
		int32 keys = modifiers();
		if ((keys & (B_COMMAND_KEY | B_CONTROL_KEY))) {
			Zoom(!down); // zoom in / out
		} else if ((keys & B_OPTION_KEY)) {
			ScrollVertical(down, 1.0);
		} else if ((keys & B_SHIFT_KEY)) {
			MoveToPage(mCurrentPage + (down ? 1 : -1));
		} else {
			ScrollVertical(down, 0.20);
		}
	}
	if (msg->FindFloat("be:wheel_delta_x", &dx) == B_OK && dx != 0.0) {
		bool right = dx > 0;
		ScrollHorizontal(right, 0.20);
	}
}

///////////////////////////////////////////////////////////////////////////
void
PDFView::DrawPage(BRect updateRect)
{
	if (mBitmap == NULL) {
#ifdef DEBUG
		fprintf (stderr, "WARNING: PDFView::Draw() NULL bitmap\n");
#endif
	} else {
		DrawBitmap(mBitmap, BRect(0, 0, mWidth - 1, mHeight - 1),
			BRect(mLeft, mTop, mLeft + mWidth - 1, mTop + mHeight - 1));
	}
}

///////////////////////////////////////////////////////////////////////////
void
PDFView::DrawBackground(BRect updateRect)
{
	BRect rect(Bounds());
	float right = mLeft + mWidth - 1, bottom = mTop + mHeight - 1;
	SetLowColor(DesktopColor());
	if (rect.left < mLeft) {
		FillRect(BRect(rect.left, rect.top, mLeft - 2, rect.bottom), B_SOLID_LOW);
	}
	if (rect.top < mTop) {
		FillRect(BRect(rect.left, rect.top, rect.right, mTop - 2), B_SOLID_LOW);
	}
	if (right < rect.right) {
		FillRect(BRect(right + 2, rect.top, rect.right, rect.bottom), B_SOLID_LOW);
	}
	if (bottom < rect.bottom) {
		FillRect(BRect(rect.left, bottom + 2, rect.right, rect.bottom), B_SOLID_LOW);
	}

	SetLowColor(ui_color(B_SHADOW_COLOR));
	StrokeRect(BRect(mLeft - 1, mTop - 1, right + 1, bottom + 1), B_SOLID_LOW);
}

///////////////////////////////////////////////////////////////////////////
// the places where the search text has been found on this page
void
PDFView::DrawFindHits(BRect updateRect)
{
	if (!mFindHighlight || mFindQuads.empty())
		return;

	SetHighColor(255, 200, 0, 110);
	SetDrawingMode(B_OP_ALPHA);
	for (size_t i = 0; i < mFindQuads.size(); i++) {
		const fz_quad& q = mFindQuads[i];
		BPoint polygon[4] = { mPage->PageToDev(q.ul), mPage->PageToDev(q.ur),
			mPage->PageToDev(q.lr), mPage->PageToDev(q.ll) };
		for (int j = 0; j < 4; j++)
			polygon[j] += BPoint(mLeft, mTop);
		FillPolygon(polygon, 4);
	}
	SetDrawingMode(B_OP_COPY);
}


static int
CollectQuads(fz_context*, void* data, int numQuads, fz_quad* quads, int, int)
{
	std::vector<fz_quad>* all = (std::vector<fz_quad>*)data;
	for (int i = 0; i < numQuads; i++)
		all->push_back(quads[i]);
	return 0;
}


// Finds all hits of the last search on the shown page, once it has been rendered (the text is not there before).
void
PDFView::UpdateFindQuads()
{
	std::vector<fz_quad> quads;
	fz_stext_page* text = mFindHighlight ? mPage->Text() : NULL;
	if (text != NULL) {
		DocumentLocker locker(mDoc);
		fz_context* context = mDoc->Context();
		fz_try(context) {
			fz_match_stext_page_cb(context, text, mFindNeedle.String(), CollectQuads, &quads,
				mFindCaseSensitive ? FZ_SEARCH_EXACT : FZ_SEARCH_IGNORE_CASE);
		}
		fz_catch(context) {
			quads.clear();
		}
	}
	mFindQuads.swap(quads);
	Invalidate();
}


void
PDFView::ClearFindHighlights()
{
	mFindHighlight = false;
	mFindQuads.clear();
	Invalidate();
}


///////////////////////////////////////////////////////////////////////////
void
PDFView::DrawSelection(BRect updateRect)
{
	if (mSelected == NOT_SELECTED)
		return;

	rgb_color fill_color = ui_color(B_CONTROL_HIGHLIGHT_COLOR);
	fill_color.alpha = 70;
	SetHighColor(fill_color); // fill color for selection
	SetPenSize(1.0);

	if (mSelectionKind == kSelectText) {
		// the text between the two points, line by line
		SetDrawingMode(B_OP_ALPHA);
		for (size_t i = 0; i < mQuads.size(); i++) {
			const fz_quad& q = mQuads[i];
			BPoint polygon[4] = { mPage->PageToDev(q.ul), mPage->PageToDev(q.ur),
				mPage->PageToDev(q.lr), mPage->PageToDev(q.ll) };
			for (int j = 0; j < 4; j++)
				polygon[j] += BPoint(mLeft, mTop);
			FillPolygon(polygon, 4);
		}
		SetDrawingMode(B_OP_COPY);
		return;
	}

	BRect selection(mSelection);
	selection.OffsetBy(mLeft, mTop);

	switch (mSelected) {
		case DO_SELECTION:
			StrokeRect(selection);
			break;
		case SELECTED:
			SetDrawingMode(B_OP_ALPHA);
			if (mFilledSelection) {
				FillRect(selection);
			} else {
				StrokeRect(selection);
			}
			SetDrawingMode(B_OP_COPY);
			break;
		default:
			break;
	}
}

///////////////////////////////////////////////////////////////////////////
void
PDFView::Draw(BRect updateRect)
{
	if (mLoading) {
		SetLowColor(DesktopColor());
		FillRect(updateRect, B_SOLID_LOW);
	} else {
		DrawBackground(updateRect);
		DrawPage(updateRect);
		BRect rect(Bounds());
		if (GetPDFWindow()) {
			GetPDFWindow()->GetFileAttributes()->SetLeftTop(rect.left, rect.top);
		}
		DrawFindHits(updateRect);
		DrawSelection(updateRect);
	}
}

///////////////////////////////////////////////////////////////////////////
void
PDFView::ScrollTo (BPoint point) {
	BView::ScrollTo(point);
	BPoint mouse; uint32 buttons;
	GetMouse(&mouse, &buttons);
	DisplayLink(mouse);
}

void
PDFView::ScrollTo(float x, float y) {
	BRect bounds(Bounds());
	float xMax = mWidth - bounds.Width();
	float yMax = mHeight - bounds.Height();

	if ((x < 0) || (mLeft > 0))
		x = 0;
	else if ((xMax > 0) && (x > xMax))
		x = xMax;

	if ((y < 0) || (mTop > 0))
		y = 0;
	else if ((yMax > 0) && (y > yMax))
		y = yMax;

	BView::ScrollTo(x, y);
}

///////////////////////////////////////////////////////////////////////////
void
PDFView::FrameResized (float width, float height)
{
	Resize();
}


///////////////////////////////////////////////////////////////////////////
void
PDFView::AttachedToWindow ()
{
	Window()->SetTitle (mTitle->String());
	SetViewCursor(gApp->handCursor);
	mPageRenderer.SetListener(Window(), this);

	// there is no message when a key like Option is pressed while the mouse rests, so look from time to time
	if (mModifierRunner == NULL) {
		BMessage poll(MODIFIERS_POLL_MSG);
		mModifierRunner = new BMessageRunner(BMessenger(this), &poll, 100000);
	}
}

///////////////////////////////////////////////////////////////////////////
void
PDFView::ScrollVertical (bool down, float by) {
	BRect rect(Bounds ());
	float scrollBy = (by > 0) ? rect.Height() * by : -by;
	if (down) {
		if (rect.bottom < mHeight-1) {
			ScrollBy (0, scrollBy);
		} else {
			if (mCurrentPage != mDoc->PageCount()) { // bottom of last page not reached
				MoveToPage(mCurrentPage + 1, true);
			}
		}
	} else { // up
		if (rect.top != 0) {
			ScrollBy (0, -scrollBy);
		} else {
			if (mCurrentPage != 1) { // top of first page not reached
				MoveToPage(mCurrentPage - 1, false);
			}
		}
	}
}

///////////////////////////////////////////////////////////////////////////
void
PDFView::ScrollHorizontal (bool right, float by) {
	BRect rect(Bounds());
	float scrollBy = (by > 0) ? rect.Width() * by : -by;
	if (right) {
		if (rect.right < mWidth - 1) {
			ScrollBy (scrollBy, 0);
		}
	} else {
		if (rect.left != 0) {
			ScrollBy (-scrollBy, 0);
		}
	}
}
///////////////////////////////////////////////////////////////////////////
void
PDFView::KeyDown (const char * bytes, int32 numBytes)
{
	switch (*bytes) {
	case B_PAGE_UP:
		MoveToPage (mCurrentPage - 1);
		break;
	case B_SPACE:
	case B_ENTER:
	case B_BACKSPACE:
		ScrollVertical (*bytes != B_BACKSPACE, 0.95);
		break;
	case B_DOWN_ARROW:
	case B_UP_ARROW:
		ScrollVertical (*bytes == B_DOWN_ARROW, -20);
		break;
	case B_LEFT_ARROW:
	case B_RIGHT_ARROW:
		ScrollHorizontal(*bytes == B_RIGHT_ARROW, -20);
		break;
	case B_PAGE_DOWN:
		MoveToPage (mCurrentPage + 1);
		break;
	case B_HOME:
		MoveToPage (1);
		break;
	case B_END:
		MoveToPage (GetNumPages ());
		break;
	default:
		BView::KeyDown (bytes, numBytes);
		break;
	}
}

///////////////////////////////////////////////////////////////////////////
void PDFView::SetAction(mouse_action action) {
	mMouseAction = action;
}

///////////////////////////////////////////////////////////////////////////
void PDFView::SetViewCursor(BCursor *cursor, bool sync) {
	if (Window()->Lock()) {
		mViewCursor = cursor;
		BView::SetViewCursor(cursor, sync);
		Window()->Unlock();
	}
}

///////////////////////////////////////////////////////////////////////////
BPoint
PDFView::CorrectMousePos(const BPoint point) {
	BPoint p(point);
	p.x -= mLeft;
	p.y -= mTop;
	return p;
}

///////////////////////////////////////////////////////////////////////////
// Option (and Alt) with the primary button selects text, like in other readers: no mode to switch.
static bool
SelectModifierDown()
{
	return (modifiers() & (B_OPTION_KEY | B_COMMAND_KEY)) != 0;
}

uint32
PDFView::GetButtons() {
	BPoint point;
	uint32 buttons;
	GetMouse(&point, &buttons, false);
	if (buttons == B_PRIMARY_MOUSE_BUTTON && !SelectModifierDown()) {
		if ((modifiers() & B_CONTROL_KEY)) {
			buttons = B_SECONDARY_MOUSE_BUTTON; // simulate secondary button
		} else if ((modifiers() & B_SHIFT_KEY)) {
			buttons = B_TERTIARY_MOUSE_BUTTON; // simulate tertiary button
		}
	}
	return buttons;
}

///////////////////////////////////////////////////////////////////////////
// Starts a selection at the position: of the text that follows the flow of the text, or a rectangle.
void
PDFView::BeginSelection(BPoint point, bool rectangle) {
	if (mSelected != NOT_SELECTED) {
		BRect old = SelectionBounds();
		mSelected = NOT_SELECTED;
		mQuads.clear();
		if (old.IsValid())
			Invalidate(old.InsetByCopy(-2, -2).OffsetByCopy(mLeft, mTop));
	}

	SetAction(SELECT_ACTION);
	mSelected = DO_SELECTION;
	mMousePosition = ConvertToScreen(point);
	point = LimitToPage(CorrectMousePos(point));
	SetViewCursor(gApp->textSelectionCursor);
	mSelectionKind = rectangle ? kSelectArea : kSelectText;
	mSelectionStart = point;
	mSelection.SetLeftTop(point);
	mSelection.SetRightBottom(point);
	if (mSelectionKind == kSelectText)
		StartTextSelection(point);
	SetMouseEventMask(B_POINTER_EVENTS);
}

///////////////////////////////////////////////////////////////////////////
void
PDFView::MouseDown (BPoint point) {
	BPoint screen;

	MakeFocus(true);
	uint32 buttons = GetButtons();
	screen = ConvertToScreen(point);

	int32 clicks = 1;
	BMessage* current = Window()->CurrentMessage();
	if (current != NULL)
		current->FindInt32("clicks", &clicks);

	switch (buttons) {
		case B_PRIMARY_MOUSE_BUTTON:
			// Option: select text, with Shift a rectangle (which also copies the picture of it)
			if (SelectModifierDown()) {
				if (mDoc->CanCopy())
					BeginSelection(point, (modifiers() & B_SHIFT_KEY) != 0);
				break;
			}
			if ((mSelected == SELECTED) && InSelection(point)) {
				SendDragMessage(B_MIME_DATA); // start text drag and drop
				break;
			}
			// double click selects a word, triple click a line
			if (clicks >= 2 && mDoc->CanCopy()
				&& SelectTextAt(point, clicks == 2 ? FZ_SELECT_WORDS : FZ_SELECT_LINES)) {
				CopySelection();
				SelectionChanged();
				break;
			}
			// follow link or move view
			SetAction(MOVE_ACTION);
			if (!HandleLink(point)) {
				mDragStarted = true;
				SetMouseEventMask(B_POINTER_EVENTS);
		  		SetViewCursor(gApp->grabCursor);
				mMousePosition = ConvertToScreen(point);
			} else {
				SetAction(NO_ACTION);
			}
			break;
		case B_SECONDARY_MOUSE_BUTTON:
			if ((mSelected == SELECTED) && InSelection(point)) {
				// a click opens the menu (see MouseUp), moving the mouse starts to drag the selection
				mSecondaryStart = screen;
				SetAction(SECONDARY_ACTION);
				SetMouseEventMask(B_POINTER_EVENTS);
				return;
			}
			ShowPopUpMenu(screen, OnLink(point), OnAnnotation(point));
			return;
		case B_TERTIARY_MOUSE_BUTTON: // zoom to selection
			if (mSelected != NOT_SELECTED) {
				BRect old = SelectionBounds();
				mSelected = NOT_SELECTED;
				mQuads.clear();
				if (old.IsValid())
					Invalidate(old.InsetByCopy(-2, -2).OffsetByCopy(mLeft, mTop));
			}

			SetAction(ZOOM_ACTION);
			mSelected = DO_SELECTION;
			mMousePosition = screen;
			point = CorrectMousePos(point);
		  	SetViewCursor(gApp->zoomCursor);
		  	mSelectionKind = kSelectArea;
			mSelectionStart = point;
			mSelection.SetLeftTop(point);
			mSelection.SetRightBottom(point);
			SetMouseEventMask(B_POINTER_EVENTS);
			break;
	}
}


void
PDFView::ScrollIfOutside(BPoint point) {
	float x, y, r_min, r_max;
	BRect bounds(Bounds());

	BScrollBar *scroll = ScrollBar(B_VERTICAL);

	scroll->GetRange(&r_min, &r_max);

	if (point.x < bounds.left) { // scroll left
		x = point.x;
	} else if (point.x > bounds.right) { // scroll right
		x = point.x - bounds.Width();
	} else {
		x = bounds.left;
	}
	x = min_c(r_max, max_c(x, r_min));

	scroll = ScrollBar(B_VERTICAL);
	scroll->GetRange(&r_min, &r_max);
	if (point.y < bounds.top) { // scroll up
		y = point.y;
	} else if (point.y > bounds.bottom) { // scroll down
		y = point.y - bounds.Height();
	} else {
		y = bounds.top;
	}
	y = min_c(r_max, max_c(y, r_min));
	if ((x != bounds.left) || (y != bounds.top)) {
		ScrollTo(x, y);
	}
}

///////////////////////////////////////////////////////////////////////////
void PDFView::SkipMouseMoveMsgs() {
	BMessage *mouseMovedMsg;
	while ((mouseMovedMsg = Looper()->MessageQueue()->FindMessage(B_MOUSE_MOVED, 0)))
	{
		Looper()->MessageQueue()->RemoveMessage(mouseMovedMsg);
		delete mouseMovedMsg;
	}
}

void
PDFView::InitViewCursor(uint32 transit) {
	// FIXME: Where is the best place to set the initial Cursor of a view?
	if ((transit == B_ENTERED_VIEW) && (mViewCursor != NULL)) {
		if (Window()->Lock()) {
			BView::SetViewCursor(mViewCursor);
			Window()->Unlock();
			mViewCursor = NULL;
		}
	}
}

void
PDFView::MouseMoved (BPoint point, uint32 transit, const BMessage *msg) {
	#define UPDATE_INTERVAL 4
	int updateCounter = UPDATE_INTERVAL;

	InitViewCursor(transit);

	switch (mMouseAction) {
		case NO_ACTION:
			if (!mDragStarted)
				DisplayLink(point);
			break;
		case MOVE_ACTION: // move view
		{
			SkipMouseMoveMsgs();

			BPoint mousePosition = point;
			uint32 buttons = GetButtons();
			BPoint offset;
			float x, y, r_min, r_max;
			BScrollBar *scroll;

			point = ConvertToScreen(mousePosition);
			offset = point - mMousePosition;
			if (mInvertVerticalScrolling) {
				offset.y = mMousePosition.y - point.y;
			}
			mMousePosition = point;

			scroll = ScrollBar(B_HORIZONTAL);
			scroll->GetRange(&r_min, &r_max);
			x = min_c(r_max, max_c(scroll->Value() - offset.x, r_min));

			scroll = ScrollBar(B_VERTICAL);
			scroll->GetRange(&r_min, &r_max);
			y = min_c(r_max, max_c(scroll->Value() - offset.y, r_min));

			ScrollTo(x, y);

			if ((buttons & B_PRIMARY_MOUSE_BUTTON) == 0) {
				MouseUp(mousePosition);
			}
			break;
		}
		case SECONDARY_ACTION:
		{
			uint32 buttons = GetButtons();
			BPoint offset = ConvertToScreen(point) - mSecondaryStart;
			if ((buttons & B_SECONDARY_MOUSE_BUTTON) == 0) {
				MouseUp(point);
			} else if (fabsf(offset.x) > 4 || fabsf(offset.y) > 4) {
				SetAction(NO_ACTION);
				SendDragMessage(B_SIMPLE_DATA); // start negotiated drag and drop
			}
			break;
		}
		case SELECT_ACTION: // text selection
		case ZOOM_ACTION: // zoom to selection
		 	while(true) {
				SkipMouseMoveMsgs();

				uint32 buttons;

				ScrollIfOutside(point);

				switch (mMouseAction) {
					case SELECT_ACTION:
					case ZOOM_ACTION:
						ResizeSelection(point);
						break;
					default:;
				}

				GetMouse(&point, &buttons, false);
				if (buttons == 0) {
					MouseUp(point);
					return;
				}
				if (updateCounter == UPDATE_INTERVAL) {
					Window()->UpdateIfNeeded();
					updateCounter = 0;
				} else {
					updateCounter ++;
				}
				snooze(10000);
			}
			break;
		default:;
	}
}

void
PDFView::ResizeSelection(BPoint point) {
	point = CorrectMousePos(point);
	if (mMouseAction == SELECT_ACTION && mSelectionKind == kSelectText) {
		ExtendTextSelection(point);
		return;
	}

	BRect rect(mSelection);
	if (mMouseAction == SELECT_ACTION) point = LimitToPage(point);
	if (point.x < mSelectionStart.x) {
		mSelection.left = point.x; mSelection.right = mSelectionStart.x;
	} else {
		mSelection.left = mSelectionStart.x; mSelection.right = point.x;
	}

	if (point.y < mSelectionStart.y) {
		mSelection.top = point.y; mSelection.bottom = mSelectionStart.y;
	} else {
		mSelection.top = mSelectionStart.y; mSelection.bottom = point.y;
	}

	if (rect != mSelection) {
		rect = rect | mSelection;
		rect.OffsetBy(mLeft, mTop);
		Invalidate(rect);
	}
}

///////////////////////////////////////////////////////////////////////////
void
PDFView::MouseUp (BPoint point) {
	if (mMouseAction == SECONDARY_ACTION) {
		SetAction(NO_ACTION);
		ShowPopUpMenu(ConvertToScreen(point), OnLink(point), OnAnnotation(point));
		return;
	}
	if (mMouseAction == SELECT_ACTION) { // copy selection
		if (mSelectionKind == kSelectText) {
			BRect bounds = SelectionBounds();
			if (!mQuads.empty()) {
				mSelected = SELECTED;
				CopySelection();
			} else {
				mSelected = NOT_SELECTED;
			}
			if (bounds.IsValid())
				Invalidate(bounds.InsetByCopy(-2, -2).OffsetByCopy(mLeft, mTop));
		} else if (mSelection.Width() * mSelection.Height() * mSelection .Height() > 200) {
			mSelected = SELECTED;
			Invalidate(mSelection.OffsetByCopy(mLeft, mTop));
			CopySelection();
		} else {
			mSelected = NOT_SELECTED;
			Invalidate(mSelection.OffsetByCopy(mLeft, mTop));
		}
	} else if (mMouseAction == ZOOM_ACTION) { // zoom to selection
		mSelected = NOT_SELECTED;
		Invalidate(mSelection.OffsetByCopy(mLeft, mTop));

		if (mSelection.Width() * mSelection .Height() > 200) {
			float a = mSelection.Width() + 1, b = mSelection.Height() + 1;
			BRect bounds(Bounds());
			float n = bounds.Width() + 1, m = bounds.Height() + 1;

			int32 zoomDPI = GetZoomDPI(), newZoomDPI;
			if (a / b > n / m) {
				newZoomDPI = (int32)(zoomDPI * n / a);
			} else {
				newZoomDPI = (int32)(zoomDPI * m / b);
			}

			if (newZoomDPI > ZOOM_DPI_MAX) newZoomDPI = ZOOM_DPI_MAX;
			float x = mSelection.left * newZoomDPI / zoomDPI,
				y = mSelection.top * newZoomDPI / zoomDPI;

			int i;
			for (i = MIN_ZOOM; i <= MAX_ZOOM; i++) {
				if (newZoomDPI == kZoomDPI[i]) {
					newZoomDPI = i; break;
				}
			}

			if (i > MAX_ZOOM)
				newZoomDPI = -newZoomDPI;

			if (mZoom != newZoomDPI) {
				PDFWindow* w = GetPDFWindow();
				if (w) w->SetZoom(newZoomDPI);
				SetZoom(newZoomDPI);
			}

			ScrollTo(x, y);
		}
	}

	SelectionChanged();

	if ((mMouseAction != NO_ACTION) || mDragStarted) {
		mDragStarted = false;
		DisplayLink(point);
		mMouseAction = NO_ACTION;
	}
}

///////////////////////////////////////////////////////////////////////////
const DocAnnotation*
PDFView::OnAnnotation(BPoint point) {
	if (mRendering || (mDoc == NULL) || (mDoc->PageCount() == 0)) return NULL;

	point = CorrectMousePos(point);
	return mPage->FindAnnotation(mPage->DevToPage(point));
}

const DocLink*
PDFView::OnLink(BPoint point) {
	if (mRendering || (mDoc == NULL) || (mDoc->PageCount() == 0)) return NULL;

	point = CorrectMousePos(point);
	return mPage->FindLink(mPage->DevToPage(point));
}

// the path of the document a link points to, if it is a link to a document that can be opened
bool
PDFView::IsLinkToDocument(const DocLink* link, BString* path) {
	BString file(link->uri);
	if (file.StartsWith("file://"))
		file.Remove(0, 7);
	else if (file.Length() == 0 || mDoc->IsExternalLink(file.String()))
		return false;	// has a scheme like "http:"

	int32 fragment = file.FindFirst('#');
	if (fragment >= 0)
		file.Truncate(fragment);
	if (file.Length() == 0 || !file.IEndsWith(".pdf"))
		return false;

	if (file[0] != '/') {
		BPath directory;
		if (BPath(mDoc->Path()).GetParent(&directory) != B_OK)
			return false;
		directory.Append(file.String());
		file = directory.Path();
	}
	*path = file;
	return true;
}

///////////////////////////////////////////////////////////////////////////
bool
PDFView::HandleLink(BPoint point) {
	const DocLink* link = OnLink(point);
	if (link == NULL)
		return false;

	BString pdfFile;
	if (IsLinkToDocument(link, &pdfFile)) {
		RecordHistory();
		if ((modifiers() & B_COMMAND_KEY)) {
			PDFWindow::Launch(pdfFile.String());
		} else {
			PDFWindow::OpenInWindow(pdfFile.String());
		}
		return true;
	}

	int page;
	float x, y;
	if (mDoc->ResolveLink(link->uri.String(), &page, &x, &y)) {
		GotoPosition(page, x, y);
		return true;
	}

	// anything else with a scheme: let the system decide
	if (mDoc->IsExternalLink(link->uri.String()) && GetPDFWindow()) {
		GetPDFWindow()->LaunchHTMLBrowser(link->uri.String());
		return true;
	}
	return false;
}

///////////////////////////////////////////////////////////////////////////
void
PDFView::GotoPosition(int page, float x, float y) {
	if (page > 0 && page != mCurrentPage)
		MoveToPage(page);
	else if (page <= 0)
		MoveToPage(1);

	if (isnan(x) && isnan(y))
		return;

	// the page has been set up by now, so the position can be calculated
	BPoint dev = mPage->PageToDev(fz_make_point(isnan(x) ? 0 : x, isnan(y) ? 0 : y));
	BRect bounds(Bounds());
	ScrollTo(isnan(x) ? bounds.left : dev.x, isnan(y) ? bounds.top : dev.y);
}

///////////////////////////////////////////////////////////////////////////
void
PDFView::ShowPopUpMenu(BPoint point, const DocLink* link, const DocAnnotation* annotation) {
	BPopUpMenu* menu = new BPopUpMenu("PopUpMenu");
	menu->SetAsyncAutoDestruct(true);

	BMessage* msg;
	BMenuItem* i;
	BString s;

	bool canCopy = mDoc->CanCopy();
	i = new BMenuItem(B_TRANSLATE("Copy"), new BMessage(COPY_SELECTION_MSG));
	i->SetTarget(this);
	i->SetEnabled(canCopy && mSelected == SELECTED);
	menu->AddItem(i);
	i = new BMenuItem(B_TRANSLATE("Select all"), new BMessage(SELECT_ALL_MSG));
	i->SetTarget(this);
	i->SetEnabled(canCopy);
	menu->AddItem(i);

	if (mDoc->CanEditAnnotations() && (HasTextSelection() || annotation != NULL)) {
		menu->AddSeparatorItem();

		if (HasTextSelection()) {
			BMessage highlight(ANNOTATE_MSG);
			highlight.AddInt32("type", kMarkupHighlight);
			menu->AddItem(BuildColorMenu(B_TRANSLATE("Highlight"), highlight, this, false, 0));

			msg = new BMessage(ANNOTATE_MSG);
			msg->AddInt32("type", kMarkupUnderline);
			msg->AddInt32("color", kMarkerColors[5].rgb);
			i = new BMenuItem(B_TRANSLATE("Underline"), msg);
			i->SetTarget(this);
			menu->AddItem(i);

			msg = new BMessage(ANNOTATE_MSG);
			msg->AddInt32("type", kMarkupStrikeOut);
			msg->AddInt32("color", kMarkerColors[5].rgb);
			i = new BMenuItem(B_TRANSLATE("Strike out"), msg);
			i->SetTarget(this);
			menu->AddItem(i);
		}

		if (annotation != NULL) {
			if (annotation->isMarkup) {
				BMessage change(CHANGE_COLOR_MSG);
				change.AddInt32("page", mCurrentPage);
				change.AddInt32("index", annotation->index);
				menu->AddItem(BuildColorMenu(B_TRANSLATE("Color"), change, this, annotation->hasColor,
					annotation->color));
			}

			msg = new BMessage(EDIT_NOTE_MSG);
			msg->AddInt32("page", mCurrentPage);
			msg->AddInt32("index", annotation->index);
			msg->AddString("text", annotation->contents);
			i = new BMenuItem(B_TRANSLATE("Edit note" B_UTF8_ELLIPSIS), msg);
			i->SetTarget(this);
			menu->AddItem(i);

			msg = new BMessage(DELETE_ANNOTATION_MSG);
			msg->AddInt32("page", mCurrentPage);
			msg->AddInt32("index", annotation->index);
			i = new BMenuItem(B_TRANSLATE("Delete annotation"), msg);
			i->SetTarget(this);
			menu->AddItem(i);
		}
	}

	if (link != NULL) {
		menu->AddSeparatorItem();

		// Open document in new window
		if (IsLinkToDocument(link, &s)) {
			msg = new BMessage(OPEN_FILE_MSG);
			msg->AddString("file", s);
			i = new BMenuItem(B_TRANSLATE("Open in new window"), msg);
			i->SetTarget(this);
			menu->AddItem(i);
		}

		// Copy link location
		msg = new BMessage(COPY_LINK_MSG);
		LinkToString(link, &s);
		msg->AddString("link", s);
		i = new BMenuItem(B_TRANSLATE("Copy link"), msg);
		i->SetTarget(this);
		menu->AddItem(i);
	}

	point -= BPoint(10, 10);
	menu->Go(point, true, false, false);
}

///////////////////////////////////////////////////////////////////////////
void
PDFView::LinkToString(const DocLink* link, BString* string) {
	if (link == NULL) {
		string->Truncate(0);
		return;
	}

	int page;
	float x, y;
	if (mDoc->ResolveLink(link->uri.String(), &page, &x, &y)) {
		char label[128];
		snprintf(label, sizeof(label), B_TRANSLATE("Go to page %d"), page);
		*string = label;
	} else if (link->uri.Length() == 0) {
		*string = B_TRANSLATE("[unknown link]");
	} else {
		*string = link->uri;
	}
}


// the text of a note for a tooltip: broken into lines, shortened if it is very long, with the author
static BString
NoteTipText(const DocAnnotation* annotation)
{
	const int kColumns = 60, kMaxLength = 800;
	BString text = annotation->contents;
	text.ReplaceAll("\r\n", "\n");
	text.ReplaceAll("\r", "\n");
	if (text.CountChars() > kMaxLength) {
		text.TruncateChars(kMaxLength);
		text << B_UTF8_ELLIPSIS;
	}

	BString result;
	int column = 0;
	const char* p = text.String();
	while (*p != '\0') {
		// the next word
		const char* end = p;
		while (*end != '\0' && *end != ' ' && *end != '\n')
			end++;
		BString word(p, end - p);
		int length = word.CountChars();
		if (column > 0 && column + 1 + length > kColumns) {
			result << '\n';
			column = 0;
		}
		if (column > 0 && *(p - 1) == ' ') {
			result << ' ';
			column++;
		}
		result << word;
		column += length;
		if (*end == '\n') {
			result << '\n';
			column = 0;
		}
		p = *end == '\0' ? end : end + 1;
	}

	if (annotation->author.Length() > 0)
		result << "\n\xe2\x80\x94 " << annotation->author;
	return result;
}


void
PDFView::DisplayLink(BPoint point)
{
	BString str;
	if (mRendering || mDragStarted || (mDoc == NULL) || (mDoc->PageCount() == 0))
		return;

	BPoint p = CorrectMousePos(point);
	// over selection?
	if (((mSelected == SELECTED) && InSelection(point)) ||
		p.x < 0 || p.y < 0 || p.x >= mWidth || p.y >= mHeight) {
		SetViewCursor((BCursor*)B_CURSOR_SYSTEM_DEFAULT);
		return;
	}

	// selecting?
	if (SelectModifierDown()) {
		SetViewCursor(gApp->textSelectionCursor);
		if (mNoteTip != 0) {
			mNoteTip = 0;
			SetToolTip("");
			HideToolTip();
		}
		return;
	}

	// a note of an annotation is shown as a tooltip, to read it while scrolling through the document
	const DocAnnotation* note = OnAnnotation(point);
	if (note != NULL && note->contents.Length() > 0) {
		if (mNoteTip != note->index + 1) {
			mNoteTip = note->index + 1;
			mLink = NULL;
			SetToolTip(NoteTipText(note).String());
			ShowToolTip();
		}
		SetViewCursor(gApp->handCursor);
		return;
	}
	if (mNoteTip != 0) {
		mNoteTip = 0;
		SetToolTip("");
		HideToolTip();
	}

	// over link?
	const DocLink* link;
	if ((link = OnLink(point)) != NULL) {
		// new link?
		if (link != mLink) {
			SetViewCursor(gApp->linkCursor);
			mLink = link;
			LinkToString(link, &str);
			SetToolTip( str.String() );
			ShowToolTip();
		}
	} else {
		if (mLink) {
			mLink = NULL;
			SetToolTip("");
			HideToolTip();
		}
		SetViewCursor(gApp->handCursor);
	}
}

///////////////////////////////////////////////////////////////////////////
void
PDFView::Redraw()
{
	PDFWindow* parentWin = GetPDFWindow();

	mMouseWheelDY = 0;

	// abort rendering process if neccesary and wait for it to finish
	WaitForPage(true);
	if (parentWin) {
		parentWin->NewPage(mCurrentPage);
	}
	mRendering = true;

	// A selection stays through zoom and rotation, it belongs to the page. Text is kept in page space, an
	// area is converted to page space here and back to the new zoom below.
	bool samePage = mRenderedPage == mCurrentPage;
	bool keepSelection = samePage && mSelected == SELECTED;
	bool keepArea = keepSelection && mSelectionKind == kSelectArea;
	fz_rect areaOnPage = fz_empty_rect;
	if (keepArea) {
		fz_point a = mPage->DevToPage(mSelection.LeftTop());
		fz_point b = mPage->DevToPage(mSelection.RightBottom());
		areaOnPage = fz_make_rect(fminf(a.x, b.x), fminf(a.y, b.y), fmaxf(a.x, b.x), fmaxf(a.y, b.y));
	}
	if (!keepSelection) {
		mSelected = NOT_SELECTED;
		mQuads.clear();
	}
	if (!samePage)
		mFindQuads.clear();
	mRenderedPage = mCurrentPage;

	mPageRenderer.Start(mPage, mCurrentPage, GetZoomDPI(), mRotation, &mRendererID);
	mLink = NULL;
	if (keepArea)
		mSelection = mPage->PageToDev(areaOnPage);

	mBitmap = mPage->GetBitmap();
	mWidth = mPage->GetWidth(); mHeight = mPage->GetHeight();
	CenterPage();
	FixScrollbars();

	if (parentWin) {
		parentWin->GetFileAttributes()->SetPage(mCurrentPage);
		parentWin->SetPage (mCurrentPage);
		parentWin->SetZoomSize (mWidth, mHeight);
	}

	Invalidate();
}

///////////////////////////////////////////////////////////
void
PDFView::RestartDoc() {
	WaitForPage(true);
	mRenderedPage = 0;
	Redraw();
}


///////////////////////////////////////////////////////////////////////////
void
PDFView::FixScrollbars ()
{
	BRect frame = Bounds();
	BScrollBar * scroll;
	float x, y;
	float bigStep, smallStep;

	x = mWidth - frame.Width();
	if (x < 0.0) {
		x = 0.0;
	}
	y = mHeight - frame.Height();
	if (y < 0.0) {
		y = 0.0;
	}

	scroll = ScrollBar (B_HORIZONTAL);
	scroll->SetRange (0.0, x);
	scroll->SetProportion ((mWidth - x) / mWidth);
	bigStep = frame.Width() - 2;
	smallStep = bigStep / 10.;
	scroll->SetSteps (smallStep, bigStep);

	scroll = ScrollBar (B_VERTICAL);
	scroll->SetRange (0.0, y);
	scroll->SetProportion ((mHeight - y) / mHeight);
	bigStep = frame.Height() - 2;
	smallStep = bigStep / 10.;
	scroll->SetSteps (smallStep, bigStep);
}

///////////////////////////////////////////////////////////////////////////
void
PDFView::PostRedraw(thread_id id, BBitmap *bitmap) {
	// TODO
	if (id != -1) {
		mRendering = false;
		mRendererID = -1;
		UpdateFindQuads();
		Invalidate();
		BPoint mouse; uint32 buttons;
		GetMouse(&mouse, &buttons);
		DisplayLink(mouse);
	}
}

///////////////////////////////////////////////////////////////////////////
void
PDFView::RedrawAborted(thread_id id, BBitmap *bitmap) {
	if ((mRendererID == id) && (id != -1)) {
		mRendering = false;
		mRendererID = -1;
	}
}

///////////////////////////////////////////////////////////////////////////
void
PDFView::WaitForPage(bool abort) {
	if (abort) {
		mPageRenderer.Abort();
	}
	mPageRenderer.Wait();
}

///////////////////////////////////////////////////////////////////////////
void
PDFView::CenterPage() {
	BRect bounds(Bounds());
	if (bounds.Width() + 1 > mWidth) { // center page horizontally
		mLeft = (bounds.Width() - mWidth) / 2;
	} else {
		mLeft = 0;
	}

	if (bounds.Height() + 1 > mHeight) { // center page vertically
		mTop = (bounds.Height() - mHeight) / 2;
	} else {
		mTop = 0;
	}
}

///////////////////////////////////////////////////////////////////////////
void
PDFView::Resize() {
	CenterPage();
	FixScrollbars();
	Invalidate();
}

///////////////////////////////////////////////////////////////////////////
void
PDFView::SetPage (int page)
{
	mSelected = NOT_SELECTED;

	int currentPage = mCurrentPage;
	if (mCurrentPage != page) {
		if (page < 1) {
			mCurrentPage = 1;
		}
		else if (page > GetNumPages()) {
			mCurrentPage = GetNumPages();
		}
		else {
			mCurrentPage = page;
		}

		if (currentPage != mCurrentPage) {
			Redraw ();
		}
	}
}

//////////////////////////////////////////////////////////////////
void
PDFView::MoveToPage(int page, bool top) {
	if (page > GetNumPages()) page = GetNumPages();
	if (page <= 0) page = 1;
	bool notChanged = mCurrentPage == page;
	if (notChanged) return;

	RecordHistory();

	BRect bounds(Bounds());
	ScrollTo(bounds.left, top ? 0 : mHeight);
	SetPage(page);
}

//////////////////////////////////////////////////////////////////
void
PDFView::BeginHistoryNavigation() {
	// Note: We are going into history navigation mode and
	// have to store the current position state in the history.
	if (mNavigationState == kNotInHistory) {
		mNavigationState = kInHistory;
		BRect bounds(Bounds());
		mHistory.AddPosition(mCurrentPage, mZoom, bounds.left, bounds.top, mRotation);
	}
}

//////////////////////////////////////////////////////////////////
void
PDFView::EndHistoryNavigation() {
	// Note: When not navigating through the history (= kNotInHistory) the
	// current position information is not stored in the history.
	// The state is stored prior to changes of the state to the history.
	// When in navigating through the history (kInHistory), the restored
	// state is the top of the history. This state has to be replaced
	// when we record the current state (= Back()).
	if (mNavigationState == kInHistory) {
		mNavigationState = kNotInHistory;
		mHistory.Back();
	}
}

//////////////////////////////////////////////////////////////////
void
PDFView::RecordHistory() {
	EndHistoryNavigation();
	BRect bounds(Bounds());
	mHistory.AddPosition(mCurrentPage, mZoom, bounds.left, bounds.top, mRotation);
}

//////////////////////////////////////////////////////////////////
void
PDFView::RecordHistory(entry_ref ref, const char* ownerPassword, const char* userPassword) {
	// XXX: record file open events too, otherwise they are missed if page state does not change between
	// open events.
	mHistory.SetFile(ref, ownerPassword, userPassword);
}

//////////////////////////////////////////////////////////////////
void
PDFView::RestoreHistory() {
	HistoryEntry* e = mHistory.GetTop();
	if (e == NULL) return;

	HistoryPosition* pos = dynamic_cast<HistoryPosition*>(e);
	if (pos) {
		PDFWindow* w = GetPDFWindow();
		HistoryFile* file = pos->GetFile();
		entry_ref ref = file->GetRef();

		if (w && !w->IsCurrentFile(&ref)) {
			bool encrypted;
			w->LoadFile(&ref, file->GetOwnerPassword(), file->GetUserPassword(), &encrypted);
			return;
		}

		int page; int32 left, top;
		page = pos->GetPage();
		mZoom = pos->GetZoom();
		left = pos->GetLeft();
		top = pos->GetTop();
		mRotation = pos->GetRotation();

		if (w) {
			w->SetZoom(mZoom); w->SetRotation(mRotation);
		}
		mCurrentPage = -1;
		SetPage(page);
		ScrollTo(left, top);
	}
}

//////////////////////////////////////////////////////////////////
void
PDFView::Back() {
	BeginHistoryNavigation();
	if (mHistory.Back()) {
		RestoreHistory();
	}
}
//////////////////////////////////////////////////////////////////
void
PDFView::Forward() {
	BeginHistoryNavigation();
	if (mHistory.Forward()) {
		RestoreHistory();
	}
}

//////////////////////////////////////////////////////////////////
void
PDFView::SetZoom (int zoom)
{
	if (mZoom != zoom) {
		RecordHistory();
		mZoom = zoom;
		Redraw ();
	}
}

//////////////////////////////////////////////////////////////////
void
PDFView::Zoom(bool zoomIn) {
	int32 zoomOld = GetZoomDPI();
	int32 zoomNew;
	if (zoomIn) {
		zoomNew = (int32)(zoomOld * 1.2);
		if (zoomNew > ZOOM_DPI_MAX) zoomNew = ZOOM_DPI_MAX;
	} else {
		zoomNew = (int32)(zoomOld / 1.2);
		if (zoomNew < ZOOM_DPI_MIN) zoomNew = ZOOM_DPI_MIN;
	}
	if (zoomNew != zoomOld) {
		SetZoom(-zoomNew);
		PDFWindow* w = GetPDFWindow();
		if (w) w->SetZoom(-zoomNew);
	}
}

//////////////////////////////////////////////////////////////////
void
PDFView::FitToPageWidth() {
	BRect r(Bounds());
	int32 zoomOld = GetZoomDPI();
	int32 zoomNew = (int32)(r.Width() * zoomOld / mWidth);
	if (zoomOld != zoomNew) {
		SetZoom(-zoomNew);
		PDFWindow* w = GetPDFWindow();
		if (w) w->SetZoom(-zoomNew);
	}
}

//////////////////////////////////////////////////////////////////
void
PDFView::FitToPage() {
	BRect r(Bounds());
	int32 zoomOld = GetZoomDPI();
	int32 zoomNewH = (int32)(r.Width() * zoomOld / mWidth);
	int32 zoomNewV = (int32)(r.Height() * zoomOld / mHeight);
	int32 zoomNew = (zoomNewH < zoomNewV) ? zoomNewH : zoomNewV;
	if (zoomOld != zoomNew) {
		SetZoom(-zoomNew);
		PDFWindow* w = GetPDFWindow();
		if (w) w->SetZoom(-zoomNew);
	}
}

//////////////////////////////////////////////////////////////////
int16
PDFView::GetZoomDPI() const {
	if (mZoom >= MIN_ZOOM)
		return kZoomDPI[mZoom -  MIN_ZOOM];
	else
		return -mZoom;
}

//////////////////////////////////////////////////////////////////
void
PDFView::SetRotation (float rotation)
{
	if (mRotation != rotation) {
		RecordHistory();
		gApp->GetSettings()->SetRotation(rotation);
		mRotation = rotation;
		PDFWindow* w = GetPDFWindow();
		if (w) w->SetRotation(mRotation);
		Redraw ();
	}
}

void
PDFView::RotateClockwise() {
	SetRotation(((int)mRotation + 90) % 360);
}

void
PDFView::RotateAntiClockwise() {
	SetRotation(((int)mRotation - 90 + 360) % 360);
}

///////////////////////////////////////////////////////////////////////////
// Text selection

// the area that is selected, in coordinates of the bitmap
BRect
PDFView::SelectionBounds() {
	if (mSelectionKind == kSelectArea)
		return mSelection;

	BRect bounds;
	for (size_t i = 0; i < mQuads.size(); i++)
		bounds = bounds | mPage->PageToDev(mQuads[i]);
	return bounds;
}

///////////////////////////////////////////////////////////////////////////
bool
PDFView::InSelection(BPoint point) {
	BPoint p = CorrectMousePos(point);
	if (mSelectionKind == kSelectArea)
		return mSelection.Contains(p);
	return InTextSelection(p);
}

bool
PDFView::InTextSelection(BPoint point) {
	fz_point p = mPage->DevToPage(point);
	for (size_t i = 0; i < mQuads.size(); i++) {
		if (fz_is_point_inside_quad(p, mQuads[i]))
			return true;
	}
	return false;
}

///////////////////////////////////////////////////////////////////////////
void
PDFView::StartTextSelection(BPoint point) {
	mSelectionKind = kSelectText;
	mQuads.clear();
	mTextStart = mTextEnd = mPage->DevToPage(point);
}

void
PDFView::ExtendTextSelection(BPoint point) {
	mTextEnd = mPage->DevToPage(LimitToPage(point));
	UpdateQuads(true);
}

// Finds the areas of the text between the two points, in page space.
void
PDFView::UpdateQuads(bool invalidate) {
	BRect old;
	if (invalidate && mSelectionKind == kSelectText)
		old = SelectionBounds();

	std::vector<fz_quad> quads;
	fz_stext_page* text = mPage->Text();
	if (text != NULL) {
		quads.resize(kMaxQuads);
		fz_quad* buffer = &quads[0];
		int count = 0;

		DocumentLocker locker(mDoc);
		fz_context* context = mDoc->Context();
		fz_var(count);
		fz_try(context) {
			count = fz_highlight_selection(context, text, mTextStart, mTextEnd, buffer, kMaxQuads);
		}
		fz_catch(context) {
			count = 0;
		}
		quads.resize(count);
	}
	mQuads.swap(quads);

	if (invalidate) {
		BRect changed = old | SelectionBounds();
		if (changed.IsValid())
			Invalidate(changed.InsetByCopy(-2, -2).OffsetByCopy(mLeft, mTop));
	}
}

// Selects the word or line at the position. Returns false if there is no text.
bool
PDFView::SelectTextAt(BPoint point, int mode) {
	fz_stext_page* text = mPage->Text();
	if (text == NULL)
		return false;

	fz_point start = mPage->DevToPage(CorrectMousePos(point));
	fz_point end = start;
	{
		DocumentLocker locker(mDoc);
		fz_context* context = mDoc->Context();
		fz_try(context) {
			fz_snap_selection(context, text, &start, &end, mode);
		}
		fz_catch(context) {
			return false;
		}
	}

	BRect old = mSelected != NOT_SELECTED ? SelectionBounds() : BRect();
	mSelectionKind = kSelectText;
	mTextStart = start;
	mTextEnd = end;
	UpdateQuads(false);
	if (mQuads.empty()) {
		mSelected = NOT_SELECTED;
		return false;
	}

	mSelected = SELECTED;
	BRect changed = old | SelectionBounds();
	if (changed.IsValid())
		Invalidate(changed.InsetByCopy(-2, -2).OffsetByCopy(mLeft, mTop));
	return true;
}

///////////////////////////////////////////////////////////////////////////
// Selects the text that has been found, from the start point to the end point (page space) and makes it
// visible. The caller holds the lock of the window.
void
PDFView::SelectFound(fz_point start, fz_point end) {
	BRect old = mSelected != NOT_SELECTED ? SelectionBounds() : BRect();
	mSelectionKind = kSelectText;
	mTextStart = start;
	mTextEnd = end;
	UpdateQuads(false);
	mSelected = mQuads.empty() ? NOT_SELECTED : SELECTED;

	BRect selection = SelectionBounds();
	BRect changed = old | selection;
	if (changed.IsValid())
		Invalidate(changed.InsetByCopy(-2, -2).OffsetByCopy(mLeft, mTop));

	if (selection.IsValid()) {
		// make selection visible
		BRect bounds(Bounds());
		BRect shown = selection.OffsetByCopy(mLeft, mTop);
		float x = bounds.left, y = bounds.top;
		if (shown.left < bounds.left || shown.right > bounds.right)
			x = selection.left - 20;
		if (shown.top < bounds.top || shown.bottom > bounds.bottom)
			y = selection.top - 40;
		ScrollTo(x, y);
	}
	SelectionChanged();
}

///////////////////////////////////////////////////////////////////////////
// returns the selected text, the caller deletes the string
BString*
PDFView::GetSelectedText() {
	if (mSelected != SELECTED)
		return NULL;
	fz_stext_page* text = mPage->Text();
	if (text == NULL)
		return NULL;

	char* copied = NULL;
	DocumentLocker locker(mDoc);
	fz_context* context = mDoc->Context();

	fz_var(copied);
	fz_try(context) {
		if (mSelectionKind == kSelectText)
			copied = fz_copy_selection(context, text, mTextStart, mTextEnd, 0);
		else {
			fz_point a = mPage->DevToPage(mSelection.LeftTop());
			fz_point b = mPage->DevToPage(mSelection.RightBottom());
			fz_rect area = fz_make_rect(fminf(a.x, b.x), fminf(a.y, b.y), fmaxf(a.x, b.x), fmaxf(a.y, b.y));
			copied = fz_copy_rectangle(context, text, area, 0);
		}
	}
	fz_catch(context) {
		copied = NULL;
	}

	if (copied == NULL)
		return NULL;

	BString* result = new BString(copied);
	fz_free(context, copied);
	if (result->Length() == 0) {
		delete result;
		return NULL;
	}
	return result;
}

///////////////////////////////////////////////////////////////////////////
void
PDFView::CopyText(BString *str) {
	if (be_clipboard->Lock()) {
		be_clipboard->Clear();

		BMessage *clip = NULL;
		if ((clip = be_clipboard->Data()) != NULL) {
			// copy text to clipboard
			clip->AddData("text/plain", B_MIME_TYPE, str->String(), str->Length());
			be_clipboard->Commit();
		}
		be_clipboard->Unlock();
	}
}

///////////////////////////////////////////////////////////////////////////
// the bitmap of the area that is selected; the caller deletes it
static BBitmap*
CopyBitmapArea(BBitmap* source, BRect area, float width, float height) {
	BRect sel(max_c(area.left, 0), max_c(area.top, 0), min_c(area.right, width - 1),
		min_c(area.bottom, height - 1));
	if (!sel.IsValid())
		return NULL;

	BRect rect(0, 0, sel.Width(), sel.Height());
	BView view(rect, NULL, B_FOLLOW_NONE, B_WILL_DRAW);
	BBitmap* bitmap = new BBitmap(rect, source->ColorSpace(), true);
	if (bitmap->Lock()) {
		bitmap->AddChild(&view);
		view.DrawBitmap(source, sel, rect);
		view.Sync();
		bitmap->RemoveChild(&view);
		bitmap->Unlock();
	}
	return bitmap;
}

void
PDFView::CopySelection() {
	if (mSelected != SELECTED)
		return;

	BString* text = GetSelectedText();
	if (mSelectionKind == kSelectText) {
		// text only, a flowing selection has no rectangle to take a picture of
		if (text != NULL) {
			CopyText(text);
			delete text;
		}
		return;
	}

	if (mSelection.left < mSelection.right && mSelection.top < mSelection.bottom) {
		if (be_clipboard->Lock()) {
			be_clipboard->Clear();

			BMessage *clip = NULL;
			if ((clip = be_clipboard->Data()) != NULL) {
				// copy bitmap to clipboard
				BBitmap* bitmap = CopyBitmapArea(mBitmap, mSelection, mWidth, mHeight);
				if (bitmap != NULL) {
					BMessage data;
					bitmap->Archive(&data);
					clip->AddMessage("image/x-vnd.Be-bitmap", &data);
					clip->AddRect("rect", bitmap->Bounds());
					delete bitmap;
				}

				// copy text to clipboard
				if (text != NULL) {
					clip->AddData("text/plain", B_MIME_TYPE, text->String(), text->Length());
				}
				be_clipboard->Commit();
			}
			be_clipboard->Unlock();
		}
	}
	delete text;
}


///////////////////////////////////////////////////////////
void PDFView::SelectAll() {
	if (!mDoc->CanCopy())
		return;

	fz_rect bounds;
	if (!mDoc->PageBounds(mCurrentPage, &bounds))
		return;

	mSelectionKind = kSelectText;
	mTextStart = fz_make_point(bounds.x0, bounds.y0);
	mTextEnd = fz_make_point(bounds.x1, bounds.y1);
	UpdateQuads(false);
	mSelected = mQuads.empty() ? NOT_SELECTED : SELECTED;
	SelectionChanged();
	Invalidate();
}

///////////////////////////////////////////////////////////
void PDFView::SelectNone() {
	if (mSelected == SELECTED) {
		mSelected = NOT_SELECTED;
		mQuads.clear();
		SelectionChanged();
		Invalidate();
	}
}

///////////////////////////////////////////////////////////
void PDFView::SetFilledSelection(bool filled) {
	mFilledSelection = filled;
	gApp->GetSettings()->SetFilledSelection(filled);

	if (mSelected == SELECTED) {
		Invalidate();
	}
}

///////////////////////////////////////////////////////////
// Marks the selected text in the document: adds an annotation that covers the lines of the selection.
bool
PDFView::AnnotateSelection(MarkupType type, uint32 rgb)
{
	if (!HasTextSelection() || mQuads.empty() || !mDoc->CanEditAnnotations()) {
		beep();
		return false;
	}

	if (!ConfirmEditable())
		return false;

	float color[3] = { ((rgb >> 16) & 0xff) / 255.0f, ((rgb >> 8) & 0xff) / 255.0f, (rgb & 0xff) / 255.0f };
	std::vector<fz_quad> quads = mQuads;
	WaitForPage(true);
	if (!mDoc->AddMarkup(mCurrentPage, type, &quads[0], (int)quads.size(), color))
		return false;

	SelectNone();
	AnnotationsChanged();
	return true;
}


// A file that cannot be written (system directory, read-only volume) can still be annotated, but the changes can
// only be kept in a copy. Says so before the first change.
bool
PDFView::ConfirmEditable()
{
	if (mDoc->IsWritable() || mReadOnlyWarned)
		return true;
#ifdef TSUNDOKU_TESTING
	if (getenv("TSUNDOKU_AUTOCONFIRM") != NULL)
		return true;
#endif

	BAlert* alert = new BAlert("Read-only", B_TRANSLATE("This file is read-only. You can add annotations, "
		"but to keep them you have to save a copy with “Save as…”."), B_TRANSLATE("Cancel"),
		B_TRANSLATE("Continue"), NULL, B_WIDTH_AS_USUAL, B_WARNING_ALERT);
	alert->SetShortcut(0, B_ESCAPE);
	if (alert->Go() != 1)
		return false;
	mReadOnlyWarned = true;
	return true;
}


// Shows the changed annotations of the page.
void
PDFView::AnnotationsChanged()
{
	mRenderedPage = 0;	// the page is new, nothing of the old one stays
	mNoteTip = 0;
	Redraw();
	SelectionChanged();
}


void PDFView::SelectionChanged() {
	PDFWindow* w = GetPDFWindow();
	if (w) {
		w->UpdateInputEnabler();
	}
}

///////////////////////////////////////////////////////////
void PDFView::SendDragMessage(uint32 protocol) {
	BRect selection = SelectionBounds();
	if (!selection.IsValid())
		return;

	mDragStarted = true;
	SetMouseEventMask(B_POINTER_EVENTS);
	if (protocol == B_SIMPLE_DATA) {
		BMessage drag(B_SIMPLE_DATA);
		drag.AddString("be:types", "text/plain");
		drag.AddString("be:types", B_FILE_MIME_TYPE);

		BTranslatorRoster *roster = BTranslatorRoster::Default();
		BBitmapStream stream(mBitmap);

		translator_info *outInfo;
		int32 outNumInfo;

		drag.AddString("be:filetypes", "text/plain");
		drag.AddString("be:type_descriptions", "Text");

		if ((B_OK == roster->GetTranslators(&stream, NULL, &outInfo, &outNumInfo)) &&
			(outNumInfo >= 1)) {
			for (int32 i = 0; i < outNumInfo; i++) {
				const translation_format *fmts;
				int32 num_fmts;
				roster->GetOutputFormats(outInfo[i].translator, &fmts, &num_fmts);
				for (int32 j = 0; j < num_fmts; j++) {
					if (strcmp(fmts[j].MIME, "image/x-be-bitmap") != 0) {
						drag.AddString("be:filetypes", fmts[j].MIME);
						drag.AddString("be:type_descriptions", fmts[j].name);
					}
				}
			}
			drag.AddInt32("be:actions", B_COPY_TARGET);
			drag.AddString("be:clip_name", "Untitled clipping");
			DragMessage(&drag, selection.OffsetByCopy(mLeft, mTop));
		}
		BBitmap *bm;
		stream.DetachBitmap(&bm);
		if (bm != mBitmap) delete bm;
	} else if (protocol == B_MIME_DATA) {
		BMessage drag(B_MIME_DATA);
		BString *str = GetSelectedText();
		if (str) {
			drag.AddInt32("be:actions", B_TRASH_TARGET);
			drag.AddData("text/plain", B_MIME_DATA, str->String(), str->Length());
			delete str;
			DragMessage(&drag, selection.OffsetByCopy(mLeft, mTop));
		}
	}
}

void PDFView::SendDataMessage(BMessage *reply) {
	BMessage data(B_MIME_DATA);

	entry_ref dir;
	BString name, filetype;
	if (B_OK != reply->FindString("be:filetypes", &filetype)) {
		return;
	}
	bool saveToFile = (B_OK == reply->FindRef("directory", &dir)) &&
					  (B_OK == reply->FindString("name", &name));

	if (filetype == "text/plain") {
		BString *str = GetSelectedText();
		if (str) {
			if (saveToFile) {
				// write text to file
				BDirectory d(&dir);
				BNode node(&d, name.String());
				// set mime type
				BNodeInfo info(&node);
				if (info.InitCheck() == B_OK) {
					info.SetType("text/plain");
				}
				// write data
				BFile file(&d, name.String(), B_WRITE_ONLY);
				file.Write(str->String(), str->Length());
			} else {
				// send text in message to target application
				data.AddString("text/plain", str->String());
				reply->SendReply(&data);
			}
			delete str;
		}
		return;
	}

	//~ sending image in message to target application not implemented
	if (!saveToFile) return;

	// copy selection to bitmap
	BBitmap *bitmap = CopyBitmapArea(mBitmap, SelectionBounds(), mWidth, mHeight);
	if (bitmap == NULL)
		return;

	BBitmapStream stream(bitmap); // destructor frees bitmap

	// identify type
	BTranslatorRoster *roster = BTranslatorRoster::Default();
	translator_info *outInfo;
	int32 outNumInfo;
	if ((B_OK == roster->GetTranslators(&stream, NULL, &outInfo, &outNumInfo)) &&
		(outNumInfo >= 1)) {

		for (int32 i = 0; i < outNumInfo; i++) {
			const translation_format *fmts;
			int32 num_fmts;
			roster->GetOutputFormats(outInfo[i].translator, &fmts, &num_fmts);
			for (int32 j = 0; j < num_fmts; j++) {
				if (strcmp(fmts[j].MIME, filetype.String()) == 0) {
					// save bitmap to file
					BDirectory d(&dir);
					BNode node(&d, name.String());
					// set mime type
					BNodeInfo info(&node);
					if (info.InitCheck() == B_OK) {
						info.SetType(fmts[j].MIME);
					}
					// write data
					BFile file(&d, name.String(), B_WRITE_ONLY);
					roster->Translate(&stream, NULL, NULL, &file, fmts[j].type);
					return;
				}
			}
		}
	}
}

///////////////////////////////////////////////////////////
void
PDFView::SetColorSpace(color_space colorSpace) {
	// the page is always rendered as B_RGB32, how it is shown is up to the app_server
	mColorSpace = colorSpace;
}

///////////////////////////////////////////////////////////
void
PDFView::UpdateSettings(GlobalSettings* settings) {
	mInvertVerticalScrolling = settings->GetInvertVerticalScrolling();
}


#ifdef TSUNDOKU_TESTING
// Test hook: "hey Tsundoku 'TSTX' to Window 0 with cmd=select with x1=.. " drives the view like the user does and
// writes what happened to /tmp/ts_test.out.
static void
TestLog(const char* format, ...)
{
	FILE* out = fopen("/tmp/ts_test.out", "a");
	if (out == NULL)
		return;
	va_list args;
	va_start(args, format);
	vfprintf(out, format, args);
	va_end(args);
	fputc('\n', out);
	fclose(out);
}

bool DrawPageInSlices(Document* document, int page, double dpi, int rotation, BView* view,
	PrintingProgressWindow* progress);

// invokes the default button of every window that has one, except the main window
static int32
TestPressDialogs(void*)
{
	for (int i = 0; i < 120; i++) {
		snooze(500000);
		for (int32 w = 0; w < be_app->CountWindows(); w++) {
			BWindow* window = be_app->WindowAt(w);
			// the main window is locked by the thread that waits for the dialog
			if (window == NULL || window->LockWithTimeout(100000) != B_OK)
				continue;
			BButton* button = window->DefaultButton();
			if (button != NULL && button->IsEnabled() && !window->IsHidden()) {
				TestLog("pressing %s in [%s]", button->Label(), window->Title());
				button->Invoke();
			}
			window->Unlock();
		}
	}
	return 0;
}

// hey passes numbers as it likes
static float
TestNumber(BMessage* message, const char* name)
{
	float f;
	if (message->FindFloat(name, &f) == B_OK)
		return f;
	int32 i;
	if (message->FindInt32(name, &i) == B_OK)
		return i;
	const char* string;
	if (message->FindString(name, &string) == B_OK)
		return atof(string);
	return 0;
}

void
PDFView::TestCommand(BMessage* message)
{
	BString cmd;
	message->FindString("cmd", &cmd);
	float x1 = TestNumber(message, "x1"), y1 = TestNumber(message, "y1");
	float x2 = TestNumber(message, "x2"), y2 = TestNumber(message, "y2");
	int32 page = (int32)TestNumber(message, "page");

	if (cmd == "select" || cmd == "area") {
		// as the secondary mouse button does it
		SetAction(SELECT_ACTION);
		mSelected = DO_SELECTION;
		mSelectionKind = cmd == "area" ? kSelectArea : kSelectText;
		BPoint start = LimitToPage(CorrectMousePos(BPoint(x1, y1)));
		mSelectionStart = start;
		mSelection.SetLeftTop(start);
		mSelection.SetRightBottom(start);
		if (mSelectionKind == kSelectText)
			StartTextSelection(start);
		ResizeSelection(BPoint(x2, y2));
		MouseUp(BPoint(x2, y2));
		BString* text = GetSelectedText();
		TestLog("%s (%g,%g)-(%g,%g): %d quads, text: [%s]", cmd.String(), x1, y1, x2, y2, (int)mQuads.size(),
			text != NULL ? text->String() : "(none)");
		delete text;
	} else if (cmd == "word" || cmd == "line") {
		bool ok = SelectTextAt(BPoint(x1, y1), cmd == "word" ? FZ_SELECT_WORDS : FZ_SELECT_LINES);
		BString* text = GetSelectedText();
		TestLog("%s at (%g,%g): %s, text: [%s]", cmd.String(), x1, y1, ok ? "ok" : "nothing", text != NULL ? text->String() : "(none)");
		delete text;
	} else if (cmd == "annotate") {
		// marks the selection: type= highlight, underline or strikeout (in "kind")
		BString kind;
		message->FindString("kind", &kind);
		MarkupType type = kind == "underline" ? kMarkupUnderline
			: kind == "strikeout" ? kMarkupStrikeOut : kMarkupHighlight;
		bool ok = AnnotateSelection(type, kind == "highlight" ? 0xffeb3b : 0xe53935);
		TestLog("annotate %s: %s, unsaved changes: %d", kind.String(), ok ? "ok" : "failed",
			(int)mDoc->HasUnsavedChanges());
	} else if (cmd == "popup") {
		// the context menu as a secondary click at (x1, y1) shows it (blocks until it is closed)
		BPoint where(x1, y1);
		ShowPopUpMenu(ConvertToScreen(where), OnLink(where), OnAnnotation(where));
	} else if (cmd == "colormenu") {
		// the items of the color submenu as a menu of their own, with "color" as the current one
		BMessage change(CHANGE_COLOR_MSG);
		BMenu* colors = BuildColorMenu("Color", change, this, true, (uint32)TestNumber(message, "color"));
		BPopUpMenu* popup = new BPopUpMenu("colors");
		popup->SetAsyncAutoDestruct(true);
		while (BMenuItem* item = colors->RemoveItem((int32)0))
			popup->AddItem(item);
		delete colors;
		popup->Go(ConvertToScreen(BPoint(x1, y1)), true, false, false);
	} else if (cmd == "setcolor") {
		bool ok = mDoc->SetAnnotationColor(mCurrentPage, (int)TestNumber(message, "which"),
			(uint32)TestNumber(message, "color"));
		if (ok)
			AnnotationsChanged();
		TestLog("setcolor: %s", ok ? "ok" : "failed");
	} else if (cmd == "hover") {
		// as if the mouse was at (x1, y1): cursor and tooltip
		DisplayLink(BPoint(x1, y1));
		ShowToolTip(ToolTip());	// a real mouse shows it when it rests
		TestLog("hover (%g,%g): note tip %d", x1, y1, mNoteTip);
	} else if (cmd == "annots") {
		// what the page has, and what is under a point (x1, y1)
		WaitForPage();
		const std::vector<DocAnnotation>& list = mPage->mAnnotations;
		TestLog("annots on page %d: %d", mCurrentPage, (int)list.size());
		for (size_t i = 0; i < list.size(); i++) {
			TestLog("  #%d type %d markup %d quads %d color %s%06x rect %g,%g-%g,%g author [%s] note [%s]", list[i].index,
				list[i].type, (int)list[i].isMarkup, (int)list[i].quads.size(), list[i].hasColor ? "#" : "none ",
				(unsigned)list[i].color, list[i].rect.x0, list[i].rect.y0,
				list[i].rect.x1, list[i].rect.y1, list[i].author.String(), list[i].contents.String());
		}
		const DocAnnotation* under = OnAnnotation(BPoint(x1, y1));
		TestLog("  at (%g,%g): %s", x1, y1, under != NULL ? "annotation" : "nothing");
	} else if (cmd == "delannot") {
		bool ok = mDoc->DeleteAnnotation(mCurrentPage, (int)TestNumber(message, "which"));
		if (ok)
			AnnotationsChanged();
		TestLog("delannot: %s", ok ? "ok" : "failed");
	} else if (cmd == "setnote") {
		BString text;
		message->FindString("text", &text);
		bool ok = mDoc->SetAnnotationContents(mCurrentPage, (int)TestNumber(message, "which"), text.String());
		if (ok)
			AnnotationsChanged();
		TestLog("setnote: %s", ok ? "ok" : "failed");
	} else if (cmd == "selectall") {
		SelectAll();
		BString* text = GetSelectedText();
		TestLog("selectall: %d quads, %d chars", (int)mQuads.size(), text != NULL ? (int)text->Length() : 0);
		delete text;
	} else if (cmd == "selectnone") {
		SelectNone();
		TestLog("selectnone: %d quads", (int)mQuads.size());
	} else if (cmd == "link") {
		const DocLink* link = OnLink(BPoint(x1, y1));
		BString description;
		LinkToString(link, &description);
		bool handled = HandleLink(BPoint(x1, y1));
		TestLog("link at (%g,%g): %s -> %s, handled %d, page now %d", x1, y1, link != NULL ? link->uri.String() : "none",
			description.String(), handled, mCurrentPage);
	} else if (cmd == "links") {
		int n = 0;
		for (size_t i = 0; i < mPage->mLinks.size(); i++) {
			const DocLink& l = mPage->mLinks[i];
			BRect r = mPage->PageToDev(l.rect);
			int target; float x, y;
			bool internal = mDoc->ResolveLink(l.uri.String(), &target, &x, &y);
			if (n++ < 6)
				TestLog("link %d: [%s] at view %g,%g-%g,%g -> %s %d", (int)i, l.uri.String(), r.left, r.top, r.right,
					r.bottom, internal ? "page" : "external", internal ? target : 0);
		}
		TestLog("page %d has %d links", mCurrentPage, (int)mPage->mLinks.size());
	} else if (cmd == "printslices") {
		// the drawing of the printing, into a bitmap that is saved: x1 is the dpi, page the page
		bool drawn = false;
		fz_matrix matrix;
		int width = 0, height = 0;
		if (mDoc->PageMatrix(page, x1, (int)mRotation, &matrix, &width, &height)) {
			BBitmap bitmap(BRect(0, 0, width - 1, height - 1), B_RGB32, true);
			BView* view = new BView(bitmap.Bounds(), "print", B_FOLLOW_NONE, B_WILL_DRAW);
			if (bitmap.Lock()) {
				bitmap.AddChild(view);
				drawn = DrawPageInSlices(mDoc, page, x1, (int)mRotation, view, NULL);
				view->Sync();
				bitmap.RemoveChild(view);
				bitmap.Unlock();
			}
			delete view;
			BString path;
			path << "/tmp/printslices-" << (int)page << "-" << (int)x1 << ".png";
			BBitmapStream stream(&bitmap);
			BFile file(path.String(), B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
			status_t status = BTranslatorRoster::Default()->Translate(&stream, NULL, NULL, &file, B_PNG_FORMAT);
			BBitmap* detached;
			stream.DetachBitmap(&detached);
			TestLog("printslices page %d at %g dpi: %dx%d, drawn %d, saved %s", (int)page, x1, width, height, drawn,
				status == B_OK ? path.String() : "FAILED");
		}
	} else if (cmd == "print") {
		// presses the default button of the dialogs of the printing (page setup and job), then prints
		TestLog("print: starting");
		thread_id helper = spawn_thread(TestPressDialogs, "press dialogs", B_NORMAL_PRIORITY, NULL);
		resume_thread(helper);
		Print();
	} else if (cmd == "goto") {
		MoveToPage(page);
		TestLog("goto %d: page now %d", (int)page, mCurrentPage);
	} else if (cmd == "scrollto") {
		ScrollTo(x1, y1);
		TestLog("scrollto (%g,%g)", x1, y1);
	} else if (cmd == "zoom") {
		SetZoom(-(int)x1);
		TestLog("zoom %g dpi", x1);
	} else if (cmd == "dump") {
		BRect bounds = Bounds();
		TestLog("page %d of %d, bitmap %gx%g, left/top %g/%g, view bounds %g,%g-%g,%g, selected %d, rendering %d",
			mCurrentPage, GetNumPages(), mWidth, mHeight, mLeft, mTop, bounds.left, bounds.top, bounds.right,
			bounds.bottom, (int)mSelected, (int)mRendering);
	}
	Invalidate();
}
#endif

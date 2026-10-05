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

// BeOS
#include <be/interface/PrintJob.h>
// BePDF
#include "Application.h"
#include "PDFView.h"
#include "PageRenderer.h"
#include "PrintingProgressWindow.h"
#include "Globals.h"

///////////////////////////////////////////////////////////////////////////
/*
	based up Be sample code in "Be Newsletter Volume 2, Issue 18 -- May 6, 1998"
*/

status_t
PDFView::PageSetup()
{
	status_t result = B_ERROR;

	BPrintJob  printJob(this->mTitle->String());


	if (mPrintSettings != NULL) {
		/* page setup has already been run */
		printJob.SetSettings(new BMessage(*mPrintSettings));
	}

	result = printJob.ConfigPage();

	if (result == B_NO_ERROR) {
	
		delete mPrintSettings;
		mPrintSettings = printJob.Settings();
	}

	return result;
}


///////////////////////////////////////////////////////////////////////////
class PrintView : public BView {
public:
	PrintView(PDFView *view, Document *mDoc, BMessage *printSettings, const char *title, BRect rect);

	void SetPage(int32 page);
	void Draw(BRect updateRect);
	friend int32 printing_thread(void *data);

	void Print();

private:
	PDFView *mView;
	Document *mDoc;
	bool mColorMode;
	int mPageWidth; // of print page
	int mPageHeight; // of print page
	int32 mCurrentPage;
	char *mTitle;
	int mZoom;
	double mScale;
	int mRotation;
	int16 mPrintSelection;
	int16 mPrintOrder;
	BMessage *mPrintSettings;
	BRect mRect;
	PrintingProgressWindow *mProgressWindow;
};

///////////////////////////////////////////////////////////////////////////
PrintView::PrintView(PDFView *view, Document *doc, BMessage *printSettings, const char *title, BRect rect) :
	BView (BRect(1000, 1000, 1000+rect.Width(), 1000+rect.Height()), "print_view", B_FOLLOW_NONE, B_WILL_DRAW) {
	GlobalSettings *s = gApp->GetSettings();
	mView = view; // PDFView
	mDoc = doc;
	mPrintSettings = printSettings;
	mTitle = (char*)title;
	mZoom = s->GetZoomPrinter();
	mRotation = (int)s->GetRotationPrinter();
	mRect = rect;
	mScale = s->GetDPI() / 72.0;
	mPrintSelection = s->GetPrintSelection();
	mPrintOrder = s->GetPrintOrder();
	// the printer driver takes care of gray scale
	mColorMode = s->GetPrintColorMode() == GlobalSettings::PRINT_COLOR_MODE;
	mProgressWindow = NULL;
	mCurrentPage = 1;
	mPageWidth = mPageHeight = 1;
}

///////////////////////////////////////////////////////////////////////////
void PrintView::SetPage(int32 page) {
	mCurrentPage = page;
}

///////////////////////////////////////////////////////////////////////////
// Draws the page into the view, in slices of about 4 MB (4 bytes per pixel) to keep the memory needed within
// bounds when it is printed with a high resolution. The view is scaled, so it draws in device pixels.
// Returns false if the page does not exist.
bool
DrawPageInSlices(Document* document, int page, double dpi, int rotation, BView* view,
	PrintingProgressWindow* progress)
{
	fz_matrix matrix;
	int width, height;
	if (!document->PageMatrix(page, dpi, rotation, &matrix, &width, &height))
		return false;

	const int64 maxSize = 1024 * 1024;
	int64 slices = (int64)width * height / maxSize;
	if (slices <= 0) {
		slices = 1;
	}
	int sliceHeight = height / slices;
	if (sliceHeight <= 0) {
		sliceHeight = 1;
	}

	BBitmap* bitmap = NULL;
	for (int sliceY = 0; sliceY < height; sliceY += sliceHeight) {
		if (progress != NULL && (progress->Stopped() || progress->Aborted()))
			break;
		if (sliceY + sliceHeight > height) {
			sliceHeight = height - sliceY;
		}
		if (sliceHeight <= 0) {
			break;
		}

		if (bitmap == NULL || bitmap->Bounds().Height() + 1 < sliceHeight || bitmap->Bounds().Width() + 1 < width) {
			delete bitmap;
			bitmap = new BBitmap(BRect(0, 0, width - 1, sliceHeight - 1), B_RGB32);
		}

		// the slice starts at sliceY
		fz_matrix sliceMatrix = fz_concat(matrix, fz_translate(0, -sliceY));
		fz_cookie cookie;
		memset(&cookie, 0, sizeof(cookie));
		PageRenderer::RenderToBitmap(document, page, sliceMatrix, bitmap, width, sliceHeight, &cookie);
		view->DrawBitmap(bitmap, BRect(0, 0, width - 1, sliceHeight - 1),
			BRect(0, sliceY, width - 1, sliceY + sliceHeight - 1));
		view->Sync();
	}
	delete bitmap;
	return true;
}


///////////////////////////////////////////////////////////////////////////
void
PrintView::Draw(BRect updateRect)
{
	if (Window()->Lock()) {
		int32 zoomDPI = mZoom * 72 / 100;
		DrawPageInSlices(mDoc, mCurrentPage, mScale * zoomDPI, mRotation, this, mProgressWindow);
		Flush();
		Window()->Unlock();
	}
}


// printing thread
int32 printing_thread(void *data) {
	PrintView *view = (PrintView*)data;
	view->Print();
	delete view;
	return 0;
}


void PrintView::Print() {
	BPrintJob printJob(mTitle);
	printJob.SetSettings(new BMessage(*mPrintSettings));
	PrintingProgressWindow *progress = NULL;
	PrintingHiddenWindow *hiddenWin = NULL;
	
	if (printJob.ConfigJob() == B_OK) {
		int32  curPage = 1;
		int32  firstPage;
		int32  lastPage;
		int32  pagesInDocument;
		BRect  pageRect = printJob.PrintableRect();

		pagesInDocument = mDoc->PageCount();
		firstPage = printJob.FirstPage();
		lastPage = printJob.LastPage();
		if (firstPage < 1) {
			firstPage = 1;
		}
		if (lastPage > pagesInDocument) {
			lastPage = pagesInDocument;
		}
		
		if (mScale == 0) { // set DPI to maximum of printer resolution
		int32 xdpi, ydpi;
			printJob.GetResolution(&xdpi, &ydpi);
			// Max. 300 DPI otherwise we might run out of memory
			// and freeze BeOS!
			// TODO Change if/when Zeta/Haiku can handle more memory!
			if (xdpi > 300) {
				xdpi = 300;
			}
			if (ydpi > 300) {
				ydpi = 300;
			}
#ifdef MORE_DEBUG
			fprintf(stderr, "print resolution= %d %d\n", xdpi, ydpi);
#endif
			if (xdpi > 0 && ydpi > 0) {
				if (xdpi > ydpi) {
					mScale = xdpi / 72;
				} else {
					mScale = ydpi / 72;
				}
			} else {
				mScale = 300 / 72; // default
			}
		}
		
		bool normalOrder = mPrintOrder == GlobalSettings::NORMAL_PRINT_ORDER;
		int16 incr = (mPrintSelection == GlobalSettings::PRINT_ALL_PAGES) ? 1 : 2;
		int32 pages = 0;

		switch (mPrintSelection) {
			case GlobalSettings::PRINT_ALL_PAGES:
				pages = lastPage - firstPage + 1;
				break;
			case GlobalSettings::PRINT_EVEN_PAGES:
				if (firstPage % 2 == 1) firstPage ++;
				if (lastPage % 2 == 1) lastPage --;
				pages = (lastPage - firstPage) / 2 + 1;
				break;
			case GlobalSettings::PRINT_ODD_PAGES:
				if (firstPage % 2 == 0) firstPage ++;
				if (lastPage % 2 == 0) lastPage --;
				pages = (lastPage - firstPage) / 2 + 1;
				break;
		}

		if (normalOrder) {
			curPage = firstPage;
		} else {
			curPage = lastPage;
			incr = -incr;
		}
		
		hiddenWin = new PrintingHiddenWindow(BRect(-100, -100, -10, -10));
		mProgressWindow = progress = new PrintingProgressWindow(mTitle, mRect, pages);
		if (hiddenWin->Lock()) {
			hiddenWin->AddChild(this);
			SetScale(1.0 / mScale);
			hiddenWin->Unlock();
		}

		int32 zoomDPI = mZoom * 72 / 100;
		zoomDPI = (int32) (zoomDPI * mScale);

		printJob.BeginJob();	

		for (; ((normalOrder && (curPage <= lastPage)) || (!normalOrder && (curPage >= firstPage))) && !progress->Stopped(); curPage += incr) {
			SetPage(curPage);
			progress->SetPage(curPage);
			fz_matrix pageMatrix;
			int pageWidth, pageHeight;
			if (!mDoc->PageMatrix(curPage, zoomDPI, mRotation, &pageMatrix, &pageWidth, &pageHeight))
				continue;
			float width = pageWidth;
			float height = pageHeight;

			mPageWidth = pageWidth;
			mPageHeight = pageHeight;
			BRect curPageRect(0, 0, width, height);
			// center page
			BPoint origin((pageRect.Width() - width / mScale) / 2,
							(pageRect.Height() - height / mScale) / 2);

			printJob.DrawView(this, curPageRect, origin);
			
			printJob.SpoolPage();
			if (!printJob.CanContinue() || progress->Aborted()) {

				if (hiddenWin->Lock()) {
					hiddenWin->RemoveChild(this);
					hiddenWin->Unlock();
				}
				goto catastrophic_exit;
			}
		}
		if (hiddenWin->Lock()) {
			hiddenWin->RemoveChild(this);
			hiddenWin->Unlock();
		}

		printJob.CommitJob();
	}

catastrophic_exit:
	if (progress != NULL) progress->PostMessage(B_QUIT_REQUESTED);
	if (hiddenWin != NULL) hiddenWin->PostMessage(B_QUIT_REQUESTED);
}

///////////////////////////////////////////////////////////////////////////
void 
PDFView::Print()
{
	if (mPrintSettings == NULL && PageSetup() != B_NO_ERROR) {
		return;
	}
	
	PrintView *pView = new PrintView(this, mDoc, mPrintSettings, 
		mTitle->String(), 
		Bounds());

	thread_id tid = spawn_thread(printing_thread, "printing_thread", B_NORMAL_PRIORITY, pView);
	resume_thread(tid);
}


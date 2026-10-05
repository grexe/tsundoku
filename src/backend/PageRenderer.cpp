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
#include "PageRenderer.h"
#include "Globals.h"

#include <stdio.h>

#include <Message.h>

#include "CachedPage.h"

// fz_try() uses setjmp()/longjmp(): no C++ objects with destructors in fz_try() blocks.

int32 page_rendering_thread(void* data);


PageRenderer::PageRenderer()
	:
	mDocument(NULL),
	mHeld(NULL),
	mLock("page renderer"),
	mRunning(false),
	mRetired(false),
	mOwnedPage(NULL),
	mWidth(0),
	mHeight(0),
	mRenderingThread(-1),
	mPage(NULL),
	mBitmap(NULL),
	mScratch(NULL),
	mPageNo(0)
{
	memset(&mCookie, 0, sizeof(mCookie));
}


PageRenderer::~PageRenderer()
{
	Abort();
	Wait();
	delete mScratch;
}


void
PageRenderer::SetDocument(Document* document)
{
	Abort();
	Wait();
	mDocument = document;
}


void
PageRenderer::SetListener(BLooper* looper, BHandler* handler)
{
	mListener = BMessenger(handler, looper);
}


void
PageRenderer::Start(CachedPage* page, int pageNo, int zoomDPI, int rotation, thread_id* id, bool keepImage)
{
	// stop thread
	Abort();
	Wait();

	mPage = page;
	mPageNo = pageNo;

	fz_matrix matrix;
	int width, height;
	if (mDocument == NULL || !mDocument->PageMatrix(pageNo, zoomDPI, rotation, &matrix, &width, &height)) {
		matrix = fz_identity;
		width = height = 1;
	}
	mWidth = width;
	mHeight = height;

	// (re-)create bitmap, it is reused as long as it is large enough
	mBitmap = mPage->GetBitmap();
	bool oldImage = mBitmap != NULL && mPage->GetWidth() == width && mPage->GetHeight() == height;
	if (mBitmap == NULL || width > mBitmap->Bounds().Width() + 1 || height > mBitmap->Bounds().Height() + 1) {
		delete mBitmap;
		mBitmap = new BBitmap(BRect(0, 0, width - 1, height - 1), B_RGB32);
		if (mBitmap->InitCheck() != B_OK) {
			// out of memory, e.g. a large page with a high zoom: show a white pixel instead of crashing
			fprintf(stderr, "Tsundoku: cannot allocate a bitmap of %dx%d pixels\n", width, height);
			delete mBitmap;
			width = height = 1;
			mWidth = mHeight = 1;
			mBitmap = new BBitmap(BRect(0, 0, 0, 0), B_RGB32);
			memset(mBitmap->Bits(), 0xff, mBitmap->BitsLength());
			matrix = fz_identity;
		}
		mPage->SetBitmap(mBitmap, width, height);
	} else
		mPage->SetBitmapSize(width, height);

	// the old image stays if it is of the size of the new one, the new one is made in a bitmap of its own and
	// copied over when it is done (no white flash in between)
	delete mScratch;
	mScratch = NULL;
	if (keepImage && oldImage && width > 1 && height > 1) {
		mScratch = new BBitmap(BRect(0, 0, width - 1, height - 1), B_RGB32);
		if (mScratch->InitCheck() != B_OK) {
			delete mScratch;
			mScratch = NULL;
		}
	}

	mPage->MakeEmpty();
	mPage->mDocument = mDocument;
	mPage->SetMatrix(matrix);
	mPage->SetState(CachedPage::RENDERING);

	// start new thread, which holds a reference to the document until it is done
	memset(&mCookie, 0, sizeof(mCookie));
	if (mDocument != NULL) {
		mDocument->Acquire();
		mHeld = mDocument;
	}
	mLock.Lock();
	mRunning = true;
	mLock.Unlock();
	mRenderingThread = spawn_thread(page_rendering_thread, "page_rendering_thread", B_NORMAL_PRIORITY, this);
	*id = mRenderingThread;
	resume_thread(mRenderingThread);
}


void
PageRenderer::Abort()
{
	// MuPDF checks the cookie while it renders
	mCookie.abort = 1;
}


void
PageRenderer::Wait()
{
	if (mRenderingThread != -1) {
		status_t status;
		wait_for_thread(mRenderingThread, &status);
		mRenderingThread = -1;
	}
}


int32
page_rendering_thread(void* data)
{
	PageRenderer* renderer = (PageRenderer*)data;
	renderer->Render();
	renderer->Finished();	// (the renderer may be gone afterwards)
	return 0;
}


// the end of the thread: it lets go of the document, and deletes what is retired
void
PageRenderer::Finished()
{
	Document* held = mHeld;
	mHeld = NULL;

	mLock.Lock();
	mRunning = false;
	bool retired = mRetired;
	CachedPage* owned = mOwnedPage;
	mLock.Unlock();

	if (held != NULL)
		held->Release();
	if (retired) {
		mRenderingThread = -1;	// this is that thread: nobody waits for it
		delete owned;
		delete this;
	}
}


bool
PageRenderer::IsRunning()
{
	mLock.Lock();
	bool running = mRunning;
	mLock.Unlock();
	return running;
}


void
PageRenderer::Retire(CachedPage* ownedPage)
{
	mLock.Lock();
	if (mRunning) {
		mRetired = true;
		mOwnedPage = ownedPage;
		Abort();
		mLock.Unlock();
		return;
	}
	mLock.Unlock();
	delete ownedPage;
	delete this;
}


bool
PageRenderer::RenderToBitmap(Document* document, int pageNo, const fz_matrix& matrix, BBitmap* bitmap,
	int width, int height, fz_cookie* cookie)
{
	DocumentLocker locker(document);
	fz_context* context = document->Context();
	fz_page* page = NULL;
	fz_pixmap* pixmap = NULL;
	fz_device* device = NULL;
	bool ok = true;

	fz_var(page);
	fz_var(pixmap);
	fz_var(device);
	fz_try(context) {
		page = fz_load_page(context, document->Doc(), pageNo - 1);
		// the pixmap draws right into the memory of the bitmap, B_RGB32 is BGRA like this one
		pixmap = fz_new_pixmap_with_data(context, fz_device_bgr(context), width, height, NULL, 1,
			bitmap->BytesPerRow(), (unsigned char*)bitmap->Bits());
		fz_clear_pixmap_with_value(context, pixmap, 0xff);
		device = fz_new_draw_device(context, fz_identity, pixmap);
		fz_run_page(context, page, device, matrix, cookie);
		// the marks of a reflowable document are not in the file, they are drawn here
		document->PaintStoredAnnotations(pageNo, device, matrix);
		fz_close_device(context, device);
	}
	fz_always(context) {
		fz_drop_device(context, device);
		fz_drop_pixmap(context, pixmap);
		fz_drop_page(context, page);
	}
	fz_catch(context) {
		fprintf(stderr, "Tsundoku: cannot render page %d: %s\n", pageNo, fz_caught_message(context));
		ok = false;
	}

	return ok && (cookie == NULL || !cookie->abort);
}


void
PageRenderer::Render()
{
	Document* document = mDocument;
	DocumentLocker locker(document);
	fz_context* context = document->Context();

	TimingMark("render: starts");
	bool ok = RenderToBitmap(document, mPageNo, mPage->Matrix(), mScratch != NULL ? mScratch : mBitmap,
		(int)mWidth, (int)mHeight, &mCookie);
	TimingMark("render: drawn");

	if (mScratch != NULL) {
		if (ok) {
			int rowBytes = (int)mWidth * 4;
			for (int y = 0; y < (int)mHeight; y++) {
				memcpy((uint8*)mBitmap->Bits() + y * mBitmap->BytesPerRow(),
					(uint8*)mScratch->Bits() + y * mScratch->BytesPerRow(), rowBytes);
			}
		}
		delete mScratch;
		mScratch = NULL;
	}

	if (ok) {
		// what is needed to select text and follow links
		fz_page* page = NULL;
		fz_stext_page* text = NULL;
		fz_link* links = NULL;

		fz_var(page);
		fz_var(text);
		fz_var(links);
		fz_try(context) {
			page = fz_load_page(context, document->Doc(), mPageNo - 1);
			text = fz_new_stext_page_from_page(context, page, NULL);
			links = fz_load_links(context, page);
			TimingMark("render: text read");
			document->LoadAnnotations(mPageNo, page, mPage->mAnnotations);
			TimingMark("render: annotations read");
		}
		fz_always(context) {
			fz_drop_page(context, page);
		}
		fz_catch(context) {
			fprintf(stderr, "Tsundoku: cannot read text of page %d: %s\n", mPageNo,
				fz_caught_message(context));
		}

		mPage->mText = text;
		for (fz_link* link = links; link != NULL; link = link->next) {
			DocLink docLink;
			docLink.rect = link->rect;
			docLink.uri = link->uri != NULL ? link->uri : "";
			mPage->mLinks.push_back(docLink);
		}
		fz_drop_link(context, links);
	}

	// notify listener
	uint32 what;
	if (!ok && mCookie.abort) {
		mPage->SetState(CachedPage::WAITING);
		what = ABORT_MSG;
	} else {
		// a page that could not be rendered is shown white
		mPage->SetState(CachedPage::READY);
		what = FINISH_MSG;
	}

	Notify(what);
}


void
PageRenderer::Notify(uint32 what)
{
	if (!mListener.IsValid())
		return;

	BMessage msg(what);
	msg.AddInt32("id", mRenderingThread);
	msg.AddPointer("bitmap", mBitmap);
	mListener.SendMessage(&msg);
}


void
PageRenderer::GetParameter(BMessage* msg, thread_id* id, BBitmap** bitmap)
{
	if (B_OK != msg->FindInt32("id", id))
		*id = -1;
	if (B_OK != msg->FindPointer("bitmap", (void**)bitmap))
		*bitmap = NULL;
}

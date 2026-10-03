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
#ifndef _PAGE_RENDERER_H
#define _PAGE_RENDERER_H

#include <Bitmap.h>
#include <Handler.h>
#include <Looper.h>

#include "Document.h"

class CachedPage;

// renders a page in its own thread
class PageRenderer {
public:
	PageRenderer();
	~PageRenderer();

	void SetDocument(Document* document);
	void SetListener(BLooper* looper, BHandler* handler);

	enum {
		// sent to listener when page has been rendered
		FINISH_MSG = 'PRfi',
		// sent to listener when rendering process has been aborted
		ABORT_MSG = 'PRab'
	};

	// start rendering of a page asynchronously, the matrix and size of the page are valid when this returns
	// returns an unique identifier in id (id is greater than or equal to zero)
	// With keepImage the old image stays visible until the new one is complete (if the size is the same), for a
	// change in a page that is shown.
	void Start(CachedPage* page, int pageNo, int zoomDPI, int rotate, thread_id* id, bool keepImage = false);
	// abort rendering process asynchronously
	void Abort();
	// waits for rendering process to finish; returns immediately when no process runs
	void Wait();

	static void GetParameter(BMessage* msg, thread_id* id, BBitmap** bitmap);

	float GetWidth() const { return mWidth; }
	float GetHeight() const { return mHeight; }

	// Renders the page into the bitmap (B_RGB32) with the given matrix, used for printing as well.
	// The bitmap is filled with white first. Returns false on failure or if aborted.
	static bool RenderToBitmap(Document* document, int pageNo, const fz_matrix& matrix, BBitmap* bitmap,
		int width, int height, fz_cookie* cookie);

private:
	friend int32 page_rendering_thread(void* data);
	void Render();
	void Notify(uint32 what);

	Document*   mDocument;
	BLooper*    mLooper;
	BHandler*   mHandler;
	float       mWidth, mHeight;
	thread_id   mRenderingThread;
	CachedPage* mPage;
	BBitmap*    mBitmap;
	BBitmap*    mScratch;   // rendered into first when the old image is kept
	int         mPageNo;
	fz_cookie   mCookie;
};

#endif

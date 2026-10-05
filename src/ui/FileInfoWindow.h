/*  
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * BePDF: The PDF reader for Haiku.
 * 	 Copyright (C) 1997 Benoit Triquet.
 * 	 Copyright (C) 1998-2000 Hubert Figuiere.
 * 	 Copyright (C) 2000-2011 Michael Pfeiffer.
 * 	 Copyright (C) 2013 waddlesplash.
 * 	 Copyright (C) 2016 Adrián Arroyo Calle
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


#ifndef FILE_INFO_WINDOW_H
#define FILE_INFO_WINDOW_H

#include <time.h>

#include <Entry.h>
#include <Looper.h>
#include <String.h>
#include <Window.h>

#include "Document.h"
#include "Settings.h"

class BGridView;

class FileInfoWindow : public BWindow {
	BLooper *mLooper;
	GlobalSettings *mSettings;

	void AddPair(BGridView *dest, BView *lv, BView *rv);
	void CreateProperty(BGridView *dest, Document *doc, const char *key, const char *title);

public:
	// The keys of the information dictionary of a PDF file (and the equivalent of other formats).
	static const char *authorKey, *creationDateKey, *modDateKey, *creatorKey,
		*producerKey, *titleKey, *subjectKey, *keywordsKey;

	// The value of a property of the document in readable form, false if the document has none.
	// If it is a date, the time is set, too.
	static bool GetProperty(Document *doc, const char *key, BString *value, time_t *time = NULL);

	enum {
		// notify main window that this window quits
		QUIT_NOTIFY = 'FInQ'
	};
	FileInfoWindow(GlobalSettings *settings, BEntry *file, Document *doc, BLooper *looper);
	// new document
	void Refresh(BEntry *file, Document *doc);

	virtual bool QuitRequested();
	virtual void FrameMoved(BPoint point);
	virtual void FrameResized(float w, float h);
};

#endif

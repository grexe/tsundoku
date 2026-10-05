/*
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
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

#ifndef _ANNOTATIONS_VIEW_H_
#define _ANNOTATIONS_VIEW_H_

#include <set>
#include <vector>

#include <Locker.h>
#include <Messenger.h>
#include <OS.h>
#include <View.h>

#include "Document.h"

class BColumn;
class BColumnListView;
class BStringView;

// A list of all annotations of the document in columns for the page, the type and the text it covers or holds. The
// columns can be sorted by clicking their titles. Choosing one sends a message with "page" and "index" to the window.
class AnnotationsView : public BView {
public:
	// what is the message that is sent to the window when an annotation is chosen
	AnnotationsView(Document* document, uint32 chosenMessage);
	virtual ~AnnotationsView();

	void SetDocument(Document* document);
	// reads the annotations of the document again (in the background)
	void Refresh();
	// reads those of one page again, for a change that is limited to it
	void RefreshPage(int page);
	// stops reading, to be called before the document goes away
	void Stop();
	int  Count() const { return fCount; }
#ifdef TSUNDOKU_TESTING
	// selects a line as a click does, to try what follows
	void TestChoose(int index);
#endif

	virtual void AttachedToWindow();
	virtual void MessageReceived(BMessage* message);

private:
	// what the worker found
	struct Result {
		bool all;
		int  page;
		std::vector<DocAnnotationEntry> entries;
	};

	// What reads the annotations of one document. It has the document (a reference to it) and everything else that it needs, so
	// that it can finish on its own when the view has moved on to another document, or is gone.
	struct Worker {
		Worker(Document* document, BMessenger messenger, int32 serial);
		~Worker();

		Document*          document;
		BMessenger         messenger;
		int32              serial;
		sem_id             wake;
		volatile bool      cancel;      // stops a scan of all pages that a new request makes obsolete
		volatile bool      quit;
		BLocker            lock;        // guards the members below
		bool               requestAll;
		std::set<int>      requestPages;
		std::vector<Result> results;
	};

	static int32 WorkerThread(void* data);
	static void Work(Worker* worker);
	void Start();
	void AddRow(const DocAnnotationEntry& entry);
	void FillAll(const std::vector<DocAnnotationEntry>& entries);
	void FillPage(int page, const std::vector<DocAnnotationEntry>& entries);
	void UpdateStatus();
	void Fill(int32 serial);

	Document*      fDocument;
	uint32         fChosenMessage;
	BColumnListView* fList;
	BColumn*       fPageColumn;
	BColumn*       fTypeColumn;
	BColumn*       fExcerptColumn;
	BStringView*   fStatus;
	BMessenger     fMessenger;
	Worker*        fWorker;     // the one that works for the current document, NULL if none
	int32          fSerial;
	int            fCount;
};

#endif

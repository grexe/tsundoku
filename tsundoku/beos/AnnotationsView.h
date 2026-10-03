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

#ifndef _ANNOTATIONS_VIEW_H_
#define _ANNOTATIONS_VIEW_H_

#include <vector>

#include <Locker.h>
#include <Messenger.h>
#include <OS.h>
#include <View.h>

#include "Document.h"

struct AnnotationColumns;
class BListView;
class BStringView;

// A list of all annotations of the document: the page, what it is and the text it covers or holds. Choosing
// one sends a message with "page" and "index" to the window.
class AnnotationsView : public BView {
public:
	// what is the message that is sent to the window when an annotation is chosen
	AnnotationsView(Document* document, uint32 chosenMessage);
	virtual ~AnnotationsView();

	void SetDocument(Document* document);
	// reads the annotations of the document again (in the background)
	void Refresh();
	// stops reading, to be called before the document goes away
	void Stop();
	int  Count() const { return fCount; }

	virtual void AttachedToWindow();
	virtual void MessageReceived(BMessage* message);

private:
	static int32 ScanThread(void* data);
	void Scan();
	void Fill();

	Document*      fDocument;
	uint32         fChosenMessage;
	BListView*     fList;
	AnnotationColumns* fColumns;
	BStringView*   fStatus;
	BMessenger     fMessenger;
	thread_id      fThread;
	volatile bool  fCancel;
	BLocker        fLock;
	std::vector<DocAnnotationEntry> fPending;   // what the thread found
	std::vector<DocAnnotationEntry> fEntries;   // what the list shows
	int            fCount;
};

#endif

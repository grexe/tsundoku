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

#include "AnnotationsView.h"

#include <stdio.h>

#include <Catalog.h>
#include <ColumnListView.h>
#include <ColumnTypes.h>
#include <LayoutBuilder.h>
#include <StringView.h>
#include <Window.h>

#include "Globals.h"

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "AnnotationsView"

static const uint32 kScanDone = 'anSD';
static const uint32 kChosen = 'anCh';


// a line of the list that knows which annotation it stands for (the order of the lines changes with the sorting)
class AnnotationRow : public BRow {
public:
	AnnotationRow(int page, int index)
		:
		BRow(),
		fPage(page),
		fIndex(index)
	{
	}

	int Page() const { return fPage; }
	int Index() const { return fIndex; }

private:
	int fPage, fIndex;
};


AnnotationsView::AnnotationsView(Document* document, uint32 chosenMessage)
	:
	BView("annotations", B_WILL_DRAW | B_FRAME_EVENTS),
	fDocument(document),
	fChosenMessage(chosenMessage),
	fWorker(NULL),
	fSerial(0),
	fCount(0)
{
	// the standard list with columns that can be sorted and resized, but not moved or taken away
	fList = new BColumnListView("annotationsList", B_NAVIGABLE, B_FANCY_BORDER, true);
	fList->SetColumnFlags(B_ALLOW_COLUMN_RESIZE);
	fList->SetSortingEnabled(true);
	fList->SetSelectionMode(B_SINGLE_SELECTION_LIST);
	fList->SetSelectionMessage(new BMessage(kChosen));

	fPageColumn = new BIntegerColumn(B_TRANSLATE("Page"), 76, 64, 140, B_ALIGN_RIGHT);
	fTypeColumn = new BStringColumn(B_TRANSLATE("Type"), 90, 40, 300, B_TRUNCATE_END);
	fExcerptColumn = new BStringColumn(B_TRANSLATE("Excerpt"), 320, 60, 4000, B_TRUNCATE_MIDDLE);
	fList->AddColumn(fPageColumn, 0);
	fList->AddColumn(fTypeColumn, 1);
	fList->AddColumn(fExcerptColumn, 2);
	fList->SetSortColumn(fPageColumn, false, true);

	fStatus = new BStringView("annotationsStatus", "");
	fStatus->SetAlignment(B_ALIGN_CENTER);

	BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_SMALL_SPACING)
		.SetInsets(0, 0, 0, B_USE_SMALL_SPACING)
		.Add(fList)
		.Add(fStatus)
	.End();
}


AnnotationsView::~AnnotationsView()
{
	Stop();
}


void
AnnotationsView::AttachedToWindow()
{
	BView::AttachedToWindow();
	fMessenger = BMessenger(this);
	fList->SetTarget(this);
	Refresh();
}


AnnotationsView::Worker::Worker(Document* doc, BMessenger target, int32 id)
	:
	document(doc),
	messenger(target),
	serial(id),
	wake(create_sem(0, "annotations wake")),
	cancel(false),
	quit(false),
	lock("annotations"),
	requestAll(false)
{
	document->Acquire();
}


AnnotationsView::Worker::~Worker()
{
	delete_sem(wake);
	document->Release();
}


// One worker thread reads the annotations, so that the window never waits for the document (a page that is
// rendered holds it). It takes the requests together: all pages, or single pages that have changed. When the
// document is replaced the worker is let go: it finishes on its own, bound to its document.
void
AnnotationsView::Start()
{
	if (fWorker != NULL || fDocument == NULL || !fMessenger.IsValid())
		return;

	fWorker = new Worker(fDocument, fMessenger, ++fSerial);
	thread_id thread = spawn_thread(WorkerThread, "annotations scan", B_LOW_PRIORITY, fWorker);
	if (thread < 0) {
		delete fWorker;
		fWorker = NULL;
		return;
	}
	resume_thread(thread);
}


void
AnnotationsView::Stop()
{
	Worker* worker = fWorker;
	fWorker = NULL;
	if (worker == NULL)
		return;

	worker->quit = true;
	worker->cancel = true;
	sem_id wake = worker->wake;
	release_sem(wake);		// (the worker deletes itself; nothing of it is used after this)
}


void
AnnotationsView::SetDocument(Document* document)
{
	Stop();
	fDocument = document;
	Refresh();
}


void
AnnotationsView::Refresh()
{
	if (fDocument == NULL || !fMessenger.IsValid())
		return;
	Start();
	if (fWorker == NULL)
		return;

	fWorker->lock.Lock();
	fWorker->requestAll = true;
	fWorker->requestPages.clear();
	fWorker->cancel = true;		// a scan that is on its way starts again
	fWorker->lock.Unlock();
	release_sem(fWorker->wake);
}


void
AnnotationsView::RefreshPage(int page)
{
	if (fDocument == NULL || !fMessenger.IsValid())
		return;
	Start();
	if (fWorker == NULL)
		return;

	fWorker->lock.Lock();
	if (!fWorker->requestAll)
		fWorker->requestPages.insert(page);
	fWorker->lock.Unlock();
	release_sem(fWorker->wake);
}


int32
AnnotationsView::WorkerThread(void* data)
{
	Worker* worker = (Worker*)data;
	Work(worker);
	delete worker;
	return 0;
}


void
AnnotationsView::Work(Worker* worker)
{
	while (acquire_sem(worker->wake) == B_OK && !worker->quit) {
		worker->lock.Lock();
		bool all = worker->requestAll;
		std::set<int> pages;
		pages.swap(worker->requestPages);
		worker->requestAll = false;
		worker->cancel = false;
		worker->lock.Unlock();

		if (all) {
			Result result;
			result.all = true;
			result.page = 0;
			TimingMark("list: scan starts");
			if (worker->document->ListAnnotations(result.entries, &worker->cancel) && !worker->cancel) {
				TimingMark("list: scan done");
				worker->lock.Lock();
				worker->results.push_back(result);
				worker->lock.Unlock();
				BMessage done(kScanDone);
				done.AddInt32("serial", worker->serial);
				worker->messenger.SendMessage(&done);
			}
			continue;
		}

		for (std::set<int>::const_iterator it = pages.begin(); it != pages.end() && !worker->quit; ++it) {
			Result result;
			result.all = false;
			result.page = *it;
			TimingMark("list: page scan starts");
			if (!worker->document->ListAnnotationsOnPage(*it, result.entries))
				continue;
			TimingMark("list: page scan done");
			worker->lock.Lock();
			worker->results.push_back(result);
			worker->lock.Unlock();
			BMessage done(kScanDone);
			done.AddInt32("serial", worker->serial);
			worker->messenger.SendMessage(&done);
		}
	}
}


void
AnnotationsView::AddRow(const DocAnnotationEntry& entry)
{
	AnnotationRow* row = new AnnotationRow(entry.page, entry.annotation.index);
	row->SetField(new BIntegerField(entry.page), 0);
	row->SetField(new BStringField(entry.annotation.label.String()), 1);
	row->SetField(new BStringField(entry.excerpt.String()), 2);
	fList->AddRow(row);
}


void
AnnotationsView::FillAll(const std::vector<DocAnnotationEntry>& entries)
{
	fList->Clear();
	for (size_t i = 0; i < entries.size(); i++)
		AddRow(entries[i]);
}


// the lines of one page are replaced, the others stay as they are (and where the user has scrolled to)
void
AnnotationsView::FillPage(int page, const std::vector<DocAnnotationEntry>& entries)
{
	std::vector<BRow*> old;
	for (int32 i = 0; i < fList->CountRows(); i++) {
		AnnotationRow* row = dynamic_cast<AnnotationRow*>(fList->RowAt(i));
		if (row != NULL && row->Page() == page)
			old.push_back(row);
	}
	for (size_t i = 0; i < old.size(); i++) {
		fList->RemoveRow(old[i]);
		delete old[i];
	}
	for (size_t i = 0; i < entries.size(); i++)
		AddRow(entries[i]);
}


void
AnnotationsView::UpdateStatus()
{
	fCount = fList->CountRows();
	BString status;
	if (fCount == 0)
		status = B_TRANSLATE("No annotations");
	else if (fCount == 1)
		status = B_TRANSLATE("1 annotation");
	else
		status << fCount << " " << B_TRANSLATE("annotations");
	fStatus->SetText(status.String());
}


void
AnnotationsView::Fill(int32 serial)
{
	// (what a worker that has been let go found is of no interest)
	if (fWorker == NULL || fWorker->serial != serial)
		return;

	std::vector<Result> results;
	fWorker->lock.Lock();
	results.swap(fWorker->results);
	fWorker->lock.Unlock();

	TimingMark("list: scan result received");
	for (size_t i = 0; i < results.size(); i++) {
		if (results[i].all)
			FillAll(results[i].entries);
		else
			FillPage(results[i].page, results[i].entries);
	}
	UpdateStatus();
	TimingMark("list: filled");
}


void
AnnotationsView::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kScanDone: {
			int32 serial = 0;
			message->FindInt32("serial", &serial);
			Fill(serial);
			break;
		}
		case kChosen: {
			AnnotationRow* row = dynamic_cast<AnnotationRow*>(fList->CurrentSelection());
			if (row != NULL && Window() != NULL) {
				BMessage chosen(fChosenMessage);
				chosen.AddInt32("page", row->Page());
				chosen.AddInt32("index", row->Index());
				Window()->PostMessage(&chosen);
			}
			break;
		}
		default:
			BView::MessageReceived(message);
	}
}


#ifdef TSUNDOKU_TESTING
void
AnnotationsView::TestChoose(int index)
{
	fList->DeselectAll();
	fList->SetFocusRow(index, true);
}
#endif

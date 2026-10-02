/*  
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


#include <ctype.h>

#include <locale/Catalog.h>
#include <Box.h>
#include <Button.h>
#include <LayoutBuilder.h>
#include <Path.h>
#include <Region.h>
#include <StringView.h>
#include <TabView.h>

#include "Globals.h"
#include "FileInfoWindow.h"
#include "LayoutUtils.h"

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "FileInfoWindow"


const char *FileInfoWindow::authorKey = "Author";
const char *FileInfoWindow::creationDateKey = "CreationDate";
const char *FileInfoWindow::modDateKey = "ModDate";
const char *FileInfoWindow::creatorKey = "Creator";
const char *FileInfoWindow::producerKey = "Producer";
const char *FileInfoWindow::titleKey = "Title";
const char *FileInfoWindow::subjectKey = "Subject";
const char *FileInfoWindow::keywordsKey = "Keywords";

static const char *YesNo(bool yesNo) {
	return yesNo ? B_TRANSLATE("Yes") : B_TRANSLATE("No");
}

static const char *Allowed(bool allowed) {
	return allowed ? B_TRANSLATE("Allowed") : B_TRANSLATE("Denied");
}

#define COPYN(N) for (n = N; n && date[i]; n--) s[j++] = date[i++];

static int ToInt(const char *s, int i, int n) {
	int d = 0;
	for (; n && s[i] && isdigit(s[i]) ; n --, i ++) {
		d = 10 * d + (int)(s[i] - '0');
	}
	return d;
}

// (D:YYYYMMDDHHmmSSOHH'mm')
static const char *ToDate(const char *date, time_t *time) {
static char s[80];
	struct tm d;
	memset(&d, 0, sizeof(d));

	if ((date[0] == 'D') && (date[1] == ':')) {
		int i = 2;
		// skip spaces
		while (date[i] == ' ') i++;
		int from = i;
		while (date[i] && isdigit(date[i])) i++;
		int to = i;
		int j = 0, n;
		i = from;
		d.tm_year = ToInt(date, i, 4) - 1900;
		if (to - from > 12)
			COPYN(to - from - 10)
		else
			COPYN(to - from - 4);
		s[j++] = '/';
		d.tm_mon = ToInt(date, i, 2)-1;
		COPYN(2)
		s[j++] = '/';
		d.tm_mday = ToInt(date, i, 2);
		COPYN(2)
		s[j++] = ' ';
		if (date[i]) {
			d.tm_hour = ToInt(date, i, 2);
			COPYN(2);
			s[j++] = ':';
			d.tm_min = ToInt(date, i, 2);
			COPYN(2);
			s[j++] = ':';
			d.tm_sec = ToInt(date, i, 2);
			COPYN(2);
			if (date[i]) {
				int off = 0;
				int sign = 1;
				s[j++] = ' ';
				if (date[i] == '-') sign = -1;
				s[j++] = date[i++];
				off = ToInt(date, i, 2) * 3600;
				COPYN(2); i++; // skip '
				s[j++] = ':';
				off += ToInt(date, i, 2) * 60;
				COPYN(2);
				d.tm_gmtoff = off * sign;
			}
		}
		s[j] = 0;
		if (time) *time = mktime(&d);
		return s;
	}
	else
		return date;
}

bool FileInfoWindow::GetProperty(Document *doc, const char *key, BString *value, time_t *time) {
	if (time) *time = 0;

	BString name("info:");
	name << key;
	BString property = doc->Metadata(name.String());
	if (property.Length() == 0)
		return false;

	const char *date = ToDate(property.String(), time);
	if (date != property.String())
		*value = date;
	else
		*value = property;
	return true;
}

void FileInfoWindow::AddPair(BGridView *dest, BView *lv, BView *rv) {
	BGridLayout *layout = dest->GridLayout();
	int32 nextRow = layout->CountRows() + 1;
	layout->AddView(lv, 1, nextRow);
	layout->AddView(rv, 2, nextRow);
}

void FileInfoWindow::CreateProperty(BGridView *view, Document *doc, const char *key, const char *title) {
	BString value;
	bool found = GetProperty(doc, key, &value);
	AddPair(view, new BStringView("", title), new BStringView("", found ? value.String() : "-"));
}

void FileInfoWindow::Refresh(BEntry *file, Document *doc) {
	BTabView *tabs = new BTabView("tabs", B_WIDTH_FROM_LABEL);

	BGridView *document = new BGridView();

	BPath path;
	if (file->GetPath(&path) == B_OK) {
		AddPair(document, new BStringView("", B_TRANSLATE("Filename:")),
			new BStringView("", path.Leaf()));
		AddPair(document, new BStringView("", B_TRANSLATE("Path:")),
			new BStringView("", path.Path()));
	}

	BString format = doc->Format();
	if (format.Length() > 0)
		AddPair(document, new BStringView("", B_TRANSLATE("Format:")), new BStringView("", format.String()));

	BString pages;
	pages << doc->PageCount();
	AddPair(document, new BStringView("", B_TRANSLATE("Pages:")), new BStringView("", pages.String()));

	CreateProperty(document, doc, titleKey, B_TRANSLATE("Title:"));
	CreateProperty(document, doc, subjectKey, B_TRANSLATE("Subject:"));
	CreateProperty(document, doc, authorKey, B_TRANSLATE("Author:"));
	CreateProperty(document, doc, keywordsKey, B_TRANSLATE("Keywords:"));
	CreateProperty(document, doc, creatorKey, B_TRANSLATE("Creator:"));
	CreateProperty(document, doc, producerKey, B_TRANSLATE("Producer:"));
	CreateProperty(document, doc, creationDateKey, B_TRANSLATE("Created:"));
	CreateProperty(document, doc, modDateKey, B_TRANSLATE("Modified:"));

	BView *docView = new BView(B_TRANSLATE("Document"), 0);
	BLayoutBuilder::Group<>(docView, B_VERTICAL)
		.SetInsets(B_USE_WINDOW_INSETS)
		.AddGroup(B_HORIZONTAL)
			.Add(document)
			.AddGlue()
		.End()
		.AddGlue();

	tabs->AddTab(docView);

	// Security
	BGridView *security = new BGridView();

	AddPair(security, new BStringView("", B_TRANSLATE("Encrypted:")), new BStringView("", YesNo(doc->IsEncrypted())));
	AddPair(security, new BStringView("", B_TRANSLATE("Printing:")), new BStringView("", Allowed(doc->CanPrint())));
	AddPair(security, new BStringView("", B_TRANSLATE("Editing:")), new BStringView("", Allowed(doc->CanEdit())));
	AddPair(security, new BStringView("", B_TRANSLATE("Copy & paste:")), new BStringView("", Allowed(doc->CanCopy())));
	AddPair(security, new BStringView("", B_TRANSLATE("Annotations:")), new BStringView("", Allowed(doc->CanAnnotate())));

	BView *secView = new BView(B_TRANSLATE("Security"), 0);
	BLayoutBuilder::Group<>(secView, B_VERTICAL)
		.SetInsets(B_USE_WINDOW_INSETS)
		.AddGroup(B_HORIZONTAL)
			.Add(security)
			.AddGlue()
		.End()
		.AddGlue();

	tabs->AddTab(secView);
	tabs->SetBorder(B_NO_BORDER);

	BLayoutBuilder::Group<>(this)
		.SetInsets(0, B_USE_WINDOW_INSETS, 0, 0)
		.Add(tabs);

	Show();
}

FileInfoWindow::FileInfoWindow(GlobalSettings *settings, BEntry *file, Document *doc,
	BLooper *looper)
	: BWindow(BRect(0, 0, 100, 100), B_TRANSLATE("File info"),
		B_TITLED_WINDOW_LOOK, B_NORMAL_WINDOW_FEEL, B_AUTO_UPDATE_SIZE_LIMITS),
		mLooper(looper), mSettings(settings) {

	AddCommonFilter(new EscapeMessageFilter(this, B_QUIT_REQUESTED));

	MoveTo(settings->GetFileInfoWindowPosition());
	float w, h;
	settings->GetFileInfoWindowSize(w, h);
	ResizeTo(w, h);

	Refresh(file, doc);
}

bool FileInfoWindow::QuitRequested() {
	if (mLooper) {
		BMessage msg(QUIT_NOTIFY);
		mLooper->PostMessage(&msg);
	}
	return true;
}

void FileInfoWindow::FrameMoved(BPoint p) {
	mSettings->SetFileInfoWindowPosition(p);
	BWindow::FrameMoved(p);
}

void FileInfoWindow::FrameResized(float w, float h) {
	mSettings->SetFileInfoWindowSize(w, h);
	BWindow::FrameResized(w, h);
}

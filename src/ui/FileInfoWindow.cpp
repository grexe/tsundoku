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


#include <ctype.h>

#include <locale/Catalog.h>
#include <vector>

#include <Bitmap.h>
#include <Box.h>
#include <Button.h>
#include <GroupLayout.h>
#include <LayoutBuilder.h>
#include <Path.h>
#include <DataIO.h>
#include <Region.h>
#include <StringView.h>
#include <TabView.h>
#include <TranslationUtils.h>

#include "ComicInfo.h"
#include "EpubInfo.h"
#include "PageRenderer.h"
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

// the cover of a book
class CoverView : public BView {
public:
	CoverView(BBitmap* bitmap)
		:
		BView("cover", B_WILL_DRAW),
		fBitmap(bitmap)
	{
		SetViewColor(B_TRANSPARENT_COLOR);
		BRect bounds = bitmap->Bounds();
		float height = 220;
		float width = height * (bounds.Width() + 1) / (bounds.Height() + 1);
		SetExplicitMinSize(BSize(width, height));
		SetExplicitMaxSize(BSize(width, height));
		SetExplicitPreferredSize(BSize(width, height));
	}

	~CoverView() { delete fBitmap; }

	virtual void Draw(BRect)
	{
		SetDrawingMode(B_OP_COPY);
		DrawBitmap(fBitmap, fBitmap->Bounds(), Bounds(), B_FILTER_BITMAP_BILINEAR);
		SetHighColor(tint_color(ui_color(B_PANEL_BACKGROUND_COLOR), B_DARKEN_2_TINT));
		StrokeRect(Bounds());
	}

private:
	BBitmap* fBitmap;
};


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

	BView* cover = NULL;
	if (const EpubInfo* epub = doc->Epub()) {
		// what the package document of the book says
		struct Row { const char* title; BString value; };
		BString series = epub->series;
		if (series.Length() > 0 && epub->seriesIndex.Length() > 0)
			series << " #" << epub->seriesIndex;
		Row rows[] = {
			{ B_TRANSLATE("Title:"), epub->title },
			{ B_TRANSLATE("Author:"), epub->Authors() },
			{ B_TRANSLATE("Series:"), series },
			{ B_TRANSLATE("Language:"), epub->language },
			{ B_TRANSLATE("Publisher:"), epub->publisher },
			{ B_TRANSLATE("Published:"), epub->date },
			{ B_TRANSLATE("Identifier:"), epub->identifier },
			{ B_TRANSLATE("ISBN:"), epub->isbn },
			{ B_TRANSLATE("Subjects:"), epub->Subjects() },
			{ B_TRANSLATE("Description:"), epub->description },
			{ B_TRANSLATE("EPUB version:"), epub->version }
		};
		for (size_t i = 0; i < sizeof(rows) / sizeof(rows[0]); i++) {
			if (rows[i].value.Length() == 0)
				continue;
			BString shown(rows[i].value);
			if (shown.CountChars() > 120) {
				shown.TruncateChars(120);
				shown << "\xe2\x80\xa6";
			}
			AddPair(document, new BStringView("", rows[i].title), new BStringView("", shown.String()));
		}

		std::vector<uint8> data;
		if (epub->coverMember.Length() > 0 && path.InitCheck() == B_OK
			&& EpubInfo::ReadMember(path.Path(), epub->coverMember.String(), &data) && !data.empty()) {
			BMemoryIO io(&data[0], data.size());
			if (BBitmap* bitmap = BTranslationUtils::GetBitmap(&io))
				cover = new CoverView(bitmap);
		}
	} else if (doc->IsComic()) {
		// what ComicInfo.xml says, if the archive has it
		struct Row { const char* title; BString value; };
		if (const ComicInfo* comic = doc->Comic()) {
			BString series = comic->series;
			if (series.Length() > 0 && comic->number.Length() > 0) {
				series << " #" << comic->number;
				if (comic->count > 0)
					series << " / " << comic->count;
			}
			Row rows[] = {
				{ B_TRANSLATE("Title:"), comic->title },
				{ B_TRANSLATE("Series:"), series },
				{ B_TRANSLATE("Author:"), comic->Authors() },
				{ B_TRANSLATE("Artists:"), comic->Artists() },
				{ B_TRANSLATE("Language:"), comic->language },
				{ B_TRANSLATE("Publisher:"), comic->publisher },
				{ B_TRANSLATE("Published:"), comic->Date() },
				{ B_TRANSLATE("Keywords:"), comic->Keywords() },
				{ B_TRANSLATE("Age rating:"), comic->ageRating },
				{ B_TRANSLATE("Reading:"), comic->rightToLeft ? B_TRANSLATE("right to left") : "" },
				{ B_TRANSLATE("Description:"), comic->summary },
				{ B_TRANSLATE("Web:"), comic->web }
			};
			for (size_t i = 0; i < sizeof(rows) / sizeof(rows[0]); i++) {
				if (rows[i].value.Length() == 0)
					continue;
				BString shown(rows[i].value);
				if (shown.CountChars() > 120) {
					shown.TruncateChars(120);
					shown << "\xe2\x80\xa6";
				}
				AddPair(document, new BStringView("", rows[i].title), new BStringView("", shown.String()));
			}
		}

		// the cover is the page that ComicInfo.xml marks as such, else the first
		int cover_page = doc->Comic() != NULL ? doc->Comic()->CoverPage() + 1 : 1;
		if (cover_page < 1 || cover_page > doc->PageCount())
			cover_page = 1;
		fz_matrix matrix;
		int width = 0, height = 0;
		fz_rect bounds;
		if (doc->PageBounds(cover_page, &bounds) && bounds.y1 > bounds.y0) {
			float dpi = 72 * 440 / (bounds.y1 - bounds.y0);
			if (doc->PageMatrix(cover_page, dpi, 0, &matrix, &width, &height) && width > 0 && height > 0) {
				BBitmap* bitmap = new BBitmap(BRect(0, 0, width - 1, height - 1), B_RGB32);
				if (bitmap->InitCheck() == B_OK
					&& PageRenderer::RenderToBitmap(doc, cover_page, matrix, bitmap, width, height, NULL))
					cover = new CoverView(bitmap);
				else
					delete bitmap;
			}
		}
	} else {
		CreateProperty(document, doc, titleKey, B_TRANSLATE("Title:"));
		CreateProperty(document, doc, subjectKey, B_TRANSLATE("Subject:"));
		CreateProperty(document, doc, authorKey, B_TRANSLATE("Author:"));
		CreateProperty(document, doc, keywordsKey, B_TRANSLATE("Keywords:"));
		CreateProperty(document, doc, creatorKey, B_TRANSLATE("Creator:"));
		CreateProperty(document, doc, producerKey, B_TRANSLATE("Producer:"));
		CreateProperty(document, doc, creationDateKey, B_TRANSLATE("Created:"));
		CreateProperty(document, doc, modDateKey, B_TRANSLATE("Modified:"));
	}

	BView *docView = new BView(B_TRANSLATE("Document"), 0);
	BGroupLayout* column = NULL;
	BLayoutBuilder::Group<>(docView, B_VERTICAL)
		.SetInsets(B_USE_WINDOW_INSETS)
		.AddGroup(B_HORIZONTAL)
			.Add(document)
			.AddGlue()
			.AddGroup(B_VERTICAL)
				.GetLayout(&column)
				.AddGlue()
			.End()
		.End()
		.AddGlue();
	// the cover of a book is to the right of the properties
	if (cover != NULL)
		column->AddView(0, cover);

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

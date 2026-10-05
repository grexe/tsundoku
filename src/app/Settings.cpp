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

#include "Settings.h"
#include "Bookmarks.h"
#include <Message.h>
#include <FindDirectory.h>
#include <Path.h>
#include <File.h>
#include <fs_attr.h>
#include <malloc.h>


GlobalSettings::GlobalSettings() :
	mChanged(false)
{
	SETTINGS(DEFINE_VARIABLE)
	STRING_SETTINGS(DEFINE_STRING_VARIABLE)

	BPath path;
	if (B_OK == find_directory(B_DESKTOP_DIRECTORY, &path)) {
		mDefaultPanelDirectory = path.Path();
	} else {
		mDefaultPanelDirectory = "/boot/home/Desktop";
	}
	mPanelDirectory = mDefaultPanelDirectory;

}

bool GlobalSettings::HasChanged() const {
	return mChanged;
}

SETTINGS(DEFINE_SETTER)
SETTINGS(DEFINE_GETTER)
STRING_SETTINGS(DEFINE_STRING_SETTER)
STRING_SETTINGS(DEFINE_STRING_GETTER)

void GlobalSettings::SetPanelDirectory(const char *dir) {
	mChanged = mChanged || (mPanelDirectory != dir); mPanelDirectory = dir;
}


const char * GlobalSettings::GetPanelDirectory() const {
	return mPanelDirectory.String();
}

void GlobalSettings::SetDisplayCIDFonts(const BMessage& fonts) {
	mChanged = true;
	mDisplayCIDFonts = fonts;
}

void GlobalSettings::GetDisplayCIDFonts(BMessage& fonts) const {
	fonts = mDisplayCIDFonts;
}

BRect GlobalSettings::GetWindowRect() const {
	BRect rect;
	float w, h;
	rect.SetLeftTop(GetWindowPosition());
	GetWindowSize(w, h);
	rect.right = rect.left + w - 1;
	rect.bottom = rect.top + h - 1;
	return rect;
}

// BArchivable:
GlobalSettings::GlobalSettings(BMessage *archive) {
	mChanged = false;

    SETTINGS(LOAD_SETTINGS)
    STRING_SETTINGS(LOAD_STRING_SETTINGS)

	if (B_OK != archive->FindString("panelDirectory", &mPanelDirectory))
		mPanelDirectory = mDefaultPanelDirectory;

	archive->FindMessage("displayCIDFonts", &mDisplayCIDFonts);
}

status_t GlobalSettings::Archive(BMessage *archive, bool deep) const {
	archive->AddString("class", "GlobalSettings");

	SETTINGS(STORE_SETTINGS)
	STRING_SETTINGS(STORE_STRING_SETTINGS)

	archive->AddString("panelDirectory", mPanelDirectory.String());
	archive->AddMessage("displayCIDFonts", &mDisplayCIDFonts);
	return B_OK;
}

BArchivable *GlobalSettings::Instantiate(BMessage *archive) {
	if (validate_instantiation(archive, "GlobalSettings")) {
		return new GlobalSettings(archive);
	} else
		return NULL;
}

void GlobalSettings::Load(const char* filename) {
	BPath path(filename);
	if (path.InitCheck() == B_OK) {
		BFile file(path.Path(), B_READ_ONLY);
		if (file.InitCheck() == B_OK) {
			BMessage archive;
			archive.Unflatten(&file);
			GlobalSettings *s = (GlobalSettings*)GlobalSettings::Instantiate(&archive);

			if (s != NULL) {
				*this = *s; delete s;
			}
		}
	}
}

void GlobalSettings::Save(const char* filename, bool force) {
BPath path(filename);
	if (HasChanged() || force) {
		BFile file(path.Path(), B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
		if (file.InitCheck() == B_OK) {
			BMessage archive;
			Archive(&archive);
			archive.Flatten(&file);
		}
	}
}


// #pragma mark - FileAttributes


void FileAttributes::SetPage(int32 page) {
	this->page = page;
}

int32 FileAttributes::GetPage() const {
	return page;
}

void FileAttributes::SetLeftTop(float left, float top) {
	this->left = left;
	this->top = top;
}

void FileAttributes::GetLeftTop(float &left, float &top) {
	left = this->left; top = this->top;
}

// What is kept with the file besides what it says about itself: where the reader stopped (page, place on the page, zoom,
// rotation, and for a book the anchor in the text, as the page changes with the text size) and the window. It is one
// attribute, a message, of the application: toji:viewState. The bookmarks of the reader are annotations
// (oa:bookmarking) in SEN:annotations (Bookmarks.h).
static const char* const kViewStateAttribute = "toji:viewState";
static const char* const kOldViewStateAttribute = "tsundoku:viewState";


static void
ReadMessageAttribute(BNode& node, const char* name, BMessage* message)
{
	message->MakeEmpty();
	attr_info info;
	if (node.GetAttrInfo(name, &info) != B_OK || info.size <= 0 || info.size > 65536)
		return;
	char* data = (char*)malloc(info.size);
	if (data == NULL)
		return;
	if (node.ReadAttr(name, B_MESSAGE_TYPE, 0, data, info.size) != info.size || message->Unflatten(data) != B_OK)
		message->MakeEmpty();
	free(data);
}


// what BePDF kept in separate attributes, as the message that the view state is now
static void
ReadLegacyViewState(BNode& node, BMessage* state)
{
	int16 zoom;
	int32 i;
	float f;
	if (node.ReadAttr("bepdf:zoom", B_INT16_TYPE, 0, &zoom, sizeof(zoom)) == sizeof(zoom))
		state->AddInt16("zoom", zoom);
	if (node.ReadAttr("bepdf:rotation", B_INT32_TYPE, 0, &i, sizeof(i)) == sizeof(i))
		state->AddInt32("rotation", i);
	if (node.ReadAttr("bepdf:page", B_INT32_TYPE, 0, &i, sizeof(i)) == sizeof(i))
		state->AddInt32("page", i);
	if (node.ReadAttr("bepdf:left", B_FLOAT_TYPE, 0, &f, sizeof(f)) == sizeof(f))
		state->AddFloat("left", f);
	if (node.ReadAttr("bepdf:top", B_FLOAT_TYPE, 0, &f, sizeof(f)) == sizeof(f))
		state->AddFloat("top", f);
	BMessage anchor;
	ReadMessageAttribute(node, "bepdf:anchor", &anchor);
	if (!anchor.IsEmpty())
		state->AddMessage("anchor", &anchor);
}


bool FileAttributes::Read(entry_ref *ref, GlobalSettings *s) {
	BNode node(ref);
	reading = -1;
	hasZoom = false;
	if (node.InitCheck() != B_OK)
		return false;

	// SEN:readingProgression: rtl, ltr or default (what the document itself says)
	char direction[16];
	ssize_t got = node.ReadAttr("SEN:readingProgression", B_STRING_TYPE, 0, direction, sizeof(direction) - 1);
	if (got > 0) {
		direction[got] = '\0';
		if (strcasecmp(direction, "rtl") == 0)
			reading = 1;
		else if (strcasecmp(direction, "ltr") == 0)
			reading = 0;
		else if (strcasecmp(direction, "ttb") == 0)
			reading = 2;
	}

	BMessage state;
	ReadMessageAttribute(node, kViewStateAttribute, &state);
	if (state.IsEmpty())
		ReadMessageAttribute(node, kOldViewStateAttribute, &state);	// (when the program was called Tsundoku)
	if (state.IsEmpty())
		ReadLegacyViewState(node, &state);

	int16 zoom;
	hasZoom = state.FindInt16("zoom", &zoom) == B_OK;
	if (!hasZoom)
		zoom = s->GetZoom();
	int32 rotation;
	if (state.FindInt32("rotation", &rotation) != B_OK)
		rotation = (int32)s->GetRotation();
	BPoint position = s->GetWindowPosition();
	int32 pos_x = (int32)position.x, pos_y = (int32)position.y;
	if (state.FindInt32("x", &pos_x) != B_OK || state.FindInt32("y", &pos_y) != B_OK) {
		pos_x = (int32)position.x;
		pos_y = (int32)position.y;
	}
	float w, h;
	s->GetWindowSize(w, h);
	int32 width = (int32)w, height = (int32)h;
	if (state.FindInt32("width", &width) != B_OK || state.FindInt32("height", &height) != B_OK) {
		width = (int32)w;
		height = (int32)h;
	}
	if (state.FindInt32("page", &page) != B_OK)
		page = 1;
	if (state.FindFloat("left", &left) != B_OK)
		left = 0;
	if (state.FindFloat("top", &top) != B_OK)
		top = 0;

	if (s->GetRestoreWindowFrame()) {
		s->SetWindowPosition(BPoint(pos_x, pos_y));
		s->SetWindowSize(width, height);
	}

	if (s->GetRestorePageNumber()) {
		s->SetZoom(zoom);
		s->SetRotation(rotation);
	}

	// where the reader stopped in a book
	anchor.MakeEmpty();
	state.FindMessage("anchor", &anchor);

	// the bookmarks of the reader
	BPath path(ref);
	Bookmarks::Read(path.Path(), &bookmarks);
	if (bookmarks.IsEmpty())
		Bookmarks::ReadLegacy(path.Path(), &bookmarks);
	return true;
}

bool FileAttributes::Write(entry_ref *ref, GlobalSettings *s) {
	BNode node(ref);
	if (node.InitCheck() != B_OK)
		return false;

	BMessage state;
	BPoint pos = s->GetWindowPosition();
	state.AddInt32("x", (int32)pos.x);
	state.AddInt32("y", (int32)pos.y);
	float width, height;
	s->GetWindowSize(width, height);
	state.AddInt32("width", (int32)width);
	state.AddInt32("height", (int32)height);
	state.AddInt16("zoom", (int16)s->GetZoom());
	state.AddInt32("rotation", (int32)s->GetRotation());
	state.AddInt32("page", page);
	state.AddFloat("left", left);
	state.AddFloat("top", top);
	if (!anchor.IsEmpty())
		state.AddMessage("anchor", &anchor);

	ssize_t size = state.FlattenedSize();
	char *buffer = new char[size];
	bool ok = state.Flatten(buffer, size) == B_OK
		&& node.WriteAttr(kViewStateAttribute, B_MESSAGE_TYPE, 0, buffer, size) == size;
	delete []buffer;
	if (!ok)
		return false;
	node.RemoveAttr(kOldViewStateAttribute);

	if (reading >= 0) {
		const char* direction = reading == 1 ? "rtl" : reading == 2 ? "ttb" : "ltr";
		node.WriteAttr("SEN:readingProgression", B_STRING_TYPE, 0, direction, strlen(direction) + 1);
	}

	BPath path(ref);
	Bookmarks::Write(path.Path(), bookmarks);

	// the attributes of BePDF are removed if the user chose to replace them (their values are in the new ones now)
	if (s->GetLegacyAttributes() == 2) {
		static const char* const kLegacy[] = { "bepdf:pos_x", "bepdf:pos_y", "bepdf:width", "bepdf:height", "bepdf:zoom",
			"bepdf:rotation", "bepdf:page", "bepdf:left", "bepdf:top", "bepdf:bookmarks", "bepdf:anchor" };
		for (size_t i = 0; i < sizeof(kLegacy) / sizeof(kLegacy[0]); i++)
			node.RemoveAttr(kLegacy[i]);
	}
	return true;
}

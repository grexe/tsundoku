/*
 * SPDX-License-Identifier: MIT
 * Copyright (C) 2026 Gregor B. Rosenauer & Claude
 */
#include "Bookmarks.h"

#include "WebAnnotation.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <Message.h>
#include <Node.h>
#include <String.h>
#include <fs_attr.h>

#include <vector>

static const char* const kAttribute = "SEN:annotations";
static const int32 kVersion = 2;


namespace Bookmarks {

static bool
ReadArchive(BNode& node, BMessage* archive)
{
	attr_info info;
	if (node.GetAttrInfo(kAttribute, &info) != B_OK || info.size <= 0 || info.size > 16 * 1024 * 1024)
		return false;
	char* buffer = new char[info.size];
	bool ok = node.ReadAttr(kAttribute, info.type, 0, buffer, info.size) == info.size
		&& archive->Unflatten(buffer) == B_OK;
	delete[] buffer;
	return ok;
}


bool
IsBookmark(const BMessage& annotation)
{
	BString motivation;
	return annotation.FindString("oa:motivatedBy", &motivation) == B_OK && motivation == WebAnnotation::kBookmarking;
}


// Whether two bookmarks are the same place (a place in a book is told by its CFI or its words, otherwise by the page)
static bool
SamePlace(const WebAnnotation::Mark& a, const WebAnnotation::Mark& b)
{
	if (a.cfi.Length() > 0 || b.cfi.Length() > 0)
		return a.cfi == b.cfi;
	if (a.quote.Length() > 0 || b.quote.Length() > 0)
		return a.quote == b.quote;
	return a.textPage == b.textPage;
}


void
Read(const char* path, BMessage* bookmarks)
{
	bookmarks->MakeEmpty();
	BNode node(path);
	BMessage archive;
	if (node.InitCheck() != B_OK || !ReadArchive(node, &archive))
		return;

	BMessage item;
	for (int32 i = 0; archive.FindMessage(WebAnnotation::kAnnotation, i, &item) == B_OK; i++) {
		WebAnnotation::Mark mark;
		if (!IsBookmark(item) || !WebAnnotation::UnarchiveMark(item, &mark))
			continue;
		bookmarks->AddString("l", mark.body);
		bookmarks->AddInt32("p", mark.textPage > 0 ? mark.textPage : 1);
		BMessage anchor;
		if (mark.quote.Length() > 0) {
			anchor.AddInt32("chapter", mark.chapter);
			anchor.AddFloat("fraction", mark.fraction);
			anchor.AddFloat("ypos", mark.ypos);
			anchor.AddString("quote", mark.quote);
			if (mark.cfi.Length() > 0)
				anchor.AddString("cfi", mark.cfi);
		}
		bookmarks->AddMessage("a", &anchor);
	}
}


void
ReadLegacy(const char* path, BMessage* bookmarks)
{
	bookmarks->MakeEmpty();
	BNode node(path);
	attr_info info;
	if (node.InitCheck() != B_OK || node.GetAttrInfo("bepdf:bookmarks", &info) != B_OK || info.size <= 0
		|| info.size > 1024 * 1024)
		return;
	char* buffer = new char[info.size];
	if (node.ReadAttr("bepdf:bookmarks", B_MESSAGE_TYPE, 0, buffer, info.size) != info.size
		|| bookmarks->Unflatten(buffer) != B_OK)
		bookmarks->MakeEmpty();
	delete[] buffer;
}


bool
Write(const char* path, const BMessage& bookmarks)
{
	BNode node(path);
	if (node.InitCheck() != B_OK)
		return false;

	// what is there: the other annotations stay, the bookmarks keep their identifiers if they are still the same place
	BMessage old;
	std::vector<WebAnnotation::Mark> before;
	BMessage kept;
	kept.AddInt32("version", kVersion);
	int others = 0;
	if (ReadArchive(node, &old)) {
		BMessage item;
		for (int32 i = 0; old.FindMessage(WebAnnotation::kAnnotation, i, &item) == B_OK; i++) {
			if (IsBookmark(item)) {
				WebAnnotation::Mark mark;
				if (WebAnnotation::UnarchiveMark(item, &mark))
					before.push_back(mark);
			} else {
				kept.AddMessage(WebAnnotation::kAnnotation, &item);
				others++;
			}
		}
	}

	BString source = WebAnnotation::SenId(path);
	int added = 0;
	BString label;
	int32 page = 1;
	for (int32 i = 0; bookmarks.FindString("l", i, &label) == B_OK && bookmarks.FindInt32("p", i, &page) == B_OK; i++) {
		WebAnnotation::Mark mark;
		mark.motivation = WebAnnotation::kBookmarking;
		mark.body = label;
		BMessage anchor;
		bookmarks.FindMessage("a", i, &anchor);
		if (anchor.FindString("quote", &mark.quote) == B_OK && mark.quote.Length() > 0) {
			// a place in a book
			anchor.FindInt32("chapter", &mark.chapter);
			anchor.FindFloat("fraction", &mark.fraction);
			anchor.FindFloat("ypos", &mark.ypos);
			anchor.FindString("cfi", &mark.cfi);
		} else {
			mark.quote = "";
			mark.textPage = page;
		}

		mark.created = (int64)time(NULL);
		for (size_t k = 0; k < before.size(); k++) {
			if (SamePlace(before[k], mark)) {
				mark.id = before[k].id;
				mark.created = before[k].created;
				mark.creator = before[k].creator;
				break;
			}
		}
		if (mark.id.Length() == 0) {
			char id[48];
			WebAnnotation::NewId(id, sizeof(id));
			mark.id = id;
		}

		BMessage item;
		WebAnnotation::ArchiveMark(mark, &item);
		if (!source.IsEmpty())
			WebAnnotation::SetSource(&item, source.String());
		kept.AddMessage(WebAnnotation::kAnnotation, &item);
		added++;
	}

	// how many there are, for showing (not indexed)
	if (added > 0) {
		int32 count = added;
		node.WriteAttr("SEN:bookmarkCount", B_INT32_TYPE, 0, &count, sizeof(count));
	} else
		node.RemoveAttr("SEN:bookmarkCount");

	if (others + added == 0) {
		node.RemoveAttr(kAttribute);
		return true;
	}
	ssize_t size = kept.FlattenedSize();
	char* buffer = new char[size];
	bool ok = kept.Flatten(buffer, size) == B_OK
		&& node.WriteAttr(kAttribute, B_MESSAGE_TYPE, 0, buffer, size) == size;
	delete[] buffer;
	return ok;
}


bool
Carry(const char* path, BMessage* archive)
{
	BNode node(path);
	BMessage old;
	if (node.InitCheck() != B_OK || !ReadArchive(node, &old))
		return false;
	bool any = false;
	BMessage item;
	for (int32 i = 0; old.FindMessage(WebAnnotation::kAnnotation, i, &item) == B_OK; i++) {
		if (IsBookmark(item)) {
			archive->AddMessage(WebAnnotation::kAnnotation, &item);
			any = true;
		}
	}
	return any;
}

}	// namespace Bookmarks

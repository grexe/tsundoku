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


#include "Scripting.h"

#include <ScrollView.h>
#include <ListView.h>
#include <OutlineListView.h>
#include <Window.h>

#include "Document.h"
#include "OutlinesWindow.h"
#include "PDFView.h"
#include "PDFWindow.h"
#include "WebAnnotation.h"

#include <stdlib.h>
#include <string.h>

#include <Message.h>
#include <Point.h>
#include <PropertyInfo.h>
#include <Rect.h>

extern "C" {
#include <mupdf/pdf.h>
}

namespace Scripting {

const char* const kSuite = "suite/vnd.sen-labs.Tsundoku";

enum {
	kPath, kTitle, kType, kPageCount, kTextSize, kPageProperty, kSelection, kGoto, kSave, kUndo, kRedo,
	kUpgrade, kAnnotations, kBookmarks, kPages, kAddAnnotation, kAddBookmark
};

static property_info sDocumentProperties[] = {
	{ "Path", { B_GET_PROPERTY, 0 }, { B_DIRECT_SPECIFIER, 0 }, "The path of the file.", 0, { B_STRING_TYPE } },
	{ "Title", { B_GET_PROPERTY, 0 }, { B_DIRECT_SPECIFIER, 0 }, "The title that the document gives itself.", 0,
		{ B_STRING_TYPE } },
	{ "Type", { B_GET_PROPERTY, 0 }, { B_DIRECT_SPECIFIER, 0 }, "pdf, epub, comic, djvu or other.", 0,
		{ B_STRING_TYPE } },
	{ "PageCount", { B_GET_PROPERTY, 0 }, { B_DIRECT_SPECIFIER, 0 }, "The number of pages.", 0, { B_INT32_TYPE } },
	{ "TextSize", { B_GET_PROPERTY, 0 }, { B_DIRECT_SPECIFIER, 0 }, "The text size of a book.", 0, { B_FLOAT_TYPE } },
	{ "Page", { B_GET_PROPERTY, B_SET_PROPERTY, 0 }, { B_DIRECT_SPECIFIER, 0 },
		"The page that is shown (1-based). Setting it goes to the page.", 0, { B_INT32_TYPE } },
	{ "Selection", { B_GET_PROPERTY, 0 }, { B_DIRECT_SPECIFIER, 0 }, "The text that is selected.", 0,
		{ B_STRING_TYPE } },
	{ "Goto", { B_EXECUTE_PROPERTY, 0 }, { B_DIRECT_SPECIFIER, 0 }, "Goes to the page given in 'page'.", 0 },
	{ "Save", { B_EXECUTE_PROPERTY, 0 }, { B_DIRECT_SPECIFIER, 0 }, "Saves the document.", 0 },
	{ "Undo", { B_EXECUTE_PROPERTY, 0 }, { B_DIRECT_SPECIFIER, 0 }, "Takes back the last change.", 0 },
	{ "Redo", { B_EXECUTE_PROPERTY, 0 }, { B_DIRECT_SPECIFIER, 0 }, "Repeats the change that was taken back.", 0 },
	{ "UpgradeAnnotations", { B_EXECUTE_PROPERTY, 0 }, { B_DIRECT_SPECIFIER, 0 },
		"Gives the annotations that have no identifier one, and saves the file. Returns how many.", 0,
		{ B_INT32_TYPE } },
	{ "Annotation", { B_GET_PROPERTY, B_COUNT_PROPERTIES, B_CREATE_PROPERTY, B_DELETE_PROPERTY, 0 },
		{ B_DIRECT_SPECIFIER, B_INDEX_SPECIFIER, B_REVERSE_INDEX_SPECIFIER, B_NAME_SPECIFIER, 0 },
		"The annotations: of the document, or of a page with context=page (and page=N).", 0, { B_MESSAGE_TYPE } },
	{ "Bookmark", { B_GET_PROPERTY, B_COUNT_PROPERTIES, B_CREATE_PROPERTY, B_DELETE_PROPERTY, 0 },
		{ B_DIRECT_SPECIFIER, B_INDEX_SPECIFIER, B_REVERSE_INDEX_SPECIFIER, B_NAME_SPECIFIER, 0 },
		"The bookmarks of the reader (the name is the label).", 0, { B_MESSAGE_TYPE } },
	{ "Page", { B_GET_PROPERTY, 0 }, { B_INDEX_SPECIFIER, 0 }, "A page (1-based).", 0, { B_MESSAGE_TYPE } },
	{ "AddAnnotation", { B_EXECUTE_PROPERTY, 0 }, { B_DIRECT_SPECIFIER, 0 },
		"Makes an annotation (what CREATE of Annotation does; hey cannot nest the specifiers of a CREATE). 'kind' is "
		"highlight, underline, strikeout or squiggly (with 'quote', the words on 'page', or the selection), note or "
		"text ('x', 'y', 'text'), rectangle, ellipse, line or arrow ('left', 'top', 'right', 'bottom') or ink "
		"('points'); 'color' and 'text' are optional. Returns the identifier.", 0, { B_STRING_TYPE } },
	{ "AddBookmark", { B_EXECUTE_PROPERTY, 0 }, { B_DIRECT_SPECIFIER, 0 },
		"Makes a bookmark (what CREATE of Bookmark does): 'label' and 'page'.", 0 },
	{ 0 }
};


enum { kId, kKind, kAnnotationPage, kText, kQuote, kColor, kBounds, kAuthor, kJson, kAnnotationGoto };

static property_info sAnnotationProperties[] = {
	{ "Id", { B_GET_PROPERTY, 0 }, { B_DIRECT_SPECIFIER, 0 }, "The identifier (the name of the annotation).", 0,
		{ B_STRING_TYPE } },
	{ "Kind", { B_GET_PROPERTY, 0 }, { B_DIRECT_SPECIFIER, 0 },
		"highlight, underline, strikeout, squiggly, note, text, rectangle, ellipse, line, ink.", 0, { B_STRING_TYPE } },
	{ "Page", { B_GET_PROPERTY, 0 }, { B_DIRECT_SPECIFIER, 0 }, "The page it is on.", 0, { B_INT32_TYPE } },
	{ "Text", { B_GET_PROPERTY, B_SET_PROPERTY, 0 }, { B_DIRECT_SPECIFIER, 0 }, "The note.", 0, { B_STRING_TYPE } },
	{ "Quote", { B_GET_PROPERTY, B_SET_PROPERTY, 0 }, { B_DIRECT_SPECIFIER, 0 },
		"The words that a mark covers. Setting them moves the mark to other words of the page.", 0,
		{ B_STRING_TYPE } },
	{ "Color", { B_GET_PROPERTY, B_SET_PROPERTY, 0 }, { B_DIRECT_SPECIFIER, 0 }, "The color, 0xRRGGBB.", 0,
		{ B_INT32_TYPE } },
	{ "Bounds", { B_GET_PROPERTY, B_SET_PROPERTY, 0 }, { B_DIRECT_SPECIFIER, 0 }, "Where it is on the page.", 0,
		{ B_RECT_TYPE } },
	{ "Author", { B_GET_PROPERTY, 0 }, { B_DIRECT_SPECIFIER, 0 }, "Who made it.", 0, { B_STRING_TYPE } },
	{ "JSON", { B_GET_PROPERTY, 0 }, { B_DIRECT_SPECIFIER, 0 }, "The annotation as a Web Annotation (JSON-LD).", 0,
		{ B_STRING_TYPE } },
	{ "Goto", { B_EXECUTE_PROPERTY, 0 }, { B_DIRECT_SPECIFIER, 0 }, "Goes to the annotation and selects it.", 0 },
	{ 0 }
};


enum { kLabel, kBookmarkPage, kBookmarkGoto };

static property_info sBookmarkProperties[] = {
	{ "Label", { B_GET_PROPERTY, B_SET_PROPERTY, 0 }, { B_DIRECT_SPECIFIER, 0 }, "The label.", 0, { B_STRING_TYPE } },
	{ "Page", { B_GET_PROPERTY, 0 }, { B_DIRECT_SPECIFIER, 0 }, "The page it is on now.", 0, { B_INT32_TYPE } },
	{ "Goto", { B_EXECUTE_PROPERTY, 0 }, { B_DIRECT_SPECIFIER, 0 }, "Goes to the bookmark.", 0 },
	{ 0 }
};


enum { kPageText, kPageSize, kPageGoto };

static property_info sPageProperties[] = {
	{ "Text", { B_GET_PROPERTY, 0 }, { B_DIRECT_SPECIFIER, 0 }, "The text of the page.", 0, { B_STRING_TYPE } },
	{ "Size", { B_GET_PROPERTY, 0 }, { B_DIRECT_SPECIFIER, 0 }, "The size of the page in points.", 0, { B_RECT_TYPE } },
	{ "Goto", { B_EXECUTE_PROPERTY, 0 }, { B_DIRECT_SPECIFIER, 0 }, "Goes to the page.", 0 },
	{ 0 }
};


// Which property of a table (its index) a command and a specifier lead to, -1 if none. (BPropertyInfo::FindMatch does the
// same, but it did not find the index specifiers of the entries here.)
static int32
FindProperty(const property_info* table, uint32 command, int32 form, const char* property, bool anyCommand = false)
{
	for (int32 i = 0; table[i].name != NULL; i++) {
		if (strcmp(table[i].name, property) != 0)
			continue;
		bool commandOk = anyCommand || table[i].commands[0] == 0, formOk = false;	// (no commands: any)
		for (int k = 0; k < 10 && table[i].commands[k] != 0; k++) {
			if (table[i].commands[k] == command)
				commandOk = true;
		}
		for (int k = 0; k < 10 && table[i].specifiers[k] != 0; k++) {
			if ((int32)table[i].specifiers[k] == form)
				formOk = true;
		}
		if (commandOk && formOk)
			return i;
	}
	return -1;
}


// ---- replies and values

static void
ReplyError(BMessage* message, status_t error, const char* text)
{
	BMessage reply(B_MESSAGE_NOT_UNDERSTOOD);
	reply.AddInt32("error", error);
	reply.AddString("message", text);
	message->SendReply(&reply);
}


static void
ReplyDone(BMessage* message, const BMessage& reply)
{
	BMessage done(reply);
	done.what = B_REPLY;
	done.AddInt32("error", B_OK);
	message->SendReply(&done);
}


// a number that may have come as text (hey gives what was typed)
static bool
NumberField(const BMessage* message, const char* name, double* value)
{
	int32 i32;
	int64 i64;
	float f;
	double d;
	const char* text;
	if (message->FindInt32(name, &i32) == B_OK) {
		*value = i32;
		return true;
	}
	if (message->FindInt64(name, &i64) == B_OK) {
		*value = (double)i64;
		return true;
	}
	if (message->FindFloat(name, &f) == B_OK) {
		*value = f;
		return true;
	}
	if (message->FindDouble(name, &d) == B_OK) {
		*value = d;
		return true;
	}
	if (message->FindString(name, &text) == B_OK && text[0] != '\0') {
		char* end;
		double parsed = strtod(text, &end);
		if (end != text) {
			*value = parsed;
			return true;
		}
	}
	return false;
}


static int
IntField(const BMessage* message, const char* name, int fallback)
{
	double value;
	return NumberField(message, name, &value) ? (int)value : fallback;
}


// 0xRRGGBB, as a number or as text (#rrggbb, rrggbb, 0xrrggbb)
static bool
ColorField(const BMessage* message, const char* name, uint32* rgb)
{
	const char* text;
	if (message->FindString(name, &text) == B_OK && text[0] != '\0') {
		if (text[0] == '#')
			text++;
		else if (text[0] == '0' && (text[1] == 'x' || text[1] == 'X'))
			text += 2;
		char* end;
		unsigned long value = strtoul(text, &end, 16);
		if (end == text)
			return false;
		*rgb = (uint32)(value & 0xffffff);
		return true;
	}
	double value;
	if (NumberField(message, name, &value)) {
		*rgb = (uint32)((int64)value & 0xffffff);
		return true;
	}
	return false;
}


// The annotations that a message asks for: the whole document, or one page (context=page, page=N or the page shown)
struct Scope {
	bool	page;
	int		pageNo;
};


static Scope
ScopeOf(PDFWindow* window, const BMessage* message)
{
	Scope scope;
	const char* context;
	scope.page = message->FindString("context", &context) == B_OK && strcasecmp(context, "page") == 0;
	scope.pageNo = IntField(message, "page", window->View()->Page());
	return scope;
}


static const char*
KindName(const DocAnnotation& a)
{
	switch (a.kind) {
		case kAnnotMarkup:
			switch (a.type) {
				case PDF_ANNOT_UNDERLINE:	return "underline";
				case PDF_ANNOT_STRIKE_OUT:	return "strikeout";
				case PDF_ANNOT_SQUIGGLY:	return "squiggly";
				default:					return "highlight";
			}
		case kAnnotNote:		return "note";
		case kAnnotText:		return "text";
		case kAnnotRectangle:	return "rectangle";
		case kAnnotEllipse:		return "ellipse";
		case kAnnotLine:		return "line";
		case kAnnotInk:			return "ink";
		default:				return "other";
	}
}


static void
DescribeAnnotation(const DocAnnotationEntry& entry, BMessage* info)
{
	const DocAnnotation& a = entry.annotation;
	info->AddString("Id", a.id);
	info->AddString("Kind", KindName(a));
	info->AddInt32("Page", entry.page);
	info->AddInt32("Index", a.index);
	info->AddString("Text", a.contents);
	info->AddString("Quote", a.isMarkup ? entry.excerpt : BString(""));
	if (a.hasColor)
		info->AddInt32("Color", (int32)a.color);
	info->AddRect("Bounds", BRect(a.rect.x0, a.rect.y0, a.rect.x1, a.rect.y1));
	info->AddString("Author", a.author);
}


// the annotations in the scope; a part of a mark that runs over a page break is the mark of the page before
static bool
ListAnnotations(PDFWindow* window, const Scope& scope, std::vector<DocAnnotationEntry>* entries)
{
	Document* doc = window->View()->GetDocument();
	if (doc == NULL)
		return false;
	std::vector<DocAnnotationEntry> all;
	bool ok;
	if (scope.page)
		ok = doc->ListAnnotationsOnPage(scope.pageNo, all);
	else {
		volatile bool cancel = false;
		ok = doc->ListAnnotations(all, &cancel);
	}
	if (!ok)
		return false;
	for (size_t i = 0; i < all.size(); i++) {
		if (!all[i].annotation.continued)
			entries->push_back(all[i]);
	}
	return true;
}


struct BookmarkInfo {
	BString	label;
	int		page;
};


// the bookmarks as the sidebar has them; for a place in a book the page it is on now
static void
ListBookmarks(PDFWindow* window, const Scope& scope, std::vector<BookmarkInfo>* bookmarks)
{
	Document* doc = window->View()->GetDocument();
	BMessage list;
	if (doc == NULL || !window->BookmarkList()->GetBookmarks(&list))
		return;
	BString label;
	int32 page;
	for (int32 i = 0; list.FindString("l", i, &label) == B_OK && list.FindInt32("p", i, &page) == B_OK; i++) {
		BMessage anchorMessage;
		TextAnchor anchor;
		if (doc->IsReflowable() && list.FindMessage("a", i, &anchorMessage) == B_OK && anchor.Unarchive(&anchorMessage)) {
			int resolved = doc->PageOfAnchor(anchor);
			if (resolved > 0)
				page = resolved;
		}
		if (scope.page && page != scope.pageNo)
			continue;
		BookmarkInfo info;
		info.label = label;
		info.page = page;
		bookmarks->push_back(info);
	}
}


static void
GoToPage(PDFWindow* window, int page)
{
	BMessage go(PDFWindow::GOTO_PAGE_CMD);
	go.AddInt32("page", page);
	window->MessageReceived(&go);
}


// where an index, a reverse index or a name leads in a list of n things (or -1)
static int32
Position(int32 form, const BMessage* specifier, int32 n)
{
	int32 index;
	if (form == B_INDEX_SPECIFIER && specifier->FindInt32("index", &index) == B_OK) {
		if (index < 0)
			index += n;		// (-1 is the last)
		return index >= 0 && index < n ? index : -1;
	}
	if (form == B_REVERSE_INDEX_SPECIFIER && specifier->FindInt32("index", &index) == B_OK)
		return index >= 0 && index < n ? n - 1 - index : -1;
	return -1;
}


// ---- an annotation

class AnnotationHandler : public BHandler {
public:
	AnnotationHandler(PDFWindow* window)
		:
		BHandler("annotation"),
		fWindow(window),
		fPage(0),
		fIndex(0)
	{
	}

	void Select(const DocAnnotationEntry& entry)
	{
		fPage = entry.page;
		fIndex = entry.annotation.index;
		fId = entry.annotation.id;
	}

	virtual status_t GetSupportedSuites(BMessage* data)
	{
		data->AddString("suites", kSuite);
		BPropertyInfo info(sAnnotationProperties);
		data->AddFlat("messages", &info);
		return BHandler::GetSupportedSuites(data);
	}

	virtual BHandler* ResolveSpecifier(BMessage* message, int32 index, BMessage* specifier, int32 what,
		const char* property)
	{
		if (FindProperty(sAnnotationProperties, message->what, what, property) >= 0)
			return this;
		return BHandler::ResolveSpecifier(message, index, specifier, what, property);
	}

	virtual void MessageReceived(BMessage* message);

	// the annotation as it is now (it may have been changed by the user since it was resolved)
	bool Current(DocAnnotationEntry* entry)
	{
		Document* doc = fWindow->View()->GetDocument();
		int page, index;
		if (doc == NULL)
			return false;
		if (fId.Length() > 0 && doc->FindAnnotationById(fId.String(), &page, &index)) {
			fPage = page;
			fIndex = index;
		}
		std::vector<DocAnnotationEntry> entries;
		if (!doc->ListAnnotationsOnPage(fPage, entries))
			return false;
		for (size_t i = 0; i < entries.size(); i++) {
			if (entries[i].annotation.index == fIndex && !entries[i].annotation.continued) {
				*entry = entries[i];
				return true;
			}
		}
		return false;
	}

private:
	PDFWindow*	fWindow;
	int			fPage;
	int			fIndex;
	BString		fId;
};


void
AnnotationHandler::MessageReceived(BMessage* message)
{
	int32 index, form;
	BMessage specifier;
	const char* name;
	if (!message->HasSpecifiers()
		|| message->GetCurrentSpecifier(&index, &specifier, &form, &name) != B_OK) {
		BHandler::MessageReceived(message);
		return;
	}

	Document* doc = fWindow->View()->GetDocument();
	DocAnnotationEntry entry;
	if (doc == NULL || !Current(&entry)) {
		ReplyError(message, B_ENTRY_NOT_FOUND, "the annotation is not there any more");
		return;
	}

	int32 match = FindProperty(sAnnotationProperties, message->what, form, name);
	const DocAnnotation& a = entry.annotation;
	BMessage reply;

	switch (match) {
		case kId:
			reply.AddString("result", a.id);
			break;
		case kKind:
			reply.AddString("result", KindName(a));
			break;
		case kAnnotationPage:
			reply.AddInt32("result", entry.page);
			break;
		case kText:
			if (message->what == B_GET_PROPERTY)
				reply.AddString("result", a.contents);
			else {
				const char* text;
				if (message->FindString("data", &text) != B_OK) {
					ReplyError(message, B_BAD_VALUE, "no text given");
					return;
				}
				if (!doc->SetAnnotationContents(entry.page, a.index, text)) {
					ReplyError(message, B_ERROR, "cannot change the note");
					return;
				}
				fWindow->View()->AnnotationsChanged(entry.page);
			}
			break;
		case kQuote:
			if (message->what == B_GET_PROPERTY)
				reply.AddString("result", a.isMarkup ? entry.excerpt : BString(""));
			else {
				const char* text;
				if (message->FindString("data", &text) != B_OK) {
					ReplyError(message, B_BAD_VALUE, "no words given");
					return;
				}
				if (!a.isMarkup || !doc->MoveMarkupToQuote(entry.page, a.index, text)) {
					ReplyError(message, B_ERROR, "the words are not on the page, or the annotation is not a mark");
					return;
				}
				fWindow->View()->AnnotationsChanged(entry.page);
			}
			break;
		case kColor:
			if (message->what == B_GET_PROPERTY) {
				if (!a.hasColor) {
					ReplyError(message, B_ENTRY_NOT_FOUND, "the annotation has no color");
					return;
				}
				reply.AddInt32("result", (int32)a.color);
			} else {
				uint32 rgb;
				if (!ColorField(message, "data", &rgb)) {
					ReplyError(message, B_BAD_VALUE, "no color given");
					return;
				}
				if (!doc->SetAnnotationColor(entry.page, a.index, rgb)) {
					ReplyError(message, B_ERROR, "cannot change the color");
					return;
				}
				fWindow->View()->AnnotationsChanged(entry.page);
			}
			break;
		case kBounds:
			if (message->what == B_GET_PROPERTY)
				reply.AddRect("result", BRect(a.rect.x0, a.rect.y0, a.rect.x1, a.rect.y1));
			else {
				BRect bounds;
				if (message->FindRect("data", &bounds) != B_OK) {
					ReplyError(message, B_BAD_VALUE, "no bounds given");
					return;
				}
				fz_rect rect = fz_make_rect(bounds.left, bounds.top, bounds.right, bounds.bottom);
				if (!doc->SetAnnotationBounds(entry.page, a.index, rect, true)) {
					ReplyError(message, B_ERROR, "cannot change the bounds");
					return;
				}
				fWindow->View()->AnnotationsChanged(entry.page);
			}
			break;
		case kAuthor:
			reply.AddString("result", a.author);
			break;
		case kJson: {
			BMessage web;
			if (!doc->WebAnnotationOf(entry.page, a.index, &web)) {
				ReplyError(message, B_ERROR, "cannot describe the annotation");
				return;
			}
			reply.AddString("result", WebAnnotation::ToJson(web));
			break;
		}
		case kAnnotationGoto: {
			BMessage show(PDFWindow::SHOW_ANNOTATION_CMD);
			show.AddInt32("page", entry.page);
			show.AddInt32("index", a.index);
			fWindow->MessageReceived(&show);
			break;
		}
		default:
			BHandler::MessageReceived(message);
			return;
	}
	ReplyDone(message, reply);
}


// ---- a bookmark

class BookmarkHandler : public BHandler {
public:
	BookmarkHandler(PDFWindow* window)
		:
		BHandler("bookmark"),
		fWindow(window),
		fPage(0)
	{
	}

	void Select(const BookmarkInfo& info)
	{
		fLabel = info.label;
		fPage = info.page;
	}

	virtual status_t GetSupportedSuites(BMessage* data)
	{
		data->AddString("suites", kSuite);
		BPropertyInfo info(sBookmarkProperties);
		data->AddFlat("messages", &info);
		return BHandler::GetSupportedSuites(data);
	}

	virtual BHandler* ResolveSpecifier(BMessage* message, int32 index, BMessage* specifier, int32 what,
		const char* property)
	{
		if (FindProperty(sBookmarkProperties, message->what, what, property) >= 0)
			return this;
		return BHandler::ResolveSpecifier(message, index, specifier, what, property);
	}

	virtual void MessageReceived(BMessage* message)
	{
		int32 index, form;
		BMessage specifier;
		const char* name;
		if (!message->HasSpecifiers()
			|| message->GetCurrentSpecifier(&index, &specifier, &form, &name) != B_OK) {
			BHandler::MessageReceived(message);
			return;
		}
		BMessage reply;
		switch (FindProperty(sBookmarkProperties, message->what, form, name)) {
			case kLabel:
				if (message->what == B_GET_PROPERTY)
					reply.AddString("result", fLabel);
				else {
					const char* text;
					if (message->FindString("data", &text) != B_OK) {
						ReplyError(message, B_BAD_VALUE, "no label given");
						return;
					}
					// the bookmark of the page is made again (with its place in the text, for a book)
					OutlinesView* list = fWindow->BookmarkList();
					BMessage all;
					BString label;
					int32 page;
					BMessage anchor;
					if (list->GetBookmarks(&all)) {
						for (int32 i = 0; all.FindString("l", i, &label) == B_OK && all.FindInt32("p", i, &page) == B_OK;
								i++) {
							if (label == fLabel && page == fPage) {
								all.FindMessage("a", i, &anchor);
								break;
							}
						}
					}
					list->AddUserBookmark(fPage, text, anchor.IsEmpty() ? NULL : &anchor);
					fWindow->BookmarksChanged();
					fLabel = text;
				}
				break;
			case kBookmarkPage:
				reply.AddInt32("result", fPage);
				break;
			case kBookmarkGoto:
				GoToPage(fWindow, fPage);
				break;
			default:
				BHandler::MessageReceived(message);
				return;
		}
		ReplyDone(message, reply);
	}

private:
	PDFWindow*	fWindow;
	BString		fLabel;
	int			fPage;
};


// ---- a page

class PageHandler : public BHandler {
public:
	PageHandler(PDFWindow* window)
		:
		BHandler("page"),
		fWindow(window),
		fPage(1)
	{
	}

	void Select(int page) { fPage = page; }

	virtual status_t GetSupportedSuites(BMessage* data)
	{
		data->AddString("suites", kSuite);
		BPropertyInfo info(sPageProperties);
		data->AddFlat("messages", &info);
		return BHandler::GetSupportedSuites(data);
	}

	virtual BHandler* ResolveSpecifier(BMessage* message, int32 index, BMessage* specifier, int32 what,
		const char* property)
	{
		if (FindProperty(sPageProperties, message->what, what, property) >= 0)
			return this;
		return BHandler::ResolveSpecifier(message, index, specifier, what, property);
	}

	virtual void MessageReceived(BMessage* message)
	{
		int32 index, form;
		BMessage specifier;
		const char* name;
		if (!message->HasSpecifiers()
			|| message->GetCurrentSpecifier(&index, &specifier, &form, &name) != B_OK) {
			BHandler::MessageReceived(message);
			return;
		}
		Document* doc = fWindow->View()->GetDocument();
		BMessage reply;
		switch (FindProperty(sPageProperties, message->what, form, name)) {
			case kPageText: {
				// the text of the page, as the search sees it
				if (doc == NULL || !doc->PageText(fPage, &fText)) {
					ReplyError(message, B_ERROR, "cannot read the text of the page");
					return;
				}
				reply.AddString("result", fText);
				break;
			}
			case kPageSize: {
				fz_rect bounds;
				if (doc == NULL || !doc->PageBounds(fPage, &bounds)) {
					ReplyError(message, B_ERROR, "cannot measure the page");
					return;
				}
				reply.AddRect("result", BRect(0, 0, bounds.x1 - bounds.x0, bounds.y1 - bounds.y0));
				break;
			}
			case kPageGoto:
				GoToPage(fWindow, fPage);
				break;
			default:
				BHandler::MessageReceived(message);
				return;
		}
		ReplyDone(message, reply);
	}

private:
	PDFWindow*	fWindow;
	int			fPage;
	BString		fText;
};


// ---- the document

DocumentHandler::DocumentHandler(PDFWindow* window)
	:
	BHandler("document"),
	fWindow(window)
{
	fAnnotation = new AnnotationHandler(window);
	fBookmark = new BookmarkHandler(window);
	fPage = new PageHandler(window);
	window->AddHandler(fAnnotation);
	window->AddHandler(fBookmark);
	window->AddHandler(fPage);
}


DocumentHandler::~DocumentHandler()
{
}


status_t
DocumentHandler::GetSupportedSuites(BMessage* data)
{
	data->AddString("suites", kSuite);
	BPropertyInfo info(sDocumentProperties);
	data->AddFlat("messages", &info);
	return BHandler::GetSupportedSuites(data);
}


BHandler*
DocumentHandler::ResolveSpecifier(BMessage* message, int32 index, BMessage* specifier, int32 what,
	const char* property)
{
	// (what a specifier leads to does not depend on the command when the specifiers go on: it is for what comes after)
	const bool last = index == 0;
	int32 match = FindProperty(sDocumentProperties, message->what, what, property, !last);
	if (match < 0)
		return BHandler::ResolveSpecifier(message, index, specifier, what, property);

	Document* doc = fWindow->View()->GetDocument();

	if (match == kAnnotations && what != B_DIRECT_SPECIFIER && !last) {
		// an annotation, and a property of it that follows
		std::vector<DocAnnotationEntry> entries, found;
		Scope scope = ScopeOf(fWindow, message);
		const char* id;
		if (what == B_NAME_SPECIFIER && specifier->FindString("name", &id) == B_OK) {
			int page, at;
			Scope all = { false, 0 };
			if (doc != NULL && doc->FindAnnotationById(id, &page, &at)
				&& ListAnnotations(fWindow, all, &entries)) {
				for (size_t i = 0; i < entries.size(); i++) {
					if (entries[i].annotation.id == id)
						found.push_back(entries[i]);
				}
			}
		} else if (ListAnnotations(fWindow, scope, &entries)) {
			int32 at = Position(what, specifier, (int32)entries.size());
			if (at >= 0)
				found.push_back(entries[at]);
		}
		if (found.empty()) {
			ReplyError(message, B_BAD_INDEX, "no such annotation");
			return NULL;
		}
		((AnnotationHandler*)fAnnotation)->Select(found[0]);
		message->PopSpecifier();
		return fAnnotation;
	}

	if (match == kBookmarks && what != B_DIRECT_SPECIFIER && !last) {
		std::vector<BookmarkInfo> bookmarks;
		ListBookmarks(fWindow, ScopeOf(fWindow, message), &bookmarks);
		int32 at = -1;
		const char* label;
		if (what == B_NAME_SPECIFIER && specifier->FindString("name", &label) == B_OK) {
			for (size_t i = 0; i < bookmarks.size(); i++) {
				if (bookmarks[i].label == label) {
					at = (int32)i;
					break;
				}
			}
		} else
			at = Position(what, specifier, (int32)bookmarks.size());
		if (at < 0) {
			ReplyError(message, B_BAD_INDEX, "no such bookmark");
			return NULL;
		}
		((BookmarkHandler*)fBookmark)->Select(bookmarks[at]);
		message->PopSpecifier();
		return fBookmark;
	}

	if (match == kPages && !last) {
		int32 number;
		if (specifier->FindInt32("index", &number) != B_OK || doc == NULL || number < 1
			|| number > doc->PageCount()) {
			ReplyError(message, B_BAD_INDEX, "no such page");
			return NULL;
		}
		((PageHandler*)fPage)->Select(number);
		message->PopSpecifier();
		return fPage;
	}
	if (match == kPages) {
		// (a page as a whole: its properties are asked for by name)
		ReplyError(message, B_BAD_SCRIPT_SYNTAX, "ask for a property of the page: Text, Size, Goto");
		return NULL;
	}

	if ((match == kAnnotations || match == kBookmarks) && what == B_DIRECT_SPECIFIER && !last) {
		ReplyError(message, B_BAD_SCRIPT_SYNTAX, "give an index or a name");
		return NULL;
	}
	return this;
}


// creates an annotation from what the message says
static bool
CreateAnnotation(PDFWindow* window, BMessage* message, BString* id, BString* error)
{
	Document* doc = window->View()->GetDocument();
	PDFView* view = window->View();
	if (doc == NULL || !doc->CanAnnotate()) {
		*error = "the document cannot be annotated";
		return false;
	}

	BString kind;
	if (message->FindString("kind", &kind) != B_OK) {
		*error = "no kind given";
		return false;
	}
	kind.ToLower();
	const int page = IntField(message, "page", view->Page());
	if (page < 1 || page > doc->PageCount()) {
		*error = "no such page";
		return false;
	}
	const char* text = "";
	if (message->FindString("text", &text) != B_OK || text == NULL)
		text = "";

	std::vector<DocAnnotationEntry> before;
	doc->ListAnnotationsOnPage(page, before);
	int highest = -1;
	for (size_t i = 0; i < before.size(); i++) {
		if (before[i].annotation.index > highest)
			highest = before[i].annotation.index;
	}

	bool ok = false;
	uint32 rgb = 0;
	bool hasColor = ColorField(message, "color", &rgb);

	if (kind == "highlight" || kind == "underline" || kind == "strikeout" || kind == "squiggly") {
		if (!doc->CanMarkText()) {
			*error = "this document cannot take marks on text";
			return false;
		}
		MarkupType type = kind == "underline" ? kMarkupUnderline : kind == "strikeout" ? kMarkupStrikeOut
			: kind == "squiggly" ? kMarkupSquiggly : kMarkupHighlight;
		if (!hasColor)
			rgb = type == kMarkupHighlight ? 0xffeb3b : 0xe53935;
		const char* quote;
		if (message->FindString("quote", &quote) == B_OK && quote[0] != '\0') {
			std::vector<fz_quad> quads;
			if (!doc->FindQuoteQuads(page, quote, &quads)) {
				*error = "the words are not on the page";
				return false;
			}
			float color[3] = { ((rgb >> 16) & 0xff) / 255.0f, ((rgb >> 8) & 0xff) / 255.0f, (rgb & 0xff) / 255.0f };
			ok = doc->AddMarkup(page, type, quads.data(), (int)quads.size(), color);
		} else if (view->HasTextSelection())
			ok = view->AnnotateSelection(type, rgb);
		else {
			*error = "give the words (quote) or select some text";
			return false;
		}
	} else if (kind == "note" || kind == "text") {
		if (!doc->CanDrawAnnotations()) {
			*error = "this document cannot take notes";
			return false;
		}
		double x = 0, y = 0;
		if (!NumberField(message, "x", &x) || !NumberField(message, "y", &y)) {
			*error = "give the place (x and y, in points)";
			return false;
		}
		fz_point where = fz_make_point(x, y);
		ok = kind == "note" ? doc->AddNote(page, where, text) : doc->AddFreeText(page, where, text);
	} else if (kind == "rectangle" || kind == "ellipse" || kind == "line" || kind == "arrow") {
		if (!doc->CanDrawAnnotations()) {
			*error = "this document cannot take shapes";
			return false;
		}
		double left, top, right, bottom;
		if (!NumberField(message, "left", &left) || !NumberField(message, "top", &top)
			|| !NumberField(message, "right", &right) || !NumberField(message, "bottom", &bottom)) {
			*error = "give left, top, right and bottom (in points)";
			return false;
		}
		ShapeType shape = kind == "rectangle" ? kShapeRectangle : kind == "ellipse" ? kShapeEllipse
			: kind == "line" ? kShapeLine : kShapeArrow;
		if (!hasColor)
			rgb = 0xe53935;
		ok = doc->AddShape(page, shape, fz_make_point(left, top), fz_make_point(right, bottom), rgb);
	} else if (kind == "ink") {
		if (!doc->CanDrawAnnotations()) {
			*error = "this document cannot take drawings";
			return false;
		}
		type_code type;
		int32 count;
		if (message->GetInfo("points", &type, &count) != B_OK || type != B_POINT_TYPE || count < 2) {
			*error = "give the points of the line (points)";
			return false;
		}
		std::vector<fz_point> points;
		BPoint point;
		for (int32 i = 0; message->FindPoint("points", i, &point) == B_OK; i++)
			points.push_back(fz_make_point(point.x, point.y));
		if (!hasColor)
			rgb = 0xe53935;
		ok = doc->AddInk(page, points.data(), (int)points.size(), rgb);
	} else {
		*error = "the kind is highlight, underline, strikeout, squiggly, note, text, rectangle, ellipse, line, arrow or ink";
		return false;
	}
	if (!ok) {
		*error = "cannot make the annotation";
		return false;
	}

	// the new one is the last on its page
	std::vector<DocAnnotationEntry> after;
	doc->ListAnnotationsOnPage(page, after);
	for (size_t i = 0; i < after.size(); i++) {
		if (after[i].annotation.index > highest && !after[i].annotation.continued) {
			if (text[0] != '\0' && kind != "note" && kind != "text")
				doc->SetAnnotationContents(page, after[i].annotation.index, text);
			*id = after[i].annotation.id;
		}
	}
	view->AnnotationsChanged(page);
	return true;
}


void
DocumentHandler::MessageReceived(BMessage* message)
{
	int32 index, form;
	BMessage specifier;
	const char* name;
	if (!message->HasSpecifiers()
		|| message->GetCurrentSpecifier(&index, &specifier, &form, &name) != B_OK) {
		BHandler::MessageReceived(message);
		return;
	}

	PDFView* view = fWindow->View();
	Document* doc = view->GetDocument();
	if (doc == NULL) {
		ReplyError(message, B_NO_INIT, "there is no document");
		return;
	}

	int32 match = FindProperty(sDocumentProperties, message->what, form, name);
	BMessage reply;

	switch (match) {
		case kPath:
			reply.AddString("result", doc->Path());
			break;
		case kTitle:
			reply.AddString("result", doc->Metadata("info:Title"));
			break;
		case kType:
			reply.AddString("result", doc->IsPDF() ? "pdf" : doc->Epub() != NULL ? "epub" : doc->IsComic() ? "comic"
				: doc->IsDjvu() ? "djvu" : "other");
			break;
		case kPageCount:
			reply.AddInt32("result", doc->PageCount());
			break;
		case kTextSize:
			reply.AddFloat("result", doc->TextSize());
			break;
		case kPageProperty:
			if (message->what == B_GET_PROPERTY)
				reply.AddInt32("result", view->Page());
			else {
				double page;
				if (!NumberField(message, "data", &page) || page < 1 || page > doc->PageCount()) {
					ReplyError(message, B_BAD_VALUE, "no such page");
					return;
				}
				GoToPage(fWindow, (int)page);
			}
			break;
		case kSelection: {
			BString* text = view->GetSelectedText();
			reply.AddString("result", text != NULL ? text->String() : "");
			delete text;
			break;
		}
		case kGoto: {
			double page;
			if (!NumberField(message, "page", &page) || page < 1 || page > doc->PageCount()) {
				ReplyError(message, B_BAD_VALUE, "give the page (page=N)");
				return;
			}
			GoToPage(fWindow, (int)page);
			break;
		}
		case kSave: {
			BMessage save(PDFWindow::SAVE_FILE_CMD);
			fWindow->MessageReceived(&save);
			break;
		}
		case kUndo: {
			BMessage undo(PDFWindow::UNDO_CMD);
			fWindow->MessageReceived(&undo);
			break;
		}
		case kRedo: {
			BMessage redo(PDFWindow::REDO_CMD);
			fWindow->MessageReceived(&redo);
			break;
		}
		case kUpgrade:
			reply.AddInt32("result", doc->UpgradeAnnotationIds());
			view->AnnotationsChanged();
			break;
		case kAddAnnotation: {
			BString id, error;
			if (!CreateAnnotation(fWindow, message, &id, &error)) {
				ReplyError(message, B_ERROR, error.String());
				return;
			}
			reply.AddString("result", id);
			break;
		}
		case kAddBookmark: {
			const char* label = "";
			if (message->FindString("label", &label) != B_OK || label == NULL)
				label = "";
			BMessage entered(BookmarkWindow::BOOKMARK_ENTERED_NOTIFY);
			entered.AddString("label", label[0] != '\0' ? label : "Bookmark");
			entered.AddInt32("pageNum", IntField(message, "page", view->Page()));
			fWindow->MessageReceived(&entered);
			break;
		}
		case kAnnotations: {
			Scope scope = ScopeOf(fWindow, message);
			if (message->what == B_CREATE_PROPERTY) {
				BString id, error;
				if (!CreateAnnotation(fWindow, message, &id, &error)) {
					ReplyError(message, B_ERROR, error.String());
					return;
				}
				reply.AddString("result", id);
				break;
			}
			std::vector<DocAnnotationEntry> entries, selected;
			if (!ListAnnotations(fWindow, scope, &entries)) {
				ReplyError(message, B_ERROR, "cannot list the annotations");
				return;
			}
			if (form == B_DIRECT_SPECIFIER)
				selected = entries;
			else if (form == B_NAME_SPECIFIER) {
				const char* id;
				Scope all = { false, 0 };
				std::vector<DocAnnotationEntry> every;
				if (specifier.FindString("name", &id) == B_OK && ListAnnotations(fWindow, all, &every)) {
					for (size_t i = 0; i < every.size(); i++) {
						if (every[i].annotation.id == id)
							selected.push_back(every[i]);
					}
				}
			} else {
				int32 at = Position(form, &specifier, (int32)entries.size());
				if (at >= 0)
					selected.push_back(entries[at]);
			}
			if (message->what == B_COUNT_PROPERTIES)
				reply.AddInt32("result", (int32)selected.size());
			else if (form != B_DIRECT_SPECIFIER && selected.empty()) {
				ReplyError(message, B_BAD_INDEX, "no such annotation");
				return;
			} else if (message->what == B_DELETE_PROPERTY && form == B_DIRECT_SPECIFIER) {
				ReplyError(message, B_BAD_SCRIPT_SYNTAX, "say which annotation to delete (an index or a name)");
				return;
			} else if (message->what == B_DELETE_PROPERTY) {
				// (from the last, so that the indices that follow stay the same)
				int page = 0;
				for (size_t i = selected.size(); i > 0; i--) {
					if (!doc->DeleteAnnotation(selected[i - 1].page, selected[i - 1].annotation.index)) {
						ReplyError(message, B_ERROR, "cannot delete the annotation");
						return;
					}
					page = selected[i - 1].page;
				}
				view->AnnotationsChanged(page);
			} else {
				for (size_t i = 0; i < selected.size(); i++) {
					BMessage described;
					DescribeAnnotation(selected[i], &described);
					reply.AddMessage("result", &described);
				}
			}
			break;
		}
		case kBookmarks: {
			Scope scope = ScopeOf(fWindow, message);
			if (message->what == B_CREATE_PROPERTY) {
				const char* label = "";
				message->FindString("label", &label);
				BMessage entered(BookmarkWindow::BOOKMARK_ENTERED_NOTIFY);
				entered.AddString("label", label[0] != '\0' ? label : "Bookmark");
				entered.AddInt32("pageNum", IntField(message, "page", view->Page()));
				fWindow->MessageReceived(&entered);
				break;
			}
			std::vector<BookmarkInfo> bookmarks, selected;
			ListBookmarks(fWindow, scope, &bookmarks);
			if (form == B_DIRECT_SPECIFIER)
				selected = bookmarks;
			else if (form == B_NAME_SPECIFIER) {
				const char* label;
				if (specifier.FindString("name", &label) == B_OK) {
					for (size_t i = 0; i < bookmarks.size(); i++) {
						if (bookmarks[i].label == label)
							selected.push_back(bookmarks[i]);
					}
				}
			} else {
				int32 at = Position(form, &specifier, (int32)bookmarks.size());
				if (at >= 0)
					selected.push_back(bookmarks[at]);
			}
			if (message->what == B_COUNT_PROPERTIES)
				reply.AddInt32("result", (int32)selected.size());
			else if (form != B_DIRECT_SPECIFIER && selected.empty()) {
				ReplyError(message, B_BAD_INDEX, "no such bookmark");
				return;
			} else if (message->what == B_DELETE_PROPERTY && form == B_DIRECT_SPECIFIER) {
				ReplyError(message, B_BAD_SCRIPT_SYNTAX, "say which bookmark to delete (an index or a name)");
				return;
			} else if (message->what == B_DELETE_PROPERTY) {
				for (size_t i = 0; i < selected.size(); i++)
					fWindow->BookmarkList()->RemoveUserBookmark(selected[i].page);
				fWindow->BookmarksChanged();
			} else {
				for (size_t i = 0; i < selected.size(); i++) {
					BMessage described;
					described.AddString("Label", selected[i].label);
					described.AddInt32("Page", selected[i].page);
					reply.AddMessage("result", &described);
				}
			}
			break;
		}
		default:
			BHandler::MessageReceived(message);
			return;
	}
	ReplyDone(message, reply);
}


// the property of the window that leads here
status_t
AddWindowSuite(BMessage* data)
{
	static property_info properties[] = {
		{ "Document", { 0 }, { B_DIRECT_SPECIFIER, 0 }, "The document that the window shows.", 0, { B_MESSAGE_TYPE } },
		{ 0 }
	};
	data->AddString("suites", kSuite);
	BPropertyInfo info(properties);
	return data->AddFlat("messages", &info);
}


bool
IsDocumentSpecifier(BMessage* message, int32 index, BMessage* specifier, int32 what, const char* property)
{
	static property_info properties[] = {
		{ "Document", { 0 }, { B_DIRECT_SPECIFIER, 0 }, "The document that the window shows.", 0, { B_MESSAGE_TYPE } },
		{ 0 }
	};
	return FindProperty(properties, message->what, what, property) >= 0;
}

}	// namespace Scripting

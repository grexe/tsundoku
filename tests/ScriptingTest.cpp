/*
 * Toji: a universal document reader for Haiku, extended for SEN.
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


// A test of the scripting suite (src/scripting/Scripting.h) that uses the scripting messages directly, as other programs do
// (hey can only do part of this: it does not nest the specifiers of a CREATE).
//
//   g++ -o ScriptingTest ScriptingTest.cpp -lbe
//   Toji gutenprint-users-manual.pdf &      (any PDF whose page 2 has the word "Krawitz"; the test adds and removes
//   ./ScriptingTest                              annotations and bookmarks, and does not save)
//
// Prints one line for each check and exits with the number of checks that failed.

#include <Application.h>
#include <Message.h>
#include <Messenger.h>
#include <Rect.h>
#include <String.h>

#include <stdio.h>
#include <string.h>

static BMessenger sTarget;
static int sFailed = 0;
static int sChecks = 0;


// The specifiers go from the property to the window: the innermost first.
static void
Chain(BMessage* message)
{
	message->AddSpecifier("Document");
	message->AddSpecifier("Window", 0);
}


// a property of the document: GET Page of Document of Window 0
static BMessage
DocumentProperty(uint32 what, const char* property)
{
	BMessage message(what);
	message.AddSpecifier(property);
	Chain(&message);
	return message;
}


// all the items of a kind (the list): COUNT, GET, CREATE of Annotation of Document of Window 0
static BMessage
Items(uint32 what, const char* kind)
{
	BMessage message(what);
	message.AddSpecifier(kind);
	Chain(&message);
	return message;
}


// an item by index, and a property of it (or the item itself if property is NULL)
static BMessage
ItemAt(uint32 what, const char* kind, int32 index, const char* property)
{
	BMessage message(what);
	if (property != NULL)
		message.AddSpecifier(property);
	message.AddSpecifier(kind, index);
	Chain(&message);
	return message;
}


static BMessage
ItemNamed(uint32 what, const char* kind, const char* name, const char* property)
{
	BMessage message(what);
	if (property != NULL)
		message.AddSpecifier(property);
	message.AddSpecifier(kind, name);
	Chain(&message);
	return message;
}


static bool
Send(BMessage& message, BMessage* reply)
{
	status_t status = sTarget.SendMessage(&message, reply, 10000000, 10000000);
	int32 error = B_ERROR;
	if (status != B_OK)
		return false;
	reply->FindInt32("error", &error);
	return reply->what == B_REPLY && error == B_OK;
}


static void
Check(const char* what, bool ok)
{
	sChecks++;
	if (!ok)
		sFailed++;
	printf("%s: %s\n", ok ? "ok  " : "FAIL", what);
}


static int32
Number(BMessage message)
{
	BMessage reply;
	int32 value = -1;
	if (Send(message, &reply))
		reply.FindInt32("result", &value);
	return value;
}


static int32
CountOnPage(const char* kind, int32 page)
{
	BMessage count = Items(B_COUNT_PROPERTIES, kind);
	count.AddString("context", "page");
	count.AddInt32("page", page);
	return Number(count);
}


static BString
Text(BMessage message)
{
	BMessage reply;
	BString value;
	if (Send(message, &reply))
		reply.FindString("result", &value);
	return value;
}


int
main()
{
	BApplication app("application/x-vnd.sen-labs.ScriptingTest");
	sTarget = BMessenger("application/x-vnd.sen-labs.Toji");
	if (!sTarget.IsValid()) {
		printf("Toji is not running\n");
		return 1;
	}

	BMessage reply;

	// the document
	Check("Type is pdf", Text(DocumentProperty(B_GET_PROPERTY, "Type")) == "pdf");
	int32 pages = Number(DocumentProperty(B_GET_PROPERTY, "PageCount"));
	Check("PageCount", pages > 2);

	// going to a page: SET Page, and EXECUTE Goto
	{
		BMessage set = DocumentProperty(B_SET_PROPERTY, "Page");
		set.AddInt32("data", 2);
		Check("SET Page", Send(set, &reply));
		Check("GET Page is 2", Number(DocumentProperty(B_GET_PROPERTY, "Page")) == 2);
		BMessage go = DocumentProperty(B_EXECUTE_PROPERTY, "Goto");
		go.AddInt32("page", 3);
		Check("EXECUTE Goto", Send(go, &reply));
		Check("GET Page is 3", Number(DocumentProperty(B_GET_PROPERTY, "Page")) == 3);
	}

	// annotations: the counts before
	int32 before = Number(Items(B_COUNT_PROPERTIES, "Annotation"));
	int32 beforeOnPage2 = CountOnPage("Annotation", 2);
	Check("COUNT Annotation", before >= 0);
	{
		BMessage count = Items(B_COUNT_PROPERTIES, "Annotation");
		count.AddString("context", "page");
		count.AddInt32("page", 2);
		Check("COUNT on page 2 (context=page)", Send(count, &reply));
	}

	// CREATE with nested specifiers (Annotation of Document of Window 0), what hey cannot send
	BString id;
	{
		BMessage create = Items(B_CREATE_PROPERTY, "Annotation");
		create.AddString("kind", "highlight");
		create.AddString("quote", "Krawitz");
		create.AddInt32("page", 2);
		bool ok = Send(create, &reply);
		reply.FindString("result", &id);
		Check("CREATE Annotation (highlight on Krawitz)", ok && id.Length() > 0);
	}
	{
		BMessage create = Items(B_CREATE_PROPERTY, "Annotation");
		create.AddString("kind", "rectangle");
		create.AddInt32("page", 2);
		create.AddFloat("left", 100);
		create.AddFloat("top", 400);
		create.AddFloat("right", 200);
		create.AddFloat("bottom", 450);
		Check("CREATE Annotation (rectangle)", Send(create, &reply));
	}
	{
		BMessage create = Items(B_CREATE_PROPERTY, "Annotation");
		create.AddString("kind", "note");
		create.AddInt32("page", 3);
		create.AddFloat("x", 100);
		create.AddFloat("y", 100);
		create.AddString("text", "a note");
		Check("CREATE Annotation (note on page 3)", Send(create, &reply));
	}
	{
		BMessage create = Items(B_CREATE_PROPERTY, "Annotation");
		create.AddString("kind", "highlight");
		create.AddString("quote", "no such words anywhere");
		create.AddInt32("page", 2);
		Check("CREATE with words that are not there fails", !Send(create, &reply));
	}

	// filtering: the whole document, a page, the page that is shown (3, the default of context=page)
	int32 all = Number(Items(B_COUNT_PROPERTIES, "Annotation"));
	Check("COUNT grew by 3", all == before + 3);
	{
		BMessage count = Items(B_COUNT_PROPERTIES, "Annotation");
		count.AddString("context", "page");
		count.AddInt32("page", 2);
		Check("COUNT on page 2 grew by 2", Number(count) == beforeOnPage2 + 2);
		BMessage shown = Items(B_COUNT_PROPERTIES, "Annotation");
		shown.AddString("context", "page");
		Check("COUNT with context=page uses the page that is shown (3)", Number(shown) >= 1);
		BMessage document = Items(B_COUNT_PROPERTIES, "Annotation");
		document.AddString("context", "document");
		Check("COUNT with context=document", Number(document) == all);
	}

	// by index (within the page) and by name (in the whole document)
	{
		BMessage kind = ItemAt(B_GET_PROPERTY, "Annotation", 0, "Kind");
		kind.AddString("context", "page");
		kind.AddInt32("page", 2);
		Check("GET Kind of Annotation 0 on page 2", Text(kind) == "highlight");
		BMessage quote = ItemNamed(B_GET_PROPERTY, "Annotation", id.String(), "Quote");
		Check("GET Quote of Annotation by name", Text(quote) == "Krawitz");
		BMessage reverse(B_GET_PROPERTY);
		reverse.AddSpecifier("Id");
		reverse.AddSpecifier("Annotation", -1);
		reverse.AddSpecifier("Document");
		reverse.AddSpecifier("Window", 0);
		Check("GET Id of the last Annotation (reverse index)", Send(reverse, &reply));
		BMessage bad = ItemAt(B_GET_PROPERTY, "Annotation", 999, "Id");
		Check("a bad index is an error", !Send(bad, &reply));
	}

	// SET: the note, the color, the bounds, and the words (the mark moves and keeps its identifier)
	{
		BMessage text = ItemNamed(B_SET_PROPERTY, "Annotation", id.String(), "Text");
		text.AddString("data", "scripted");
		Check("SET Text", Send(text, &reply));
		Check("GET Text", Text(ItemNamed(B_GET_PROPERTY, "Annotation", id.String(), "Text")) == "scripted");
		BMessage color = ItemNamed(B_SET_PROPERTY, "Annotation", id.String(), "Color");
		color.AddInt32("data", 0x2196f3);
		Check("SET Color", Send(color, &reply));
		Check("GET Color", Number(ItemNamed(B_GET_PROPERTY, "Annotation", id.String(), "Color")) == 0x2196f3);
		BMessage quote = ItemNamed(B_SET_PROPERTY, "Annotation", id.String(), "Quote");
		quote.AddString("data", "License");
		Check("SET Quote (re-anchors)", Send(quote, &reply));
		Check("the mark keeps its identifier", Text(ItemNamed(B_GET_PROPERTY, "Annotation", id.String(), "Id")) == id);
		Check("the mark has the new words", Text(ItemNamed(B_GET_PROPERTY, "Annotation", id.String(), "Quote")) == "License");
		Check("the mark keeps its note", Text(ItemNamed(B_GET_PROPERTY, "Annotation", id.String(), "Text")) == "scripted");
		Check("GET JSON", Text(ItemNamed(B_GET_PROPERTY, "Annotation", id.String(), "JSON")).FindFirst("oa:Annotation") >= 0);
		BMessage go = ItemNamed(B_EXECUTE_PROPERTY, "Annotation", id.String(), "Goto");
		Check("EXECUTE Goto of Annotation", Send(go, &reply));
		Check("the page is the annotation's", Number(DocumentProperty(B_GET_PROPERTY, "Page")) == 2);
	}

	// DELETE and undo
	{
		BMessage del = ItemNamed(B_DELETE_PROPERTY, "Annotation", id.String(), NULL);
		Check("DELETE Annotation by name", Send(del, &reply));
		Check("COUNT after DELETE", Number(Items(B_COUNT_PROPERTIES, "Annotation")) == all - 1);
		BMessage undo = DocumentProperty(B_EXECUTE_PROPERTY, "Undo");
		Check("EXECUTE Undo", Send(undo, &reply));
		Check("COUNT after Undo", Number(Items(B_COUNT_PROPERTIES, "Annotation")) == all);
		BMessage everything = Items(B_DELETE_PROPERTY, "Annotation");
		Check("DELETE of all without an index is refused", !Send(everything, &reply));
	}

	// bookmarks: nested CREATE, filtering, Goto, DELETE by name
	{
		int32 marks = Number(Items(B_COUNT_PROPERTIES, "Bookmark"));
		int32 on4 = CountOnPage("Bookmark", 4), on5 = CountOnPage("Bookmark", 5);
		BMessage create = Items(B_CREATE_PROPERTY, "Bookmark");
		create.AddString("label", "ScriptTest");
		create.AddInt32("page", 4);
		Check("CREATE Bookmark", Send(create, &reply));
		Check("COUNT Bookmark grew", Number(Items(B_COUNT_PROPERTIES, "Bookmark")) == marks + 1);
		Check("COUNT Bookmark on page 4 grew", CountOnPage("Bookmark", 4) == on4 + 1);
		Check("COUNT Bookmark on page 5 did not", CountOnPage("Bookmark", 5) == on5);
		Check("GET Page of Bookmark by label", Number(ItemNamed(B_GET_PROPERTY, "Bookmark", "ScriptTest", "Page")) == 4);
		BMessage go = ItemNamed(B_EXECUTE_PROPERTY, "Bookmark", "ScriptTest", "Goto");
		Check("EXECUTE Goto of Bookmark", Send(go, &reply));
		Check("the page is the bookmark's", Number(DocumentProperty(B_GET_PROPERTY, "Page")) == 4);
		BMessage del = ItemNamed(B_DELETE_PROPERTY, "Bookmark", "ScriptTest", NULL);
		Check("DELETE Bookmark by label", Send(del, &reply));
		Check("COUNT Bookmark back", Number(Items(B_COUNT_PROPERTIES, "Bookmark")) == marks);
	}

	// pages
	{
		BMessage text(B_GET_PROPERTY);
		text.AddSpecifier("Text");
		text.AddSpecifier("Page", 2);
		Chain(&text);
		Check("GET Text of Page 2", Text(text).FindFirst("Krawitz") >= 0);
		BMessage size(B_GET_PROPERTY);
		size.AddSpecifier("Size");
		size.AddSpecifier("Page", 2);
		Chain(&size);
		BRect rect;
		Send(size, &reply);
		Check("GET Size of Page 2", reply.FindRect("result", &rect) == B_OK && rect.Width() > 100);
		BMessage go(B_EXECUTE_PROPERTY);
		go.AddSpecifier("Goto");
		go.AddSpecifier("Page", 6);
		Chain(&go);
		Check("EXECUTE Goto of Page 6", Send(go, &reply));
		Check("the page is 6", Number(DocumentProperty(B_GET_PROPERTY, "Page")) == 6);
	}

	printf("%d checks, %d failed\n", sChecks, sFailed);
	return sFailed;
}

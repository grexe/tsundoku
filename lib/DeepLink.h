/*
 * SPDX-License-Identifier: MIT
 * Copyright (C) 2026 Gregor B. Rosenauer & Claude
 */

#ifndef _DEEP_LINK_H_
#define _DEEP_LINK_H_

#include <Message.h>
#include <String.h>

// Links to a place in a document, as URIs that people and programs can hand around (text, mail, a browser):
//
//   toji:///boot/home/books/a%20book.epub#epubcfi(/6/4[chap01]!/4/10)
//   toji:///boot/home/papers/x.pdf#page=5
//   toji:///boot/home/papers/x.pdf#page=5&xywh=percent:10,20,30,40
//   toji:///boot/home/papers/x.pdf#page=5:~:text=the%20words
//   toji:///boot/home/papers/x.pdf#annotation=02SEHT4FEMN73
//
// The part after # is made of the fragment identifiers that exist for this: RFC 3778 (page=), W3C Media Fragments (xywh=),
// the EPUB CFI (epubcfi(...)) and the Text Fragments of the web platform (:~:text=prefix-,words,-suffix); only the annotation
// is a term of ours (the identifier of an annotation, the TSID of urn:sen:...). They are the same places as the selectors of
// the W3C Web Annotation model (oa:FragmentSelector, oa:TextQuoteSelector). A link can also be a file: URI or a plain path with
// such a fragment.
namespace DeepLink {

extern const char* const kScheme;		// toji

struct Place {
	int		page;				// 1-based, 0 if not given
	bool	hasRegion;			// a region of the page, in percent of it: left, top, width, height
	float	region[4];
	BString	quote;				// words (a text fragment), with some of the text before and after them
	BString	prefix, suffix;
	BString	cfi;				// epubcfi(...) of an EPUB
	BString	annotation;			// the identifier (TSID) of an annotation

	Place() : page(0), hasRegion(false)
	{
		region[0] = region[1] = region[2] = region[3] = 0;
	}
};

// The URI of a place in the file at path (an absolute path).
BString Make(const char* path, const Place& place);

// The file and the place of a toji: or file: URI, or of a path with a fragment; false if it is no such thing.
bool Parse(const char* uri, BString* path, Place* place);

// Whether the text is a link that Parse() takes (a toji: URI, a file: URI)
bool IsLink(const char* text);

// The target of the place in the Web Annotation model: a message with oa:hasSelector entries (a page selector, refined by the
// region; the words; the CFI). The annotation is not part of it, it is named by its identifier.
void ToTarget(const Place& place, BMessage* target);

}	// namespace DeepLink

#endif

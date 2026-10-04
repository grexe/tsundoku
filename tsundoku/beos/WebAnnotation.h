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


#ifndef _WEB_ANNOTATION_H_
#define _WEB_ANNOTATION_H_

#include <Message.h>
#include <String.h>

// The W3C Web Annotation Data Model (https://www.w3.org/TR/annotation-model/) is how Tsundoku and SEN describe
// annotations and places in documents: for PDF files, EPUB books and what comes later. An annotation has a target (the
// document, and selectors that say where in it) and perhaps a body (the note); the motivation says what it is for.
//
// The model is kept in BMessages whose field names are the compact IRIs of the properties (oa:hasTarget, ...), so that a
// message can be written as JSON-LD as it is; the file info and the scripting of other programs can use it too.
namespace WebAnnotation {

// the prefixes of the vocabularies that are used
extern const char* const kNamespaceOa;			// http://www.w3.org/ns/oa#
extern const char* const kNamespaceSen;			// the terms that SEN defines

// types
extern const char* const kAnnotation;			// oa:Annotation
extern const char* const kTextualBody;			// oa:TextualBody
extern const char* const kTextQuoteSelector;		// oa:TextQuoteSelector
extern const char* const kFragmentSelector;		// oa:FragmentSelector
extern const char* const kSvgSelector;			// oa:SvgSelector
extern const char* const kCssStyle;				// oa:CssStyle

// motivations: those of the vocabulary, and those that SEN adds for what a text can be marked with besides a highlight
// (sen:underline, sen:strikethrough and sen:squiggle are oa:Motivations, skos:broader oa:highlighting)
extern const char* const kHighlighting;			// oa:highlighting
extern const char* const kCommenting;			// oa:commenting
extern const char* const kBookmarking;			// oa:bookmarking
extern const char* const kUnderline;			// sen:underline
extern const char* const kStrikethrough;		// sen:strikethrough
extern const char* const kSquiggle;				// sen:squiggle

// what a fragment selector conforms to
extern const char* const kConformsToEpubCfi;	// the EPUB Canonical Fragment Identifier specification
extern const char* const kConformsToPdf;		// RFC 3778: page=5
extern const char* const kConformsToMediaFragments;	// W3C Media Fragments: xywh=pixel:10,20,30,40

// A mark on a text: highlight, underline, strike out, squiggly line, with a note perhaps. The place is told by the words
// (a text quote selector, with some of the text before and after it) and, for an EPUB, by its CFI; where it was
// (chapter, place in it) are hints for finding it quickly, which are not part of the model (sen:chapter, ...).
struct Mark {
	BString id;				// a UUID; the identifier of the annotation is urn:uuid:<id>
	BString motivation;
	uint32  color;			// 0xRRGGBB
	bool    hasColor;
	BString body;			// the note, may be empty
	BString creator;
	int64   created;		// 0 if unknown
	BString quote, prefix, suffix;
	BString cfi;
	int32   chapter;
	float   fraction;
	float   ypos;

	Mark() : color(0), hasColor(false), created(0), chapter(0), fraction(0), ypos(0) {}
};

// The annotation as a message in the model, and back; false if it is not an annotation of a text.
void ArchiveMark(const Mark& mark, BMessage* annotation);
bool UnarchiveMark(const BMessage& annotation, Mark* mark);

// ... the parts of a message that other code builds
void AddTextualBody(BMessage* annotation, const char* text);
void AddCssStyle(BMessage* annotation, const char* css);
void AddCreator(BMessage* annotation, const char* name, int64 created);
// a selector message of the type, with its properties (rdf:value and so on are added by the caller)
void MakeFragmentSelector(BMessage* selector, const char* conformsTo, const char* value);

// the CSS for the color of a mark of a motivation, and the color in some CSS (the first #rrggbb)
BString ColorStyle(const char* motivation, uint32 color);
bool    ColorOfStyle(const char* css, uint32* color);

// The identifier as an IRI, and the UUID in an identifier that is one (urn:uuid:..., or the UUID itself)
BString IdentifierIri(const char* id);
BString IdentifierUuid(const char* iri);

// the IRI of a file: file:///boot/home/a%20book.epub
BString FileIri(const char* path);

// The identifier that SEN has given the file (its attribute SEN:ID, a TSID), empty if it has none: every object in the
// personal knowledge graph has one, and it is what the annotations of the file are about.
BString SenId(const char* path);
// the source of the target of an annotation (oa:hasSource); replaces the one that is there
void SetSource(BMessage* annotation, const char* source);

// time as in JSON-LD (2026-10-04T10:00:00Z) and back
BString TimeToIso(int64 time);
int64   IsoToTime(const char* iso);

// The message as JSON(-LD), indented; the prefixes of the compact IRIs are given in @context when it is an annotation.
BString ToJson(const BMessage& message, bool withContext = true);

}	// namespace WebAnnotation

#endif

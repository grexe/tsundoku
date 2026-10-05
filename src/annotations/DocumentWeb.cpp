/*
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
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


// Annotations and places as W3C Web Annotations (see WebAnnotation.h): any annotation of a document as a message in the
// model, and the places that a message in the model points to (deep links).

#include "Bookmarks.h"
#include "Document.h"

#include <Node.h>

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "EpubCfi.h"
#include "EpubInfo.h"
#include "WebAnnotation.h"

extern "C" {
#include <mupdf/pdf.h>
}

using namespace WebAnnotation;


static BString
Number(float value)
{
	char buffer[32];
	snprintf(buffer, sizeof(buffer), "%g", value);
	return BString(buffer);
}


static BString
RgbText(uint32 color)
{
	char buffer[16];
	snprintf(buffer, sizeof(buffer), "#%06x", (unsigned)(color & 0xffffff));
	return BString(buffer);
}


// The words that a mark covers, from the text of the page.
static BString
MarkedWords(Document* document, fz_stext_page* text, const DocAnnotation& annotation)
{
	if (annotation.quads.empty() || text == NULL)
		return BString();

	fz_context* context = document->Context();
	char* copied = NULL;
	const fz_quad& first = annotation.quads.front();
	const fz_quad& last = annotation.quads.back();
	fz_point a = fz_make_point(first.ul.x + 0.5f, (first.ul.y + first.ll.y) / 2);
	fz_point b = fz_make_point(last.ur.x - 0.5f, (last.ur.y + last.lr.y) / 2);
	fz_var(copied);
	fz_try(context) {
		copied = fz_copy_selection(context, text, a, b, 0);
	}
	fz_catch(context) {
		copied = NULL;
	}
	BString words(copied != NULL ? copied : "");
	fz_free(context, copied);
	words.ReplaceAll("\r", " ");
	words.ReplaceAll("\n", " ");
	while (words.FindFirst("  ") >= 0)
		words.ReplaceAll("  ", " ");
	words.Trim();
	return words;
}


// the SVG of a shape on a page, in the coordinates of the page
static BString
ShapeSvg(const DocAnnotation& annotation, const fz_rect& bounds)
{
	BString svg;
	svg << "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"" << Number(bounds.x0) << " " << Number(bounds.y0)
		<< " " << Number(bounds.x1 - bounds.x0) << " " << Number(bounds.y1 - bounds.y0) << "\">";
	const fz_rect& r = annotation.rect;
	switch (annotation.kind) {
		case kAnnotRectangle:
			svg << "<rect x=\"" << Number(r.x0) << "\" y=\"" << Number(r.y0) << "\" width=\"" << Number(r.x1 - r.x0)
				<< "\" height=\"" << Number(r.y1 - r.y0) << "\"/>";
			break;
		case kAnnotEllipse:
			svg << "<ellipse cx=\"" << Number((r.x0 + r.x1) / 2) << "\" cy=\"" << Number((r.y0 + r.y1) / 2)
				<< "\" rx=\"" << Number((r.x1 - r.x0) / 2) << "\" ry=\"" << Number((r.y1 - r.y0) / 2) << "\"/>";
			break;
		case kAnnotLine:
		case kAnnotInk:
			for (size_t i = 0; i < annotation.paths.size(); i++) {
				const std::vector<fz_point>& path = annotation.paths[i];
				if (path.empty())
					continue;
				svg << "<path d=\"";
				for (size_t k = 0; k < path.size(); k++)
					svg << (k == 0 ? "M " : " L ") << Number(path[k].x) << " " << Number(path[k].y);
				svg << "\"/>";
			}
			break;
		default:
			break;
	}
	svg << "</svg>";
	return svg;
}


// A PDF annotation of a page as a Web Annotation. The text of the page is made when a mark needs it. The source of the
// target is what the annotation is about: the identifier that SEN gave the file, or its IRI, or none if it is stored with
// the file and SEN does not know it.
static void
BuildPdfAnnotation(Document* document, int pageNo, fz_page* page, fz_stext_page** text, const DocAnnotation& a,
	const fz_rect& bounds, const char* source, BMessage* annotation)
{
	BString motivation = kHighlighting;
	switch (a.type) {
		case PDF_ANNOT_UNDERLINE:	motivation = kUnderline; break;
		case PDF_ANNOT_STRIKE_OUT:	motivation = kStrikethrough; break;
		case PDF_ANNOT_SQUIGGLY:	motivation = kSquiggle; break;
		case PDF_ANNOT_TEXT:
		case PDF_ANNOT_FREE_TEXT:	motivation = kCommenting; break;
		default: break;
	}

	if (!a.id.IsEmpty())
		annotation->AddString("id", IdentifierIri(a.id.String()));
	annotation->AddString("type", kAnnotation);
	annotation->AddString("oa:motivatedBy", motivation);
	// (to give an annotation that has no name of its own the same identifier when the file is opened again)
	annotation->AddString("sen:key", ForeignAnnotationKey(pageNo, a));
	AddCreator(annotation, a.author.String(), 0);
	if (!a.contents.IsEmpty())
		AddTextualBody(annotation, a.contents.String());
	if (a.hasColor) {
		BString css;
		if (a.kind == kAnnotMarkup)
			css = ColorStyle(motivation.String(), a.color);
		else if (a.kind == kAnnotNote || a.kind == kAnnotText)
			css << "color: " << RgbText(a.color) << ";";
		else
			css << "stroke: " << RgbText(a.color) << "; fill: none;";
		AddCssStyle(annotation, css.String());
	}

	// the target: the page of the file, and then where on it
	BMessage target;
	if (source != NULL && source[0] != '\0')
		target.AddString("oa:hasSource", source);
	BMessage pageSelector;
	char pageValue[24];
	snprintf(pageValue, sizeof(pageValue), "page=%d", pageNo);
	MakeFragmentSelector(&pageSelector, kConformsToPdf, pageValue);

	BMessage refinement;
	bool refined = false;
	if (a.kind == kAnnotMarkup) {
		fz_context* context = document->Context();
		if (*text == NULL && page != NULL) {
			fz_try(context) {
				*text = fz_new_stext_page_from_page(context, page, NULL);
			}
			fz_catch(context) {
				*text = NULL;
			}
		}
		BString words = MarkedWords(document, *text, a);
		if (!words.IsEmpty()) {
			refinement.AddString("type", kTextQuoteSelector);
			refinement.AddString("oa:exact", words);
			refined = true;
		}
	} else if (a.kind == kAnnotRectangle || a.kind == kAnnotEllipse || a.kind == kAnnotLine
		|| a.kind == kAnnotInk) {
		refinement.AddString("type", kSvgSelector);
		refinement.AddString("rdf:value", ShapeSvg(a, bounds));
		refined = true;
	} else if (a.kind == kAnnotNote || a.kind == kAnnotText) {
		BString region;
		region << "xywh=pixel:" << Number(a.rect.x0) << "," << Number(a.rect.y0) << "," << Number(a.rect.x1 - a.rect.x0)
			<< "," << Number(a.rect.y1 - a.rect.y0);
		MakeFragmentSelector(&refinement, kConformsToMediaFragments, region.String());
		refined = true;
	}
	if (refined)
		pageSelector.AddMessage("oa:refinedBy", &refinement);
	target.AddMessage("oa:hasSelector", &pageSelector);
	annotation->AddMessage("oa:hasTarget", &target);
}


bool
Document::WebAnnotationOf(int pageNo, int index, BMessage* annotation)
{
	if (UsesStore())
		return StoreWebAnnotation(pageNo, index, annotation);
	if (!fIsPDF || pageNo < 1 || pageNo > fPageCount)
		return false;

	DocumentLocker locker(this);
	std::vector<DocAnnotation> list;
	fz_page* page = NULL;
	int loaded = 0;
	fz_var(page);
	fz_try(fContext) {
		page = fz_load_page(fContext, fDocument, pageNo - 1);
		loaded = 1;
	}
	fz_catch(fContext) {
		loaded = 0;
	}
	if (!loaded)
		return false;
	LoadAnnotations(pageNo, page, list);

	bool ok = index >= 0 && index < (int)list.size();
	fz_stext_page* text = NULL;
	if (ok) {
		fz_rect bounds = fz_empty_rect;
		PageBounds(pageNo, &bounds);
		// the identifier that SEN gave the file, or else its IRI
		BString senId = SenId(fPath.String());
		BString source = senId.IsEmpty() ? FileIri(fPath.String()) : senId;
		BuildPdfAnnotation(this, pageNo, page, &text, list[index], bounds, source.String(), annotation);
	}
	fz_drop_stext_page(fContext, text);
	fz_drop_page(fContext, page);
	return ok;
}


// All annotations of a PDF file as Web Annotations in the attribute SEN:annotations of the file (it is taken away if
// there are none), so that they can be found and used without opening the PDF file; the annotations themselves stay in
// the file. Done when the file is saved or a copy is made.
bool
Document::WriteWebAnnotations(const char* path)
{
	if (!fIsPDF)
		return true;

	BNode node(path);
	if (node.InitCheck() != B_OK)
		return false;

	DocumentLocker locker(this);
	BString source = SenId(path);	// (if SEN knows the file)
	BMessage archive;
	archive.AddInt32("version", 2);
	int count = 0;
	for (int pageNo = 1; pageNo <= fPageCount; pageNo++) {
		if (!PageMayHaveAnnotations(pageNo))
			continue;
		fz_page* page = NULL;
		int loaded = 0;
		fz_var(page);
		fz_try(fContext) {
			page = fz_load_page(fContext, fDocument, pageNo - 1);
			loaded = 1;
		}
		fz_catch(fContext) {
			loaded = 0;
		}
		if (!loaded)
			continue;

		std::vector<DocAnnotation> list;
		LoadAnnotations(pageNo, page, list);
		fz_rect bounds = fz_empty_rect;
		PageBounds(pageNo, &bounds);
		fz_stext_page* text = NULL;
		for (size_t i = 0; i < list.size(); i++) {
			BMessage annotation;
			BuildPdfAnnotation(this, pageNo, page, &text, list[i], bounds, source.String(), &annotation);
			archive.AddMessage(kAnnotation, &annotation);
			count++;
		}
		fz_drop_stext_page(fContext, text);
		fz_drop_page(fContext, page);
	}

	// the bookmarks of the reader are in the attribute as well
	bool bookmarks = Bookmarks::Carry(path, &archive);
	if (count == 0 && !bookmarks) {
		node.RemoveAttr("SEN:annotations");
		return true;
	}
	ssize_t size = archive.FlattenedSize();
	char* buffer = new char[size];
	bool ok = archive.Flatten(buffer, size) == B_OK
		&& node.WriteAttr("SEN:annotations", B_MESSAGE_TYPE, 0, buffer, size) == size;
	delete[] buffer;
	return ok;
}


// ---- deep links

// The selectors of a target (and what refines them), the information in them taken together.
static void
ReadSelector(const BMessage& selector, DocTarget* result)
{
	BString type, value;
	selector.FindString("type", &type);
	selector.FindString("rdf:value", &value);

	if (type == kTextQuoteSelector) {
		selector.FindString("oa:exact", &result->quote);
		selector.FindString("oa:prefix", &result->prefix);
		selector.FindString("oa:suffix", &result->suffix);
	} else if (type == kFragmentSelector) {
		if (value.IFindFirst("epubcfi(") == 0)
			result->cfi = value;
		else {
			// page=5&..., xywh=pixel:10,20,30,40 or xywh=percent:...
			int32 start = 0;
			while (start <= value.Length()) {
				int32 end = value.FindFirst('&', start);
				if (end < 0)
					end = value.Length();
				BString part;
				value.CopyInto(part, start, end - start);
				int page = 0;
				float x, y, w, h;
				if (sscanf(part.String(), "page=%d", &page) == 1 && page > 0)
					result->page = page;
				else if (sscanf(part.String(), "xywh=pixel:%f,%f,%f,%f", &x, &y, &w, &h) == 4) {
					result->hasRegion = true;
					result->regionPercent = false;
					result->region = fz_make_rect(x, y, x + w, y + h);
				} else if (sscanf(part.String(), "xywh=percent:%f,%f,%f,%f", &x, &y, &w, &h) == 4) {
					result->hasRegion = true;
					result->regionPercent = true;
					result->region = fz_make_rect(x, y, x + w, y + h);
				}
				start = end + 1;
			}
		}
	} else if (type == kSvgSelector) {
		// the place that the shapes of an SVG cover; its viewBox says what its numbers are relative to: the whole page
		float box[4], viewBox[4];
		bool hasViewBox;
		if (SvgBoundingBox(value.String(), box, viewBox, &hasViewBox)) {
			result->hasRegion = true;
			if (hasViewBox && viewBox[2] > 0 && viewBox[3] > 0) {
				result->regionPercent = true;
				result->region = fz_make_rect((box[0] - viewBox[0]) / viewBox[2] * 100,
					(box[1] - viewBox[1]) / viewBox[3] * 100, (box[2] - viewBox[0]) / viewBox[2] * 100,
					(box[3] - viewBox[1]) / viewBox[3] * 100);
			} else {
				result->regionPercent = false;
				result->region = fz_make_rect(box[0], box[1], box[2], box[3]);
			}
		}
	}

	BMessage refinement;
	if (selector.FindMessage("oa:refinedBy", &refinement) == B_OK)
		ReadSelector(refinement, result);
}


bool
Document::ResolveTarget(const BMessage& target, DocTarget* result)
{
	BMessage selector;
	for (int32 i = 0; target.FindMessage("oa:hasSelector", i, &selector) == B_OK; i++)
		ReadSelector(selector, result);

	// an EPUB CFI leads to the words, and these to the page of the layout as it is
	if (result->page == 0 && !result->cfi.IsEmpty() && fEpub != NULL) {
		int spine = 0;
		BString words;
		float fraction = 0;
		if (EpubCfi::Resolve(fPath.String(), *fEpub, result->cfi.String(), &spine, &words, &fraction)) {
			TextAnchor anchor;
			anchor.chapter = spine;
			anchor.fraction = fraction;
			anchor.quote = words;
			anchor.cfi = result->cfi;
			result->page = PageOfAnchor(anchor);
			if (result->quote.IsEmpty())
				result->quote = words;
		}
	}
	if (result->hasRegion && result->regionPercent && result->page > 0) {
		fz_rect bounds;
		if (PageBounds(result->page, &bounds)) {
			float w = bounds.x1 - bounds.x0, h = bounds.y1 - bounds.y0;
			result->region = fz_make_rect(bounds.x0 + result->region.x0 * w / 100, bounds.y0 + result->region.y0 * h / 100,
				bounds.x0 + result->region.x1 * w / 100, bounds.y0 + result->region.y1 * h / 100);
		}
		result->regionPercent = false;
	}
	return result->page > 0 || !result->quote.IsEmpty() || result->hasRegion;
}

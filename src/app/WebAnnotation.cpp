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


#include "WebAnnotation.h"

#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include <Node.h>
#include <TypeConstants.h>

namespace WebAnnotation {

const char* const kNamespaceOa = "http://www.w3.org/ns/oa#";
const char* const kNamespaceSen = "https://sen-labs.org/ns/sen#";

const char* const kAnnotation = "oa:Annotation";
const char* const kTextualBody = "oa:TextualBody";
const char* const kTextQuoteSelector = "oa:TextQuoteSelector";
const char* const kFragmentSelector = "oa:FragmentSelector";
const char* const kSvgSelector = "oa:SvgSelector";
const char* const kCssStyle = "oa:CssStyle";

const char* const kHighlighting = "oa:highlighting";
const char* const kCommenting = "oa:commenting";
const char* const kBookmarking = "oa:bookmarking";
const char* const kUnderline = "sen:underline";
const char* const kStrikethrough = "sen:strikethrough";
const char* const kSquiggle = "sen:squiggle";

const char* const kConformsToEpubCfi = "http://www.idpf.org/epub/linking/cfi/epub-cfi.html";
const char* const kConformsToPdf = "http://tools.ietf.org/rfc/rfc3778";
const char* const kConformsToMediaFragments = "http://www.w3.org/TR/media-frags/";


// ---- time

BString
TimeToIso(int64 time)
{
	if (time <= 0)
		return BString();
	time_t t = (time_t)time;
	struct tm parts;
	gmtime_r(&t, &parts);
	char buffer[40];
	strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &parts);
	return BString(buffer);
}


// days since 1970-01-01 of a date of the proleptic Gregorian calendar
static int64
DaysFromCivil(int64 y, int m, int d)
{
	y -= m <= 2;
	int64 era = (y >= 0 ? y : y - 399) / 400;
	int64 yoe = y - era * 400;
	int64 doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
	int64 doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
	return era * 146097 + doe - 719468;
}


int64
IsoToTime(const char* iso)
{
	int y = 0, mo = 1, d = 1, h = 0, mi = 0, s = 0;
	if (iso == NULL || sscanf(iso, "%d-%d-%dT%d:%d:%d", &y, &mo, &d, &h, &mi, &s) < 3)
		return 0;
	return DaysFromCivil(y, mo, d) * 86400 + h * 3600 + mi * 60 + s;
}


// ---- identifiers and colors

BString
SenId(const char* path)
{
	BNode node(path);
	char buffer[128];
	ssize_t size = node.InitCheck() == B_OK ? node.ReadAttr("SEN:ID", B_STRING_TYPE, 0, buffer, sizeof(buffer) - 1) : -1;
	if (size <= 0)
		return BString();
	buffer[size] = '\0';
	return BString(buffer);
}


void
SetSource(BMessage* annotation, const char* source)
{
	BMessage target;
	if (annotation->FindMessage("oa:hasTarget", &target) != B_OK)
		return;
	if (target.HasString("oa:hasSource"))
		target.ReplaceString("oa:hasSource", source);
	else
		target.AddString("oa:hasSource", source);
	annotation->ReplaceMessage("oa:hasTarget", &target);
}

BString
FileIri(const char* path)
{
	BString iri("file://");
	for (const char* c = path; *c != '\0'; c++) {
		unsigned char u = (unsigned char)*c;
		if ((u >= 'a' && u <= 'z') || (u >= 'A' && u <= 'Z') || (u >= '0' && u <= '9') || strchr("/-._~", u) != NULL)
			iri << (char)u;
		else {
			char hex[4];
			snprintf(hex, sizeof(hex), "%%%02X", u);
			iri << hex;
		}
	}
	return iri;
}




BString
IdentifierIri(const char* id)
{
	BString result(id);
	if (result.Length() > 0 && result.FindFirst(':') < 0)
		result.Prepend("urn:uuid:");
	return result;
}


BString
IdentifierUuid(const char* iri)
{
	BString result(iri);
	if (result.IFindFirst("urn:uuid:") == 0)
		result.Remove(0, 9);
	return result;
}


BString
ColorStyle(const char* motivation, uint32 color)
{
	char hex[16];
	snprintf(hex, sizeof(hex), "#%06x", (unsigned)(color & 0xffffff));
	BString css;
	if (strcmp(motivation, kHighlighting) == 0)
		css << "background-color: " << hex << ";";
	else if (strcmp(motivation, kUnderline) == 0)
		css << "text-decoration-line: underline; text-decoration-color: " << hex << ";";
	else if (strcmp(motivation, kStrikethrough) == 0)
		css << "text-decoration-line: line-through; text-decoration-color: " << hex << ";";
	else if (strcmp(motivation, kSquiggle) == 0)
		css << "text-decoration-line: underline; text-decoration-style: wavy; text-decoration-color: " << hex << ";";
	else
		css << "color: " << hex << ";";
	return css;
}


bool
ColorOfStyle(const char* css, uint32* color)
{
	const char* hash = css != NULL ? strchr(css, '#') : NULL;
	unsigned value;
	if (hash == NULL || sscanf(hash + 1, "%6x", &value) != 1)
		return false;
	*color = value & 0xffffff;
	return true;
}


// ---- the message model

void
AddTextualBody(BMessage* annotation, const char* text)
{
	BMessage body;
	body.AddString("type", kTextualBody);
	body.AddString("rdf:value", text);
	body.AddString("dc:format", "text/plain");
	annotation->AddMessage("oa:hasBody", &body);
}


void
AddCssStyle(BMessage* annotation, const char* css)
{
	BMessage style;
	style.AddString("type", kCssStyle);
	style.AddString("rdf:value", css);
	annotation->AddMessage("oa:hasStyle", &style);
}


void
AddCreator(BMessage* annotation, const char* name, int64 created)
{
	if (name != NULL && name[0] != '\0') {
		BMessage agent;
		agent.AddString("type", "foaf:Person");
		agent.AddString("foaf:name", name);
		annotation->AddMessage("dcterms:creator", &agent);
	}
	BString iso = TimeToIso(created);
	if (iso.Length() > 0)
		annotation->AddString("dcterms:created", iso);
}


void
MakeFragmentSelector(BMessage* selector, const char* conformsTo, const char* value)
{
	selector->AddString("type", kFragmentSelector);
	if (conformsTo != NULL && conformsTo[0] != '\0')
		selector->AddString("dcterms:conformsTo", conformsTo);
	selector->AddString("rdf:value", value);
}


// ---- drawn annotations on a page

static BString
Percent(float fraction)
{
	char buffer[32];
	snprintf(buffer, sizeof(buffer), "%.3f", fraction * 100);
	return BString(buffer);
}


// a shape that is told by a rectangle: the others have an SVG
static bool
HasRectangleSelector(const BString& shape)
{
	return shape == "rectangle" || shape == "note" || shape == "text";
}


static BString
ShapeSvg(const Mark& mark)
{
	BString svg("<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 100 100\" preserveAspectRatio=\"none\">");
	if (mark.shape == "ellipse") {
		svg << "<ellipse cx=\"" << Percent((mark.box[0] + mark.box[2]) / 2) << "\" cy=\""
			<< Percent((mark.box[1] + mark.box[3]) / 2) << "\" rx=\"" << Percent((mark.box[2] - mark.box[0]) / 2)
			<< "\" ry=\"" << Percent((mark.box[3] - mark.box[1]) / 2) << "\"/>";
	} else {
		if (mark.shape == "arrow")
			svg << "<defs><marker id=\"head\" markerWidth=\"4\" markerHeight=\"4\" refX=\"3\" refY=\"2\" orient=\"auto\">"
				<< "<polygon points=\"0,0 4,2 0,4\"/></marker></defs>";
		for (size_t i = 0; i < mark.paths.size(); i++) {
			const std::vector<float>& path = mark.paths[i];
			if (path.size() < 4)
				continue;
			svg << "<path d=\"";
			for (size_t k = 0; k + 1 < path.size(); k += 2)
				svg << (k == 0 ? "M " : " L ") << Percent(path[k]) << " " << Percent(path[k + 1]);
			svg << "\"" << (mark.shape == "arrow" ? " marker-end=\"url(#head)\"" : "") << "/>";
		}
	}
	svg << "</svg>";
	return svg;
}


// the number after name=" in the SVG, false if there is none
static bool
SvgNumber(const char* svg, const char* name, float* value)
{
	BString key(" ");
	key << name << "=\"";
	const char* at = strstr(svg, key.String());
	if (at == NULL)
		return false;
	*value = (float)atof(at + key.Length());
	return true;
}


// reads the shapes of an SVG that ShapeSvg() wrote (percent of the page) into the mark
static bool
ReadSvg(const char* svg, Mark* mark)
{
	float cx, cy, rx, ry;
	if (strstr(svg, "<ellipse") != NULL && SvgNumber(svg, "cx", &cx) && SvgNumber(svg, "cy", &cy)
		&& SvgNumber(svg, "rx", &rx) && SvgNumber(svg, "ry", &ry)) {
		mark->shape = "ellipse";
		mark->box[0] = (cx - rx) / 100;
		mark->box[1] = (cy - ry) / 100;
		mark->box[2] = (cx + rx) / 100;
		mark->box[3] = (cy + ry) / 100;
		return true;
	}

	mark->paths.clear();
	for (const char* at = strstr(svg, "<path"); at != NULL; at = strstr(at + 5, "<path")) {
		const char* d = strstr(at, " d=\"");
		if (d == NULL)
			continue;
		d += 4;
		std::vector<float> path;
		while (*d != '\0' && *d != '"') {
			if ((*d >= '0' && *d <= '9') || *d == '-' || *d == '.') {
				char* end;
				path.push_back((float)strtod(d, &end) / 100);
				d = end;
			} else
				d++;
		}
		if (path.size() >= 4)
			mark->paths.push_back(path);
	}
	if (mark->paths.empty())
		return false;
	if (mark->shape != "line" && mark->shape != "arrow" && mark->shape != "ink")
		mark->shape = strstr(svg, "marker-end") != NULL ? "arrow" : mark->paths.size() == 1
			&& mark->paths[0].size() == 4 ? "line" : "ink";
	// the box is what the points span
	float x0 = 2, y0 = 2, x1 = -1, y1 = -1;
	for (size_t i = 0; i < mark->paths.size(); i++) {
		for (size_t k = 0; k + 1 < mark->paths[i].size(); k += 2) {
			x0 = fminf(x0, mark->paths[i][k]);
			x1 = fmaxf(x1, mark->paths[i][k]);
			y0 = fminf(y0, mark->paths[i][k + 1]);
			y1 = fmaxf(y1, mark->paths[i][k + 1]);
		}
	}
	mark->box[0] = x0;
	mark->box[1] = y0;
	mark->box[2] = x1;
	mark->box[3] = y1;
	return true;
}


// ---- the bounds of any SVG

static void
Grow(float box[4], bool* any, float x, float y)
{
	if (!*any) {
		box[0] = box[2] = x;
		box[1] = box[3] = y;
		*any = true;
		return;
	}
	box[0] = fminf(box[0], x);
	box[1] = fminf(box[1], y);
	box[2] = fmaxf(box[2], x);
	box[3] = fmaxf(box[3], y);
}


// the numbers of a list (a path or points), two at a time are points
static void
GrowByNumbers(float box[4], bool* any, const char* text)
{
	float first = 0;
	bool haveFirst = false;
	while (*text != '\0' && *text != '"') {
		if ((*text >= '0' && *text <= '9') || *text == '-' || *text == '.' || *text == '+') {
			char* end;
			float value = (float)strtod(text, &end);
			if (end == text) {
				text++;
				continue;
			}
			text = end;
			if (haveFirst) {
				Grow(box, any, first, value);
				haveFirst = false;
			} else {
				first = value;
				haveFirst = true;
			}
		} else
			text++;
	}
}


static bool
SvgAttributeNumber(const char* from, const char* name, float* value)
{
	// only inside the tag that starts at from
	const char* close = strchr(from, '>');
	BString key(" ");
	key << name << "=\"";
	const char* at = strstr(from, key.String());
	if (at == NULL || (close != NULL && at > close))
		return false;
	*value = (float)atof(at + key.Length());
	return true;
}


bool
SvgBoundingBox(const char* svg, float box[4], float viewBox[4], bool* hasViewBox)
{
	*hasViewBox = false;
	const char* vb = strstr(svg, "viewBox=\"");
	if (vb != NULL) {
		float values[4];
		if (sscanf(vb + 9, "%f%*[ ,]%f%*[ ,]%f%*[ ,]%f", &values[0], &values[1], &values[2], &values[3]) == 4) {
			memcpy(viewBox, values, sizeof(values));
			*hasViewBox = true;
		}
	}

	bool any = false;
	for (const char* at = strchr(svg, '<'); at != NULL; at = strchr(at + 1, '<')) {
		float a, b, c, d;
		if (strncmp(at, "<rect", 5) == 0) {
			if (SvgAttributeNumber(at, "width", &c) && SvgAttributeNumber(at, "height", &d)) {
				a = b = 0;
				SvgAttributeNumber(at, "x", &a);
				SvgAttributeNumber(at, "y", &b);
				Grow(box, &any, a, b);
				Grow(box, &any, a + c, b + d);
			}
		} else if (strncmp(at, "<ellipse", 8) == 0) {
			if (SvgAttributeNumber(at, "cx", &a) && SvgAttributeNumber(at, "cy", &b) && SvgAttributeNumber(at, "rx", &c)
				&& SvgAttributeNumber(at, "ry", &d)) {
				Grow(box, &any, a - c, b - d);
				Grow(box, &any, a + c, b + d);
			}
		} else if (strncmp(at, "<circle", 7) == 0) {
			if (SvgAttributeNumber(at, "cx", &a) && SvgAttributeNumber(at, "cy", &b) && SvgAttributeNumber(at, "r", &c)) {
				Grow(box, &any, a - c, b - c);
				Grow(box, &any, a + c, b + c);
			}
		} else if (strncmp(at, "<line", 5) == 0) {
			if (SvgAttributeNumber(at, "x1", &a) && SvgAttributeNumber(at, "y1", &b) && SvgAttributeNumber(at, "x2", &c)
				&& SvgAttributeNumber(at, "y2", &d)) {
				Grow(box, &any, a, b);
				Grow(box, &any, c, d);
			}
		} else if (strncmp(at, "<polygon", 8) == 0 || strncmp(at, "<polyline", 9) == 0) {
			const char* points = strstr(at, " points=\"");
			if (points != NULL)
				GrowByNumbers(box, &any, points + 9);
		} else if (strncmp(at, "<path", 5) == 0) {
			const char* d = strstr(at, " d=\"");
			if (d != NULL)
				GrowByNumbers(box, &any, d + 4);
		}
	}
	return any;
}


static void
ArchiveDrawn(const Mark& mark, BMessage* annotation)
{
	annotation->AddString("sen:shape", mark.shape);

	BMessage target;
	BMessage pageSelector;
	char pageValue[24];
	snprintf(pageValue, sizeof(pageValue), "page=%d", (int)mark.page);
	MakeFragmentSelector(&pageSelector, kConformsToPdf, pageValue);

	BMessage refinement;
	if (HasRectangleSelector(mark.shape)) {
		BString region("xywh=percent:");
		region << Percent(mark.box[0]) << "," << Percent(mark.box[1]) << "," << Percent(mark.box[2] - mark.box[0])
			<< "," << Percent(mark.box[3] - mark.box[1]);
		MakeFragmentSelector(&refinement, kConformsToMediaFragments, region.String());
	} else {
		refinement.AddString("type", kSvgSelector);
		refinement.AddString("rdf:value", ShapeSvg(mark));
	}
	pageSelector.AddMessage("oa:refinedBy", &refinement);
	target.AddMessage("oa:hasSelector", &pageSelector);
	annotation->AddMessage("oa:hasTarget", &target);
}


// the page, shape and place that a target says; false if it is not an annotation on a page
static bool
UnarchiveDrawn(const BMessage& annotation, const BMessage& target, Mark* mark)
{
	BMessage selector;
	for (int32 i = 0; target.FindMessage("oa:hasSelector", i, &selector) == B_OK; i++) {
		BString type, value;
		selector.FindString("type", &type);
		if (type != kFragmentSelector || selector.FindString("rdf:value", &value) != B_OK
			|| value.IFindFirst("page=") != 0)
			continue;
		mark->page = atoi(value.String() + 5);

		BMessage refinement;
		if (mark->page < 1 || selector.FindMessage("oa:refinedBy", &refinement) != B_OK)
			return false;
		refinement.FindString("type", &type);
		if (type == kTextQuoteSelector) {
			// not drawn: the words on a page
			mark->textPage = mark->page;
			mark->page = 0;
			refinement.FindString("oa:exact", &mark->quote);
			refinement.FindString("oa:prefix", &mark->prefix);
			refinement.FindString("oa:suffix", &mark->suffix);
			return false;
		}
		annotation.FindString("sen:shape", &mark->shape);
		if (refinement.FindString("rdf:value", &value) != B_OK)
			return false;
		if (type == kSvgSelector)
			return ReadSvg(value.String(), mark);

		// xywh=percent:x,y,w,h
		float x, y, w, h;
		if (value.IFindFirst("xywh=percent:") != 0
			|| sscanf(value.String() + 13, "%f,%f,%f,%f", &x, &y, &w, &h) != 4)
			return false;
		mark->box[0] = x / 100;
		mark->box[1] = y / 100;
		mark->box[2] = (x + w) / 100;
		mark->box[3] = (y + h) / 100;
		if (!HasRectangleSelector(mark->shape))
			mark->shape = "rectangle";
		return true;
	}
	return false;
}


void
ArchiveMark(const Mark& mark, BMessage* annotation)
{
	annotation->AddString("id", IdentifierIri(mark.id.String()));
	annotation->AddString("type", kAnnotation);
	annotation->AddString("oa:motivatedBy", mark.motivation);
	AddCreator(annotation, mark.creator.String(), mark.created);
	if (mark.body.Length() > 0)
		AddTextualBody(annotation, mark.body.String());
	if (mark.hasColor) {
		BString css = ColorStyle(mark.motivation.String(), mark.color);
		if (mark.page > 0 && mark.shape != "note" && mark.shape != "text") {
			// a line that is drawn, as in an SVG
			char hex[16];
			snprintf(hex, sizeof(hex), "#%06x", (unsigned)(mark.color & 0xffffff));
			css = "stroke: ";
			css << hex << "; fill: none;";
		}
		AddCssStyle(annotation, css.String());
	}

	if (mark.page > 0) {
		ArchiveDrawn(mark, annotation);
		return;
	}

	// the target is the document that the annotation is stored with, so there is no source in it
	BMessage target;
	BMessage quote;
	quote.AddString("type", kTextQuoteSelector);
	quote.AddString("oa:exact", mark.quote);
	if (mark.prefix.Length() > 0)
		quote.AddString("oa:prefix", mark.prefix);
	if (mark.suffix.Length() > 0)
		quote.AddString("oa:suffix", mark.suffix);
	if (mark.textPage > 0) {
		// a page of a document with fixed pages, and the words on it (as the marks of a PDF file are written)
		BMessage pageSelector;
		char pageValue[24];
		snprintf(pageValue, sizeof(pageValue), "page=%d", (int)mark.textPage);
		MakeFragmentSelector(&pageSelector, kConformsToPdf, pageValue);
		pageSelector.AddMessage("oa:refinedBy", &quote);
		target.AddMessage("oa:hasSelector", &pageSelector);
	} else
		target.AddMessage("oa:hasSelector", &quote);
	if (mark.cfi.Length() > 0) {
		BMessage cfi;
		MakeFragmentSelector(&cfi, kConformsToEpubCfi, mark.cfi.String());
		target.AddMessage("oa:hasSelector", &cfi);
	}
	// where it was (to look there first)
	target.AddInt32("sen:chapter", mark.chapter);
	target.AddFloat("sen:fraction", mark.fraction);
	target.AddFloat("sen:ypos", mark.ypos);
	annotation->AddMessage("oa:hasTarget", &target);
}


bool
UnarchiveMark(const BMessage& annotation, Mark* mark)
{
	BString id;
	if (annotation.FindString("id", &id) != B_OK)
		return false;
	mark->id = IdentifierUuid(id.String());
	if (annotation.FindString("oa:motivatedBy", &mark->motivation) != B_OK)
		mark->motivation = kHighlighting;

	BMessage body;
	mark->body = "";
	if (annotation.FindMessage("oa:hasBody", &body) == B_OK)
		body.FindString("rdf:value", &mark->body);

	BMessage style;
	mark->hasColor = false;
	BString css;
	if (annotation.FindMessage("oa:hasStyle", &style) == B_OK && style.FindString("rdf:value", &css) == B_OK)
		mark->hasColor = ColorOfStyle(css.String(), &mark->color);

	BMessage agent;
	mark->creator = "";
	if (annotation.FindMessage("dcterms:creator", &agent) == B_OK)
		agent.FindString("foaf:name", &mark->creator);
	BString created;
	mark->created = annotation.FindString("dcterms:created", &created) == B_OK ? IsoToTime(created.String()) : 0;

	BMessage target;
	if (annotation.FindMessage("oa:hasTarget", &target) != B_OK)
		return false;
	mark->page = 0;
	if (UnarchiveDrawn(annotation, target, mark))
		return true;
	if (mark->page > 0)
		return false;	// a page, but no place on it
	target.FindInt32("sen:chapter", &mark->chapter);
	target.FindFloat("sen:fraction", &mark->fraction);
	target.FindFloat("sen:ypos", &mark->ypos);

	BMessage selector;
	for (int32 i = 0; target.FindMessage("oa:hasSelector", i, &selector) == B_OK; i++) {
		BString type;
		selector.FindString("type", &type);
		if (type == kTextQuoteSelector) {
			selector.FindString("oa:exact", &mark->quote);
			selector.FindString("oa:prefix", &mark->prefix);
			selector.FindString("oa:suffix", &mark->suffix);
		} else if (type == kFragmentSelector) {
			BString value;
			if (selector.FindString("rdf:value", &value) == B_OK && value.IFindFirst("epubcfi(") == 0)
				mark->cfi = value;
		}
	}
	return mark->quote.Length() > 0;
}


// ---- JSON

static void
WriteString(const char* text, BString& out)
{
	out << '"';
	for (const char* c = text; *c != '\0'; c++) {
		switch (*c) {
			case '"':	out << "\\\""; break;
			case '\\':	out << "\\\\"; break;
			case '\n':	out << "\\n"; break;
			case '\r':	out << "\\r"; break;
			case '\t':	out << "\\t"; break;
			default:
				if ((unsigned char)*c < 0x20) {
					char escape[8];
					snprintf(escape, sizeof(escape), "\\u%04x", (unsigned)*c);
					out << escape;
				} else
					out << *c;
		}
	}
	out << '"';
}


static void
WriteIndent(int level, BString& out)
{
	for (int i = 0; i < level; i++)
		out << "  ";
}


static void WriteMessage(const BMessage& message, int level, BString& out);


static void
WriteValue(const BMessage& message, const char* name, type_code type, int32 index, int level, BString& out)
{
	switch (type) {
		case B_STRING_TYPE: {
			const char* value = "";
			message.FindString(name, index, &value);
			WriteString(value, out);
			break;
		}
		case B_INT32_TYPE: {
			int32 value = 0;
			message.FindInt32(name, index, &value);
			out << (int)value;
			break;
		}
		case B_INT64_TYPE: {
			int64 value = 0;
			message.FindInt64(name, index, &value);
			char buffer[32];
			snprintf(buffer, sizeof(buffer), "%lld", (long long)value);
			out << buffer;
			break;
		}
		case B_FLOAT_TYPE: {
			float value = 0;
			message.FindFloat(name, index, &value);
			char buffer[32];
			snprintf(buffer, sizeof(buffer), "%g", value);
			out << buffer;
			break;
		}
		case B_DOUBLE_TYPE: {
			double value = 0;
			message.FindDouble(name, index, &value);
			char buffer[32];
			snprintf(buffer, sizeof(buffer), "%g", value);
			out << buffer;
			break;
		}
		case B_BOOL_TYPE: {
			bool value = false;
			message.FindBool(name, index, &value);
			out << (value ? "true" : "false");
			break;
		}
		case B_MESSAGE_TYPE: {
			BMessage value;
			message.FindMessage(name, index, &value);
			WriteMessage(value, level, out);
			break;
		}
		default:
			out << "null";
	}
}


static void
WriteMessage(const BMessage& message, int level, BString& out)
{
	out << "{\n";
	char* name;
	type_code type;
	int32 count;
	bool first = true;
	for (int32 i = 0; message.GetInfo(B_ANY_TYPE, i, &name, &type, &count) == B_OK; i++) {
		if (!first)
			out << ",\n";
		first = false;
		WriteIndent(level + 1, out);
		WriteString(name, out);
		out << ": ";
		if (count > 1) {
			out << "[\n";
			for (int32 k = 0; k < count; k++) {
				WriteIndent(level + 2, out);
				WriteValue(message, name, type, k, level + 2, out);
				out << (k + 1 < count ? ",\n" : "\n");
			}
			WriteIndent(level + 1, out);
			out << "]";
		} else
			WriteValue(message, name, type, 0, level + 1, out);
	}
	out << "\n";
	WriteIndent(level, out);
	out << "}";
}


BString
ToJson(const BMessage& message, bool withContext)
{
	BMessage copy;
	if (withContext) {
		// the prefixes of the compact IRIs, first
		BMessage context;
		context.AddString("oa", kNamespaceOa);
		context.AddString("rdf", "http://www.w3.org/1999/02/22-rdf-syntax-ns#");
		context.AddString("dc", "http://purl.org/dc/elements/1.1/");
		context.AddString("dcterms", "http://purl.org/dc/terms/");
		context.AddString("foaf", "http://xmlns.com/foaf/0.1/");
		context.AddString("schema", "https://schema.org/");
		context.AddString("sen", kNamespaceSen);
		copy.AddMessage("@context", &context);
	}
	char* name;
	type_code type;
	int32 count;
	for (int32 i = 0; message.GetInfo(B_ANY_TYPE, i, &name, &type, &count) == B_OK; i++) {
		for (int32 k = 0; k < count; k++) {
			// (copied field by field, to keep the order)
			switch (type) {
				case B_STRING_TYPE: { const char* v = ""; message.FindString(name, k, &v); copy.AddString(name, v); break; }
				case B_INT32_TYPE: { int32 v = 0; message.FindInt32(name, k, &v); copy.AddInt32(name, v); break; }
				case B_INT64_TYPE: { int64 v = 0; message.FindInt64(name, k, &v); copy.AddInt64(name, v); break; }
				case B_FLOAT_TYPE: { float v = 0; message.FindFloat(name, k, &v); copy.AddFloat(name, v); break; }
				case B_DOUBLE_TYPE: { double v = 0; message.FindDouble(name, k, &v); copy.AddDouble(name, v); break; }
				case B_BOOL_TYPE: { bool v = false; message.FindBool(name, k, &v); copy.AddBool(name, v); break; }
				case B_MESSAGE_TYPE: { BMessage v; message.FindMessage(name, k, &v); copy.AddMessage(name, &v); break; }
				default: break;
			}
		}
	}
	BString json;
	WriteMessage(copy, 0, json);
	json << "\n";
	return json;
}

}	// namespace WebAnnotation

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


void
ArchiveMark(const Mark& mark, BMessage* annotation)
{
	annotation->AddString("id", IdentifierIri(mark.id.String()));
	annotation->AddString("type", kAnnotation);
	annotation->AddString("oa:motivatedBy", mark.motivation);
	AddCreator(annotation, mark.creator.String(), mark.created);
	if (mark.body.Length() > 0)
		AddTextualBody(annotation, mark.body.String());
	if (mark.hasColor)
		AddCssStyle(annotation, ColorStyle(mark.motivation.String(), mark.color).String());

	// the target is the document that the annotation is stored with, so there is no source in it
	BMessage target;
	BMessage quote;
	quote.AddString("type", kTextQuoteSelector);
	quote.AddString("oa:exact", mark.quote);
	if (mark.prefix.Length() > 0)
		quote.AddString("oa:prefix", mark.prefix);
	if (mark.suffix.Length() > 0)
		quote.AddString("oa:suffix", mark.suffix);
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

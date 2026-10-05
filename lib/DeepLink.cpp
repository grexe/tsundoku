/*
 * SPDX-License-Identifier: MIT
 * Copyright (C) 2026 Gregor B. Rosenauer & Claude
 */

#include "DeepLink.h"

#include "WebAnnotation.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <vector>

namespace DeepLink {

const char* const kScheme = "toji";


// percent-encodes what is not unreserved; in the words of a text fragment the comma and the hyphen are encoded, too (they
// separate the parts there)
static BString
Encode(const char* text, bool path)
{
	BString out;
	for (const unsigned char* c = (const unsigned char*)text; *c != '\0'; c++) {
		bool plain = (*c >= 'A' && *c <= 'Z') || (*c >= 'a' && *c <= 'z') || (*c >= '0' && *c <= '9') || *c == '.'
			|| *c == '_' || *c == '~' || (path && (*c == '/' || *c == '-'));
		if (plain)
			out.Append((const char*)c, 1);
		else {
			char hex[4];
			snprintf(hex, sizeof(hex), "%%%02X", *c);
			out << hex;
		}
	}
	return out;
}


static BString
Decode(const BString& text)
{
	BString out;
	const char* s = text.String();
	for (int32 i = 0; i < text.Length(); i++) {
		if (s[i] == '%' && i + 2 < text.Length() + 0 && isxdigit((unsigned char)s[i + 1])
			&& isxdigit((unsigned char)s[i + 2])) {
			char hex[3] = { s[i + 1], s[i + 2], '\0' };
			char byte = (char)strtol(hex, NULL, 16);
			out.Append(&byte, 1);
			i += 2;
		} else
			out.Append(s + i, 1);
	}
	return out;
}


BString
Make(const char* path, const Place& place)
{
	BString uri(kScheme);
	uri << "://" << Encode(path, true);

	BString fragment;
	if (place.cfi.Length() > 0)
		fragment << place.cfi;
	else {
		if (place.page > 0)
			fragment << "page=" << (int)place.page;
		if (place.hasRegion) {
			char region[96];
			snprintf(region, sizeof(region), "xywh=percent:%g,%g,%g,%g", place.region[0], place.region[1],
				place.region[2], place.region[3]);
			if (fragment.Length() > 0)
				fragment << "&";
			fragment << region;
		}
		if (place.annotation.Length() > 0) {
			if (fragment.Length() > 0)
				fragment << "&";
			fragment << "annotation=" << place.annotation;
		}
	}
	if (place.quote.Length() > 0) {
		fragment << ":~:text=";
		if (place.prefix.Length() > 0)
			fragment << Encode(place.prefix.String(), false) << "-,";
		fragment << Encode(place.quote.String(), false);
		if (place.suffix.Length() > 0)
			fragment << ",-" << Encode(place.suffix.String(), false);
	}
	if (fragment.Length() > 0)
		uri << "#" << fragment;
	return uri;
}


bool
IsLink(const char* text)
{
	return text != NULL && (strncasecmp(text, "toji:", 5) == 0 || strncasecmp(text, "file:", 5) == 0);
}


bool
Parse(const char* uri, BString* path, Place* place)
{
	if (uri == NULL)
		return false;
	BString text(uri);
	text.Trim();

	// the scheme and the empty (or local) host
	if (text.IFindFirst("toji:") == 0 || text.IFindFirst("file:") == 0) {
		text.Remove(0, 5);
		if (text.FindFirst("//") == 0) {
			text.Remove(0, 2);
			if (text.IFindFirst("localhost/") == 0)
				text.Remove(0, 9);
		}
	}
	if (text.Length() == 0 || text[0] != '/')
		return false;

	BString fragment;
	int32 hash = text.FindFirst('#');
	if (hash >= 0) {
		text.CopyInto(fragment, hash + 1, text.Length() - hash - 1);
		text.Truncate(hash);
	}
	*path = Decode(text);
	*place = Place();

	// the directive (the words) follows :~:
	BString directive;
	int32 colon = fragment.FindFirst(":~:");
	if (colon >= 0) {
		fragment.CopyInto(directive, colon + 3, fragment.Length() - colon - 3);
		fragment.Truncate(colon);
	}

	if (fragment.IFindFirst("epubcfi(") == 0)
		place->cfi = Decode(fragment);
	else {
		int32 start = 0;
		while (start < fragment.Length()) {
			int32 end = fragment.FindFirst('&', start);
			if (end < 0)
				end = fragment.Length();
			BString part;
			fragment.CopyInto(part, start, end - start);
			start = end + 1;
			if (part.IFindFirst("page=") == 0)
				place->page = atoi(part.String() + 5);
			else if (part.IFindFirst("xywh=percent:") == 0) {
				float x, y, w, h;
				if (sscanf(part.String() + 13, "%f,%f,%f,%f", &x, &y, &w, &h) == 4) {
					place->hasRegion = true;
					place->region[0] = x;
					place->region[1] = y;
					place->region[2] = w;
					place->region[3] = h;
				}
			} else if (part.IFindFirst("annotation=") == 0)
				place->annotation = Decode(BString(part.String() + 11));
		}
	}

	if (directive.IFindFirst("text=") == 0) {
		// [prefix-,]start[,end][,-suffix]
		BString rest(directive.String() + 5);
		std::vector<BString> items;
		int32 start = 0;
		while (start <= rest.Length()) {
			int32 end = rest.FindFirst(',', start);
			if (end < 0)
				end = rest.Length();
			BString item;
			rest.CopyInto(item, start, end - start);
			items.push_back(item);
			start = end + 1;
		}
		if (!items.empty() && items.front().Length() > 1 && items.front()[items.front().Length() - 1] == '-') {
			BString prefix = items.front();
			prefix.Truncate(prefix.Length() - 1);
			place->prefix = Decode(prefix);
			items.erase(items.begin());
		}
		if (!items.empty() && items.back().Length() > 1 && items.back()[0] == '-') {
			BString suffix(items.back().String() + 1);
			place->suffix = Decode(suffix);
			items.pop_back();
		}
		if (!items.empty())
			place->quote = Decode(items.front());
	}
	return true;
}


void
ToTarget(const Place& place, BMessage* target)
{
	if (place.page > 0) {
		BMessage pageSelector;
		char value[24];
		snprintf(value, sizeof(value), "page=%d", (int)place.page);
		WebAnnotation::MakeFragmentSelector(&pageSelector, WebAnnotation::kConformsToPdf, value);
		if (place.hasRegion) {
			char region[96];
			snprintf(region, sizeof(region), "xywh=percent:%g,%g,%g,%g", place.region[0], place.region[1],
				place.region[2], place.region[3]);
			BMessage regionSelector;
			WebAnnotation::MakeFragmentSelector(&regionSelector, WebAnnotation::kConformsToMediaFragments, region);
			pageSelector.AddMessage("oa:refinedBy", &regionSelector);
		}
		target->AddMessage("oa:hasSelector", &pageSelector);
	}
	if (place.quote.Length() > 0) {
		BMessage quote;
		quote.AddString("type", WebAnnotation::kTextQuoteSelector);
		quote.AddString("oa:exact", place.quote);
		if (place.prefix.Length() > 0)
			quote.AddString("oa:prefix", place.prefix);
		if (place.suffix.Length() > 0)
			quote.AddString("oa:suffix", place.suffix);
		target->AddMessage("oa:hasSelector", &quote);
	}
	if (place.cfi.Length() > 0) {
		BMessage cfi;
		WebAnnotation::MakeFragmentSelector(&cfi, WebAnnotation::kConformsToEpubCfi, place.cfi.String());
		target->AddMessage("oa:hasSelector", &cfi);
	}
}

}	// namespace DeepLink

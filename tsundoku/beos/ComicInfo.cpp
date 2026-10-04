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

#include "ComicInfo.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <libxml/parser.h>
#include <libxml/tree.h>

static const size_t kMaxSize = 4 * 1024 * 1024;


static BString
Text(const xmlNode* node)
{
	BString result;
	xmlChar* content = xmlNodeGetContent(node);
	if (content != NULL) {
		result = (const char*)content;
		xmlFree(content);
	}
	result.ReplaceAll("\r", " ");
	result.ReplaceAll("\n", " ");
	result.ReplaceAll("\t", " ");
	while (result.FindFirst("  ") >= 0)
		result.ReplaceAll("  ", " ");
	result.Trim();
	return result;
}


static BString
Attribute(const xmlNode* node, const char* name)
{
	BString result;
	xmlChar* value = xmlGetProp(node, (const xmlChar*)name);
	if (value != NULL) {
		result = (const char*)value;
		xmlFree(value);
	}
	return result;
}


// the values of a list that is written with commas: "Gregor B. Rosenauer, Claude"
static void
AddList(std::vector<BString>* list, const BString& text)
{
	int32 start = 0;
	while (start <= text.Length()) {
		int32 comma = text.FindFirst(',', start);
		if (comma < 0)
			comma = text.Length();
		BString part;
		text.CopyInto(part, start, comma - start);
		part.Trim();
		if (part.Length() > 0)
			list->push_back(part);
		start = comma + 1;
	}
}


static BString
Join(const std::vector<BString>& list)
{
	BString result;
	for (size_t i = 0; i < list.size(); i++) {
		if (i > 0)
			result << ", ";
		result << list[i];
	}
	return result;
}


// The text of the file ComicInfo.xml (its name may be written in another case), empty if there is none.
static BString
ReadFile(fz_context* context, fz_archive* archive)
{
	const char* name = NULL;
	int count = fz_count_archive_entries(context, archive);
	for (int i = 0; i < count; i++) {
		const char* entry = fz_list_archive_entry(context, archive, i);
		if (entry != NULL && strcasecmp(entry, "ComicInfo.xml") == 0) {
			name = entry;
			break;
		}
	}
	if (name == NULL)
		return BString();

	fz_buffer* buffer = NULL;
	BString result;
	bool failed = false;
	fz_var(buffer);
	fz_try(context) {
		buffer = fz_read_archive_entry(context, archive, name);
	}
	fz_catch(context) {
		failed = true;
	}
	if (buffer != NULL) {
		unsigned char* data = NULL;
		size_t size = fz_buffer_storage(context, buffer, &data);
		if (!failed && data != NULL && size <= kMaxSize)
			result.SetTo((const char*)data, (int32)size);
		fz_drop_buffer(context, buffer);
	}
	return result;
}


ComicInfo*
ComicInfo::Read(fz_context* context, fz_archive* archive)
{
	BString text = ReadFile(context, archive);
	if (text.Length() == 0)
		return NULL;

	xmlDoc* doc = xmlReadMemory(text.String(), text.Length(), "ComicInfo.xml", NULL,
		XML_PARSE_NONET | XML_PARSE_NOERROR | XML_PARSE_NOWARNING | XML_PARSE_NOBLANKS);
	if (doc == NULL)
		return NULL;
	xmlNode* root = xmlDocGetRootElement(doc);
	if (root == NULL || strcmp((const char*)root->name, "ComicInfo") != 0) {
		xmlFreeDoc(doc);
		return NULL;
	}

	ComicInfo* info = new ComicInfo();
	for (xmlNode* node = root->children; node != NULL; node = node->next) {
		if (node->type != XML_ELEMENT_NODE)
			continue;
		const char* name = (const char*)node->name;
		BString value = Text(node);
		if (strcmp(name, "Pages") == 0) {
			for (xmlNode* page = node->children; page != NULL; page = page->next) {
				if (page->type != XML_ELEMENT_NODE || strcmp((const char*)page->name, "Page") != 0)
					continue;
				Page entry;
				entry.image = atoi(Attribute(page, "Image").String());
				entry.type = Attribute(page, "Type");
				entry.doublePage = strcasecmp(Attribute(page, "DoublePage").String(), "true") == 0;
				info->pages.push_back(entry);
			}
			continue;
		}
		if (value.Length() == 0)
			continue;

		if (strcmp(name, "Title") == 0) info->title = value;
		else if (strcmp(name, "Series") == 0) info->series = value;
		else if (strcmp(name, "Number") == 0) info->number = value;
		else if (strcmp(name, "Count") == 0) info->count = atoi(value.String());
		else if (strcmp(name, "Volume") == 0) info->volume = atoi(value.String());
		else if (strcmp(name, "Summary") == 0) info->summary = value;
		else if (strcmp(name, "Year") == 0) info->year = atoi(value.String());
		else if (strcmp(name, "Month") == 0) info->month = atoi(value.String());
		else if (strcmp(name, "Day") == 0) info->day = atoi(value.String());
		else if (strcmp(name, "Writer") == 0) AddList(&info->writers, value);
		else if (strcmp(name, "Penciller") == 0) AddList(&info->pencillers, value);
		else if (strcmp(name, "Inker") == 0) AddList(&info->inkers, value);
		else if (strcmp(name, "Colorist") == 0) AddList(&info->colorists, value);
		else if (strcmp(name, "Letterer") == 0) AddList(&info->letterers, value);
		else if (strcmp(name, "CoverArtist") == 0) AddList(&info->coverArtists, value);
		else if (strcmp(name, "Editor") == 0) AddList(&info->editors, value);
		else if (strcmp(name, "Publisher") == 0) info->publisher = value;
		else if (strcmp(name, "Genre") == 0) AddList(&info->genres, value);
		else if (strcmp(name, "Tags") == 0) AddList(&info->tags, value);
		else if (strcmp(name, "LanguageISO") == 0) info->language = value;
		else if (strcmp(name, "AgeRating") == 0) info->ageRating = value;
		else if (strcmp(name, "Web") == 0) info->web = value;
		else if (strcmp(name, "PageCount") == 0) info->pageCount = atoi(value.String());
		else if (strcmp(name, "Manga") == 0) info->rightToLeft = value.IFindFirst("RightToLeft") >= 0;
	}
	xmlFreeDoc(doc);
	return info;
}


BString
ComicInfo::Authors() const
{
	return Join(writers);
}


BString
ComicInfo::Artists() const
{
	std::vector<BString> all;
	for (size_t i = 0; i < pencillers.size(); i++)
		all.push_back(pencillers[i]);
	for (size_t i = 0; i < inkers.size(); i++)
		all.push_back(inkers[i]);
	for (size_t i = 0; i < colorists.size(); i++)
		all.push_back(colorists[i]);
	// the same person often does several of it
	std::vector<BString> unique;
	for (size_t i = 0; i < all.size(); i++) {
		bool known = false;
		for (size_t k = 0; k < unique.size(); k++)
			known = known || unique[k] == all[i];
		if (!known)
			unique.push_back(all[i]);
	}
	return Join(unique);
}


BString
ComicInfo::Keywords() const
{
	std::vector<BString> all(genres);
	for (size_t i = 0; i < tags.size(); i++)
		all.push_back(tags[i]);
	return Join(all);
}


BString
ComicInfo::Date() const
{
	BString result;
	if (year <= 0)
		return result;
	result << year;
	if (month >= 1 && month <= 12) {
		char buffer[16];
		snprintf(buffer, sizeof(buffer), "-%02d", (int)month);
		result << buffer;
		if (day >= 1 && day <= 31) {
			snprintf(buffer, sizeof(buffer), "-%02d", (int)day);
			result << buffer;
		}
	}
	return result;
}


int32
ComicInfo::CoverPage() const
{
	for (size_t i = 0; i < pages.size(); i++) {
		if (pages[i].type == "FrontCover")
			return pages[i].image;
	}
	return 0;
}

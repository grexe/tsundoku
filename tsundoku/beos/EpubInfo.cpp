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


#include "EpubInfo.h"

#include <string.h>

#include <libxml/parser.h>
#include <libxml/tree.h>
#include <zip.h>

#include <algorithm>

static const size_t kMaxMemberSize = 16 * 1024 * 1024;


bool
EpubInfo::ReadMember(const char* path, const char* member, std::vector<uint8>* data)
{
	int error = 0;
	zip_t* archive = zip_open(path, ZIP_RDONLY, &error);
	if (archive == NULL)
		return false;

	bool ok = false;
	zip_stat_t stat;
	if (zip_stat(archive, member, ZIP_FL_ENC_GUESS, &stat) == 0 && (stat.valid & ZIP_STAT_SIZE) != 0
		&& stat.size <= kMaxMemberSize) {
		zip_file_t* file = zip_fopen(archive, member, ZIP_FL_ENC_GUESS);
		if (file != NULL) {
			data->resize((size_t)stat.size);
			zip_int64_t got = stat.size > 0 ? zip_fread(file, &(*data)[0], stat.size) : 0;
			ok = got == (zip_int64_t)stat.size;
			zip_fclose(file);
		}
	}
	zip_close(archive);
	return ok;
}


// ---- the XML helpers: elements are matched by their local names, namespaces are not checked

static bool
IsElement(const xmlNode* node, const char* name)
{
	return node != NULL && node->type == XML_ELEMENT_NODE && strcmp((const char*)node->name, name) == 0;
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


// the text of an element with white space made single spaces
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


// the directory part of a path inside the container, with the slash
static BString
Directory(const BString& path)
{
	int32 slash = path.FindLast('/');
	if (slash < 0)
		return BString();
	BString result;
	path.CopyInto(result, 0, slash + 1);
	return result;
}


// a reference inside the container: relative to the directory, with ../ resolved, without fragment and query
static BString
Resolve(const BString& base, const BString& href)
{
	BString ref(href);
	int32 cut = ref.FindFirst('#');
	if (cut >= 0)
		ref.Truncate(cut);
	cut = ref.FindFirst('?');
	if (cut >= 0)
		ref.Truncate(cut);

	BString full(base);
	full << ref;

	std::vector<BString> parts;
	int32 start = 0;
	while (start <= full.Length()) {
		int32 slash = full.FindFirst('/', start);
		if (slash < 0)
			slash = full.Length();
		BString part;
		full.CopyInto(part, start, slash - start);
		if (part == "..") {
			if (!parts.empty())
				parts.pop_back();
		} else if (part.Length() > 0 && part != ".")
			parts.push_back(part);
		start = slash + 1;
	}
	BString result;
	for (size_t i = 0; i < parts.size(); i++) {
		if (i > 0)
			result << "/";
		result << parts[i];
	}
	return result;
}


static bool
ReadAll(zip_t* archive, const char* member, std::vector<char>* data)
{
	zip_stat_t stat;
	if (zip_stat(archive, member, ZIP_FL_ENC_GUESS, &stat) != 0 || (stat.valid & ZIP_STAT_SIZE) == 0
		|| stat.size > kMaxMemberSize)
		return false;
	zip_file_t* file = zip_fopen(archive, member, ZIP_FL_ENC_GUESS);
	if (file == NULL)
		return false;
	data->resize((size_t)stat.size);
	zip_int64_t got = stat.size > 0 ? zip_fread(file, &(*data)[0], stat.size) : 0;
	zip_fclose(file);
	return got == (zip_int64_t)stat.size;
}


static xmlDoc*
Parse(const std::vector<char>& data)
{
	if (data.empty())
		return NULL;
	// no network, no entities, no messages
	return xmlReadMemory(&data[0], (int)data.size(), NULL, NULL,
		XML_PARSE_NONET | XML_PARSE_NOERROR | XML_PARSE_NOWARNING | XML_PARSE_NOBLANKS);
}


EpubInfo*
EpubInfo::Read(const char* path)
{
	int error = 0;
	zip_t* archive = zip_open(path, ZIP_RDONLY, &error);
	if (archive == NULL)
		return NULL;

	// META-INF/container.xml names the package document
	BString opfPath;
	std::vector<char> data;
	if (ReadAll(archive, "META-INF/container.xml", &data)) {
		if (xmlDoc* container = Parse(data)) {
			for (xmlNode* node = xmlDocGetRootElement(container); node != NULL;) {
				if (IsElement(node, "rootfile") && opfPath.Length() == 0) {
					opfPath = Attribute(node, "full-path");
					break;
				}
				if (node->children != NULL)
					node = node->children;
				else {
					while (node != NULL && node->next == NULL)
						node = node->parent;
					if (node != NULL)
						node = node->next;
				}
			}
			xmlFreeDoc(container);
		}
	}
	if (opfPath.Length() == 0 || !ReadAll(archive, opfPath.String(), &data)) {
		zip_close(archive);
		return NULL;
	}
	zip_close(archive);

	xmlDoc* package = Parse(data);
	if (package == NULL)
		return NULL;
	xmlNode* root = xmlDocGetRootElement(package);
	if (!IsElement(root, "package")) {
		xmlFreeDoc(package);
		return NULL;
	}

	EpubInfo* info = new EpubInfo;
	info->version = Attribute(root, "version");
	BString uniqueId = Attribute(root, "unique-identifier");
	BString base = Directory(opfPath);

	struct Meta {
		BString property, refines, id, name, content, text;
	};
	std::vector<Meta> metas;
	struct Item {
		BString id, href, type, properties;
	};
	std::vector<Item> items;
	BString firstIdentifier;

	struct ItemRef {
		BString idref;
		bool    linear;
		int32   step;
	};
	std::vector<ItemRef> itemrefs;
	int32 packageElements = 0;

	for (xmlNode* section = root->children; section != NULL; section = section->next) {
		if (section->type == XML_ELEMENT_NODE)
			packageElements++;
		if (IsElement(section, "spine")) {
			info->spineStep = 2 * packageElements;
			int32 spineElements = 0;
			for (xmlNode* node = section->children; node != NULL; node = node->next) {
				if (node->type != XML_ELEMENT_NODE)
					continue;
				spineElements++;
				if (!IsElement(node, "itemref"))
					continue;
				ItemRef ref;
				ref.idref = Attribute(node, "idref");
				ref.linear = Attribute(node, "linear") != "no";
				ref.step = 2 * spineElements;
				itemrefs.push_back(ref);
			}
		} else if (IsElement(section, "metadata")) {
			for (xmlNode* node = section->children; node != NULL; node = node->next) {
				if (node->type != XML_ELEMENT_NODE)
					continue;
				BString name((const char*)node->name);
				BString text = Text(node);
				if (name == "title") {
					if (info->title.Length() == 0)
						info->title = text;
				} else if (name == "creator") {
					if (text.Length() > 0)
						info->authors.push_back(text);
				} else if (name == "language") {
					if (info->language.Length() == 0)
						info->language = text;
				} else if (name == "publisher") {
					if (info->publisher.Length() == 0)
						info->publisher = text;
				} else if (name == "date") {
					if (info->date.Length() == 0)
						info->date = text;
				} else if (name == "description") {
					if (info->description.Length() == 0)
						info->description = text;
				} else if (name == "subject") {
					if (text.Length() > 0)
						info->subjects.push_back(text);
				} else if (name == "identifier") {
					// an ISBN is one of the identifiers: urn:isbn:... or marked with the ISBN scheme
					BString lower(text);
					lower.ToLower();
					BString scheme = Attribute(node, "scheme");
					scheme.ToLower();
					if (info->isbn.Length() == 0) {
						if (lower.FindFirst("urn:isbn:") == 0)
							text.CopyInto(info->isbn, 9, text.Length() - 9);
						else if (lower.FindFirst("isbn:") == 0)
							text.CopyInto(info->isbn, 5, text.Length() - 5);
						else if (scheme == "isbn")
							info->isbn = text;
						info->isbn.RemoveAll("-");
						info->isbn.Trim();
					}
					if (firstIdentifier.Length() == 0)
						firstIdentifier = text;
					if (uniqueId.Length() > 0 && Attribute(node, "id") == uniqueId)
						info->identifier = text;
				} else if (name == "meta") {
					Meta meta;
					meta.property = Attribute(node, "property");
					meta.refines = Attribute(node, "refines");
					meta.id = Attribute(node, "id");
					meta.name = Attribute(node, "name");
					meta.content = Attribute(node, "content");
					meta.text = text;
					metas.push_back(meta);
				}
			}
		} else if (IsElement(section, "manifest")) {
			for (xmlNode* node = section->children; node != NULL; node = node->next) {
				if (!IsElement(node, "item"))
					continue;
				Item item;
				item.id = Attribute(node, "id");
				item.href = Attribute(node, "href");
				item.type = Attribute(node, "media-type");
				item.properties = Attribute(node, "properties");
				items.push_back(item);
			}
		}
	}
	xmlFreeDoc(package);

	for (size_t i = 0; i < itemrefs.size(); i++) {
		for (size_t k = 0; k < items.size(); k++) {
			if (items[k].id != itemrefs[i].idref)
				continue;
			SpineItem item;
			item.idref = itemrefs[i].idref;
			item.path = Resolve(base, items[k].href);
			item.step = itemrefs[i].step;
			item.linear = itemrefs[i].linear;
			info->spine.push_back(item);
			break;
		}
	}

	if (info->identifier.Length() == 0)
		info->identifier = firstIdentifier;

	// the series: EPUB 3 collections (with the position given by a refining meta element), or calibre's
	for (size_t i = 0; i < metas.size() && info->series.Length() == 0; i++) {
		const Meta& meta = metas[i];
		if (meta.property == "belongs-to-collection" && meta.text.Length() > 0) {
			// a collection that is a series (or has no type)
			BString type;
			for (size_t k = 0; k < metas.size(); k++) {
				if (metas[k].property == "collection-type" && meta.id.Length() > 0
					&& metas[k].refines == BString("#") << meta.id)
					type = metas[k].text;
			}
			if (type.Length() > 0 && type != "series")
				continue;
			info->series = meta.text;
			for (size_t k = 0; k < metas.size(); k++) {
				if (metas[k].property == "group-position" && meta.id.Length() > 0
					&& metas[k].refines == BString("#") << meta.id)
					info->seriesIndex = metas[k].text;
			}
		}
	}
	if (info->series.Length() == 0) {
		for (size_t i = 0; i < metas.size(); i++) {
			if (metas[i].name == "calibre:series")
				info->series = metas[i].content;
			else if (metas[i].name == "calibre:series_index")
				info->seriesIndex = metas[i].content;
		}
	}

	// the cover: the item with the cover-image property, or the one that the old cover meta element names
	const Item* cover = NULL;
	for (size_t i = 0; i < items.size() && cover == NULL; i++) {
		BString padded(" ");
		padded << items[i].properties << " ";
		if (padded.FindFirst(" cover-image ") >= 0)
			cover = &items[i];
	}
	if (cover == NULL) {
		for (size_t i = 0; i < metas.size() && cover == NULL; i++) {
			if (metas[i].name != "cover")
				continue;
			for (size_t k = 0; k < items.size(); k++) {
				if (items[k].id == metas[i].content && items[k].type.IFindFirst("image/") == 0) {
					cover = &items[k];
					break;
				}
			}
		}
	}
	if (cover != NULL && cover->href.Length() > 0) {
		info->coverMember = Resolve(base, cover->href);
		info->coverType = cover->type;
	}
	return info;
}


BString
EpubInfo::Authors() const
{
	BString result;
	for (size_t i = 0; i < authors.size(); i++) {
		if (i > 0)
			result << ", ";
		result << authors[i];
	}
	return result;
}


BString
EpubInfo::Subjects() const
{
	BString result;
	for (size_t i = 0; i < subjects.size(); i++) {
		if (i > 0)
			result << ", ";
		result << subjects[i];
	}
	return result;
}

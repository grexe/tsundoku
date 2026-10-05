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

#include "BbfInfo.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>

#include <memory>

// Everything is little-endian, whatever the machine is.
static uint16
Le16(const uint8* p)
{
	return (uint16)(p[0] | (p[1] << 8));
}


static uint32
Le32(const uint8* p)
{
	return (uint32)p[0] | ((uint32)p[1] << 8) | ((uint32)p[2] << 16) | ((uint32)p[3] << 24);
}


static uint64
Le64(const uint8* p)
{
	return (uint64)Le32(p) | ((uint64)Le32(p + 4) << 32);
}


static const uint64 kHeaderSize = 64, kFooterSize = 256;
static const uint64 kAssetSize = 48, kPageSize = 16, kSectionSize = 32, kMetaSize = 32;
static const uint64 kMaxEntries = 1000000;		// of a table
static const uint64 kMaxStrings = 16 * 1024 * 1024;
static const size_t kMaxString = 2048;


// A table of the file: the offset and the number of entries, checked against the size of the file.
static bool
ReadTable(BbfSource& source, uint64 fileSize, uint64 offset, uint64 count, uint64 entrySize, std::vector<uint8>* into)
{
	if (count == 0) {
		into->clear();
		return true;
	}
	if (count > kMaxEntries || count > fileSize / entrySize || offset > fileSize
		|| count * entrySize > fileSize - offset)
		return false;
	into->resize((size_t)(count * entrySize));
	return source.Read(offset, &(*into)[0], into->size());
}


// A string of the pool. The offsets are absolute offsets in the file; one that is smaller than the pool is taken as
// relative to its start.
static bool
StringAt(const std::vector<uint8>& pool, uint64 poolOffset, uint64 offset, BString* string)
{
	if (offset >= poolOffset && offset - poolOffset < pool.size())
		offset -= poolOffset;
	else if (offset >= pool.size())
		return false;
	size_t length = 0;
	while (offset + length < pool.size() && pool[(size_t)offset + length] != 0) {
		if (++length > kMaxString)
			return false;
	}
	if (offset + length >= pool.size())
		return false;	// no end
	string->SetTo((const char*)&pool[(size_t)offset], (int32)length);
	return true;
}


BbfInfo*
BbfInfo::Parse(BbfSource& source)
{
	uint64 size = source.Size();
	uint8 header[kHeaderSize];
	if (size < kHeaderSize + kFooterSize || !source.Read(0, header, kHeaderSize))
		return NULL;
	if (memcmp(header, "BBF3", 4) != 0 || Le16(header + 4) != 3 || Le16(header + 6) < kHeaderSize)
		return NULL;

	uint64 footerOffset = Le64(header + 16);
	if (footerOffset > size || size - footerOffset < kFooterSize)
		return NULL;
	uint8 footer[kFooterSize];
	if (!source.Read(footerOffset, footer, kFooterSize))
		return NULL;

	uint64 assetOffset = Le64(footer), pageOffset = Le64(footer + 8), sectionOffset = Le64(footer + 16);
	uint64 metaOffset = Le64(footer + 24), poolOffset = Le64(footer + 40), poolSize = Le64(footer + 48);
	uint64 assetCount = Le64(footer + 56), pageCount = Le64(footer + 64), sectionCount = Le64(footer + 72);
	uint64 metaCount = Le64(footer + 80);

	if (poolOffset > size || poolSize > size - poolOffset || poolSize > kMaxStrings)
		return NULL;
	std::vector<uint8> assets, pages, sections, metas, pool(poolSize);
	if (!ReadTable(source, size, assetOffset, assetCount, kAssetSize, &assets)
		|| !ReadTable(source, size, pageOffset, pageCount, kPageSize, &pages)
		|| !ReadTable(source, size, sectionOffset, sectionCount, kSectionSize, &sections)
		|| !ReadTable(source, size, metaOffset, metaCount, kMetaSize, &metas)
		|| (poolSize > 0 && !source.Read(poolOffset, &pool[0], (size_t)poolSize)))
		return NULL;

	std::unique_ptr<BbfInfo> info(new BbfInfo());
	for (uint64 i = 0; i < pageCount; i++) {
		uint64 asset = Le64(&pages[(size_t)(i * kPageSize)]);
		if (asset >= assetCount)
			return NULL;
		const uint8* entry = &assets[(size_t)(asset * kAssetSize)];
		Page page;
		page.offset = Le64(entry);
		page.size = Le64(entry + 24);
		page.type = entry[38];
		if (page.offset > size || page.size > size - page.offset)
			return NULL;
		info->pages.push_back(page);
	}

	for (uint64 i = 0; i < sectionCount; i++) {
		const uint8* entry = &sections[(size_t)(i * kSectionSize)];
		Section section;
		section.firstPage = Le64(entry + 8);
		uint64 parent = Le64(entry + 16);
		if (!StringAt(pool, poolOffset, Le64(entry), &section.title))
			return NULL;
		if (parent != ~(uint64)0 && !StringAt(pool, poolOffset, parent, &section.parent))
			return NULL;
		info->sections.push_back(section);
	}

	for (uint64 i = 0; i < metaCount; i++) {
		const uint8* entry = &metas[(size_t)(i * kMetaSize)];
		Meta meta;
		uint64 parent = Le64(entry + 16);
		if (!StringAt(pool, poolOffset, Le64(entry), &meta.key) || !StringAt(pool, poolOffset, Le64(entry + 8), &meta.value))
			return NULL;
		if (parent != ~(uint64)0)
			StringAt(pool, poolOffset, parent, &meta.parent);
		info->metadata.push_back(meta);
	}
	return info.release();
}


class FileSource : public BbfSource {
public:
	FileSource(const char* path) : fFile(fopen(path, "rb")), fSize(0)
	{
		if (fFile != NULL && fseeko(fFile, 0, SEEK_END) == 0)
			fSize = (uint64)ftello(fFile);
	}

	~FileSource()
	{
		if (fFile != NULL)
			fclose(fFile);
	}

	bool IsOpen() const { return fFile != NULL; }
	virtual uint64 Size() { return fSize; }

	virtual bool Read(uint64 offset, void* buffer, size_t size)
	{
		return fFile != NULL && fseeko(fFile, (off_t)offset, SEEK_SET) == 0 && fread(buffer, 1, size, fFile) == size;
	}

private:
	FILE*  fFile;
	uint64 fSize;
};


BbfInfo*
BbfInfo::Read(const char* path)
{
	FileSource source(path);
	return source.IsOpen() ? Parse(source) : NULL;
}


BString
BbfInfo::Value(const char* const* keys) const
{
	for (int i = 0; keys[i] != NULL; i++) {
		for (size_t k = 0; k < metadata.size(); k++) {
			if (strcasecmp(metadata[k].key.String(), keys[i]) == 0 && metadata[k].value.Length() > 0)
				return metadata[k].value;
		}
	}
	return BString();
}


const char*
BbfInfo::ExtensionOf(uint8 type)
{
	switch (type) {
		case kAvif:		return "avif";
		case kPng:		return "png";
		case kWebp:		return "webp";
		case kJxl:		return "jxl";
		case kBmp:		return "bmp";
		case kGif:		return "gif";
		case kTiff:		return "tif";
		case kJpeg:		return "jpg";
		default:		return NULL;
	}
}


const char*
BbfInfo::ExtensionOfData(const uint8* data, size_t size)
{
	if (size >= 3 && data[0] == 0xff && data[1] == 0xd8)
		return "jpg";
	if (size >= 8 && memcmp(data, "\x89PNG", 4) == 0)
		return "png";
	if (size >= 6 && memcmp(data, "GIF8", 4) == 0)
		return "gif";
	if (size >= 12 && memcmp(data, "RIFF", 4) == 0 && memcmp(data + 8, "WEBP", 4) == 0)
		return "webp";
	if (size >= 12 && memcmp(data + 4, "ftypavif", 8) == 0)
		return "avif";
	if (size >= 2 && memcmp(data, "BM", 2) == 0)
		return "bmp";
	if (size >= 4 && (memcmp(data, "II*\0", 4) == 0 || memcmp(data, "MM\0*", 4) == 0))
		return "tif";
	if (size >= 2 && data[0] == 0xff && data[1] == 0x0a)
		return "jxl";
	if (size >= 12 && memcmp(data, "\0\0\0\x0cJXL ", 8) == 0)
		return "jxl";
	return NULL;
}

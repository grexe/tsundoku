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

#include "ComicArchive.h"
#include "ComicInfo.h"

#include "BbfInfo.h"

#include <Bitmap.h>
#include <BitmapStream.h>
#include <DataIO.h>
#include <TranslationUtils.h>
#include <TranslatorRoster.h>

#include <archive.h>
#include <archive_entry.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <map>
#include <new>
#include <string>
#include <vector>

namespace ComicArchive {

namespace {

// The code between fz_try and fz_catch leaves with a long jump: nothing with a destructor lives in it, the
// objects with one are members of the structures that are constructed and destroyed by hand.

// ------------------------------------------------------------------------------------------------------------
// RAR and 7z with libarchive. libarchive reads an archive from the start to the end, so a file is found by going
// through the headers (and by starting again if it is before the position we are at). The pages are read in
// order, mostly, which is what this is good at. Reading is one at a time.

struct LibArchive {
	fz_archive super;
	fz_context* context;         // of the caller, for the callbacks of libarchive
	pthread_mutex_t lock;
	struct archive* archive;
	size_t next;                 // the number of files whose header has been read
	std::vector<std::string> names;
	std::map<std::string, size_t> index;
	unsigned char block[16384];  // read from the stream for libarchive
	unsigned char chunk[65536];  // read from libarchive for the entry
};


la_ssize_t
LibRead(struct archive* a, void* data, const void** buffer)
{
	LibArchive* arch = (LibArchive*)data;
	fz_context* context = arch->context;
	size_t got = 0;
	fz_var(got);
	fz_try(context) {
		got = fz_read(context, arch->super.file, arch->block, sizeof(arch->block));
	}
	fz_catch(context) {
		archive_set_error(a, EIO, "%s", fz_caught_message(context));
		return -1;
	}
	*buffer = arch->block;
	return (la_ssize_t)got;
}


la_int64_t
LibSkip(struct archive* a, void* data, la_int64_t request)
{
	LibArchive* arch = (LibArchive*)data;
	fz_context* context = arch->context;
	int64_t before = 0, after = 0;
	fz_var(before);
	fz_var(after);
	fz_try(context) {
		before = fz_tell(context, arch->super.file);
		fz_seek(context, arch->super.file, before + request, SEEK_SET);
		after = fz_tell(context, arch->super.file);
	}
	fz_catch(context) {
		archive_set_error(a, EIO, "%s", fz_caught_message(context));
		return -1;
	}
	return after - before;
}


la_int64_t
LibSeek(struct archive* a, void* data, la_int64_t offset, int whence)
{
	LibArchive* arch = (LibArchive*)data;
	fz_context* context = arch->context;
	int64_t position = 0;
	fz_var(position);
	fz_try(context) {
		fz_seek(context, arch->super.file, offset, whence);
		position = fz_tell(context, arch->super.file);
	}
	fz_catch(context) {
		archive_set_error(a, EIO, "%s", fz_caught_message(context));
		return -1;
	}
	return position;
}


int
LibClose(struct archive*, void*)
{
	return ARCHIVE_OK;	// the stream belongs to the fz_archive
}


// Starts reading the archive from its start. False if libarchive cannot read it (arch->context is set).
bool
LibStart(LibArchive* arch)
{
	fz_context* context = arch->context;
	bool seeked = true;
	fz_var(seeked);
	fz_try(context) {
		fz_seek(context, arch->super.file, 0, SEEK_SET);
	}
	fz_catch(context) {
		seeked = false;
	}
	if (!seeked)
		return false;

	if (arch->archive != NULL)
		archive_read_free(arch->archive);
	arch->archive = archive_read_new();
	archive_read_support_filter_all(arch->archive);
	archive_read_support_format_tar(arch->archive);
	archive_read_support_format_rar(arch->archive);
	archive_read_support_format_rar5(arch->archive);
	archive_read_support_format_7zip(arch->archive);
	archive_read_set_seek_callback(arch->archive, LibSeek);
	arch->next = 0;
	if (archive_read_open2(arch->archive, arch, NULL, LibRead, LibSkip, LibClose) != ARCHIVE_OK) {
		archive_read_free(arch->archive);
		arch->archive = NULL;
		return false;
	}
	return true;
}


// The next file of the archive (without directories and the like); false at the end or if reading fails, which
// is told by a message.
bool
LibNextFile(LibArchive* arch, struct archive_entry** entry, const char** problem)
{
	for (;;) {
		int result = archive_read_next_header(arch->archive, entry);
		if (result == ARCHIVE_EOF)
			return false;
		if (result < ARCHIVE_WARN) {
			*problem = archive_error_string(arch->archive);
			return false;
		}
		if (archive_entry_filetype(*entry) == AE_IFREG) {
			arch->next++;
			return true;
		}
	}
}


// The path of an entry as MuPDF writes names: with slashes, without a leading one.
std::string
LibName(struct archive_entry* entry)
{
	const char* path = archive_entry_pathname_utf8(entry);
	if (path == NULL)
		path = archive_entry_pathname(entry);
	std::string name = path != NULL ? path : "";
	for (size_t i = 0; i < name.size(); i++) {
		if (name[i] == '\\')
			name[i] = '/';
	}
	while (name.compare(0, 2, "./") == 0)
		name.erase(0, 2);
	while (!name.empty() && name[0] == '/')
		name.erase(0, 1);
	return name;
}


// The file number 'file' of the archive, NULL if that fails (with a message).
fz_buffer*
LibExtract(LibArchive* arch, size_t file, char* problem, size_t problemSize)
{
	fz_context* context = arch->context;
	const char* text = NULL;
	struct archive_entry* entry = NULL;

	// what is before the position is read again from the start
	if (file < arch->next && !LibStart(arch)) {
		strlcpy(problem, "cannot read the archive again", problemSize);
		return NULL;
	}
	while (arch->next <= file) {
		if (!LibNextFile(arch, &entry, &text)) {
			strlcpy(problem, text != NULL ? text : "the file is missing", problemSize);
			return NULL;
		}
	}

	fz_buffer* buffer = NULL;
	ssize_t got = 0;
	fz_var(buffer);
	fz_var(got);
	fz_try(context) {
		int64_t size = archive_entry_size_is_set(entry) ? archive_entry_size(entry) : 0;
		buffer = fz_new_buffer(context, size > 0 && size < (1 << 30) ? (size_t)size : 65536);
		while ((got = archive_read_data(arch->archive, arch->chunk, sizeof(arch->chunk))) > 0)
			fz_append_data(context, buffer, arch->chunk, (size_t)got);
	}
	fz_catch(context) {
		fz_drop_buffer(context, buffer);
		strlcpy(problem, fz_caught_message(context), problemSize);
		return NULL;
	}
	if (got < 0) {
		fz_drop_buffer(context, buffer);
		strlcpy(problem, archive_error_string(arch->archive), problemSize);
		return NULL;
	}
	return buffer;
}


void
LibDrop(fz_context*, fz_archive* archive)
{
	LibArchive* arch = (LibArchive*)archive;
	if (arch->archive != NULL)
		archive_read_free(arch->archive);
	pthread_mutex_destroy(&arch->lock);
	arch->names.~vector();
	arch->index.~map();
}


int
LibCount(fz_context*, fz_archive* archive)
{
	return (int)((LibArchive*)archive)->names.size();
}


const char*
LibList(fz_context*, fz_archive* archive, int i)
{
	LibArchive* arch = (LibArchive*)archive;
	if (i < 0 || (size_t)i >= arch->names.size())
		return NULL;
	return arch->names[i].c_str();
}


// The number of the file of that name, -1 if there is none.
long
LibFind(LibArchive* arch, const char* name)
{
	std::map<std::string, size_t>::const_iterator found = arch->index.find(name);
	return found == arch->index.end() ? -1 : (long)found->second;
}


int
LibHas(fz_context*, fz_archive* archive, const char* name)
{
	return LibFind((LibArchive*)archive, name) >= 0;
}


fz_buffer*
LibReadEntry(fz_context* context, fz_archive* archive, const char* name)
{
	LibArchive* arch = (LibArchive*)archive;
	long file = LibFind(arch, name);
	if (file < 0)
		return NULL;

	char problem[256];
	pthread_mutex_lock(&arch->lock);
	arch->context = context;
	fz_buffer* buffer = LibExtract(arch, (size_t)file, problem, sizeof(problem));
	arch->context = NULL;
	pthread_mutex_unlock(&arch->lock);
	if (buffer == NULL)
		fz_throw(context, FZ_ERROR_FORMAT, "cannot read %s: %s", name, problem);
	return buffer;
}


fz_stream*
LibOpenEntry(fz_context* context, fz_archive* archive, const char* name)
{
	fz_buffer* buffer = LibReadEntry(context, archive, name);
	if (buffer == NULL)
		return NULL;
	fz_stream* stream = NULL;
	fz_var(stream);
	fz_try(context) {
		stream = fz_open_buffer(context, buffer);
	}
	fz_always(context) {
		fz_drop_buffer(context, buffer);
	}
	fz_catch(context) {
		fz_rethrow(context);
	}
	return stream;
}


int
LibRecognize(fz_context* context, fz_stream* file)
{
	unsigned char magic[8];
	memset(magic, 0, sizeof(magic));
	size_t got = fz_read(context, file, magic, sizeof(magic));
	static const unsigned char kSevenZip[6] = { '7', 'z', 0xbc, 0xaf, 0x27, 0x1c };
	static const unsigned char kXz[6] = { 0xfd, '7', 'z', 'X', 'Z', 0 };
	static const unsigned char kZstd[4] = { 0x28, 0xb5, 0x2f, 0xfd };
	// RAR 4 and 5 start with Rar! 1A 07, followed by 00 or 01 00
	if (got >= 7 && memcmp(magic, "Rar!\x1a\x07", 6) == 0)
		return 1;
	if (got >= 6 && (memcmp(magic, kSevenZip, sizeof(kSevenZip)) == 0 || memcmp(magic, kXz, sizeof(kXz)) == 0))
		return 1;
	// a TAR file (MuPDF reads those that are not compressed) with gzip, bzip2 or Zstandard
	return (got >= 2 && magic[0] == 0x1f && magic[1] == 0x8b) || (got >= 3 && memcmp(magic, "BZh", 3) == 0)
		|| (got >= 4 && memcmp(magic, kZstd, sizeof(kZstd)) == 0);
}


fz_archive*
LibOpen(fz_context* context, fz_stream* file)
{
	LibArchive* arch = fz_new_derived_archive(context, file, LibArchive);
	// constructed by hand, the memory is zeroed
	pthread_mutex_init(&arch->lock, NULL);
	new (&arch->names) std::vector<std::string>();
	new (&arch->index) std::map<std::string, size_t>();
	arch->super.format = "rar";
	arch->super.drop_archive = LibDrop;
	arch->super.count_entries = LibCount;
	arch->super.list_entry = LibList;
	arch->super.has_entry = LibHas;
	arch->super.read_entry = LibReadEntry;
	arch->super.open_entry = LibOpenEntry;

	// the names of all files
	const char* problem = NULL;
	arch->context = context;
	if (LibStart(arch)) {
		struct archive_entry* entry;
		while (LibNextFile(arch, &entry, &problem)) {
			std::string name = LibName(entry);
			arch->index[name] = arch->names.size();
			arch->names.push_back(name);
		}
		int format = archive_format(arch->archive) & ARCHIVE_FORMAT_BASE_MASK;
		arch->super.format = format == ARCHIVE_FORMAT_7ZIP ? "7z" : format == ARCHIVE_FORMAT_TAR ? "tar" : "rar";
	} else
		problem = "libarchive cannot read it";
	arch->context = NULL;

	if (arch->names.empty()) {
		char message[256];
		strlcpy(message, problem != NULL ? problem : "no files", sizeof(message));
		fz_drop_archive(context, &arch->super);
		fz_throw(context, FZ_ERROR_FORMAT, "cannot read the archive: %s", message);
	}
	// the listing went through the whole archive, the first read starts from the beginning
	arch->next = (size_t)-1;
	return &arch->super;
}


const fz_archive_handler kLibArchiveHandler = { LibRecognize, LibOpen };


// ------------------------------------------------------------------------------------------------------------
// Bound Book Format: the pages are what the page table says, in the order that it says, and are found by their offset

class StreamSource : public BbfSource {
public:
	StreamSource(fz_context* context, fz_stream* file) : fContext(context), fFile(file), fSize(0)
	{
		fz_var(fSize);
		fz_try(fContext) {
			fz_seek(fContext, fFile, 0, SEEK_END);
			fSize = (uint64)fz_tell(fContext, fFile);
		}
		fz_catch(fContext) {
			fSize = 0;
		}
	}

	virtual uint64 Size() { return fSize; }

	virtual bool Read(uint64 offset, void* buffer, size_t size)
	{
		bool ok = true;
		fz_var(ok);
		fz_try(fContext) {
			fz_seek(fContext, fFile, (int64_t)offset, SEEK_SET);
			ok = fz_read(fContext, fFile, (unsigned char*)buffer, size) == size;
		}
		fz_catch(fContext) {
			ok = false;
		}
		return ok;
	}

private:
	fz_context* fContext;
	fz_stream*  fFile;
	uint64      fSize;
};


struct BbfArchive {
	fz_archive super;
	pthread_mutex_t lock;
	BbfInfo* info;
	std::vector<std::string> names;		// page00001.png, ...
	std::vector<size_t> pageOf;			// the page of the table that the name stands for
	std::map<std::string, size_t> index;
};


void
BbfDrop(fz_context*, fz_archive* archive)
{
	BbfArchive* arch = (BbfArchive*)archive;
	delete arch->info;
	pthread_mutex_destroy(&arch->lock);
	arch->names.~vector();
	arch->pageOf.~vector();
	arch->index.~map();
}


int
BbfCount(fz_context*, fz_archive* archive)
{
	return (int)((BbfArchive*)archive)->names.size();
}


const char*
BbfList(fz_context*, fz_archive* archive, int i)
{
	BbfArchive* arch = (BbfArchive*)archive;
	if (i < 0 || (size_t)i >= arch->names.size())
		return NULL;
	return arch->names[i].c_str();
}


int
BbfHas(fz_context*, fz_archive* archive, const char* name)
{
	BbfArchive* arch = (BbfArchive*)archive;
	return arch->index.find(name) != arch->index.end();
}


fz_buffer*
BbfReadEntry(fz_context* context, fz_archive* archive, const char* name)
{
	BbfArchive* arch = (BbfArchive*)archive;
	std::map<std::string, size_t>::const_iterator found = arch->index.find(name);
	if (found == arch->index.end())
		return NULL;
	const BbfInfo::Page& page = arch->info->pages[arch->pageOf[found->second]];
	if (page.size > ((uint64)1 << 29))
		fz_throw(context, FZ_ERROR_FORMAT, "page %s is too large", name);

	size_t size = (size_t)page.size;
	unsigned char* data = (unsigned char*)fz_malloc(context, size > 0 ? size : 1);
	fz_try(context) {
		pthread_mutex_lock(&arch->lock);
		fz_seek(context, arch->super.file, (int64_t)page.offset, SEEK_SET);
		if (fz_read(context, arch->super.file, data, size) != size)
			fz_throw(context, FZ_ERROR_FORMAT, "page %s is cut off", name);
	}
	fz_always(context) {
		pthread_mutex_unlock(&arch->lock);
	}
	fz_catch(context) {
		fz_free(context, data);
		fz_rethrow(context);
	}
	return fz_new_buffer_from_data(context, data, size);	// the buffer owns the data
}


fz_stream*
BbfOpenEntry(fz_context* context, fz_archive* archive, const char* name)
{
	fz_buffer* buffer = BbfReadEntry(context, archive, name);
	if (buffer == NULL)
		return NULL;
	fz_stream* stream = NULL;
	fz_var(stream);
	fz_try(context) {
		stream = fz_open_buffer(context, buffer);
	}
	fz_always(context) {
		fz_drop_buffer(context, buffer);
	}
	fz_catch(context) {
		fz_rethrow(context);
	}
	return stream;
}


int
BbfRecognize(fz_context* context, fz_stream* file)
{
	unsigned char magic[4];
	return fz_read(context, file, magic, sizeof(magic)) == sizeof(magic) && memcmp(magic, "BBF3", 4) == 0;
}


fz_archive*
BbfOpen(fz_context* context, fz_stream* file)
{
	StreamSource source(context, file);
	BbfInfo* info = BbfInfo::Parse(source);
	if (info == NULL)
		fz_throw(context, FZ_ERROR_FORMAT, "not a BBF file, or its index is damaged");

	BbfArchive* arch = NULL;
	fz_var(arch);
	fz_try(context) {
		arch = fz_new_derived_archive(context, file, BbfArchive);
	}
	fz_catch(context) {
		delete info;
		fz_rethrow(context);
	}
	// constructed by hand, the memory is zeroed
	pthread_mutex_init(&arch->lock, NULL);
	new (&arch->names) std::vector<std::string>();
	new (&arch->pageOf) std::vector<size_t>();
	new (&arch->index) std::map<std::string, size_t>();
	arch->info = info;
	arch->super.format = "bbf";
	arch->super.drop_archive = BbfDrop;
	arch->super.count_entries = BbfCount;
	arch->super.list_entry = BbfList;
	arch->super.has_entry = BbfHas;
	arch->super.read_entry = BbfReadEntry;
	arch->super.open_entry = BbfOpenEntry;

	// a name for each page, in the order of the table: the extension tells what picture it is (from the type of the
	// asset, or the first bytes of it if the type is not known)
	for (size_t i = 0; i < info->pages.size(); i++) {
		const char* extension = BbfInfo::ExtensionOf(info->pages[i].type);
		if (extension == NULL) {
			uint8 head[16];
			size_t length = info->pages[i].size < sizeof(head) ? (size_t)info->pages[i].size : sizeof(head);
			if (source.Read(info->pages[i].offset, head, length))
				extension = BbfInfo::ExtensionOfData(head, length);
		}
		if (extension == NULL)
			continue;
		char name[48];
		snprintf(name, sizeof(name), "page%05d.%s", (int)i + 1, extension);
		arch->index[name] = arch->names.size();
		arch->pageOf.push_back(i);
		arch->names.push_back(name);
	}
	return &arch->super;
}


const fz_archive_handler kBbfHandler = { BbfRecognize, BbfOpen };


// ------------------------------------------------------------------------------------------------------------
// An archive as it is without the junk, and with the pages that MuPDF cannot read as PNG files, converted by the
// translators of Haiku (WebP, AVIF) when they are asked for

const uint32 kPngFormat = 'PNG ';

struct Entry {
	int         inner;       // number of the entry in the inner archive
	bool        converted;   // read through a translator, and called .png
	std::string name;
};

struct Filtered {
	fz_archive super;
	fz_archive* inner;
	std::vector<Entry> entries;
	std::map<std::string, size_t> index;
};


// The images that MuPDF does not read, but a translator may.
bool
NeedsTranslator(const char* name)
{
	static const char* const kExtensions[] = { ".webp", ".avif", ".jxl", ".heic", ".heif", ".tga", NULL };
	const char* extension = strrchr(name, '.');
	if (extension == NULL)
		return false;
	for (int i = 0; kExtensions[i] != NULL; i++) {
		if (strcasecmp(extension, kExtensions[i]) == 0)
			return true;
	}
	return false;
}


// The image as a PNG file, in memory allocated with malloc; false if there is no translator that reads it. The
// translators of Haiku turn a file into a bitmap and a bitmap into a file, not one file into another.
bool
ToPng(const unsigned char* data, size_t size, unsigned char** png, size_t* pngSize)
{
	BMemoryIO source(data, size);
	BBitmap* bitmap = BTranslationUtils::GetBitmap(&source);
	if (bitmap == NULL)
		return false;
	BBitmapStream stream(bitmap);	// owns the bitmap
	BMallocIO destination;
	if (BTranslatorRoster::Default()->Translate(&stream, NULL, NULL, &destination, kPngFormat) != B_OK)
		return false;
	size_t length = destination.BufferLength();
	if (length == 0)
		return false;
	*png = (unsigned char*)malloc(length);
	if (*png == NULL)
		return false;
	memcpy(*png, destination.Buffer(), length);
	*pngSize = length;
	return true;
}


void
FilteredDrop(fz_context* context, fz_archive* archive)
{
	Filtered* filtered = (Filtered*)archive;
	fz_drop_archive(context, filtered->inner);
	filtered->entries.~vector();
	filtered->index.~map();
}


int
FilteredCount(fz_context*, fz_archive* archive)
{
	return (int)((Filtered*)archive)->entries.size();
}


const char*
FilteredList(fz_context*, fz_archive* archive, int i)
{
	Filtered* filtered = (Filtered*)archive;
	if (i < 0 || (size_t)i >= filtered->entries.size())
		return NULL;
	return filtered->entries[i].name.c_str();
}


// The entry of that name, -1 if there is none.
long
FilteredFind(Filtered* filtered, const char* name)
{
	std::map<std::string, size_t>::const_iterator found = filtered->index.find(name);
	return found == filtered->index.end() ? -1 : (long)found->second;
}


int
FilteredHas(fz_context*, fz_archive* archive, const char* name)
{
	return FilteredFind((Filtered*)archive, name) >= 0;
}


fz_buffer*
FilteredRead(fz_context* context, fz_archive* archive, const char* name)
{
	Filtered* filtered = (Filtered*)archive;
	long found = FilteredFind(filtered, name);
	if (found < 0)
		return NULL;
	const Entry& entry = filtered->entries[found];
	const char* innerName = fz_list_archive_entry(context, filtered->inner, entry.inner);
	fz_buffer* buffer = fz_read_archive_entry(context, filtered->inner, innerName);
	if (!entry.converted)
		return buffer;

	unsigned char* data = NULL;
	unsigned char* png = NULL;
	size_t size = fz_buffer_storage(context, buffer, &data);
	size_t pngSize = 0;
	bool converted = ToPng(data, size, &png, &pngSize);
	fz_drop_buffer(context, buffer);
	if (!converted)
		fz_throw(context, FZ_ERROR_FORMAT, "no translator reads %s", innerName);

	fz_buffer* result = NULL;
	fz_var(result);
	fz_try(context) {
		result = fz_new_buffer_from_copied_data(context, png, pngSize);
	}
	fz_always(context) {
		free(png);
	}
	fz_catch(context) {
		fz_rethrow(context);
	}
	return result;
}


fz_stream*
FilteredOpen(fz_context* context, fz_archive* archive, const char* name)
{
	fz_buffer* buffer = FilteredRead(context, archive, name);
	if (buffer == NULL)
		return NULL;
	fz_stream* stream = NULL;
	fz_var(stream);
	fz_try(context) {
		stream = fz_open_buffer(context, buffer);
	}
	fz_always(context) {
		fz_drop_buffer(context, buffer);
	}
	fz_catch(context) {
		fz_rethrow(context);
	}
	return stream;
}


fz_archive*
NewFiltered(fz_context* context, fz_archive* inner)
{
	Filtered* filtered = fz_new_derived_archive(context, NULL, Filtered);
	new (&filtered->entries) std::vector<Entry>();
	new (&filtered->index) std::map<std::string, size_t>();
	filtered->inner = fz_keep_archive(context, inner);
	filtered->super.format = fz_archive_format(context, inner);
	filtered->super.drop_archive = FilteredDrop;
	filtered->super.count_entries = FilteredCount;
	filtered->super.list_entry = FilteredList;
	filtered->super.has_entry = FilteredHas;
	filtered->super.read_entry = FilteredRead;
	filtered->super.open_entry = FilteredOpen;

	int count = fz_count_archive_entries(context, inner);
	for (int i = 0; i < count; i++) {
		const char* name = fz_list_archive_entry(context, inner, i);
		if (name == NULL || IsJunk(name))
			continue;
		Entry entry;
		entry.inner = i;
		entry.converted = NeedsTranslator(name);
		entry.name = name;
		if (entry.converted)
			entry.name += ".png";
		filtered->index[entry.name] = filtered->entries.size();
		filtered->entries.push_back(entry);
	}
	return &filtered->super;
}

}	// namespace


void
RegisterHandlers(fz_context* context)
{
	fz_register_archive_handler(context, &kLibArchiveHandler);
	fz_register_archive_handler(context, &kBbfHandler);
}


bool
IsComicFile(const char* path)
{
	const char* extension = strrchr(path, '.');
	if (extension == NULL)
		return false;
	return strcasecmp(extension, ".cbz") == 0 || strcasecmp(extension, ".cbr") == 0
		|| strcasecmp(extension, ".cb7") == 0 || strcasecmp(extension, ".cbt") == 0
		|| strcasecmp(extension, ".bbf") == 0;
}


fz_archive*
Open(fz_context* context, const char* path)
{
	fz_archive* inner = fz_open_archive(context, path);
	fz_archive* result = NULL;
	fz_var(result);
	fz_try(context) {
		result = NewFiltered(context, inner);
	}
	fz_always(context) {
		fz_drop_archive(context, inner);
	}
	fz_catch(context) {
		fz_rethrow(context);
	}
	return result;
}


bool
IsJunk(const char* name)
{
	// the junk of macOS: a folder __MACOSX with the resource forks of all files, ._ files next to them, and what
	// Windows and file managers keep in folders
	const char* part = name;
	for (;;) {
		const char* slash = strchr(part, '/');
		size_t length = slash != NULL ? (size_t)(slash - part) : strlen(part);
		if (length == 8 && strncmp(part, "__MACOSX", 8) == 0)
			return true;
		if (slash == NULL) {
			if (length >= 2 && part[0] == '.' && part[1] == '_')
				return true;
			return strcasecmp(part, ".DS_Store") == 0 || strcasecmp(part, "Thumbs.db") == 0
				|| strcasecmp(part, "desktop.ini") == 0;
		}
		part = slash + 1;
	}
}

// The text of the file ComicInfo.xml (its name may be written in another case), empty if there is none.
ComicInfo*
ReadComicInfo(fz_context* context, fz_archive* archive)
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
		return NULL;

	fz_buffer* buffer = NULL;
	ComicInfo* info = NULL;
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
		if (!failed && data != NULL)
			info = ComicInfo::Parse((const char*)data, size);
		fz_drop_buffer(context, buffer);
	}
	return info;
}


}	// namespace ComicArchive

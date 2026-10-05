/*
 * SPDX-License-Identifier: MIT
 * Copyright (C) 2026 Gregor B. Rosenauer & Claude
 */
#ifndef _BBF_INFO_H_
#define _BBF_INFO_H_

#include <vector>

#include <String.h>
#include <SupportDefs.h>

// The Bound Book Format (BBF, version 3, https://github.com/ef1500/libbbf): a container for the pages of comics and
// manga, which has a table with the pages in reading order, the metadata (key and value) and a table of sections
// (chapters, volumes). The index is read from the footer, so a page is found without reading the file.
//
// This reads the index only, from a file or from any other source of bytes (MuPDF's stream for the archive), and does not
// depend on MuPDF.
class BbfSource {
public:
	virtual ~BbfSource() {}
	virtual bool   Read(uint64 offset, void* buffer, size_t size) = 0;
	virtual uint64 Size() = 0;
};


class BbfInfo {
public:
	// The index of the file; NULL if it is not a BBF file of version 3 or the index is damaged (offsets outside of the
	// file, tables that are larger than the file).
	static BbfInfo* Parse(BbfSource& source);
	static BbfInfo* Read(const char* path);

	enum MediaType {
		kUnknown = 0, kAvif = 1, kPng = 2, kWebp = 3, kJxl = 4, kBmp = 5, kGif = 7, kTiff = 8, kJpeg = 9
	};

	struct Page {
		uint64  offset;      // of the picture in the file
		uint64  size;
		uint8   type;        // MediaType
	};
	std::vector<Page> pages;     // in reading order; pages may share a picture

	struct Section {
		BString title;
		uint64  firstPage;   // from 0
		BString parent;      // the title of the section it is in, empty at the top
	};
	std::vector<Section> sections;

	struct Meta {
		BString key, value, parent;
	};
	std::vector<Meta> metadata;

	// The value of the first of the keys that the file has (ignoring the case), empty if it has none.
	BString Value(const char* const* keys) const;

	// The extension of a file of the type, NULL if unknown.
	static const char* ExtensionOf(uint8 type);
	// The extension that the first bytes of a picture tell, NULL if they are none of those that are known.
	static const char* ExtensionOfData(const uint8* data, size_t size);

private:
	BbfInfo() {}
};

#endif

/*
 * SPDX-License-Identifier: MIT
 * Copyright (C) 2026 Gregor B. Rosenauer & Claude
 */
#ifndef _MOBI_H_
#define _MOBI_H_

#include <string>
#include <vector>

// Mobipocket books (.mobi, .prc, and the .azw of Kindle that has no DRM): the format of the PalmDOC database with a MOBI header, which
// is documented by its users (the MobileRead wiki) and not by a standard. This reads the old format (version 6: the text with
// PalmDOC compression or none, images, the metadata of the EXTH header) and makes of it the pieces of a book: chapters of XHTML,
// images, a table of contents and the cover, and an EPUB (in memory) from them, which a reader that has EPUB can show. Books with
// DRM are recognized and not read. The code needs nothing but the standard library.
namespace Mobi {

enum Result {
	kOk,
	kNotMobi,			// not a Mobipocket file
	kEncrypted,			// it has DRM
	kUnsupported,		// a kind of compression that is not read (Huffman/CDIC), or the new format (KF8) only
	kBroken				// the records do not fit
};

struct Image {
	std::string name;		// the name in the book: image0001.jpg
	std::string type;		// image/jpeg, image/png, image/gif
	std::string data;
};

struct Chapter {
	std::string name;		// chapter001.xhtml
	std::string xhtml;
};

struct TocEntry {
	std::string title;
	std::string href;		// chapter001.xhtml#id
	int         level;		// 1 is the top
};

struct Book {
	std::string title;
	std::vector<std::string> authors;
	std::string publisher, description, isbn, language, date, rights, identifier;
	std::vector<std::string> subjects;
	std::vector<Chapter> chapters;
	std::vector<Image> images;
	int coverImage;			// the index in images, -1 if there is none
	std::vector<TocEntry> toc;
	Book() : coverImage(-1) {}
};

// whether the first bytes of a file say that it is a Mobipocket book (the 68 bytes of its header are enough)
bool LooksLikeMobi(const unsigned char* header, size_t size);

// reads a book from the bytes of a file
Result Read(const unsigned char* data, size_t size, Book* book);

// The book as an EPUB 3 (a ZIP file, with the files stored), for a reader of EPUB.
std::string MakeEpub(const Book& book);

}	// namespace Mobi

#endif

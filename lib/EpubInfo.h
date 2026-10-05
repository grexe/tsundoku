/*
 * SPDX-License-Identifier: MIT
 * Copyright (C) 2026 Gregor B. Rosenauer & Claude
 */
#ifndef _EPUB_INFO_H_
#define _EPUB_INFO_H_

#include <vector>

#include <String.h>
#include <SupportDefs.h>

// What an EPUB file says about itself: the metadata of its package document (OPF) and its cover. Rendering,
// the table of contents and the text are left to MuPDF; this reads the container with libzip and the metadata
// with libxml2.
class EpubInfo {
public:
	// Reads the metadata of the file, NULL if it is not an EPUB container with a package document.
	static EpubInfo* Read(const char* path);

	BString              title;
	std::vector<BString> authors;
	BString              language;
	BString              publisher;
	BString              identifier;
	BString              isbn;            // if one of the identifiers is an ISBN (urn:isbn: or the ISBN scheme)
	BString              date;            // as written: 2026, 2026-09 or 2026-09-01
	BString              description;
	std::vector<BString> subjects;
	BString              series;          // the collection the book belongs to
	BString              seriesIndex;     // its position in it, if given
	BString              version;         // of the EPUB standard, e.g. 3.0
	BString              pageProgression; // of the spine (page-progression-direction): ltr, rtl, or empty/default

	// The reading order: the files of the book (in the container) with the steps that a CFI takes to them: <spine> is
	// the child element number spineStep / 2 of <package>, an itemref the number step / 2 of <spine>.
	struct SpineItem {
		BString idref;
		BString path;      // of the content document in the container
		int32   step;
		bool    linear;
	};
	std::vector<SpineItem> spine;
	int32                  spineStep;

	// The cover image: the file inside the container and its type, empty if there is none.
	BString              coverMember;
	BString              coverType;

	BString              Authors() const;   // joined for display
	BString              Subjects() const;

	// The bytes of a file inside the container (the cover), false if it is missing or too large (> 16 MB).
	static bool          ReadMember(const char* path, const char* member, std::vector<uint8>* data);

private:
	EpubInfo() : spineStep(0) {}
};

#endif

/*
 * SPDX-License-Identifier: MIT
 * Copyright (C) 2026 Gregor B. Rosenauer & Claude
 */
#ifndef _COMIC_INFO_H_
#define _COMIC_INFO_H_

#include <vector>

#include <String.h>
#include <SupportDefs.h>

class BbfInfo;

// What a comic book archive says about itself in ComicInfo.xml, the metadata that ComicRack introduced and that
// the comic managers and taggers (ComicTagger, Calibre, Komga, ...) write. The pages are left to MuPDF.
class ComicInfo {
public:
	// Reads the metadata from the text of a ComicInfo.xml file, NULL if it is not one.
	static ComicInfo* Parse(const char* xml, size_t size);
	// The same from the metadata of a BBF file (Title, Author, Series, ...), NULL if it has none that is known.
	static ComicInfo* FromBbf(const BbfInfo& bbf);

	BString              title;
	BString              series;
	BString              number;           // of this issue in the series, as written (it can be 3, 3.5 or 3a)
	int32                count;            // of issues in the series, 0 if not given
	int32                volume;
	BString              summary;
	int32                year, month, day;    // 0 if not given
	std::vector<BString> writers;
	std::vector<BString> pencillers;
	std::vector<BString> inkers;
	std::vector<BString> colorists;
	std::vector<BString> letterers;
	std::vector<BString> coverArtists;
	std::vector<BString> editors;
	BString              publisher;
	std::vector<BString> genres;
	std::vector<BString> tags;
	BString              language;         // ISO code
	BString              ageRating;
	BString              web;
	int32                pageCount;
	bool                 rightToLeft;      // a manga, to be read from the right to the left
	bool                 topToBottom;      // a webtoon, to be read by scrolling down

	struct Page {
		int32   image;       // number of the page, from 0
		BString type;        // FrontCover, Story, Deleted, ...
		bool    doublePage;
	};
	std::vector<Page>    pages;

	BString              Authors() const;     // the writers, joined for display
	BString              Artists() const;     // pencillers, inkers and colorists, joined
	BString              Keywords() const;    // genres and tags
	BString              Date() const;        // 2026, 2026-09 or 2026-09-01, as far as it is given
	int32                CoverPage() const;   // the page marked as the front cover, else the first

private:
	ComicInfo()
		: count(0), volume(0), year(0), month(0), day(0), pageCount(0), rightToLeft(false), topToBottom(false) {}
};

#endif

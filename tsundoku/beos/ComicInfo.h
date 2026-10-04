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

#ifndef _COMIC_INFO_H_
#define _COMIC_INFO_H_

#include <vector>

#include <String.h>
#include <SupportDefs.h>

#include <mupdf/fitz.h>

// What a comic book archive says about itself in ComicInfo.xml, the metadata that ComicRack introduced and that
// the comic managers and taggers (ComicTagger, Calibre, Komga, ...) write. The pages are left to MuPDF.
class ComicInfo {
public:
	// Reads the metadata from the archive, NULL if it has no ComicInfo.xml.
	static ComicInfo* Read(fz_context* context, fz_archive* archive);

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
		: count(0), volume(0), year(0), month(0), day(0), pageCount(0), rightToLeft(false) {}
};

#endif

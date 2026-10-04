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


#ifndef _EPUB_CFI_H_
#define _EPUB_CFI_H_

#include <String.h>

class EpubInfo;

// EPUB Canonical Fragment Identifiers (https://idpf.org/epub/linking/cfi/): a path through the package document and
// the content document that is the same for every reading system and every layout, like an XPath. A mark in a book
// is a range: epubcfi(/6/4[chap01]!/4/10,/1:3,/3:12).
class EpubCfi {
public:
	// The range of the words (a text as MuPDF reads it from a page, in one line) in the content document number
	// spineIndex of the container. If the words are in it more than once, the place nearest to fraction (0 to 1, the
	// place in the text) is meant. False if they are not found in it.
	static bool Create(const char* container, const EpubInfo& info, int spineIndex, const char* words,
		float fraction, BString* cfi);

	// Where a CFI is: the content document, the words that it covers (for a CFI that is a point the words that
	// follow it) and the place in the text as a fraction. The assertions of the CFI (the id of an itemref or of an
	// element) are followed when the path does not lead to the same element any more.
	static bool Resolve(const char* container, const EpubInfo& info, const char* cfi, int* spineIndex,
		BString* words, float* fraction);
};

#endif

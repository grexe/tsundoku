/*
 * SPDX-License-Identifier: MIT
 * Copyright (C) 2026 Gregor B. Rosenauer & Claude
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
	// If prefix and suffix are given, they get the text before and after the words (about 32 characters of each).
	static bool Create(const char* container, const EpubInfo& info, int spineIndex, const char* words,
		float fraction, BString* cfi, BString* prefix = NULL, BString* suffix = NULL);

	// Where a CFI is: the content document, the words that it covers (for a CFI that is a point the words that
	// follow it) and the place in the text as a fraction. The assertions of the CFI (the id of an itemref or of an
	// element) are followed when the path does not lead to the same element any more.
	static bool Resolve(const char* container, const EpubInfo& info, const char* cfi, int* spineIndex,
		BString* words, float* fraction);
};

#endif

/*
 * SPDX-License-Identifier: MIT
 * Copyright (C) 2026 Gregor B. Rosenauer & Claude
 */
// Makes an EPUB of a Mobipocket book with lib/Mobi.cpp and lists what it found, on any system:
//   g++ -std=c++11 -I lib -o MobiTest tests/MobiTest.cpp lib/Mobi.cpp && ./MobiTest book.mobi book.epub
#include <stdio.h>

#include <fstream>
#include <sstream>

#include "Mobi.h"

int
main(int argc, char** argv)
{
	if (argc < 3) {
		fprintf(stderr, "usage: MobiTest book.mobi book.epub\n");
		return 2;
	}
	std::ifstream in(argv[1], std::ios::binary);
	std::stringstream buffer;
	buffer << in.rdbuf();
	std::string data = buffer.str();
	Mobi::Book book;
	Mobi::Result result = Mobi::Read((const unsigned char*)data.data(), data.size(), &book);
	printf("result %d: \"%s\", %zu author(s), %zu chapters, %zu images, %zu entries of contents, cover %d, language %s\n", result,
		book.title.c_str(), book.authors.size(), book.chapters.size(), book.images.size(), book.toc.size(), book.coverImage,
		book.language.c_str());
	if (result != Mobi::kOk)
		return 1;
	std::string epub = Mobi::MakeEpub(book);
	std::ofstream out(argv[2], std::ios::binary);
	out << epub;
	return 0;
}

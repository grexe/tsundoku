#!/bin/sh
# Builds the user guide from the Markdown chapters: HTML (one file), EPUB 3, PDF and DjVu, in build/.
# Needs pandoc, typst (the PDF engine) and pdf2djvu (DjVu); the formats whose tool is missing are skipped.
set -e
cd "$(dirname "$0")"
OUT=build
NAME=tsundoku-guide
mkdir -p "$OUT"
CHAPTERS="metadata.yaml $(ls [0-9][0-9]-*.md)"
COMMON="-f markdown+smart --number-sections --toc --toc-depth=2"

command -v pandoc >/dev/null || { echo "pandoc is needed"; exit 1; }

echo "HTML"
pandoc $CHAPTERS $COMMON -t html5 -s --embed-resources --css=guide.css -o "$OUT/$NAME.html"

echo "EPUB 3"
pandoc $CHAPTERS $COMMON -t epub3 --css=guide.css --epub-cover-image=../../images/tsundoku-logo_small.jpg \
	-o "$OUT/$NAME.epub"

if command -v typst >/dev/null; then
	echo "PDF"
	pandoc $CHAPTERS $COMMON --pdf-engine=typst -V papersize=a4 -V lang=en -o "$OUT/$NAME.pdf"
	if command -v pdf2djvu >/dev/null; then
		echo "DjVu"
		pdf2djvu -o "$OUT/$NAME.djvu" "$OUT/$NAME.pdf"
	else
		echo "pdf2djvu is missing: no DjVu"
	fi
else
	echo "typst is missing: no PDF, no DjVu"
fi

ls -l "$OUT"

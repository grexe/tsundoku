#!/bin/sh
# Builds the user guide from the Markdown chapters: HTML (one file), EPUB 3, PDF and DjVu, in build/, and the start page
# (start.typ, a PDF that opens when the program is started without a file).
# Needs pandoc, typst (the PDF engine) and pdf2djvu (DjVu); the formats whose tool is missing are skipped.
set -e
cd "$(dirname "$0")"
OUT=build
NAME=toji-guide
mkdir -p "$OUT"
# the logo is kept in images/ of the repository; the guide needs it next to its own pictures (not committed twice)
cp ../../images/toji-logo.png images/toji-logo.png
CHAPTERS="metadata.yaml $(ls [0-9][0-9]-*.md)"
COMMON="-f markdown+smart --number-sections --toc --toc-depth=2"

command -v pandoc >/dev/null || { echo "pandoc is needed"; exit 1; }

echo "HTML"
pandoc $CHAPTERS $COMMON -t html5 -s --embed-resources --css=guide.css -o "$OUT/$NAME.html"

echo "EPUB 3"
pandoc $CHAPTERS $COMMON -t epub3 --css=guide.css --epub-cover-image=../../images/toji-logo.png \
	-o "$OUT/$NAME.epub"

if command -v typst >/dev/null; then
	echo "Start page"
	typst compile --root ../.. start.typ "$OUT/toji-start.pdf"
	echo "PDF"
	# the cover (logo, "User Guide", the haiku) is the first page, instead of the title block
	{ echo '#set document(title: "Toji User Guide", author: "SEN Labs e.U.")'; echo '#page(numbering: none)['; sed 's/^#let subtitle = none/#let subtitle = [USER GUIDE]/' cover.typ; echo ']'; } > "$OUT/guide-cover.typ"
	pandoc $CHAPTERS $COMMON --pdf-engine=typst -V papersize=a4 -V lang=en -M title= -M subtitle= -M author= \
		-B "$OUT/guide-cover.typ" -o "$OUT/$NAME.pdf"
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

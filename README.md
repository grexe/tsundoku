<p align="center">
  <img src="Tsundoku.png" alt="Tsundoku" width="320">
</p>

# Tsundoku

*積ん読 (tsundoku): buying reading material and letting it pile up, unread, "for later".*

Tsundoku is a document reader for [Haiku](https://www.haiku-os.org), made for the papers and documents we collect
and mean to read someday. It is a fork of [BePDF](https://github.com/HaikuArchives/BePDF) that renders with
[MuPDF](https://mupdf.com) instead of XPDF: faster, with better quality, and with support for many more formats than
PDF (XPS, comics, images and more, as the user interface learns to handle them).

## Why a fork?

Tsundoku is the reader of [SEN](https://github.com/sen-laboratories) (Semantic Extensions Native), which adds semantic
relations between files and documents to Haiku. Reading is where relations become useful, so the reader needs to take
part in them:

- **Navigation:** SEN navigators hand over a target, e.g. the page of a bookmark or reference in a relation, and
  Tsundoku jumps right there. The target is passed as `bepdf:page_num`, the same message field BePDF understands, so
  both readers remain interchangeable for SEN.
- **Planned:** highlighting quoted passages, and more semantic extensions on top of that.

Such extensions do not belong into a general purpose reader, so this is an independent fork with its own name,
application signature (`application/x-vnd.sen-labs.Tsundoku`) and release cycle.

### What is different so far

- It renders with MuPDF, and the text can be selected like in any modern reader: drag with the secondary mouse
  button (or Ctrl + primary button) and the selection follows the text, double click selects a word, triple click a
  line. The selected text is copied right away. With the command key held the selection is a rectangle, which also
  copies the picture of that area.
- Searching finds the text in the order of reading and continues after the previous hit.
- The outline follows the current page: the chapter a page belongs to is selected and scrolled into view, also after a
  jump from another application.
- The side bar is wider by default, and it collapses on its own if the document has neither an outline nor bookmarks.
- Settings are stored in `~/config/settings/Tsundoku`, separate from BePDF. Everything else BePDF stores (bookmarks and
  the position per file in BFS attributes) is unchanged and compatible.

Not there yet, and planned: editing of annotations (BePDF could, Tsundoku shows the ones that are in the file),
the list of attachments, the fonts of a document in the file info. See [PLAN-mupdf.md](PLAN-mupdf.md).

### Features

  -  Viewing of encrypted and password protected PDF files.
  -  Opens file dropped on window.
  -  Navigation (with keyboard, toolbar, dragging with the mouse, mouse wheel, links).
  -  Displays annotations.
  -  Zooming (in/out, selecting a rectangle with the mouse).
  -  Rotating the page.
  -  Can show a page list and bookmarks (the outline of the document and your own).
  -  Window mode or fullscreen mode.
  -  Searching text.
  -  Copying text or graphics (via drag and drop to other applications (e.g. Tracker) and into the clipboard).
  -  Printing (range of pages; even or odd pages only; reverse or in order).
  -  Session management for documents on BFS (open file with the settings when it was last closed).
  -  Information (about the file, security).

## Building

On Haiku, with the development packages of the libraries MuPDF uses (they are not built, but taken from the system):

```
pkgman install freetype_devel harfbuzz_devel openjpeg_devel jbig2dec_devel brotli_devel libjpeg_turbo_devel
git clone https://github.com/grexe/tsundoku
cd tsundoku
./build.sh
```

This downloads MuPDF 1.28.5 to `3rd-party/` and builds its libraries once (this takes a while), then builds the
application into `dist/Tsundoku`, which needs to stay next to the `docs` folder there. To use another build of
MuPDF set `MUPDF_DIR`.

To build an installable package (`tsundoku-<version>-<arch>.hpkg`) from the result, run `./package.sh`. Copy it to
`~/config/packages` to install it for your user.

Tsundoku has no manual of its own yet. Since it works like BePDF, use the [BePDF manual](http://haikuarchives.github.io/BePDF/English/table_of_contents.html)
for now; "Help" in the application opens it as well.

Bug reports and ideas: [issues](https://github.com/grexe/tsundoku/issues).

## Credits and license

Tsundoku is free software under the GNU Affero General Public License, version 3 or any later version (see
`LICENSE`). It is based on BePDF, which is under the GNU GPL version 2 or any later version, and renders with MuPDF,
which is under the GNU AGPL version 3.

- © 2026 Gregor B. Rosenauer & Claude
- © 2013-2017 waddlesplash
- © 2000-2011 Michael Pfeiffer
- © 1998-2000 Hubert Figuiere
- © 1997 Benoit Triquet
- and the contributors to [BePDF](https://github.com/HaikuArchives/BePDF)

MuPDF, © Artifex Software, Inc. The notes on how it is used on Haiku are in [MUPDF-NOTES.md](MUPDF-NOTES.md).

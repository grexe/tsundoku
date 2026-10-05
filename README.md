<p align="center">
  <img src="images/tsundoku-logo_small.jpg" alt="Tsundoku" width="320">
</p>

# Tsundoku

*積ん読 (tsundoku): buying reading material and letting it pile up, unread, "for later".*

Tsundoku is a document reader for [Haiku](https://www.haiku-os.org), made for the papers and documents we collect
and mean to read someday. It is a fork of [BePDF](https://github.com/HaikuArchives/BePDF) that renders with
[MuPDF](https://mupdf.com) instead of XPDF: faster, with better quality, and with support for many more formats than
PDF: EPUB books, comics (CBZ, CBR, CB7, CBT, BBF), DjVu, XPS and images.

## Formats

| Format | Notes |
|--------|-------|
| PDF | encrypted and password protected files too; annotations are real PDF annotations |
| EPUB | laid out for a text size, anchored marks and bookmarks, metadata in file attributes |
| Comics: CBZ, CBR, CB7, CBT, BBF | double pages, manga (right to left) and webtoons (top to bottom), `ComicInfo.xml` |
| DjVu | text layer, outline, links, metadata, text marks |
| FictionBook (`.fbz`, `.fb2.zip`), XPS, images | through MuPDF |

How each format is read, and the file types: [docs/reference/formats.md](docs/reference/formats.md).

## Features

### Reading

- Pages shown one at a time, two side by side like a book ("Double-sided", the title page can stay alone), or all one below
  the other and scrolled through ("Continuous"). The buttons next to the fit buttons and the View menu switch between them.
- Zooming (in and out, or a rectangle chosen with the mouse), fit to page or width, rotating the page.
- Navigation with the keyboard, the toolbar, dragging, the mouse wheel and links (also those that lead to another page).
- Window mode or fullscreen. Files can be dropped on the window.
- The position and settings per file are kept in BFS attributes and used when the file is opened again.
- Several documents at once: a document is shared by the view and the threads that work on it, so another file can be opened
  in the window while the pages of the first are still rendered in the background.

### The sidebar

Switch it with the icon tabs or Cmd+1 to Cmd+4, show or hide it with Cmd+H.

- **Bookmarks:** the outline of the document and your own (annotations with the motivation `oa:bookmarking`). The outline
  follows the current page.
- **Pages:** the page list. For books it is an outline of chapters.
- **Attachments:** the files embedded in a PDF, which can be saved.
- **Annotations:** all annotations in columns for page, type and the text they mark or hold, sorted by clicking a title;
  choosing one goes there and selects it.

The sidebar is wider by default and collapses on its own if the document has neither an outline nor bookmarks.

### Selecting, copying and searching

- The text can be selected without a mode to switch: hold Option (or Alt) and drag, and the selection follows the text. Double
  click selects a word, triple click a line. The selected text is copied right away. With Shift as well the selection is a
  rectangle, which also copies the picture of that area. The cursor is an I-beam while Option or Cmd is held.
- Dragging without a key moves the page; the secondary button opens a menu (copy, select all, the link actions).
- A selection can run over several pages (the continuous flow, or the two pages of a spread): it is copied or marked page by
  page (a mark on each page). It stays when you zoom or rotate.
- Searching finds the text in the order of reading, continues after the previous hit (also backwards, Shift+G) and shows all hits
  on the page.
- Copy text or graphics to the clipboard, or by drag and drop to other applications (Tracker too).

### Annotating

- **Marks on text:** highlight, underline, strike out (Edit menu or the menu of the secondary button, with a choice of colors,
  each shown with a sample). From the menu of an existing mark its color and its note can be changed, or the mark deleted; the
  note shows as a tooltip when the mouse rests on the mark.
- **More than text:** Edit > Add (and the same entry in the menu of the secondary button) creates a note, text on the page, a
  rectangle, an ellipse, a line, an arrow or a freehand drawing. Choose one, then click (note, text) or drag (the others) on the
  page; the cursor is a cross until then, and Escape cancels.
- **Editing:** click a note, text, shape, line or drawing to select it; drag it to move it, drag a handle to resize it, press
  Delete to remove it, Escape to let go.
- **Undo and redo** (Cmd+Z, Cmd+Shift+Z) take back and repeat the changes one by one and go to the page they were made on; the
  history ends when the document is saved.
- **Saving:** File > Save (Cmd+S) adds the changes to the end of a PDF file, so its attributes stay as they are. For a file that
  cannot be written (system directory, the title says "read-only") or that MuPDF had to repair, Save and File > Save as…
  (Cmd+Shift+S) write a copy, with the attributes of the original, and Tsundoku goes on with the copy.
- **Where they are kept:** annotations of a PDF are PDF annotations that other readers show. Books, comics and DjVu cannot take
  them in the file, so they are kept in the attribute `SEN:annotations`, and they go along when the file is copied.

What is available where:

| | Text marks | Notes, text, shapes, drawings |
|---|---|---|
| PDF | yes | yes |
| EPUB | yes | no (the pages change with the text size) |
| Comics | no (no text) | yes |
| DjVu | yes | yes |

The model, the storage and the access from other programs: [docs/reference/annotations.md](docs/reference/annotations.md).

### Books

- A book is laid out as pages of 6 by 9 inches for a text size: View > Larger text, Smaller text (Cmd+T, Cmd+Shift+T). The reader
  stays where the text was and the text size is kept in the settings.
- Bookmarks, marks and the place where you stopped reading are tied to the text, not to a page, so they are found again for
  every text size.
- Page numbers (a bookmark of your own, the page to come back to) are only right for the size they were made at.

### Comics

- Double-sided flow that follows how comics are made: a page wider than high is a spread of its own.
- **View > Right to left** for manga: the places of the pages in a spread are swapped, the pages themselves are not mirrored.
- **View > Top to bottom** for webtoons: an endless strip as wide as the window. It is set automatically for a comic that says it
  is a webtoon or whose first pages are very tall.
- The metadata of `ComicInfo.xml` and BBF shows in File info with the cover.

### Scripting

Other programs (and `hey`) can read and change the annotations and bookmarks of the document in a window, go to a page or to an
annotation, and save: `hey Tsundoku do AddAnnotation of Document of Window 0 with kind=highlight and quote="the words" and
page=3`. See [docs/reference/scripting.md](docs/reference/scripting.md).

### Printing and information

- Printing (range of pages, even or odd pages only, reverse or in order).
- File info (about the file, the metadata, security).

### Files and metadata

What a document says about itself (title, author, series, language, publisher, date, ISBN, ...) is written to separate BFS
attributes of the file with the names of the established ontologies, so that Tracker can show them as columns and queries can
find them. Settings are stored in `~/config/settings/Tsundoku`, separate from BePDF.

The names and where each value comes from: [docs/reference/metadata.md](docs/reference/metadata.md).

## Why a fork?

Tsundoku is the reader of [SEN](https://github.com/sen-laboratories) (Semantic Extensions Native), which adds semantic
relations between files and documents to Haiku. Reading is where relations become useful, so the reader needs to take
part in them:

- **Navigation:** SEN navigators hand over a target, e.g. the page of a bookmark or reference in a relation, and
  Tsundoku jumps right there. The target is a Web Annotation target (`oa:hasTarget`: a page, a region on it, the words, an EPUB CFI) or the identifier of
  an annotation.
- **Annotations as data:** annotations are described with the W3C Web Annotation model and kept where SEN can find them, even
  without opening the document; every annotation has an identifier, and a scripting suite lets other programs read and change
  them.

Such extensions do not belong into a general purpose reader, so this is an independent fork with its own name,
application signature (`application/x-vnd.sen-labs.Tsundoku`) and release cycle.

## Status

Tsundoku is in preview: formats and attribute names may still change before 1.0.

- **Not there yet:** line widths and fill colors, the fonts of a
  document in the file info, fixed-layout EPUB, DRM, JPEG XL and HEIC pages (no translator on Haiku), margin notes.
- See [PLAN-mupdf.md](PLAN-mupdf.md) for the plans and design notes.

## Installing

Tsundoku and the MuPDF library it needs are in the package repository of SEN Labs:

```
pkgman add-repo https://kiri.sen-labs.org/x86_64
pkgman install tsundoku
```

Each release on GitHub also has the package as a file.

## Building

With the repository above added, `pkgman install mupdf1.28_devel libarchive_devel djvu_devel libzip_devel libxml2_devel freetype_devel` is all that is needed, and
`./build.sh` uses it. Without it, the build gets MuPDF itself, and the development packages of the libraries MuPDF
uses are taken from the system:

```
pkgman install libarchive_devel djvu_devel libzip_devel libxml2_devel freetype_devel harfbuzz_devel openjpeg_devel jbig2dec_devel brotli_devel libjpeg_turbo_devel
git clone https://github.com/sen-laboratories/tsundoku
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

Bug reports and ideas: [issues](https://github.com/sen-laboratories/tsundoku/issues).

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

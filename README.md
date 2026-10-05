<p align="center">
  <img src="Tsundoku.png" alt="Tsundoku" width="320">
</p>

# Tsundoku

*積ん読 (tsundoku): buying reading material and letting it pile up, unread, "for later".*

Tsundoku is a document reader for [Haiku](https://www.haiku-os.org), made for the papers and documents we collect
and mean to read someday. It is a fork of [BePDF](https://github.com/HaikuArchives/BePDF) that renders with
[MuPDF](https://mupdf.com) instead of XPDF: faster, with better quality, and with support for many more formats than
PDF (EPUB books, XPS, comics, images and more, as the user interface learns to handle them).

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

- It renders with MuPDF, and the text can be selected like in other readers, without a mode to switch: hold Option
  (or Alt) and drag, and the selection follows the text. Double click selects a word, triple click a line. The
  selected text is copied right away. With Shift as well the selection is a rectangle, which also copies the picture
  of that area. Dragging without a key still moves the page, and the secondary button opens a menu (copy, select
  all, and the link actions).
- Searching finds the text in the order of reading and continues after the previous hit (also backwards, Shift+G), and
  shows all hits on the page. A selection stays when you zoom or rotate.
- Selected text can be highlighted, underlined or struck out (Edit menu or the menu of the secondary button, with a
  choice of colors, each shown with a sample). The marks are real PDF annotations, other readers show them. From the
  menu of an existing mark its color and its note can be changed, or the mark deleted; the note shows as a tooltip when the mouse rests on the mark.
  More than text can be marked: Edit > Add (and the same entry in the menu of the secondary button) creates a note, text
  on the page, a rectangle, an ellipse, a line, an arrow or a freehand drawing. Choose one, then click (note, text) or
  drag (the others) on the page; the cursor is a cross until then, and Escape cancels. A note or text from the
  menu of the secondary button goes where that menu was opened. Their color can be changed there too.
  Click a note, text, shape, line or drawing to select it: drag it to move it, drag a handle to resize it, press
  Delete to remove it, Escape to let go. Every annotation gets a unique name (the `/NM` of the PDF) when it is
  created, so that other programs, SEN in particular, can refer to it later.
  Edit > Undo and Redo (Cmd+Z, Cmd+Shift+Z) take back and repeat these changes one by one and go to the page they
  were made on; the history ends when the document is saved.
  File > Save (Cmd+S) adds the changes to the end of the file, so its attributes stay as they are. For a file that
  cannot be written (system directory, the title says "read-only") or that MuPDF had to repair, Save and
  File > Save as… (Cmd+Shift+S) write a copy, with the attributes of the original, and Tsundoku goes on with the copy. The
  cursor becomes an I-beam while Option or Cmd is held, to show that dragging selects.
- Annotations and deep links are described with the [W3C Web Annotation Data Model](https://www.w3.org/TR/annotation-model/)
  (`oa:`), for PDF files and EPUB books alike: an annotation has a target (the document and selectors that say where in
  it: `oa:FragmentSelector` with `page=5` or an EPUB CFI, `oa:TextQuoteSelector` with the words and some text around
  them, `oa:SvgSelector` for shapes and drawings, which can be refined by one another with `oa:refinedBy`), perhaps a
  body (`oa:TextualBody`, the note), a style (`oa:CssStyle`) and a motivation: `oa:highlighting`, `oa:commenting`, and
  `sen:underline`, `sen:strikethrough` and `sen:squiggle` for the other marks of a text (SEN's own `oa:Motivation`s,
  which are `skos:broader oa:highlighting`). "Copy as Web Annotation" in the menu of an annotation puts it on the
  clipboard as JSON-LD. The marks of an EPUB are stored in this form (the attribute `SEN:annotations`); the
  annotations of a PDF file stay PDF annotations in the file; they are described in this form when they are handed on, and when the file
  is saved or a copy is made they are written to `SEN:annotations` of the file too (with `SEN:annotationCount`), so that they can be found
  and used without opening the PDF file. If SEN knows the file (it has a `SEN:ID`, its identifier in the personal knowledge graph), that
  identifier is the `oa:hasSource` of the targets, both in the attribute and when an annotation is handed on; otherwise the
  attribute has no source (the annotations are about the file that they are stored with) and a handed-on annotation has the
  `file:` IRI.
  Other applications (SEN) open a document at a place with `B_REFS_RECEIVED`: the file in `refs`, and
  `oa:hasTarget` (a message with `oa:hasSelector` entries as in the model, any of the selectors above that the document
  understands; an EPUB CFI is found in whatever layout the book has, the words are searched for from the page that
  was named), and with `oa:motivatedBy` the passage that the words name is also marked (not saved); or
  `oa:Annotation` with the identifier of an annotation (`urn:uuid:...`): the document goes there and selects it.
- The threads that render pages work for the document, not for the window: a document is shared (reference counted) by the view and
  by every thread that works on it, so another file can be opened in the window at once, and the pages of the first one that
  are still being rendered finish in the background (and so does the list of its annotations).
- The pages can be shown one at a time, two side by side like the pages of a book ("Double-sided"; the title page can
  stay alone, View > Title page alone), or all one below the other and scrolled through ("Continuous"). The buttons
  next to the fit buttons and the View menu switch between them; next and previous page go by a spread. In the code
  the arrangements are presets of a grid of columns and rows (`PageLayout`), so more of them (four pages at a time,
  two columns of scrolling pages) are a line in a table; the four-fold one is in the table already, without a button.
- The sidebar is switched with icon tabs (bookmarks, page list, attachments, annotations; the tooltip names them) or
  with Cmd+1 to Cmd+4, and shown or hidden with the button next to the fullscreen button (Cmd+H). The annotations tab
  lists all annotations of the document in columns for page, type and the text they mark or hold, sorted by clicking a
  title; choosing one goes there and selects it. View > Hide sidebar (it reads Show sidebar when the sidebar is hidden)
  sits with Fullscreen.
- Comic books open like PDF files: CBZ (ZIP), CBR (RAR), CB7 (7z) and CBT (TAR, also compressed with gzip, xz or
  bzip2). MuPDF reads ZIP and TAR and draws the pages; RAR, 7z and compressed TAR come from libarchive, which is
  added to MuPDF as an archive handler. What file managers put into an archive (`__MACOSX`, `._*`, `.DS_Store`,
  `Thumbs.db`) is not taken for pages, and pages in WebP or AVIF, which MuPDF cannot read, are converted by the
  translators of Haiku when they are shown. The metadata of `ComicInfo.xml` (ComicRack, ComicTagger, Calibre,
  Komga: title, series and number, writers, artists, publisher, date, genres, language, summary) is written to the
  same attributes as for books, and File info shows it with the cover (the page marked as such, else the first).
  Haiku sniffs the content before it looks at the extension, and a CBZ or CBR has no mark of its own (it is a ZIP or
  RAR file), so the comic types have a sniffer rule: the first file of the archive is a page (or ComicInfo.xml), which
  its header near the start of the file tells. A ZIP that starts with an image is a comic to Tracker, too. CB7 has no
  such place, it is known by its extension. Zipped FictionBooks (`.fbz`, `.fb2.zip`) open as well.
  Comics are annotated with the same tools as PDF files (Add: note, text, rectangle, ellipse, line, arrow, drawing; move,
  resize, undo, the list of annotations), for there is no text to mark. A comic archive cannot take annotations, so they
  are kept in the attribute `SEN:annotations` of the file, as Web Annotations: the target is the page (`page=3`, an
  `oa:FragmentSelector`) refined by a rectangle as a media fragment (`xywh=percent:10,20,30,40`) or, for ellipses, lines,
  arrows and drawings, by an `oa:SvgSelector` whose `viewBox` is the page in percent (`0 0 100 100`,
  `preserveAspectRatio="none"`), so that it fits the page at any size. `sen:shape` says what was drawn, notes and text
  have the motivation `oa:commenting` and the note as the body. Save a copy writes them to the copy as well.
  Comics in the double-sided flow follow how comics are made: a page that is wider than high (or marked `DoublePage`
  in `ComicInfo.xml`) is a spread of its own and fills the view, and the page before it stands alone if it has no
  partner. View > Right to left reads a manga the other way: only the places of the pages in a spread are swapped (the
  first page on the right, the title page alone on the left), the pages themselves are not mirrored. A manga says so in
  `ComicInfo.xml` (`Manga`) and an EPUB says so in its spine (`page-progression-direction`), and the choice is kept with
  the file in the attribute `SEN:readingProgression` (named after the W3C Publication Manifest property, with SEN's prefix
  since no ontology has one for a BFS attribute; `rtl`, `ltr` or `default`, which is what the document says;
  a document that says `rtl` has the attribute, so that it can be found). A webtoon or a manhua that is a strip is read
  from the top to the bottom (`ttb`, also a value of that property): a comic with `Webtoon` as its format, genre or tag in
  `ComicInfo.xml`, or whose first pages are all at least three times as high as they are wide, opens in the continuous flow with
  its strips one against the next and as wide as the window, without changing the flow that is set for other documents;
  View > Top to bottom switches it for a file.
- The Bound Book Format (`.bbf`, version 3, [libbbf](https://github.com/ef1500/libbbf)) is read as a comic book, too.
  It has the pages in a table in reading order, so one is found without reading the file; it is a MuPDF archive handler
  of its own (the format is small, and only its index is parsed: `BbfInfo`, which does not depend on MuPDF). The sections
  of the file are the outline of the document, its metadata (Title, Author, Series, Publisher, Language, Year,
  Description, ...) goes to the same attributes as the metadata of `ComicInfo.xml`. The hashes are not checked. The type
  `application/x-bbf` has the mark of the format as its sniffer rule.
- A deep link to a place on a page (`xywh=` or an `oa:SvgSelector`, whose bounding box is taken, in the units of its `viewBox`
  relative to the page) goes to the page and marks the place for a moment.
- DjVu files open like PDF files. DjVuLibre draws the pages at the size that is asked for (as a document handler for MuPDF,
  `DjvuDocument.cpp`, so everything above it works as it does for other documents): the hidden text layer is put into the
  page as invisible text, so search, selection and copy work; the outline, the hyperlinks (also those that lead to another
  page of the file) and the metadata (title, author, keywords) are those of the file; highlight, underline and strike out
  work on the words of the text layer, and notes, text, shapes and drawings are annotations of the same kind as those of comics
  (all of them in `SEN:annotations`; a mark on words is kept as the page and the quoted words). A document that is in several files (an index with files next to it) is read as well, pages that are turned are
  turned (and their text with them); the year in the metadata is the date (`dc:date`).
  DjVuLibre has a bug on Haiku that this works around (see the note in `DjvuDocument.cpp`): a once-only initialisation that
  does not happen, so that it stores its data of a thread under a key that belongs to someone else.
- EPUB books open like PDF files. A book has no fixed pages, so it is laid out as pages of 6 by 9 inches for a text
  size (View > Larger text, Smaller text, Cmd+T and Cmd+Shift+T); the reader stays where the text was, and the
  text size is kept in the settings. MuPDF lays out and draws the book; its metadata is read from the package
  document with libzip and libxml2 (title, authors, series, language, publisher, date, identifier, subjects, the
  description and the cover show in File info). The EPUB type is made known to Haiku by its first file, so that
  an EPUB is not taken for a web page; which application opens it is left to you.
  The page list of a book is an outline: chapters, with the pages of the chapter that is read below it. The text
  size is changed in a thread of its own, so the window does not freeze; if it takes more than a moment a small
  window with a barber pole says that the book is laid out. Quitting or opening another file gives the layout up
  (after the chapter that is being laid out); only what works with the pages of this book waits. What a book says about itself is written to separate
  BFS attributes of the file, and nothing is invented for books: the names are the properties of the established
  ontologies with the prefix that is commonly used for each: `dc:` (Dublin Core: `dc:description`, `dc:publisher`,
  `dc:language`, `dc:date`, `dc:identifier`), `dcterms:` (`dcterms:isPartOf`, the series) and `schema:`
  ([schema.org/Book](https://schema.org/Book): `schema:isbn`, `schema:position`, the number in the series); other
  prefixes such as `foaf:` come the same way when they are needed. The attributes of `application/pdf` stay as they
  are (`META:title`, `META:author`, `META:keyw`, `META:pages`, they are dc:title, dc:creator, dc:subject and
  schema:numberOfPages) and are written for books as well, so that a column or a query works for both. They are
  defined for the type (so Tracker offers them as columns), and the indices are made on the volume. `META:pages` is
  the number of pages in the standard configuration (6 by 9 inches, the default text size), an estimate that stays
  the same for inventory and citations, as the shops give it for an e-book; it is not written when the book is first
  opened at another text size. `SEN:annotationCount` (the number of annotations, not there if there are none; an
  integer, so that it can be indexed) says for any PDF or EPUB file how many it has. The prefix `META:` is only kept for
  the attributes that exist already, those of PDF files.
  Places in a book are anchors in the manner of the W3C Web Annotation model, and not page numbers, which change with
  the text size: the words at the place (a text quote), its [EPUB CFI](https://idpf.org/epub/linking/cfi/) (the
  standard path through the package and the content document, e.g. `epubcfi(/6/4[chap01]!/4/10,/1:3,/3:12)`, which
  other reading systems understand), and where it was (chapter, place in the chapter) to look there first. A mark
  keeps its anchor in `SEN:annotations`, a bookmark of yours in an extra `a` entry of `bepdf:bookmarks` (BePDF
  ignores it), and the place where you stopped reading in `bepdf:anchor`; they find their page again for the text
  size the book is shown at. If the words are not found any more (another version of the book), the CFI, which names
  a chapter by the id of its entry in the reading order, leads to the words that are there now.
  Text can be highlighted, underlined and struck out in books as well (the other kinds of annotations are for
  pages that stay as they are). A mark is tied to the text, not to a page: the chapter, where on the page it was and
  the words it covers. So it is found again when the pages change with the text size, and a mark that runs over a
  page break is drawn on both pages. The marks are kept in the attribute `SEN:annotations` of the EPUB file itself (a standard message that other applications can use for
  the same purpose; the names `META:annotations` and `tsundoku:annotations` of early versions are still read),
  like the styles of StyledEdit are, and not in a file of their own: they go along when the file is copied in
  Tracker, and File > Save as… copies them with the book. Save, Undo and Redo work as for PDF files; a book on a
  read-only volume is saved as a copy.
- The files embedded in a PDF are listed in View > Show attachments and can be saved.
- The outline follows the current page: the chapter a page belongs to is selected and scrolled into view, also after a
  jump from another application.
- The side bar is wider by default, and it collapses on its own if the document has neither an outline nor bookmarks.
- Settings are stored in `~/config/settings/Tsundoku`, separate from BePDF. Everything else BePDF stores (bookmarks and
  the position per file in BFS attributes) is unchanged and compatible.

Known limits: in the continuous flow a text selection stops at the end of the page it was started on. In books, the
pages follow the text size, so a page number (a bookmark of your own, the page to come back to) is only right for
the size it was made at, and notes, shapes and drawings are PDF only.

Not there yet, and planned: line widths and fill colors, scripting of annotations from other programs, the fonts of a document in
the file info. See [PLAN-mupdf.md](PLAN-mupdf.md).

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

# Toji: a semantic, multi-format document reader for Haiku (PDF, EPUB, comics, DjVu)

Hey everyone,

I'd like to introduce a project I have been working on for Haiku: **Toji** (綴じ, "binding": the way a book is bound). It was called
*Tsundoku* until recently, and started as a fork of the classic BePDF. Many thanks to its authors, Michael Pfeiffer, Hubert
Figuiere, Benoit Triquet, waddlesplash and everyone who contributed over the years. Toji has grown into a modern, standards-based
reader that uses what Haiku is good at: attributes, queries and messages. It is now in **beta**, and I'd be glad for testers.

![Toji with a PDF: outline, marks and margin notes](guide/images/main-window.png)

## Formats

- **PDF**, rendered with MuPDF (faster and better than the XPDF of BePDF), also encrypted files.
- **EPUB** 2 and 3: laid out for a text size (pages stay where your reading is when you change it). Not yet: fixed-layout EPUB and
  DRM.
- **Comic books:** CBZ, CBR, CB7, CBT and the new Bound Book Format (BBF), with double pages, manga reading order (right to left)
  and webtoons (top to bottom), `ComicInfo.xml` metadata and the cover.
- **DjVu**, with its text layer: search, selection and marks work.
- XPS, FictionBook and pictures through MuPDF.

## Annotating

Highlight, underline and strike out text in colors, add **margin notes**, free notes, text, rectangles, ellipses, lines, arrows
and drawings with line width and fill. Toolbar buttons arm a tool (marker colors, note, shapes), and text can be selected over
several pages. In a PDF the annotations are normal PDF annotations that other readers show; for EPUB, comics and DjVu they are kept
with the file.

## Semantic features and BFS

- **Annotations as W3C Web Annotations:** every annotation is described in the W3C Web Annotation model (targets with page,
  region, quoted words or EPUB CFI), stored as BFS attributes of the file (`SEN:annotations`), so other programs can find and
  use them without opening the document. Bookmarks are annotations, too. Every annotation has a stable identifier.
- **Native BFS metadata:** title, author, series, language, ISBN, date, the number of pages and more are written as BFS attributes
  with standard names (`dc:title`, `dc:creator`, `schema:isbn`, ...), so Tracker can show them as columns and queries can find
  files. Where you stopped reading is kept with the file.
- **Places in documents:** Toji opens a document at an exact place (a page, a region, words, an EPUB position, an annotation) with
  the standard fragments, from the command line (`Toji <file>#page=5`), from a message or with `Goto` in the scripting suite.
  **Copy link to this place** makes a `toji:` link for it. These links hold a file path, which can change; stable links that name a
  document by its SEN:ID (a `sen://` handler) are coming with SEN.
- **Scripting:** the document, its pages, annotations and bookmarks can be read and changed with `hey Toji ...` or with scripting
  messages from your own programs (get and set the note or the words of a mark, create annotations, go to a place, save).

## A showcase for SEN

Toji is a daily reader and an entry point to **SEN (Semantic Extensions Native)**, which brings semantic relations to Haiku files
and documents. It shows how file attributes, open standards and messages can work together on Haiku. More about SEN later this
year.

## Install

```
pkgman add-repo https://kiri.sen-labs.org/x86_64
pkgman install toji
```

The libraries it needs (MuPDF, libarchive, libzip, libxml2, DjVuLibre) come with the package. A **user guide** (PDF, EPUB, DjVu,
HTML) is built from Markdown and installed with it; **Help** opens it.

## Licenses

Toji is free software under the **AGPL-3.0-or-later** (MuPDF is AGPL, BePDF GPL-2+). The reusable parts (the Web Annotation model,
bookmarks, the EPUB, ComicInfo and BBF readers, EPUB CFI, links) are in a separate folder, `lib/`, under the **MIT license**, so
other programs can use them. The artwork is CC BY 4.0, and every file has an SPDX identifier (REUSE compliant).

## Source and status

👉 https://github.com/sen-laboratories/toji

It is a beta: the features are in and usable, and are being tested on Haiku R1/beta6. Bug reports, ideas and feedback are very
welcome, especially from people who read EPUB or comics on Haiku, work with scanned DjVu documents, or want to script their
documents.

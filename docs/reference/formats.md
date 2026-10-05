# Formats: how they are read

Toji renders with [MuPDF](https://mupdf.com); other libraries add what MuPDF does not read. For what is supported at a
glance see the [README](../../README.md#formats).

## Comic books

CBZ (ZIP), CBR (RAR), CB7 (7z) and CBT (TAR, also compressed with gzip, xz or bzip2) open like PDF files.

- **Archives:** MuPDF reads ZIP and TAR and draws the pages. RAR, 7z and compressed TAR come from libarchive, which is added to
  MuPDF as an archive handler. What file managers put into an archive (`__MACOSX`, `._*`, `.DS_Store`, `Thumbs.db`) is not taken
  for pages.
- **Pages:** WebP and AVIF, which MuPDF cannot read, are converted by the translators of Haiku when they are shown.
- **Zipped FictionBooks** (`.fbz`, `.fb2.zip`) open as well.
- **Metadata:** `ComicInfo.xml` fills the attributes described in [metadata.md](metadata.md).

### File types

Haiku sniffs the content before it looks at the extension, and a CBZ or CBR has no mark of its own (it is a ZIP or RAR file), so
the comic types have a sniffer rule: the first file of the archive is a page (or `ComicInfo.xml`), which its header near the
start of the file tells. A ZIP that starts with an image is a comic to Tracker, too. CB7 has no such place, it is known by its
extension.

| Type | Format |
|------|--------|
| `application/vnd.comicbook+zip` | CBZ |
| `application/vnd.comicbook-rar` | CBR |
| `application/x-cb7` | CB7 |
| `application/x-cbt` | CBT |
| `application/x-bbf` | BBF (the mark of the format) |

### Double pages, manga and webtoons

- In the double-sided flow a page that is wider than high (or marked `DoublePage` in `ComicInfo.xml`) is a spread of its own and
  fills the view, and the page before it stands alone if it has no partner.
- **View > Right to left** reads a manga the other way: only the places of the pages in a spread are swapped (the first page on
  the right, the title page alone on the left), the pages themselves are not mirrored.
- A webtoon or a manhua that is a strip is read from the top to the bottom: a comic with `Webtoon` as its format, genre or tag in
  `ComicInfo.xml`, or whose first pages are all at least three times as high as they are wide, opens in the continuous flow with
  its strips one against the next and as wide as the window, without changing the flow that is set for other documents.
  **View > Top to bottom** switches it for a file.

How the direction is kept: [metadata.md](metadata.md#reading-direction).

## Bound Book Format

`.bbf` (version 3, [libbbf](https://github.com/ef1500/libbbf)) is read as a comic book. It has the pages in a table in reading
order, so one is found without reading the file. It is a MuPDF archive handler of its own (the format is small, and only its
index is parsed: `BbfInfo`, which does not depend on MuPDF). The sections of the file are the outline of the document. The hashes
are not checked.

## DjVu

DjVu files open like PDF files. DjVuLibre draws the pages at the size that is asked for (as a document handler for MuPDF,
`DjvuDocument.cpp`, so everything above it works as it does for other documents).

- The hidden **text layer** is put into the page as invisible text, so search, selection and copy work, also on pages that are
  stored turned.
- The **outline**, the **hyperlinks** (also those that lead to another page of the file) and the **metadata** are those of the
  file.
- **Annotations:** highlight, underline and strike out work on the words of the text layer; notes, text, shapes and drawings are
  annotations of the same kind as those of comics (see [annotations.md](annotations.md)).
- A document that is in several files (an index with files next to it) is read as well.
- DjVuLibre has a bug on Haiku that this works around (see the note in `DjvuDocument.cpp` and [TODO.md](../../TODO.md)): a
  once-only initialisation that does not happen, so that it stores its data of a thread under a key that belongs to someone
  else.

## EPUB

- A book has no fixed pages, so it is laid out as pages of 6 by 9 inches for a text size (View > Larger text, Smaller text,
  Cmd+T and Cmd+Shift+T). The reader stays where the text was, and the text size is kept in the settings. MuPDF lays out and draws
  the book.
- The text size is changed in a thread of its own, so the window does not freeze; if it takes more than a moment a small window
  with a barber pole says that the book is laid out. Quitting or opening another file gives the layout up (after the chapter
  that is being laid out).
- The **page list** of a book is an outline: chapters, with the pages of the chapter that is read below it.
- The EPUB type is made known to Haiku by its first file, so that an EPUB is not taken for a web page; which application opens it
  is left to you.
- Marks, bookmarks and the reading position are tied to the text, not to a page, so they are found again for every text size
  ([metadata.md](metadata.md#anchors-in-books)). A mark that runs over a page break is drawn on both pages.
- Only text marks (highlight, underline, strike out) are for books; notes, shapes and drawings need pages that stay as they are.

## Other

PDF, XPS and images open through MuPDF. Files embedded in a PDF are listed in View > Show attachments and can be saved.

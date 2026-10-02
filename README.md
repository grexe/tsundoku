<p align="center">
  <img src="Tsundoku.png" alt="Tsundoku" width="320">
</p>

# Tsundoku

*積ん読 (tsundoku): buying reading material and letting it pile up, unread, "for later".*

Tsundoku is a document reader for [Haiku](https://www.haiku-os.org), made for the papers and documents we collect
and mean to read someday. It is a fork of [BePDF](https://github.com/HaikuArchives/BePDF) and, like BePDF, based on
[XPDF](http://www.foolabs.com/xpdf/) 4.0. It handles PDF files up to PDF version 1.7 (Adobe Reader 9+).

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

- The outline follows the current page: the chapter a page belongs to is selected and scrolled into view, also after a
  jump from another application.
- The side bar is wider by default, and it collapses on its own if the document has neither an outline nor bookmarks.
- Settings are stored in `~/config/settings/Tsundoku`, separate from BePDF. Everything else BePDF stores (bookmarks and
  the position per file in BFS attributes, annotations in the PDF files) is unchanged and compatible.

### List of features

  -  Viewing of encrypted and password protected PDF 1.7 files.
  -  Opens file dropped on window.
  -  Navigation (with keyboard, toolbar, dragging with the mouse, mouse wheel, links).
  -  Displays annotations.
  -  Zooming (in/out, selecting a rectangle with the mouse).
  -  Rotating the page.
  -  Can show a page list, bookmarks and attachments.
  -  Window mode or fullscreen mode.
  -  Japanese, Chinese (simplified, traditional) and Korean font support.
  -  Renders embedded fonts (Type 1, Truetype) with FreeType 2 library.
  -  Multithreaded (rendering is done in a separate thread).
  -  Editing (Annotations can be added to an unencrypted PDF file)
  -  File Attachment Annotations and Attachments can be saved.
  -  Printing (range of pages; even or odd pages only; reverse or in order).
  -  Searching text.
  -  Copying text or graphics (via drag and drop to other applications (e.g. Tracker) and into the clipboard).
  -  Session management for PDF files on BFS (open file with the settings when it was last closed).
  -  Information (about the file, security, fonts used).

## Building

On Haiku, with the FreeType development files installed (`pkgman install freetype_devel`):

```
git clone https://github.com/grexe/tsundoku
cd tsundoku
./build.sh
```

This builds the XPDF library and the application into `dist/Tsundoku`, which needs to stay next to the `docs`, `fonts`
and `encodings` folders there. The script also downloads the user manual, which is still the one of BePDF. It will be replaced once Tsundoku
differs from BePDF enough to warrant its own.

To build an installable package (`tsundoku-<version>-<arch>.hpkg`) from the result, run `./package.sh`. Copy it to
`~/config/packages` to install it for your user.

Bug reports and ideas: [issues](https://github.com/grexe/tsundoku/issues).

## Credits and license

Tsundoku is free software under the GNU GPL v2, or any later version.

- © 1997 Benoit Triquet
- © 1998-2000 Hubert Figuiere
- © 2000-2011 Michael Pfeiffer
- © 2013-2017 waddlesplash
- and the contributors to [BePDF](https://github.com/HaikuArchives/BePDF)

Based on XPDF, © Glyph & Cog, LLC. See the files in `xpdf/` and `dist/license/` for their license.

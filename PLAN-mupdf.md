# Plan: replace XPDF with MuPDF

Goal: render with [MuPDF](https://mupdf.com) instead of XPDF, for speed, quality and many more formats (PDF, XPS, CBZ,
images, later EPUB/FB2/MOBI). DjVu follows through a second backend.

## Decisions (made)

AGPL v3 accepted. Work happens on the `mupdf` branch. MuPDF 1.28.5, built from the upstream source tarball against
the HaikuPorts libraries (see M0 results). The text below keeps the reasoning.

## Decisions to make first

1. **License.** MuPDF is AGPL v3 (or commercial from Artifex). BePDF's code is "GPL v2 or any later version", so it may
   be combined with AGPL v3 code, but the combined work, i.e. the Tsundoku binary, is then AGPL v3, and a v2-only
   option is gone. For a desktop app with public sources that costs nothing in practice. What changes: license text in
   the package and README, `.PackageInfo` (`licenses`), About dialog ("GNU AGPL v3"), headers of new files.
   XPDF credits go away with XPDF; the BePDF history stays.
2. **MuPDF version.** HaikuPorts has 1.20.3 (2022). Upstream is much newer (better annotation API, more formats).
   Recommendation: pin a recent upstream tag and build it from source (static, with system freetype/harfbuzz/zlib/jpeg
   where possible), instead of depending on the old HaikuPorts package. The C API changes between releases, so pinning
   matters. CI time grows (the Haiku VM is emulated), so cache the built library or build it as its own package.
3. **Branch.** XPDF and MuPDF cannot both be active in the middle of the migration. Work on a `mupdf` branch until
   viewing, navigation, search and outline have parity, then merge to `main` and release as 0.2.0. Editing may come
   later (see M4), which would make 0.2.0 view-only for annotations.

## Where XPDF is used today (21k lines in `tsundoku/beos`)

| Area | Files | Lines | What happens to it |
|------|-------|-------|--------------------|
| Rendering | `PageRenderer`, `CachedPage`, `BitmapPool`, `xpdf/beos/BeSplashOutputDev` | ~600 | rewritten around `fz_run_page`/pixmap, small |
| Document, links, dests, outline | `PDFView`, `PDFWindow`, `OutlinesWindow`, `PageLabels`, `TreeParser` | ~5.5k, partly | mostly simpler: `fz_load_outline`, `fz_load_links`, `fz_resolve_link`, page labels |
| Text: selection, copy, search | `PDFView` (rect selection + `TextOutputDev`), `PDFSearch` | ~600 | `fz_stext_page`, `fz_search_page`, `fz_copy_selection` |
| Print | `PDFPrint`, `PrintSettingsWindow` | ~500 | keep the bitmap approach, new renderer |
| Info, attachments, security | `FileInfoWindow`, `AttachmentView`, `FileSpec`, `PasswordWindow` | ~1k | metadata/permission/embedded-file APIs; "fonts used" has no direct MuPDF API |
| Annotations (edit) | `Annotation`, `AnnotWriter`, `AnnotAppearance`, `AnnotationWindow` | ~3.7k | **largest part**, see M4 |
| xpdf plumbing | `Init`, `GlobalParams`, `BeFontEncoding`, `EncodingReader`, `GDir`, `dist/encodings` (10 MB), `dist/fonts` | - | deleted; CJK/encodings come with MuPDF, non-embedded fonts via a Haiku system-font hook |

Nothing about SEN changes: `bepdf:page_num`, BFS bookmarks and attributes, settings stay as they are.

## Milestones

**M0, spike (small).** Build MuPDF on Haiku, render a page of a test PDF into a `BBitmap` in a tiny program. Check:
BGRA pixmap straight into `B_RGB32`, one `fz_context` per thread (`fz_clone_context`) with locks, display lists for
fast re-render when zooming. Decide license/version/branch (above).

**M1, viewing parity (medium).** A thin `Document` layer over `fz_document` (pages, size, render, outline, links,
metadata, password/permissions) so UI code stops touching XPDF types, and DjVu can plug in later. Open, password,
render, zoom, rotate, fit modes, page cache, link clicks, outline (my "follow the page" code becomes simpler because
`fz_outline` already has locations), page labels, file info, SEN page jumps, layers (`pdf_layer` API instead of
`BeOptionalContent`). MuPDF draws existing annotations itself, so they show without any editing code.

**M2, text (small to medium).** Selection, copy to clipboard and drag, find in document. Rectangle selection stays
(`fz_copy_rectangle`), flowing selection becomes possible.

**M3, the rest of read-only (small).** Printing, attachments, form fields displayed, document info.

**M4, annotations (large).** Replace the hand-written PDF writer by `pdf_annot`: `pdf_create_annot` (highlight,
underline, strike-out, text note, free text, line, square, circle, ink, stamp, file attachment), `pdf_update_annot`
for appearance streams, `pdf_save_document` with incremental save. `AnnotWriter` and most of `AnnotAppearance` go
away, the UI classes (selection, move, resize, property window) are ported to `pdf_annot`. This is also where
**highlighting quoted passages for SEN** becomes straightforward (stext quads, then a highlight annotation).

**M5, formats (separate, later).**
- XPS, CBZ, images, SVG: nearly free once M1 is done (fixed layout).
- EPUB/FB2/MOBI: reflowable, needs relayout on resize/zoom and stable positions (`fz_bookmark`) instead of page
  numbers, so `bepdf:page_num` and BFS bookmarks need a policy. Own milestone.
- DjVu: assessed in "DjVu" below (a document handler for MuPDF on top of `libdjvulibre`, not a second backend).

**Cleanup.** Remove the `xpdf/` tree, `dist/encodings`, `dist/fonts`, XPDF license files and CI steps for XPDF; update
README, About text ("based on MuPDF"), package license and size.

## Rough effort

M0-M3 is the straightforward part, in the order of a week or two of focused work together. M4 is about as much again,
because the UI classes need real rework. M5 items are independent and can wait. These are estimates, not commitments.

## Risks

- AGPL (decide consciously, see above).
- Threading: MuPDF contexts are not thread-safe, the rendering thread and the UI thread need separate contexts and
  document locking. `gPdfLock` is the starting point, not the end.
- Behaviour changes in selection and search results (different text model than XPDF).
- "Fonts used" in the info window, and rarely used PDF features BePDF has handled through XPDF quirks, may regress.
- Annotations written by BePDF are read by MuPDF without problems in principle, but test with a corpus of PDFs
  annotated by BePDF.
- CI build time with MuPDF in the emulated VM.

## M0 results (spike done, `spike/mupdf-spike.cpp`)

MuPDF 1.28.5 builds on Haiku without patches, with `make build=release HAVE_X11=no HAVE_GLUT=no HAVE_CURL=no
USE_SYSTEM_FREETYPE=yes USE_SYSTEM_HARFBUZZ=yes USE_SYSTEM_ZLIB=yes USE_SYSTEM_LIBJPEG=yes USE_SYSTEM_OPENJPEG=yes
USE_SYSTEM_BROTLI=yes USE_SYSTEM_JBIG2DEC=yes libs` (needs `harfbuzz_devel openjpeg_devel jbig2dec_devel
brotli_devel` from HaikuPorts; gumbo, lcms2, mujs, cmark stay bundled because they are not packaged). The link line
needs `libbrotlienc` and `libbrotlicommon` besides `libbrotlidec`. Result: a 56 MB static `libmupdf.a` (unstripped)
plus 3 MB of third-party code.

Verified with a test program on a 73 page PDF (Gutenprint manual), the BePDF manual and Tsundoku's start page:

- Pixmap `fz_device_bgr` with alpha is byte-compatible with `B_RGB32`: rendering is a plain row copy into a `BBitmap`,
  no conversion. Output looks right (fonts, images, layout).
- Display lists pay off when a page is shown repeatedly: at zoom 0.5 6.6 ms versus 18.7 ms, at 2.0 214 ms versus
  256 ms (emulated single CPU, so only the ratios matter).
- Outline: `fz_load_outline` gave 123 entries with resolved page numbers
  (`fz_page_number_from_location`), enough for the "outline follows the page" logic.
- Page labels, permissions (copy/edit/annotate), metadata title, link resolution: available and working.
- Text: `fz_search_page_number` found 346 hits over 73 pages in 1.4 s, `fz_copy_rectangle` returns text of an area.
- Other formats: a PNG opens as a one-page document without any extra code (`is PDF: no`); XPS, CBZ and EPUB use the
  same API.
- Threads: two threads, each with a cloned context and its own `fz_document`, rendered 30 pages without errors.

Findings that change the plan:

- **Locks are mandatory.** `fz_clone_context` returns NULL unless the main context was created with a
  `fz_locks_context` (pthread mutexes, `FZ_LOCK_MAX` of them). The design therefore is: one context with locks, one
  clone per thread, and a separate `fz_document` per thread (render thread and UI thread each open the file),
  instead of sharing one document behind `gPdfLock`.
- **A `BApplication` must exist** before the first `BBitmap` is created; irrelevant inside Tsundoku, but test
  programs need it.
- The package will require the HaikuPorts libraries above instead of bundling them; the install gets much smaller
  because `dist/encodings` (10 MB) and `dist/fonts` disappear.
- Build time on the emulated single CPU: about 45 minutes for the library with bundled HarfBuzz started, about
  25 minutes with system libraries. CI should use a cached build or a separate package.

Next: M1, the `Document` layer and viewing parity.

## Annotations from outside (planned)

SEN has to be able to point at an annotation and to read or change it. What exists, and what comes:

- **Identity (done).** Every annotation created by Tsundoku gets a UUID as its `/NM` (`pdf_set_annot_name`) at creation;
  it is loaded with the annotation (`DocAnnotation::id`) and survives saving, moving, resizing and undo.
  `Document::FindAnnotationById()` finds it in the document. Annotations of other programs have their own `/NM` or
  none; those without one can only be named by page and index, which is not stable.
- **Deep link (done).** `B_REFS_RECEIVED` takes `SEN:annotation` (the id) next to `bepdf:page_num` and `SEN:quote`:
  Tsundoku opens the file, goes to the page, selects the annotation and scrolls to it (`PDFView::ShowAnnotation`).
- **Message suite (later).** A BeOS scripting suite on the window (`GetSupportedSuites`, `ResolveSpecifier`), so that
  `hey Tsundoku get Annotation "<id>" of Window 0` and the same in code work:
  properties Id, Kind, Page, Bounds, Text, Color, Author, Created, Modified; GET, SET (text, color, bounds), DELETE,
  COUNT and CREATE (markup from a quoted text, notes, shapes with their fields); Annotation addressed by index or by
  name (the id), on the document and per page.
- **Change notices (later).** Observers (`StartWatching`) get a message when annotations were added, changed or
  removed, so that SEN can keep relations to annotations in step.

## Page layouts (done, the basis for other formats)

`PageLayout` computes where pages go (a grid of columns x rows per spread, the title page alone or not, or all
spreads below each other); it takes page sizes from `Document::PageMatrix` and knows nothing about rendering.
`PDFView` has a list of slots (a `CachedPage` and a renderer each) for the pages that are shown, one of them is the
active page that the selecting, links, annotations and tools work with, and the mouse makes the page under it the
active one (`SlotAt`, `ActivateSlot`, `SlotScope` for hovering). In the continuous flow the slots follow the scrolling:
pages in view and some around them are rendered, the others are let go.

For other formats this is what is needed: EPUB and other reflowable documents have pages that change size with the
text width, so `SetPages` is run again on a resize and the page the reader is at is kept by a position instead of a
number (`fz_bookmark`); comics (CBZ) are fixed pages and use the presets as they are (a double page spread and a
right-to-left order would be two more switches of the layout, in the same table).

## DjVu (assessed 2026-10-04, not started)

**What there is.** MuPDF reads no DjVu. HaikuPorts has `djvu` (DjVuLibre 3.5.29, library `lib:libdjvulibre`, package
`djvu_devel`, tools `djvu_tools` with `c44`, `cjb2`, `djvm`, `djvused`, `djvutxt`), a Haiku translator (`djvutranslator`, by
3dEyes, which hands out a page as a bitmap) and two viewers (`djvuviewer`, `djview`). The library's license is GPL version 2
**or any later version** (stated in the headers, the recipe only says "GPL v2"), so it can be combined with the AGPL version
3 of Tsundoku. DjVu files have a real mark (`AT&TFORM` and `DJVU` or `DJVM` at offset 12), so the type (`image/vnd.djvu`,
extensions `djvu`, `djv`) gets a plain sniffer rule.

**The way to do it: a document handler for MuPDF**, not a second backend and not the translator. MuPDF's document API is
public (`fz_register_document_handler`, `fz_new_derived_document`, `fz_new_derived_page`), as the comic work showed for
archives. A handler `DjvuDocument.cpp` that implements the callbacks on `ddjvu_*` makes DjVu look like any fixed-layout
document to everything above it (`Document`, `PageLayout`, rendering, search, selection, outline, links, deep links,
printing), so none of that is written twice. The translator cannot be used: it has one bitmap and no text, no outline, no
links. A command line tool (`ddjvu`) is out of the question for speed and text.

**What the handler does** (the `ddjvu` API, `ddjvuapi.h`):
- *Pages and size:* `ddjvu_document_get_pagenum`, `ddjvu_document_get_pageinfo` (width, height, dpi, rotation); the page is
  `width * 72 / dpi` points wide, so a 300 dpi scan has its paper size. DjVu documents can be bundled or indirect (an
  index and files next to it), both are opened by `ddjvu_document_create_by_filename`.
- *Drawing:* `fz_run_page` renders the page for the size that the matrix asks for (`ddjvu_page_render`, which scales and
  clips itself, color mode for the mixed layers) into a pixmap and draws it as an image. The decoding is asynchronous
  in the API (messages, `ddjvu_message_wait`), so the handler waits for the page to be decoded; it runs with the document
  locked like all MuPDF calls in Tsundoku, so one thread at a time uses the context of DjVu.
- *Text:* the hidden text layer (`ddjvu_document_get_pagetext`, a nested list of page, column, paragraph, line and word
  with boxes) goes to the page as invisible text, one word per `fz_text` stretched to its box (`fz_ignore_text`), which is
  what the text extraction of MuPDF expects of an OCR layer. Search, flowing selection and copy then work as for a PDF.
  Boxes are in DjVu coordinates (origin at the bottom left, in pixels of the page's dpi).
- *Outline:* `ddjvu_document_get_outline` (title and `#page` targets) to `fz_outline`. *Links:* the hyperlinks of the page
  annotations (`ddjvu_document_get_pageanno`, `ddjvu_anno_get_hyperlinks`: rectangles, ovals, polygons with a URL) to
  `fz_link`. *Metadata:* the keys of the document annotations (title, author, year, ...) for `fz_lookup_metadata` and so
  for the file attributes (`META:title`, `META:author`, `dc:date`) and File info.
- *Annotations:* the shapes, notes and text of the comic work (the store in `SEN:annotations`, DocumentDraw.cpp) work on
  any fixed document, so a DjVu file has them from the start; marks on text (highlight, underline) need the store to find
  words on fixed pages, which `ResolveAnnotation` does by search (a small change: it is written for books now).

**Effort** (the size of the comic work, most of it testing): the handler with pages, size and drawing is about 250 lines
and gives a viewer (M1); the text layer about 200 (M2); outline, links, metadata, type, File info, build and package
requirements about 250 (M3); annotations on top of the store, a day at most (M4). Together roughly 3 to 4 days of work in
the shape of the existing code, and the first usable result (reading, zooming, the page list) after the first of them.

**Risks and things to check first:**
- The asynchronous message API: a small wrapper that blocks until the page is done, with a timeout and the abort of the
  cookie (`fz_cookie`) so that a zoom while the render goes on does not hang.
- Memory and speed: a 600 dpi scan is large; render for the zoom only (never the whole page at full size), and cache the
  decoded page in DjVuLibre (`ddjvu_cache_set_size`). In the emulated VM decoding is slow, so judge speed on a real machine.
- Rotation and the dpi of pages that have none; pages of different size in one file (the layout handles them).
- The text boxes of words that DjVu stores per line only (no word zones): fall back to lines.
- CI needs `djvu_devel` (HaikuPorts) as a build dependency and `lib:libdjvulibre` in the package requirements.

**Test material:** `djvulibre-book-en.djvu` (installed in the documentation of `djvu_devel`: many pages, an outline, text
and links), and small files made with the tools (`c44` for a photo page, `cjb2` for a bilevel scan, `djvm` to join,
`djvused` to add a text layer and an outline).

**To decide:** (1) that the handler is the way (this plan), and that the GPL 2-or-later library is a dependency of the
package; (2) whether text marks for DjVu are wanted at once or after the shapes; (3) the name of the type: `image/vnd.djvu`
is the registered one, Haiku's translator has registered its own for the format, to be looked at on the VM when starting.

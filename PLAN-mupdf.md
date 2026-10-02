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
- DjVu: a second backend behind the `Document` layer, through Haiku's DjVu translator or `libdjvulibre` directly.
  To check: whether the translator can select a page, and whether text layers are available.

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

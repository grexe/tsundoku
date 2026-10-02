# MuPDF on Haiku: notes from the port

What we learned while moving Tsundoku from XPDF to MuPDF 1.28.5. Written for whoever touches the code next, and as
a basis for the HaikuPorts recipe.

## Building MuPDF

MuPDF builds on Haiku with its own Makefile and no patches (MuPDF 1.28.5, gcc 13, x86_64):

```
make build=release HAVE_X11=no HAVE_GLUT=no HAVE_CURL=no \
	USE_SYSTEM_FREETYPE=yes USE_SYSTEM_HARFBUZZ=yes USE_SYSTEM_ZLIB=yes USE_SYSTEM_LIBJPEG=yes \
	USE_SYSTEM_OPENJPEG=yes USE_SYSTEM_BROTLI=yes USE_SYSTEM_JBIG2DEC=yes libs
```

- `libs` builds `build/release/libmupdf.a` (56 MB, not stripped) and `libmupdf-third.a` (3 MB), no tools or viewers.
- Use the switches per library, not `USE_SYSTEM_LIBS=yes`: that one also wants GLUT, curl, tesseract, leptonica and
  zxing-cpp, which are not needed.
- Libraries that HaikuPorts packages are used from the system (see below). Bundled are the ones that are not
  packaged or that MuPDF needs in its own fork: gumbo, lcms2 (lcms2mt), mujs, cmark-gfm, extract.
- Packages: `freetype_devel harfbuzz_devel openjpeg_devel jbig2dec_devel brotli_devel libjpeg_turbo_devel
  zlib_devel`.

| Library | HaikuPorts | Bundled in MuPDF 1.28.5 |
|---------|-----------|-------------------------|
| freetype | 2.14.3 | 2.14.3 |
| harfbuzz | 14.4.0 | 13.0.1 |
| openjpeg | 2.5.4 | 2.5.3 |
| jbig2dec | 0.19 | - |
| brotli | 1.2.0 | - |

- The link line needs `libbrotlienc` and `libbrotlicommon` besides `libbrotlidec`, MuPDF can compress with brotli:
  `-lfreetype -lharfbuzz -lopenjp2 -ljbig2dec -lbrotlidec -lbrotlienc -lbrotlicommon -ljpeg -lz`.
- Build time on a single emulated CPU: about 25 minutes with system libraries, 45 minutes and more with the bundled
  HarfBuzz. A parallel build (`-j`) helps on real hardware.
- The HaikuPorts recipe is at 1.20.3 (2022). The C API changed since, mind that when updating it: the recipe has to
  provide the headers (`mupdf_devel`) and either `libmupdf.a` or a shared library.

## Using it from C++

- **`fz_try`/`fz_catch` are `setjmp`/`longjmp`.** Do not create or destroy C++ objects with destructors (`BString`,
  `std::vector`) inside a `fz_try` block, and mark locals that are assigned in the block and read after an error with
  `fz_var()`. All MuPDF calls in `Document.cpp` follow this: plain C types inside, C++ objects outside.
- **Threads.** A `fz_context` must not be used by two threads at once. `fz_clone_context()` for another thread only
  works if the original was created with lock callbacks (`fz_locks_context`, pthread mutexes), otherwise it returns
  NULL. Tsundoku uses one context per document and takes the document's `BLocker` around every use (`Document` does
  it, `DocumentLocker` for direct use). The alternative for later (prefetching pages) is one clone and one
  `fz_document` per thread, which was verified to work (two threads rendered 30 pages without errors).
- **A `BApplication` must exist before the first `BBitmap`**, otherwise the process dies in
  `AppServerLink` ("You need to have a valid app_server connection first!"). Only matters for test programs.
- **Aborting a render:** pass an `fz_cookie` to `fz_run_page` and set `cookie.abort = 1` from another thread.
- **Memory:** the store defaults to 256 MB per context, Tsundoku uses 128 MB.

## Rendering

- `fz_device_bgr` with alpha is byte-compatible with `B_RGB32`, so MuPDF draws straight into the memory of the
  `BBitmap` (`fz_new_pixmap_with_data`, stride `BytesPerRow()`), without copying or converting. Clear it to white
  (`0xff`) first, pages have no background of their own.
- The page matrix: `fz_scale(dpi/72)`, `fz_pre_rotate(rotation)`, then translate by the negative top left of the
  transformed bounds so that the page starts at 0,0 of the bitmap (`Document::PageMatrix`). It maps page space to
  bitmap pixels, the inverse maps mouse positions back.
- Page space is MuPDF's: points, y points down, the `/Rotate` of the page already applied (`fz_bound_page`). Links,
  text quads and destinations are all in it, only the bitmap is transformed. This replaced the user space and
  `CvtUserToDev` of XPDF.
- Display lists (`fz_new_display_list_from_page`) make rendering a page again at another zoom cheaper (6.6 ms against
  18.7 ms at 50%, 214 ms against 256 ms at 200% on the emulated CPU). Not used yet, the page is rendered once per
  zoom and rotation.
- Existing annotations and form widgets are drawn by `fz_run_page`, nothing to do for showing them.

## Text, selection, search

- `fz_new_stext_page_from_page()` gives the structured text of a page. It is created in the render thread right
  after rendering and kept with the page (`CachedPage::Text()`), as long as the document lock is held when it is used.
- Flowing selection: `fz_highlight_selection(a, b)` returns one quad per line fragment between two points (also if
  `b` comes before `a`), `fz_copy_selection(a, b, crlf)` the text (UTF-8, free with `fz_free`). A rectangle is
  `fz_copy_rectangle`. `fz_snap_selection(&a, &b, FZ_SELECT_WORDS|FZ_SELECT_LINES)` for double and triple click; it
  moves the points to the snapped ends.
- Search: `fz_match_stext_page_cb()` with `FZ_SEARCH_IGNORE_CASE` or `FZ_SEARCH_EXACT`. It calls back once per hit with
  all the quads of the hit (a hit that wraps lines has more than one). Tsundoku selects a hit by selecting from the
  middle of the left edge of its first quad to the middle of the right edge of the last one, which gives the same
  highlight as the search found. `fz_search_page`/`fz_search_stext_page` are case insensitive only.
- Text of a page with text in a picture only (scans) is empty; OCR (tesseract) is a build option, not used.

## Document

- Opening: `fz_open_document` picks the handler by file type; `fz_needs_password` / `fz_authenticate_password`. One
  password is tried as user and as owner password. Permissions: `fz_has_permission(FZ_PERMISSION_PRINT|COPY|EDIT|
  ANNOTATE)`. Tested with the encrypted files of the qpdf test suite (wrong password: needs password again, user
  and owner password open it, permissions are reported).
- Pages are numbered from 0 in MuPDF, from 1 in Tsundoku (the `bepdf:page_num` of SEN stays 1-based).
- Outline: `fz_load_outline` gives a tree with `title`, `is_open`, flags (bold, italic), color and a `fz_location`
  (`fz_page_number_from_location` for the number) plus `x`, `y`. Some entries have only a `uri`: resolve it with
  `fz_resolve_link`.
- Links: `fz_load_links(page)`; internal ones look like `#page=5&zoom=nan,82,71`, resolve with `fz_resolve_link`
  which also gives the position on the page, `NaN` for what the destination does not set (FitH has only a `y`).
  `fz_is_external_link` tells `http:`, `mailto:` and so on; a relative path to another PDF has no scheme.
- Page labels: `fz_page_label(page)` needs the page loaded, so labels of a very large document are skipped
  (more than 2000 pages) when the page list is filled.
- Metadata: `fz_lookup_metadata("info:Title"|"info:Author"|"info:CreationDate"|"format"|"encryption", ...)`. PDF
  dates stay in the PDF form (`D:2016...`) and are converted by Tsundoku.
- Not available in the C API: the list of fonts of a page (the "fonts of this page" tab of the file info is gone),
  PDF version and linearization (the BFS attributes `PDF:version` and `PDF:linearized` are not written any more).

## Testing without a mouse

`make DEFINES=TSUNDOKU_TESTING` compiles hooks that let `hey` drive the view and write what happened to
`/tmp/ts_test.out`:

```
hey Tsundoku "TSTX" Window 0 with cmd=select and x1=90 and y1=131 and x2=300 and y2=191
hey Tsundoku "TSTX" Window 0 with cmd=word and x1=150 and y1=131
hey Tsundoku "TSTX" Window 0 with cmd=do_fileinfo         # any window command: do_rotate, do_zoomin, ...
```

Coordinates are in the view. Things that cost time: `hey` needs `and` between the `name=value` pairs, turns a
field called `name` (or any value that looks like a file) into an `entry_ref`, and the screen blanker of the VM has
to be killed or the screenshot is black. `screenshot -s -f png file.png` takes the picture.

## Known gaps after M1

Printing was compiled and reviewed but not run (no printer in the VM); drag and drop of a selection and the real
mouse events (as opposed to the test hooks that call the same functions) have not been exercised; rendering is not
progressive, a very large page at a high zoom shows nothing until it is done.

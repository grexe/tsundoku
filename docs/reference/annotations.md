# Annotations: model, storage and access from other programs

How Toji keeps and describes annotations. For how to use them, see the [README](../../README.md#annotating).

## Where annotations live

| Document | Where the annotations are |
|----------|---------------------------|
| PDF | Real PDF annotations in the file; other readers show them. Every annotation gets a unique name (the `/NM` of the PDF, a UUID) when it is created, so that other programs can refer to it. |
| EPUB, comics (CBZ, CBR, CB7, CBT, BBF), DjVu | The attribute `SEN:annotations` of the file (these formats cannot take annotations), with `SEN:annotationCount` (the number, an integer so that it can be indexed; not there if there are none). |

The attribute is a standard message that other applications can use for the same purpose, like the styles of StyledEdit are
kept in an attribute. The annotations go along when the file is copied in Tracker, and File > Save as… copies them with the
document.

For a PDF file the annotations are described in the same form when they are handed on, and when the file is saved or a copy is
made they are written to `SEN:annotations` too, so that they can be found and used without opening the PDF file.

## Identifiers

Every annotation has an identifier. A PDF annotation that was made by another program has none (the `/NM` of the PDF is empty).
When such a file is opened its annotations are described in `SEN:annotations` (the PDF file itself is not changed) and those
without a name get a UUID, which is kept in the attribute together with a key of the annotation (page, kind, place, text), so
that the same annotation has the same identifier the next time. The identifiers go into the PDF when it is saved. This is the
setting "Describe annotations of other programs in the file's attributes" (on by default). The same can be done from outside with
the scripting suite (`UpgradeAnnotations`, see [scripting.md](scripting.md)).

## Bookmarks

The bookmarks of the reader (the sidebar's bookmarks list; Bookmark > Add) are annotations with the motivation
`oa:bookmarking` in `SEN:annotations`, for every format, PDF too (the attribute of a PDF file holds its annotations as well,
which are rewritten when it is saved, and the bookmarks stay). The label is the body. The target is the page (`page=3`, an
`oa:FragmentSelector`) or, in a book, the place in the text (a `oa:TextQuoteSelector` and the EPUB CFI, see
[metadata.md](metadata.md#anchors-in-books)). They are written when the file is closed, and are not counted in
`SEN:annotationCount` (`SEN:bookmarkCount`, not indexed, says how many there are).

## The Web Annotation model

Annotations and deep links are described with the [W3C Web Annotation Data Model](https://www.w3.org/TR/annotation-model/)
(`oa:`), as BMessages in the attribute and as JSON-LD when handed on. "Copy as Web Annotation" in the menu of an annotation
puts it on the clipboard as JSON-LD.

An annotation has:

- a **target**: the document and **selectors** that say where in it
  - `oa:FragmentSelector` with `page=5` or an EPUB CFI,
  - `oa:TextQuoteSelector` with the words and some text around them,
  - `oa:SvgSelector` for shapes and drawings;
  - selectors can be refined by one another with `oa:refinedBy`;
- perhaps a **body** (`oa:TextualBody`, the note), a **style** (`oa:CssStyle`);
- a **motivation**: `oa:highlighting`, `oa:commenting`, and `sen:underline`, `sen:strikethrough` and `sen:squiggle` for the other
  marks of a text (SEN's own `oa:Motivation`s, which are `skos:broader oa:highlighting`).

If SEN knows the file (it has a `SEN:ID`, its identifier in the personal knowledge graph), that identifier is the `oa:hasSource`
of the targets, both in the attribute and when an annotation is handed on. Otherwise the attribute has no source (the
annotations are about the file that they are stored with) and a handed-on annotation has the `file:` IRI.

### Marks on words

| Document | Target |
|----------|--------|
| PDF, DjVu | the page (`page=N`, an `oa:FragmentSelector`) refined by the quoted words (`oa:TextQuoteSelector`) |
| EPUB | the words, and their [EPUB CFI](https://idpf.org/epub/linking/cfi/) (see [anchors](metadata.md#anchors-in-books)) |

### Notes, text, shapes and drawings on fixed pages (comics, DjVu)

The target is the page (`page=3`, an `oa:FragmentSelector`) refined by

- a rectangle as a media fragment (`xywh=percent:10,20,30,40`), or
- for ellipses, lines, arrows and drawings, an `oa:SvgSelector` whose `viewBox` is the page in percent (`0 0 100 100`,
  `preserveAspectRatio="none"`), so that it fits the page at any size.

`sen:shape` says what was drawn. Notes and text have the motivation `oa:commenting` and the note as the body.

## From other programs

Other applications (SEN) open a document at a place with `B_REFS_RECEIVED`; there is no other way to say a page (the `bepdf:page_num` of BePDF is not read any more, a page is the selector `page=N`):

- the file in `refs`, and `oa:hasTarget`: a message with `oa:hasSelector` entries as in the model, any of the selectors above
  that the document understands. An EPUB CFI is found in whatever layout the book has; the words are searched for from the page
  that was named. `xywh=` and an `oa:SvgSelector` (its bounding box is taken, in the units of its `viewBox`, relative to the
  page) go to the page and mark the place for a moment.
- with `oa:motivatedBy`, the passage that the words name is also marked (not saved).
- or `oa:Annotation` with the identifier of an annotation (`urn:uuid:...`): the document goes there and selects it.

Annotations can be read and changed with the scripting suite: [scripting.md](scripting.md). There are no change notices; the
attribute `SEN:annotations` is what to watch.

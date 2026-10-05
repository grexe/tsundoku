# File attributes and anchors

What Tsundoku writes to the BFS attributes of a file, and how places in a document are kept. For the annotation attribute see
[annotations.md](annotations.md).

## Principles

- What a document says about itself (title, author, series, ...) is written to **separate BFS attributes** of the file, and
  nothing is invented: the names are the properties of the established ontologies with the prefix that is commonly used for
  each. Where there is no established name, the prefix is `SEN:` (`PDF:` for what only a PDF file has, `tsundoku:` for what
  only the application needs).
- The attributes are defined for the file types (so that Tracker offers them as columns), and the indices are made on the
  volume.
- What BePDF made up (`META:*`, `bepdf:*`) is not used any more, but it is read: legacy bookmarks and the place where you
  stopped reading are taken over, and the standard attributes are written to such a file in any case. The legacy attributes
  **stay in place**, so that users can go back to BePDF, unless the user chooses to replace them: the first time a file with
  legacy attributes is opened Tsundoku asks (Keep them / Replace them, recommended), and the answer is the setting "Replace
  legacy attributes with standard ones" in the preferences. When they are replaced the values are moved to the standard names
  (`META:title` to `dc:title`, ...) and the legacy attributes are taken away. While they are kept, the columns of the old names
  stay defined for PDF files.

## Attributes

| Attribute | What |
|-----------|------|
| `dc:title`, `dc:creator`, `dc:subject` | title, author and keywords |
| `dc:description`, `dc:publisher`, `dc:language`, `dc:date`, `dc:identifier` | Dublin Core (for a PDF file the description is its Subject) |
| `dcterms:isPartOf` | the series |
| `schema:isbn`, `schema:position` | [schema.org/Book](https://schema.org/Book): the ISBN, and the number in the series |
| `schema:numberOfPages` | the number of pages (see below) |
| `PDF:creator`, `PDF:producer`, `PDF:created`, `PDF:modified` | what only a PDF file has: the program that made the document and the one that made the PDF, and the dates |
| `SEN:annotations`, `SEN:annotationCount`, `SEN:bookmarkCount` | the annotations and the bookmarks, and how many of each there are (see [annotations.md](annotations.md)); only the count of annotations is indexed |
| `SEN:readingProgression` | `rtl`, `ltr`, `ttb` or `default` (see below) |
| `tsundoku:viewState` | where you stopped reading (see below) |

Other prefixes such as `foaf:` come the same way when they are needed.

`schema:numberOfPages` of a book is the number of pages in the standard configuration (6 by 9 inches, the default text size),
an estimate that stays the same for inventory and citations, as the shops give it for an e-book. It is not written when the book
is first opened at another text size.

## View state

`tsundoku:viewState` is one attribute (a message) with where you stopped reading: the page, the place on the page, zoom and
rotation, the position and size of the window, and for a book the [anchor](#anchors-in-books) of the place in the text.

## Where the metadata comes from

| Format | Source |
|--------|--------|
| PDF | the document information |
| EPUB | the package document (title, authors, series, language, publisher, date, identifier, subjects, description, cover), read with libzip and libxml2 |
| Comics | `ComicInfo.xml` (ComicRack, ComicTagger, Calibre, Komga: title, series and number, writers, artists, publisher, date, genres, language, summary) |
| BBF | the metadata of the file (Title, Author, Series, Publisher, Language, Year, Description, ...) |
| DjVu | the annotations of the document (title, author, keywords; the year is `dc:date`) |

File info shows it, with the cover (for comics the page marked as such, else the first).

## Reading direction

`SEN:readingProgression` is named after the W3C Publication Manifest property, with SEN's prefix since no ontology has one for a
BFS attribute. `rtl`, `ltr` or `ttb`, or `default`, which is what the document says. A document that says `rtl` has the
attribute, so that it can be found.

A manga says so in `ComicInfo.xml` (`Manga`), an EPUB in its spine (`page-progression-direction`). A webtoon or manhua that is a
strip is `ttb`. The choice made with View > Right to left or Top to bottom is kept in the attribute.

## Anchors in books

Places in a book are anchors in the manner of the W3C Web Annotation model, and not page numbers, which change with the text
size. An anchor has

- the words at the place (a text quote),
- its [EPUB CFI](https://idpf.org/epub/linking/cfi/), the standard path through the package and the content document, e.g.
  `epubcfi(/6/4[chap01]!/4/10,/1:3,/3:12)`, which other reading systems understand,
- where it was (chapter, place in the chapter), to look there first.

A mark and a bookmark keep their anchor in `SEN:annotations`, and the place where you stopped reading is in
`tsundoku:viewState`. They find their page again for the text size the book is shown at.
If the words are not found any more (another version of the book), the CFI, which names a chapter by the id of its entry in the
reading order, leads to the words that are there now.

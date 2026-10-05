# File attributes and anchors

What Tsundoku writes to the BFS attributes of a file, and how places in a document are kept. For the annotation attribute see
[annotations.md](annotations.md).

## Principles

- What a document says about itself (title, author, series, ...) is written to **separate BFS attributes** of the file, and
  nothing is invented for books: the names are the properties of the established ontologies with the prefix that is commonly
  used for each. Where there is no established name, the prefix is `SEN:`.
- The attributes are defined for the file type (so that Tracker offers them as columns), and the indices are made on the volume.
- Everything BePDF stores (bookmarks and the position per file) is unchanged and compatible.

## Attributes

| Attribute | What |
|-----------|------|
| `META:title`, `META:author`, `META:keyw`, `META:pages` | The attributes of `application/pdf` (they are dc:title, dc:creator, dc:subject and schema:numberOfPages), written for books, comics and DjVu as well, so that a column or a query works for all. The prefix `META:` is only kept for the attributes that exist already. |
| `dc:description`, `dc:publisher`, `dc:language`, `dc:date`, `dc:identifier` | Dublin Core |
| `dcterms:isPartOf` | the series |
| `schema:isbn`, `schema:position` | [schema.org/Book](https://schema.org/Book): the ISBN, and the number in the series |
| `SEN:annotations`, `SEN:annotationCount` | the annotations, and how many there are (see [annotations.md](annotations.md)) |
| `SEN:readingProgression` | `rtl`, `ltr`, `ttb` or `default` (see below) |
| `bepdf:bookmarks`, `bepdf:anchor` | bookmarks and the place where you stopped reading (see below) |

Other prefixes such as `foaf:` come the same way when they are needed.

`META:pages` of a book is the number of pages in the standard configuration (6 by 9 inches, the default text size), an estimate
that stays the same for inventory and citations, as the shops give it for an e-book. It is not written when the book is first
opened at another text size.

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

A mark keeps its anchor in `SEN:annotations`, a bookmark of yours in an extra `a` entry of `bepdf:bookmarks` (BePDF ignores it),
and the place where you stopped reading in `bepdf:anchor`. They find their page again for the text size the book is shown at.
If the words are not found any more (another version of the book), the CFI, which names a chapter by the id of its entry in the
reading order, leads to the words that are there now.

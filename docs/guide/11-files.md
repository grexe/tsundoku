# Files, attributes and SEN

Toji keeps what belongs to a file **with the file**, in its attributes, and not in files of its own. Moving or copying the file
in Tracker takes everything along. (On a volume that is not BFS the attributes are lost.)

## What it writes

| Attribute | |
|---|---|
| `dc:title`, `dc:creator`, `dc:subject`, `dc:description`, `dc:publisher`, `dc:language`, `dc:date`, `dc:identifier` | what the document says about itself (Dublin Core) |
| `dcterms:isPartOf`, `schema:position` | the series and the number in it |
| `schema:isbn`, `schema:numberOfPages` | the ISBN and the number of pages |
| `PDF:creator`, `PDF:producer`, `PDF:created`, `PDF:modified` | for PDF files |
| `SEN:annotations` | annotations (of books, comics, DjVu; for a PDF a description of the ones in the file) and bookmarks |
| `SEN:annotationCount`, `SEN:bookmarkCount` | how many there are |
| `SEN:readingProgression` | `rtl`, `ltr`, `ttb` or `default` |
| `toji:viewState` | where you stopped reading |

Tracker can show these as columns for the types Toji knows, and queries can use them (Toji makes the indices on the
volume).

## Annotations as Web Annotations

Annotations and bookmarks are kept as [W3C Web Annotations](https://www.w3.org/TR/annotation-model/), a standard way to say what
was marked and where. Other programs (SEN in particular) can read them from the attribute without opening the document. A
document can be opened at a place the same way: a page, a region of a page, some words or an EPUB position.

## Links to places

**Edit > Copy link to this place** (Cmd+Shift+L) puts a link on the clipboard that opens the document at the page you are on, at
the words that are selected, or at the annotation that is selected. In a book it is the place in the text. The link looks like
`toji:///boot/home/papers/x.pdf#page=5`; paste it into a note or a mail, or open it with `Toji <link>` or `open <link>`. The
details are in `docs/reference/links.md`.

## Scripting

Toji can be controlled from other programs and from the command line with `hey`:

```
hey Toji get Path of Document of Window 0
hey Toji do Goto of Document of Window 0 with page=12
hey Toji count Annotation of Document of Window 0
hey Toji do AddAnnotation of Document of Window 0 with kind=highlight and quote="some words" and page=3
```

The full list of what can be asked and done is in the scripting reference
([`docs/reference/scripting.md`](https://github.com/sen-laboratories/toji/blob/main/docs/reference/scripting.md)).

## More reference

The technical details (the model of the annotations, all attributes, how each format is read) are in `docs/reference/` of the
repository.

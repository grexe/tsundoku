# Scripting

Tsundoku has a scripting suite, `suite/vnd.sen-labs.Tsundoku`, on its windows. It works from the command line with
[`hey`](https://github.com/HaikuArchives/Hey) and from code with scripting messages (`B_GET_PROPERTY`, ...). Changes go through
the same code as what the user does: they can be undone and count as unsaved. There are no change notices: watch the file's
attributes (`SEN:annotations`) instead.

## Objects

```
Window
  Document            Path, Title, Type, PageCount, TextSize, Page, Selection
    Page N            Text, Size
    Annotation ...    the annotations
    Bookmark ...      the bookmarks
```

Items are addressed by index (0-based), reverse index or name: `Annotation 0`, `Annotation -1`, `Annotation "<id>"`; for a
bookmark the name is its label.

### Document

| Property | Verbs | |
|----------|-------|-|
| `Path`, `Title`, `Type` (`pdf`, `epub`, `comic`, `djvu`, `other`), `PageCount`, `TextSize` | GET | |
| `Page` | GET, SET | the page that is shown (1-based); setting it goes there |
| `Selection` | GET | the selected text |
| `Goto` | DO | `page=N` |
| `Save`, `Undo`, `Redo` | DO | as the menu entries |
| `UpgradeAnnotations` | DO | describes the annotations of a PDF in `SEN:annotations` and gives those without an identifier one (the PDF file is not changed); returns how many were named |
| `AddAnnotation`, `AddBookmark` | DO | make one (see below) |

### Page

`Text` (GET, the text of the page) and `Size` (GET, a `BRect` in points). The page is also a way to go there: `do Goto of Page 3
of Document of Window 0`.

### Annotation

| Property | Verbs | |
|----------|-------|-|
| `Id` | GET | the identifier (the name of the annotation: the `/NM` of a PDF) |
| `Kind` | GET | `highlight`, `underline`, `strikeout`, `squiggly`, `note`, `text`, `rectangle`, `ellipse`, `line`, `ink` |
| `Page` | GET | the page it is on |
| `Text` | GET, SET | the note |
| `Quote` | GET, SET | the words a mark covers; setting them moves the mark to other words of the page (it keeps its identifier, color and note) |
| `Color` | GET, SET | `0xRRGGBB` |
| `Bounds` | GET, SET | a `BRect` in points on the page |
| `Author` | GET | |
| `JSON` | GET | the annotation as a [Web Annotation](annotations.md) (JSON-LD) |
| `Goto` | DO | goes to the annotation and selects it |

DELETE removes it. `GET` of an annotation without a property returns all of them as a message; `COUNT` counts.

SET always updates what belongs with the value (a new `Quote` finds the words again and re-anchors the mark).

### Bookmark

`Label` (GET, SET), `Page` (GET, the page it is on now; a bookmark in a book is a place in the text), `Goto` (DO), DELETE.

## Which annotations or bookmarks

Counting, listing and addressing by index work on the whole document by default. Two fields of the message narrow that to one
page: `context=page` and `page=N` (the page that is shown if `page` is left out). An item addressed by name is found in the
whole document in any case.

## Making annotations

`CREATE` of `Annotation` or `Bookmark` works for code (the specifiers `Annotation`, `Document`, `Window 0`, in that order, as
`tests/ScriptingTest.cpp` does it). `hey` cannot nest the specifiers of a CREATE, so `do AddAnnotation` and `do AddBookmark` do
the same:

| `kind` | Fields |
|--------|--------|
| `highlight`, `underline`, `strikeout`, `squiggly` | `quote` (the words, on `page`), or the selection if there is no quote; `color`, `text` |
| `note`, `text` | `page`, `x`, `y` (points), `text` |
| `rectangle`, `ellipse`, `line`, `arrow` | `page`, `left`, `top`, `right`, `bottom` (points), `color` |
| `ink` | `page`, `points` (an array of `BPoint`), `color` |

The result is the identifier of the new annotation. A bookmark takes `label` and `page`.

## Tests

`tests/ScriptingTest.cpp` uses the scripting messages directly (nested specifiers, filtering by context and page, SET, DELETE,
Undo, Goto) and prints one line per check: `tests/run_scripting_test.sh <file.pdf>` on Haiku.

## Examples

```sh
hey Tsundoku get Path of Document of Window 0
hey Tsundoku do Goto of Document of Window 0 with page=12
hey Tsundoku count Annotation of Document of Window 0 with context=page and page=12
hey Tsundoku get Annotation of Document of Window 0
hey Tsundoku get Quote of Annotation 0 of Document of Window 0
hey Tsundoku set Text of Annotation 0 of Document of Window 0 to "check this"
hey Tsundoku do Goto of Annotation '"6f0c3e1a-..."' of Document of Window 0
hey Tsundoku do AddAnnotation of Document of Window 0 with kind=highlight and quote="the words" and page=3
hey Tsundoku do AddBookmark of Document of Window 0 with label=Chapter and page=40
hey Tsundoku delete Bookmark Chapter of Document of Window 0
```

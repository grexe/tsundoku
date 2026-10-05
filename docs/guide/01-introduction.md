# Introduction

Toji is a document reader for Haiku. The name is Japanese (綴じ) and means binding, stitching things together, the way a book is
bound. Toji binds the papers, books and comics you read to what is known about them: the attributes of the file, your marks
and notes, and the relations of SEN. (Until version 0.9 the program was called Tsundoku.)

Toji started as a fork of BePDF and renders with MuPDF. It reads

- PDF files (also encrypted ones),
- EPUB books,
- comic books: CBZ, CBR, CB7, CBT and BBF,
- DjVu documents,
- XPS files, FictionBooks (`.fbz`) and pictures.

It remembers where you stopped reading, lets you select, copy and search text, mark passages, add notes and shapes, keep
bookmarks, and it writes what a document says about itself (title, author, series, ...) to the attributes of the file, so that
Tracker can show it in columns and queries can find it.

![The main window: the outline in the sidebar, marks and margin notes on the page, the annotation buttons in the toolbar.](images/main-window.png)

Toji is also the reader of SEN (Semantic Extensions Native): other programs can open a document at an exact place, and
read and change annotations. [Chapter 11](#files-attributes-and-sen) tells how.

## About this guide

The guide follows a reader's path: [opening and reading](#reading-a-document), [selecting and searching](#selecting-copying-and-searching),
[annotating](#annotating), [bookmarks and the sidebar](#the-sidebar-and-bookmarks), the [formats](#formats) (books, comics, DjVu),
[printing](#printing-and-file-info), the [settings](#settings), the [keyboard](#keyboard-and-mouse) and, for the curious, how
Toji keeps its data in [files, attributes and SEN](#files-attributes-and-sen).

Menu entries are written like this: **View > Fit to page width**. Keys: Cmd is the command key (Alt on a PC keyboard), Option is
the key next to it.

The newest version of this guide is in the [repository](https://github.com/sen-laboratories/toji/tree/main/docs/guide).

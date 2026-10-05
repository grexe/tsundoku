// SPDX-FileCopyrightText: 2026 Gregor B. Rosenauer & Claude
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The start page of Toji: shown when the program is started without a file (the user guide is in the Help menu).
// typst compile --root ../.. start.typ build/toji-start.pdf
#set document(title: "Toji", author: "SEN Labs e.U.")
#set page(paper: "a4", margin: (x: 2.5cm, y: 2.5cm), fill: white)
#set text(font: "Libertinus Serif", size: 11pt, lang: "en")
#set par(justify: false, leading: 0.7em)

#align(center)[
  #v(1.2cm)
  #image("/images/toji-logo.png", height: 12cm)
  #v(1.2cm)
  #text(size: 12pt, tracking: 0.08em)[A DOCUMENT READER FOR HAIKU]
  #v(1.6cm)
  #text(size: 15pt, style: "italic")[
    Tabs, feeds, papers, tides: \
    curiosity surfaces where \
    one thread ties two shores.
  ]
]

#pagebreak()

#set text(size: 11.5pt)
#show heading: set text(weight: "regular")
#show heading.where(level: 1): set text(size: 22pt)
#show heading.where(level: 2): it => [#v(0.8em) #text(size: 14pt, weight: "bold", it.body) #v(0.2em)]

= Welcome to Toji

Toji reads PDF files, EPUB books, comics (CBZ, CBR, CB7, CBT, BBF), DjVu documents and more. You can mark text, add notes
and shapes, and the marks stay with the file. What the document says about itself, and where you stopped reading, is kept
in the attributes of the file, so Tracker and SEN can use it.

== Open a document

Drop a file on the window, choose *File > Open* (Cmd+O), or open a file with Toji from Tracker. Toji remembers the page where you
stopped and the other settings of each file.

== Read

Move with the keys, the toolbar or the mouse wheel, type a page number in the toolbar to jump to it, and use the outline in the sidebar shows the
structure of the document. In the *View* menu you find the page flow (single, double-sided, continuous, top to bottom for
webtoons), the zoom and the reading direction (right to left for manga).

== Mark and annotate

Select words and press the marker button in the toolbar (the small arrow opens the colors), or add a note, a text or a shape from
the other buttons. *Edit > Add margin note* puts a note beside the text. Every mark is a real annotation of the file for PDF, and
is kept in the attributes for the other formats.

== Learn more

*Help > User guide* opens the full user guide, which you can also keep in your own folders: it is a normal PDF, with a chapter
for each of these things, the settings, the keyboard and the attributes that Toji writes. The reference documents in the
repository describe scripting (`hey Toji ...`), annotations and links.

#v(1fr)
#line(length: 100%, stroke: 0.4pt + luma(160))
#text(size: 9.5pt, fill: luma(90))[
  Toji (綴じ, "binding") is free software under the GNU Affero General Public License, version 3 or later, from SEN Labs e.U.
  Toji™, SEN™ and SEN Labs™ are names of SEN Labs e.U. #h(1fr) github.com/sen-laboratories/toji
]

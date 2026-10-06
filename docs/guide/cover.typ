// SPDX-FileCopyrightText: 2026 Gregor B. Rosenauer & Claude
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The cover of the start page and of the user guide: the logo, an optional subtitle, the tagline and the haiku. The guide's build
// (build.sh) puts the subtitle in; the start page (start.typ) has none.
#let subtitle = none

#align(center)[
  #v(1.2cm)
  #image("images/toji-logo.png", height: 11cm)
  #if subtitle != none [
    #v(-0.7cm)
    #text(size: 36pt, tracking: 0.1em)[#subtitle]
    #v(0.5cm)
  ] else [
    #v(-0.6cm)
  ]
  #text(size: 12pt, tracking: 0.08em)[A DOCUMENT READER FOR HAIKU]
  #v(1.6cm)
  #text(size: 15pt, style: "italic")[
    Tabs, feeds, papers, tides: \
    curiosity surfaces where \
    one thread ties two shores.
  ]
]

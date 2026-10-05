# Reading a document

## Opening

Open a file from **File > Open**, by dropping it on the window or on the application, from Tracker, or from the command line:

```
Tsundoku document.pdf        # where you left off
Tsundoku document.pdf 12     # on page 12
```

When you open a file again Tsundoku shows it as you left it: the page, the zoom, the rotation and (if you chose that in the
[settings](#settings)) the position and size of the window. This is kept in the attribute `tsundoku:viewState` of the file.

If a file has a password, Tsundoku asks for it.

## Moving through the document

- **Keyboard:** arrow keys scroll, Space and Page Down go a page forward and Backspace and Page Up a page back, Home and End go
  to the first and last page, Cmd+J asks for a page number. Cmd+Left and Cmd+Right go back and forward through the places you jumped from, like a browser.
- **Toolbar:** the buttons for the first, previous, next and last page, and the page box, where you can type a page.
- **Mouse:** drag the page to move it, use the wheel to scroll. Links are followed with a click, and the pointer shows where
  they lead.
- **Sidebar:** choosing an entry of the outline or the page list goes there.

## Pages: single, double-sided, continuous

**View** has three ways to show pages:

| | |
|---|---|
| **Single page** | one page at a time |
| **Double-sided** | two pages side by side like the pages of a book; **Title page alone** lets the first page stand alone, as in a book |
| **Continuous** | all pages one below the other, scrolled through |

The buttons next to the fit buttons switch the same way. In the double-sided flow the next and previous page go by a spread.

## Zoom and rotation

**View > Zoom in** and **Zoom out** (Cmd++ and Cmd+-) change the size in steps; **View > Zoom** has fixed values from 25% to 300%.
**Fit to page width** (Cmd+/) and **Fit to page** (Cmd+*) fit the page to the window. To zoom to a part of the page, drag a
rectangle with the middle mouse button. **View > Rotation** turns the page by 90 degrees.

## Fullscreen

**View > Fullscreen** shows only the document. In the [settings](#settings) you choose whether the toolbar, the status bar and the
scroll bars stay.

## Several documents

Open another file in the same window or in a new one (**File > Open in new window**); the pages that are still being drawn for
the first document finish in the background.

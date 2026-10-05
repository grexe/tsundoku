# Installing

Toji and the libraries it needs are in the package repository of SEN Labs. Add it once, then install:

```
pkgman add-repo https://kiri.sen-labs.org/x86_64
pkgman install toji
```

Each release on GitHub also has the package as a file; copy it to `~/config/packages` to install it. Toji is then in the
Deskbar menu under *Applications*.

Which program opens a file type is up to you: Toji offers itself for the types it reads (**Open with** in Tracker), but never
changes your preferred application. To make it the one for PDF files, use the *Preferred Applications*-style way of Haiku, or
Tracker's **File > Open with > Toji** and "Set as preferred".

## Updating

`pkgman update` brings Toji to the newest version of the repository. Your settings (`~/config/settings/Toji`) and what
Toji keeps with your files (positions, bookmarks, annotations) stay.

## What it needs

MuPDF, libarchive, libzip, libxml2 and DjVuLibre; `pkgman` installs them together with Toji.

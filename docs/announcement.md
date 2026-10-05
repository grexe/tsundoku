# Toji: A Semantic, Multi-Format Document Viewer for Haiku (EPUB, ComicBook, DjVu, and PDF)

Hey everyone,

I wanted to introduce a project I’ve been actively developing for Haiku called Toji.

Originally started as a fork of the classic BePDF, Toji has evolved far beyond standard PDF viewing into a modern, standards-based, semantic document viewer designed to leverage Haiku's native strengths.

## Multi-Format Document Support

While PDF support remains rock-solid, Toji expands format coverage significantly:

⚬ Full eBook Support: Complete rendering and navigation for EPUB (up to EPUB v3).
⚬ ComicBook Archives: Native handling of compressed comic formats (CBZ, CBA, CB7, CBR).
⚬ PDF Engine: Preserved and modernized from BePDF.
⚬ DjVu: Support coming soon.

## Semantic Features & BFS Integration

⚬ Standards-Based WebAnnotations: Full support for document annotations following the W3C WebAnnotation specification across all supported formats. Annotations, highlights, and notes remain open, portable, and interoperable rather than locked into a proprietary viewer format.
⚬ Native BFS Metadata: Deep integration with Haiku's file system attributes using established schemas. Document metadata, reading progress, and annotations are stored as BFS attributes, making them directly queryable through native Haiku Queries and Tracker.
⚬ Deep Linking: Precise URI/deep-linking support, allowing you to link directly to specific sections, pages, chapters, or highlighted passages within any document across the system.

## Showcase for SEN (Semantic Extensions Native)

Toji serves both as a versatile daily document viewer for Haiku and as a showcase/entry point into SEN (Semantic Extensions Native), demonstrating how native OS file attributes, semantic schemas, and open standards can seamlessly blend on Haiku. More details on the broader SEN ecosystem will be released later this year.

## Source Code & Project Status

The project is live and open source:
👉 https://github.com/sen-laboratories/Toji

It’s an active work in progress, and I’d love for anyone interested in semantic document handling, EPUB/Comic reading on Haiku, or BFS attribute workflows to check it out and share feedback!

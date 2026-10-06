#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Gregor B. Rosenauer & Claude
# SPDX-License-Identifier: CC-BY-4.0
#
# Makes the program icon of Toji (toji.hvif, a Haiku vector icon): three books bound together with three bookmarks, after the
# logo. The icon is made of flat polygons (no strokes) in a space of 64 by 64. Run: python3 make-toji-icon.py toji.hvif
import struct
import sys

INK = (0x36, 0x32, 0x2f)
PAPER = (0xf6, 0xf1, 0xe5)
BOOKS = [  # from the bottom: the cover and its light top
    ((0xe0, 0xa4, 0x95), (0xed, 0xc4, 0xb8)),
    ((0xe4, 0xcc, 0xa8), (0xf0, 0xe2, 0xc8)),
    ((0x9a, 0xb7, 0x9a), (0xb9, 0xd1, 0xb6)),
]
MARKS = [(0xd1, 0x40, 0x2f), (0x2a, 0x73, 0xd0), (0xf2, 0xc2, 0x30)]  # red, blue, yellow


def shade(rgb, factor):
    return tuple(int(c * factor) for c in rgb)


def offset_convex(points, d):
    # the polygon pushed outwards by d (for a convex polygon in clockwise order on the screen)
    n = len(points)
    lines = []
    for i in range(n):
        (x1, y1), (x2, y2) = points[i], points[(i + 1) % n]
        dx, dy = x2 - x1, y2 - y1
        length = (dx * dx + dy * dy) ** 0.5
        nx, ny = dy / length, -dx / length  # outward for clockwise (y down)
        lines.append((x1 + nx * d, y1 + ny * d, dx, dy))
    out = []
    for i in range(n):
        ax, ay, adx, ady = lines[i - 1]
        bx, by, bdx, bdy = lines[i]
        det = adx * bdy - ady * bdx
        t = ((bx - ax) * bdy - (by - ay) * bdx) / det
        out.append((ax + adx * t, ay + ady * t))
    return out


class Icon:
    def __init__(self):
        self.styles = []
        self.paths = []
        self.shapes = []

    def style(self, rgb):
        if rgb not in self.styles:
            self.styles.append(rgb)
        return self.styles.index(rgb)

    def polygon(self, points, rgb):
        self.paths.append(points)
        self.shapes.append((self.style(rgb), len(self.paths) - 1))

    def coordinate(self, value):
        n = int(round((value + 128.0) * 102.0))
        return bytes([0x80 | (n >> 8), n & 0xff])

    def data(self):
        out = bytearray(b'ncif')
        out.append(len(self.styles))
        for r, g, b in self.styles:
            out += bytes([3, r, g, b])  # a solid color without alpha
        out.append(len(self.paths))
        for points in self.paths:
            out += bytes([0x0a, len(points)])  # closed, no curves
            for x, y in points:
                out += self.coordinate(x) + self.coordinate(y)
        out.append(len(self.shapes))
        for style, path in self.shapes:
            out += bytes([10, style, 1, path, 0])  # a shape from one path, no transform
        return bytes(out)


def build():
    icon = Icon()
    # isometric: u runs to the right and down, v to the right and up, a book is a box of A (along u) by B (along v) by T (height)
    c, s = 0.866, 0.5
    A, B, T = 34.0, 26.0, 8.0

    def add(p, q, k=1.0):
        return (p[0] + q[0] * k, p[1] + q[1] * k)

    u, v, up = (c, s), (c, -s), (0.0, -1.0)
    # from the bottom: where the left corner of the top face of each book is
    tops = [(6.5, 34.0), (4.0, 26.0), (7.0, 18.0)]
    spans = []
    for (cover, light), p0 in zip(BOOKS, tops):
        p1 = add(p0, u, A)
        p2 = add(p1, v, B)
        p3 = add(p0, v, B)
        down = (0.0, T)
        hexagon = [p0, p3, p2, add(p2, down), add(p1, down), add(p0, down)]
        icon.polygon(offset_convex(hexagon, 1.1), INK)
        # the spine and the cover on the left, the pages on the right (with the boards of the cover above and below)
        icon.polygon([p0, p1, add(p1, down), add(p0, down)], shade(cover, 0.86))
        icon.polygon([p1, p2, add(p2, down), add(p1, down)], PAPER)
        for lo, hi, rgb in ((0.0, 1.5, cover), (T - 1.5, T, cover)):
            icon.polygon([add(p1, up, -lo), add(p2, up, -lo), add(add(p2, up, -lo), (0, 1)), add(p1, (0, 1), 1)] if False else
                         [add(p1, (0, lo)), add(p2, (0, lo)), add(p2, (0, hi)), add(p1, (0, hi))], shade(cover, 0.92))
        for z in (3.1, 4.9):
            icon.polygon([add(p1, (0, z)), add(p2, (0, z)), add(p2, (0, z + 0.55)), add(p1, (0, z + 0.55))], (0xd9, 0xd0, 0xbc))
        icon.polygon([p0, p1, p2, p3], light)
        spans.append((p0, p1))
    # the bookmarks hang out of the spine of each book, over the book below
    for rgb, (p0, p1), at in zip(MARKS, spans, (0.30, 0.52, 0.22)):
        r = add(p0, u, A * at)
        w = 4.2
        ribbon = [r, add(r, u, w), add(add(r, u, w), (0, 11.5)), add(add(r, u, w / 2), (0, 8.8)), add(r, (0, 11.5))]
        icon.polygon(ribbon, rgb)
    return icon.data()


if __name__ == '__main__':
    data = build()
    open(sys.argv[1], 'wb').write(data)
    print(len(data), 'bytes', file=sys.stderr)

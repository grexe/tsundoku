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
    W, H, DX, DY = 44.0, 13.0, 13.0, -7.0
    shift = (3.5, -2.0)

    def at(x0, y0, points):
        return [(x0 + x + shift[0], y0 + y + shift[1]) for x, y in points]

    fronts = [44.0, 32.0, 20.0]
    for (cover, light), y0 in zip(BOOKS, fronts):
        hexagon = [(0, 0), (DX, DY), (W + DX, DY), (W + DX, DY + H), (W, H), (0, H)]
        icon.polygon(at(0, y0, offset_convex(hexagon, 1.1)), INK)
        icon.polygon(at(0, y0, [(W, 0), (W + DX, DY), (W + DX, DY + H), (W, H)]), shade(cover, 0.82))
        icon.polygon(at(0, y0, [(0, 0), (W, 0), (W, H), (0, H)]), cover)
        icon.polygon(at(0, y0, [(2.6, 2.4), (W, 2.4), (W, H - 2.4), (2.6, H - 2.4)]), PAPER)
        icon.polygon(at(0, y0, [(0, 0), (DX, DY), (W + DX, DY), (W, 0)]), light)
    for rgb, x, y0 in zip(MARKS, (8.0, 16.0, 11.0), fronts):
        ribbon = [(0, 0), (4.8, 0), (4.8, 11), (2.4, 8.4), (0, 11)]
        icon.polygon(at(x, y0 + 2.0, offset_convex(ribbon, 0.0)), rgb)
    return icon.data()


if __name__ == '__main__':
    data = build()
    open(sys.argv[1], 'wb').write(data)
    print(len(data), 'bytes', file=sys.stderr)

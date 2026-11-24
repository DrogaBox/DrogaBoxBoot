#!/usr/bin/env python3
# Turns the .bgra snapshots of Canopy into PNG files next to them.
#   python3 png.py <file.bgra>...
# Copyright (C) 2026, DrogaBox. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

import struct
import sys
import zlib


def convert(path):
    data = open(path, 'rb').read()
    width, height, magic, _ = struct.unpack('<IIII', data[:16])
    if magic != 0x41524742:
        raise SystemExit('%s: not a Canopy snapshot' % path)
    pixels = data[16:]
    rows = bytearray()
    for y in range(height):
        rows.append(0)
        row = pixels[y * width * 4:(y + 1) * width * 4]
        for x in range(0, len(row), 4):
            rows += bytes((row[x + 2], row[x + 1], row[x]))

    def chunk(kind, body):
        return struct.pack('>I', len(body)) + kind + body + struct.pack('>I', zlib.crc32(kind + body) & 0xffffffff)

    png = b'\x89PNG\r\n\x1a\n'
    png += chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 2, 0, 0, 0))
    png += chunk(b'IDAT', zlib.compress(bytes(rows), 9))
    png += chunk(b'IEND', b'')
    out = path[:-5] + '.png' if path.endswith('.bgra') else path + '.png'
    open(out, 'wb').write(png)
    print(out)


for arg in sys.argv[1:]:
    convert(arg)

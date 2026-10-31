#!/usr/bin/env python3
"""Render the DeMic application icon.

The icon is described as vector geometry in a 256 x 256 design space:

    * a rounded-square background filled with a vertical gradient,
    * a white microphone (capsule head, U shaped cradle, stem and base),
    * a red "muted" slash drawn on top of the microphone.

Running this script rewrites:

    icons/design/DeMic-<size>.png   one PNG per icon size
    DeMic.ico                       the icon resource used by DeMic.rc

Requires only the Python standard library.
"""

import math
import os
import struct
import zlib

DESIGN = 256.0
SIZES = (256, 128, 64, 48, 40, 32, 24, 20, 16)

WHITE = (255, 255, 255)
SLASH_COLOR = (229, 72, 77)

BACKGROUND_RADIUS = 56.35
BACKGROUND_TOP = (32, 52, 80)
BACKGROUND_BOTTOM = (13, 24, 40)

HEAD_CENTER_X = 128.0
HEAD_RADIUS = 34.0
HEAD_TOP = 25.5
HEAD_BOTTOM = 144.0

CRADLE_CENTER_X = 128.0
CRADLE_CENTER_Y = 115.12
CRADLE_OUTER_RADIUS = 60.79
CRADLE_INNER_RADIUS = 42.29
CRADLE_MID_RADIUS = (CRADLE_OUTER_RADIUS + CRADLE_INNER_RADIUS) / 2.0
CRADLE_HALF_THICKNESS = (CRADLE_OUTER_RADIUS - CRADLE_INNER_RADIUS) / 2.0
ARM_TOP = 113.0

STEM_LEFT = 118.76
STEM_RIGHT = 137.24
# the stem spans into the cradle and the base so the union has no seams
STEM_TOP = 165.0
STEM_BOTTOM = 212.0

BASE_LEFT = 79.32
BASE_RIGHT = 176.68
BASE_TOP = 207.0
BASE_BOTTOM = 229.0
BASE_RADIUS = 11.0

SLASH_START = (54.27, 54.27)
SLASH_END = (201.73, 201.73)
SLASH_RADIUS = 10.9


def rounded_rect_distance(x, y, left, top, right, bottom, radius):
    half_w = (right - left) / 2.0 - radius
    half_h = (bottom - top) / 2.0 - radius
    qx = abs(x - (left + right) / 2.0) - half_w
    qy = abs(y - (top + bottom) / 2.0) - half_h
    outside = math.hypot(max(qx, 0.0), max(qy, 0.0))
    return outside + min(max(qx, qy), 0.0) - radius


def capsule_distance(x, y, start, end, radius):
    dx = end[0] - start[0]
    dy = end[1] - start[1]
    length_sq = dx * dx + dy * dy
    t = ((x - start[0]) * dx + (y - start[1]) * dy) / length_sq
    t = min(1.0, max(0.0, t))
    return math.hypot(x - (start[0] + t * dx), y - (start[1] + t * dy)) - radius


def head_distance(x, y):
    return capsule_distance(
        x, y,
        (HEAD_CENTER_X, HEAD_TOP + HEAD_RADIUS),
        (HEAD_CENTER_X, HEAD_BOTTOM - HEAD_RADIUS),
        HEAD_RADIUS)


def cradle_distance(x, y):
    # The cradle is the stroke of a U shaped path with square ends: a vertical
    # arm, a half circle and a second vertical arm, all with the same thickness.
    dx = x - CRADLE_CENTER_X
    dy = y - CRADLE_CENTER_Y
    if dy > 0.0:
        path = abs(math.hypot(dx, dy) - CRADLE_MID_RADIUS)
    else:
        path = math.hypot(abs(dx) - CRADLE_MID_RADIUS, max(0.0, ARM_TOP - y))
    return max(path - CRADLE_HALF_THICKNESS, ARM_TOP - y)


def microphone_distance(x, y):
    return min(head_distance(x, y),
               cradle_distance(x, y),
               rounded_rect_distance(x, y, STEM_LEFT, STEM_TOP, STEM_RIGHT,
                                     STEM_BOTTOM, 0.0),
               rounded_rect_distance(x, y, BASE_LEFT, BASE_TOP, BASE_RIGHT,
                                     BASE_BOTTOM, BASE_RADIUS))


def slash_distance(x, y):
    return capsule_distance(x, y, SLASH_START, SLASH_END, SLASH_RADIUS)


def coverage(distance, scale):
    """Anti-aliased coverage of a shape from its signed distance (design units)."""
    return min(1.0, max(0.0, 0.5 - distance * scale))


def background_color(y):
    t = min(1.0, max(0.0, y / (DESIGN - 1.0)))
    return tuple(BACKGROUND_TOP[i] + (BACKGROUND_BOTTOM[i] - BACKGROUND_TOP[i]) * t
                 for i in range(3))


def render(size):
    scale = size / DESIGN
    pixels = bytearray(size * size * 4)
    for py in range(size):
        v = (py + 0.5) / scale
        color = background_color(v)
        row = py * size * 4
        for px in range(size):
            u = (px + 0.5) / scale
            alpha = coverage(rounded_rect_distance(u, v, 0.0, 0.0, DESIGN, DESIGN,
                                                   BACKGROUND_RADIUS), scale)
            if alpha <= 0.0:
                continue
            mic = coverage(microphone_distance(u, v), scale)
            slash = coverage(slash_distance(u, v), scale)
            r, g, b = color
            r = r * (1.0 - mic) + WHITE[0] * mic
            g = g * (1.0 - mic) + WHITE[1] * mic
            b = b * (1.0 - mic) + WHITE[2] * mic
            r = r * (1.0 - slash) + SLASH_COLOR[0] * slash
            g = g * (1.0 - slash) + SLASH_COLOR[1] * slash
            b = b * (1.0 - slash) + SLASH_COLOR[2] * slash
            o = row + px * 4
            pixels[o] = int(r + 0.5)
            pixels[o + 1] = int(g + 0.5)
            pixels[o + 2] = int(b + 0.5)
            pixels[o + 3] = int(alpha * 255.0 + 0.5)
    return pixels


def png_chunk(tag, data):
    return (struct.pack('>I', len(data)) + tag + data
            + struct.pack('>I', zlib.crc32(tag + data) & 0xffffffff))


def png_bytes(size, rgba):
    raw = b''.join(b'\x00' + rgba[y * size * 4:(y + 1) * size * 4]
                   for y in range(size))
    return (b'\x89PNG\r\n\x1a\n'
            + png_chunk(b'IHDR', struct.pack('>IIBBBBB', size, size, 8, 6, 0, 0, 0))
            + png_chunk(b'IDAT', zlib.compress(raw, 9))
            + png_chunk(b'IEND', b''))


def bmp_frame(size, rgba):
    """32bpp bottom-up DIB plus the 1bpp AND mask, as stored inside .ico files."""
    xor = bytearray()
    for y in range(size - 1, -1, -1):
        row = rgba[y * size * 4:(y + 1) * size * 4]
        for x in range(size):
            r, g, b, a = row[x * 4:x * 4 + 4]
            xor += bytes((b, g, r, a))
    mask = bytearray()
    mask_stride = ((size + 31) // 32) * 4
    for y in range(size - 1, -1, -1):
        bits = bytearray(mask_stride)
        row = rgba[y * size * 4:(y + 1) * size * 4]
        for x in range(size):
            if row[x * 4 + 3] == 0:
                bits[x // 8] |= 0x80 >> (x % 8)
        mask += bits
    header = struct.pack('<IiiHHIIiiII', 40, size, size * 2, 1, 32, 0,
                         len(xor) + len(mask), 0, 0, 0, 0)
    return header + bytes(xor) + bytes(mask)


def ico_bytes(frames):
    """frames: list of (size, rgba); PNG is used for 256/128, DIB otherwise."""
    payloads = []
    for size, rgba in frames:
        payloads.append(png_bytes(size, rgba) if size >= 128 else bmp_frame(size, rgba))
    offset = 6 + 16 * len(payloads)
    out = bytearray(struct.pack('<HHH', 0, 1, len(payloads)))
    for (size, _), payload in zip(frames, payloads):
        out += struct.pack('<BBBBHHII', size % 256, size % 256, 0, 0, 1, 32,
                           len(payload), offset)
        offset += len(payload)
    for payload in payloads:
        out += payload
    return bytes(out)


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    design_dir = os.path.join(here, 'design')
    ico_path = os.path.join(here, os.pardir, 'DeMic.ico')
    os.makedirs(design_dir, exist_ok=True)

    frames = []
    for size in SIZES:
        rgba = render(size)
        with open(os.path.join(design_dir, 'DeMic-%d.png' % size), 'wb') as f:
            f.write(png_bytes(size, rgba))
        frames.append((size, rgba))
        print('rendered %dx%d' % (size, size))

    with open(ico_path, 'wb') as f:
        f.write(ico_bytes(frames))
    print('wrote %s' % os.path.normpath(ico_path))


if __name__ == '__main__':
    main()

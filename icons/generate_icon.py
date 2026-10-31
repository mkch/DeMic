#!/usr/bin/env python3
"""Render the DeMic application icon and the tray notification icons.

The application icon is described as vector geometry in a 256 x 256 design space:

    * a rounded-square background filled with a vertical gradient,
    * a white microphone (capsule head, U shaped cradle, stem and base),
    * a red "muted" slash drawn on top of the microphone.

It also renders the tray notification icons.  They reuse the same microphone,
widened horizontally by TRAY_STRETCH and scaled uniformly so that it fills the
canvas, and outlined in black so that it stays readable on light desktop themes:

    microphone.ico          plain microphone
    microphone_muted.ico    microphone with a red slash
    no_microphone.ico       microphone with a red cross
    update_available.ico    microphone with a blue update badge

Running this script rewrites:

    app-logo.png   the 512 px application icon (solution root)
    DeMic.ico     the icon resource used by DeMic.rc
    <name>.ico    the tray icon resources

Requires only the Python standard library.
"""

import math
import os
import struct
import zlib

DESIGN = 256.0
SIZES = (256, 128, 64, 48, 40, 32, 24, 20, 16)
APP_LOGO_SIZE = 512

WHITE = (255, 255, 255)
SLASH_COLOR = (229, 72, 77)

BACKGROUND_RADIUS = 56.35
BACKGROUND_TOP = (32, 52, 80)
BACKGROUND_BOTTOM = (13, 24, 40)

HEAD_CENTER_X = 128.0
CRADLE_CENTER_X = 128.0
CRADLE_CENTER_Y = 115.12

HEAD_RADIUS = 39.1
HEAD_TOP = 25.5

CRADLE_OUTER_RADIUS = 65.89
CRADLE_INNER_RADIUS = 47.39
CRADLE_MID_RADIUS = (CRADLE_OUTER_RADIUS + CRADLE_INNER_RADIUS) / 2.0
CRADLE_HALF_THICKNESS = (CRADLE_OUTER_RADIUS - CRADLE_INNER_RADIUS) / 2.0
ARM_TOP = 113.0

STEM_HALF_WIDTH = 13.86
# the stem spans into the cradle and the base so the union has no seams
STEM_TOP = 165.0
STEM_BOTTOM = 212.0
# how far the stem reaches past the inner edge of the cradle, which keeps its top
# edge hidden behind the cradle arc whatever the cradle radius is
STEM_OVERLAP = STEM_TOP - (CRADLE_CENTER_Y + CRADLE_INNER_RADIUS)

BASE_LEFT = 79.32
BASE_RIGHT = 176.68
BASE_TOP = 207.0
BASE_BOTTOM = 229.0
BASE_RADIUS = 11.0

SLASH_START = (54.27, 54.27)
SLASH_END = (201.73, 201.73)
SLASH_RADIUS = 10.9

BLACK = (0, 0, 0)

# --- tray notification icons -------------------------------------------------

# The tray microphone is widened horizontally: the head's cap radius, the cradle
# radii and the stem width are all enlarged by this factor while every circle
# keeps its centre.  See microphone_distance().
TRAY_STRETCH = 1.15

TRAY_NAMES = ('microphone', 'microphone_muted', 'no_microphone', 'update_available')
TRAY_SIZES = (64, 48, 40, 32, 24, 20, 16)
TRAY_OUTLINE_LIMIT = 24
TRAY_OUTLINE_SMALL = 1.0
TRAY_OUTLINE_LARGE = 2.0

# The no-microphone mark is a red cross centred in the microphone head.  The
# tray microphone is widened by TRAY_STRETCH, which adds to the cap radius and
# therefore lowers the bottom of the head, so the centre follows it.
CROSS_CENTER = (HEAD_CENTER_X,
                (HEAD_TOP + CRADLE_CENTER_Y + HEAD_RADIUS * TRAY_STRETCH) / 2.0)
CROSS_HALF_SPAN = 24.0
CROSS_HALF_WIDTH = 7.5
# the cross keeps a minimum stroke width so that it stays readable when the
# tray icon is drawn small
CROSS_MIN_WIDTH = 1.6

BADGE_COLOR = (10, 132, 216)
BADGE_CENTER = 0.755
BADGE_RADIUS = 0.235
BADGE_SHAFT_HALF_WIDTH = 0.17
BADGE_SHAFT_TOP = -0.58
BADGE_SHAFT_BOTTOM = 0.03
BADGE_HEAD_HALF_WIDTH = 0.42
BADGE_HEAD_TIP = 0.52


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
    if length_sq <= 0.0:
        return math.hypot(x - start[0], y - start[1]) - radius
    t = ((x - start[0]) * dx + (y - start[1]) * dy) / length_sq
    t = min(1.0, max(0.0, t))
    return math.hypot(x - (start[0] + t * dx), y - (start[1] + t * dy)) - radius


def head_distance(x, y, radius=HEAD_RADIUS):
    # The head ends on a cap whose centre is the cradle centre, which keeps the
    # gap between the head and the cradle even.  A head that is shorter than it is
    # wide degenerates into a circle, so the cap radius cannot exceed half the
    # height.
    bottom = CRADLE_CENTER_Y + radius
    radius = min(radius, (bottom - HEAD_TOP) / 2.0)
    return capsule_distance(
        x, y,
        (HEAD_CENTER_X, HEAD_TOP + radius),
        (HEAD_CENTER_X, bottom - radius),
        radius)


def cradle_distance(x, y, mid_radius=CRADLE_MID_RADIUS,
                    half_thickness=CRADLE_HALF_THICKNESS):
    # The cradle is the stroke of a U shaped path with round ends: a vertical
    # arm, a half circle and a second vertical arm, all with the same thickness.
    # Rounding the ends with the radius of the stroke turns the flat tops into
    # domes, which keeps their topmost point at ARM_TOP.
    dx = x - CRADLE_CENTER_X
    dy = y - CRADLE_CENTER_Y
    across = abs((math.hypot(dx, dy) if dy > 0.0 else abs(dx)) - mid_radius)
    along = ARM_TOP + half_thickness - y
    return (math.hypot(max(across, 0.0), max(along, 0.0))
            + min(max(across, along), 0.0) - half_thickness)


def stretched_geometry(stretch):
    """Cap radius, cradle radii and stem size of the microphone widened by `stretch`.

    The cap radius and the stem width are multiplied by `stretch`, while the
    cradle radii grow by the same absolute amount as the cap radius, so the gap
    between the head and the cradle stays even and the cradle keeps its
    thickness.  Every centre stays where it is.
    """
    head_radius = HEAD_RADIUS * stretch
    growth = head_radius - HEAD_RADIUS
    inner = CRADLE_INNER_RADIUS + growth
    outer = CRADLE_OUTER_RADIUS + growth
    return (head_radius, inner, outer,
            STEM_HALF_WIDTH * stretch,
            CRADLE_CENTER_Y + inner + STEM_OVERLAP)


def microphone_distance(x, y, stretch=1.0):
    """Widen the microphone horizontally by `stretch`; see stretched_geometry()."""
    head_radius, inner, outer, stem_half, stem_top = stretched_geometry(stretch)
    return min(head_distance(x, y, head_radius),
               cradle_distance(x, y, (inner + outer) / 2.0,
                               (outer - inner) / 2.0),
               rounded_rect_distance(x, y, HEAD_CENTER_X - stem_half, stem_top,
                                     HEAD_CENTER_X + stem_half, STEM_BOTTOM, 0.0),
               rounded_rect_distance(x, y, BASE_LEFT, BASE_TOP, BASE_RIGHT,
                                     BASE_BOTTOM, BASE_RADIUS))


def slash_distance(x, y):
    return capsule_distance(x, y, SLASH_START, SLASH_END, SLASH_RADIUS)


def cross_distance(x, y, half_width):
    """Distance to the red cross drawn in the microphone head."""
    cx, cy = CROSS_CENTER
    return min(capsule_distance(x, y, (cx - CROSS_HALF_SPAN, cy - CROSS_HALF_SPAN),
                                (cx + CROSS_HALF_SPAN, cy + CROSS_HALF_SPAN),
                                half_width),
               capsule_distance(x, y, (cx - CROSS_HALF_SPAN, cy + CROSS_HALF_SPAN),
                                (cx + CROSS_HALF_SPAN, cy - CROSS_HALF_SPAN),
                                half_width))


def coverage(distance, scale):
    """Anti-aliased coverage of a shape from its signed distance (design units)."""
    return min(1.0, max(0.0, 0.5 - distance * scale))


def polygon_distance(x, y, points):
    """Distance to a convex polygon whose vertices are given clockwise on screen."""
    inside = 1e9
    for i in range(len(points)):
        ax, ay = points[i]
        bx, by = points[(i + 1) % len(points)]
        ex, ey = bx - ax, by - ay
        inside = min(inside, ((x - ax) * -ey + (y - ay) * ex) / math.hypot(ex, ey))
    return -inside


def badge_arrow_distance(x, y):
    """Distance to the white arrow of the update badge, in units of its radius."""
    shaft = rounded_rect_distance(x, y, -BADGE_SHAFT_HALF_WIDTH, BADGE_SHAFT_TOP,
                                 BADGE_SHAFT_HALF_WIDTH, BADGE_SHAFT_BOTTOM, 0.0)
    head = polygon_distance(x, y, ((-BADGE_HEAD_HALF_WIDTH, BADGE_SHAFT_BOTTOM),
                                   (BADGE_HEAD_HALF_WIDTH, BADGE_SHAFT_BOTTOM),
                                   (0.0, BADGE_HEAD_TIP)))
    return min(shaft, head)


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


def tray_outline_width(size):
    return TRAY_OUTLINE_SMALL if size <= TRAY_OUTLINE_LIMIT else TRAY_OUTLINE_LARGE


def tray_mapping(size):
    """Map canvas pixels onto the design space of the application icon.

    The microphone is widened by TRAY_STRETCH in microphone_distance() and then
    scaled uniformly so that it fills the canvas height, which leaves exactly the
    outline width free on the top and bottom edges.
    """
    stroke = tray_outline_width(size)
    scale = (size - 2.0 * stroke) / (BASE_BOTTOM - HEAD_TOP)
    return stroke, scale


def overlay(r, g, b, alpha, amount, color):
    """Source-over composite of `color` with the given coverage."""
    if amount <= 0.0:
        return r, g, b, alpha
    r = r * (1.0 - amount) + color[0] * amount
    g = g * (1.0 - amount) + color[1] * amount
    b = b * (1.0 - amount) + color[2] * amount
    return r, g, b, alpha * (1.0 - amount) + amount


def render_tray(size, name):
    stroke, scale = tray_mapping(size)
    badge_gap = stroke
    badge_center = BADGE_CENTER * size
    badge_radius = BADGE_RADIUS * size
    cross_half_width = max(CROSS_HALF_WIDTH, CROSS_MIN_WIDTH / (2.0 * scale))

    pixels = bytearray(size * size * 4)
    for py in range(size):
        row = py * size * 4
        y = py + 0.5
        for px in range(size):
            x = px + 0.5
            u = HEAD_CENTER_X + (x - size / 2.0) / scale
            v = HEAD_TOP + (y - stroke) / scale
            distance = microphone_distance(u, v, TRAY_STRETCH) * scale

            white = coverage(distance, 1.0)
            alpha = coverage(distance - stroke, 1.0)
            black = alpha - white
            total = white + black
            if total > 0.0:
                r = (WHITE[0] * white + BLACK[0] * black) / total
                g = (WHITE[1] * white + BLACK[1] * black) / total
                b = (WHITE[2] * white + BLACK[2] * black) / total
            else:
                r = g = b = 0.0

            if name == 'microphone_muted':
                slash = coverage(slash_distance(u, v) * scale, 1.0)
                r, g, b, alpha = overlay(r, g, b, alpha, slash, SLASH_COLOR)
            elif name == 'no_microphone':
                mark = coverage(cross_distance(u, v, cross_half_width) * scale, 1.0)
                r, g, b, alpha = overlay(r, g, b, alpha, mark, SLASH_COLOR)
            elif name == 'update_available':
                dx = x - badge_center
                dy = y - badge_center
                radial = math.hypot(dx, dy)
                # keep a transparent gap so the badge does not touch the microphone
                alpha *= 1.0 - coverage(radial - badge_radius - badge_gap, 1.0)
                badge = coverage(radial - badge_radius, 1.0)
                r, g, b, alpha = overlay(r, g, b, alpha, badge, BADGE_COLOR)
                arrow = coverage(badge_arrow_distance(dx / badge_radius,
                                                      dy / badge_radius)
                                 * badge_radius, 1.0)
                r, g, b, alpha = overlay(r, g, b, alpha, arrow, WHITE)

            o = row + px * 4
            pixels[o] = int(r + 0.5)
            pixels[o + 1] = int(g + 0.5)
            pixels[o + 2] = int(b + 0.5)
            pixels[o + 3] = int(alpha * 255.0 + 0.5)
    return pixels


def png_chunk(tag, data):
    return (struct.pack('>I', len(data)) + tag + data
            + struct.pack('>I', zlib.crc32(tag + data) & 0xffffffff))


def png_bytes(size, rgba, height=None):
    """PNG of an RGBA bitmap; `size` is its width, `height` defaults to it."""
    height = size if height is None else height
    raw = b''.join(b'\x00' + rgba[y * size * 4:(y + 1) * size * 4]
                   for y in range(height))
    return (b'\x89PNG\r\n\x1a\n'
            + png_chunk(b'IHDR', struct.pack('>IIBBBBB', size, height, 8, 6, 0, 0, 0))
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
    root_dir = os.path.normpath(os.path.join(here, os.pardir))

    frames = [(size, render(size)) for size in SIZES]
    write_ico(os.path.join(root_dir, 'DeMic.ico'), frames)

    write_png(os.path.join(root_dir, 'app-logo.png'), APP_LOGO_SIZE,
              render(APP_LOGO_SIZE))

    for name in TRAY_NAMES:
        frames = []
        for size in TRAY_SIZES:
            rgba = render_tray(size, name)
            frames.append((size, rgba))
        write_ico(os.path.join(root_dir, name + '.ico'), frames)


def write_png(path, size, rgba):
    with open(path, 'wb') as f:
        f.write(png_bytes(size, rgba))
    print('wrote %s' % path)


def write_ico(path, frames):
    with open(path, 'wb') as f:
        f.write(ico_bytes(frames))
    print('wrote %s' % path)


if __name__ == '__main__':
    main()

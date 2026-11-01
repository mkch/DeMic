#!/usr/bin/env python3
"""Development tools for the DeMic icon geometry.

Nothing here writes the icon files that ships with the application: the tools
render previews into icons/.preview/, inspect the geometry numerically and
compare the files on disk with what generate_icon.py would produce.  Every
constant and distance function comes from generate_icon, so the geometry has a
single source of truth.

    frames      unpack the frames of an .ico into PNGs and print its table
    preview     render the microphone silhouette, optionally cropped and zoomed
    tray        render the tray icons at the given sizes
    sheet       stitch the tray icons into one image, one row per name, one
                column per size, upscaled with nearest neighbour so that the
                smallest sizes can be inspected pixel by pixel
    probe       scan a row/column or find an extreme point of the geometry
    invariants  print the key dimensions and assert the design invariants
    check       verify the files on disk against the script and the golden hashes
    verify      run the whole check of one editing round: previews, files, geometry

The bytecode cache that importing generate_icon leaves behind in
icons/__pycache__ makes later runs start a little faster and is ignored by git.

Examples:

    python icons/icon_tools.py verify
    python icons/icon_tools.py preview --crop 105,155,151,195 --zoom 22
    python icons/icon_tools.py sheet --names no_microphone --sizes 16,24,32,64
    python icons/icon_tools.py sheet --sizes 16,32,64 --zoom 6 --crop 78,20,178,165
    python icons/icon_tools.py probe --y 168 --part mic
    python icons/icon_tools.py invariants
    python icons/icon_tools.py check
    python icons/icon_tools.py check --update-golden
"""

import sys

import argparse
import ast
import hashlib
import json
import math
import os
import struct

HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
    sys.path.insert(0, HERE)

import generate_icon as icon

ROOT = os.path.dirname(HERE)
PREVIEW_DIR = os.path.join(HERE, '.preview')
GOLDEN = os.path.join(HERE, 'icon-golden.json')

INSIDE = 0.0

TRAY_STEMS = [stem for stem, _, _ in icon.tray_icons()]

# The previews composite the icons over a mid grey so that the white and the
# black microphone are both visible.
SHEET_BACKGROUND = 0x80
SHEET_PIXEL = bytes((SHEET_BACKGROUND, SHEET_BACKGROUND, SHEET_BACKGROUND, 255))


# --- file helpers -------------------------------------------------------------

def write_file(path, data):
    """Write bytes, falling back to a numbered name while the file is locked."""
    directory = os.path.dirname(path)
    if directory:
        os.makedirs(directory, exist_ok=True)
    stem, ext = os.path.splitext(path)
    candidate = path
    for suffix in [''] + ['-%d' % i for i in range(1, 100)]:
        candidate = stem + suffix + ext
        try:
            with open(candidate, 'wb') as f:
                f.write(data)
            return candidate
        except PermissionError:
            continue
    raise PermissionError(path)


def compare_bytes(expected, actual, label):
    """Exact content comparison, reporting the first difference."""
    if expected == actual:
        return True
    limit = min(len(expected), len(actual))
    offset = limit
    for i in range(limit):
        if expected[i] != actual[i]:
            offset = i
            break
    print('%s: MISMATCH at offset %d (%s != %s), sizes %d/%d'
          % (label, offset,
             expected[offset:offset + 1].hex() or 'EOF',
             actual[offset:offset + 1].hex() or 'EOF',
             len(expected), len(actual)))
    return False


def digest(data):
    """Fingerprint used only for the persisted golden baseline."""
    return hashlib.blake2b(data, digest_size=16).hexdigest()


# --- .ico inspection ----------------------------------------------------------

def read_ico(path):
    """Return (type, frames) where each frame is a dict."""
    with open(path, 'rb') as f:
        data = f.read()
    _, kind, count = struct.unpack('<HHH', data[:6])
    frames = []
    for i in range(count):
        width, _, _, _, _, _, size, offset = struct.unpack(
            '<BBBBHHII', data[6 + 16 * i:22 + 16 * i])
        payload = data[offset:offset + size]
        frames.append({
            'size': width or 256,
            'kind': 'png' if payload[:4] == b'\x89PNG' else 'dib',
            'payload': payload,
        })
    return kind, frames


def dib_to_rgba(payload, size):
    """Decode a 32 bpp bottom-up DIB frame, ignoring the AND mask."""
    header = struct.unpack('<Iii', payload[:12])
    height = abs(header[2]) // 2
    stride = size * 4
    rgba = bytearray(size * size * 4)
    for y in range(height):
        source = header[0] + (height - 1 - y) * stride
        for x in range(size):
            b, g, r, a = payload[source + x * 4:source + x * 4 + 4]
            o = (y * size + x) * 4
            rgba[o:o + 4] = bytes((r, g, b, a))
    return rgba


def cmd_frames(args):
    files = args.files or [os.path.join(ROOT, 'DeMic.ico')] + [
        os.path.join(ROOT, stem + '.ico') for stem in TRAY_STEMS]
    wanted = None
    if args.sizes:
        wanted = {int(s) for s in args.sizes.split(',') if s.strip()}
    for path in files:
        kind, frames = read_ico(path)
        print('%s: type %d, %d frames' % (os.path.basename(path), kind, len(frames)))
        for frame in frames:
            if wanted is not None and frame['size'] not in wanted:
                continue
            if frame['kind'] == 'png':
                data = frame['payload']
            else:
                data = icon.png_bytes(frame['size'],
                                      dib_to_rgba(frame['payload'], frame['size']))
            name = '%s-%d.png' % (os.path.splitext(os.path.basename(path))[0],
                                  frame['size'])
            print('  %4d  %-3s  %s' % (frame['size'], frame['kind'],
                                       write_file(os.path.join(args.dir, name), data)))


# --- rendering ----------------------------------------------------------------

def render_silhouette(left, top, right, bottom, zoom,
                      stretch=1.0, mode='fill'):
    """Render the microphone as a greyscale PNG bitmap; returns (w, h, pixels)."""
    width = max(1, int(round((right - left) * zoom)))
    height = max(1, int(round((bottom - top) * zoom)))
    fill_level = 0.0 if mode == 'fill' else 170.0
    draw_fill = mode != 'outline'
    draw_outline = mode != 'fill'

    pixels = bytearray(width * height * 4)
    for py in range(height):
        v = top + (py + 0.5) / zoom
        for px in range(width):
            u = left + (px + 0.5) / zoom
            distance = icon.microphone_distance(u, v, stretch)
            fill = icon.coverage(distance, zoom) if draw_fill else 0.0
            line = icon.coverage(abs(distance) - 0.35, zoom) if draw_outline else 0.0
            value = 255.0 * (1.0 - fill) + fill_level * fill
            value *= 1.0 - line
            o = (py * width + px) * 4
            shade = int(value + 0.5)
            pixels[o] = pixels[o + 1] = pixels[o + 2] = shade
            pixels[o + 3] = 255
    return width, height, bytes(pixels)


def cmd_preview(args):
    left, top, right, bottom = (float(v) for v in args.crop.split(','))
    width, height, pixels = render_silhouette(left, top, right, bottom, args.zoom,
                                              args.stretch, args.mode)
    path = args.out or os.path.join(
        PREVIEW_DIR, 'silhouette-%s-%gx.png' % (args.crop.replace(',', '_'), args.zoom))
    print(write_file(path, icon.png_bytes(width, pixels)))
    print('%dx%d design %g..%g x %g..%g' % (width, height, left, right, top, bottom))


def tray_mark(text):
    """(mark name, microphone colour) of a base name or an icon file name stem."""
    for stem, name, fill in icon.tray_icons():
        if text == stem:
            return name, fill
    return text, icon.WHITE


def cmd_tray(args):
    sizes = [int(s) for s in args.sizes.split(',') if s.strip()]
    names = [n for n in args.names.split(',') if n.strip()]
    for name in names:
        mark, fill = tray_mark(name)
        for size in sizes:
            data = icon.png_bytes(size, icon.render_tray(size, mark, fill))
            path = os.path.join(args.dir, 'tray-%s-%d.png' % (name, size))
            print(write_file(path, data))


def tray_cell(size, name, crop, zoom):
    """One tray icon as an upscaled RGBA tile; `crop` is in design units.

    The zoom is a nearest neighbour one, so the tile shows the pixels the icon
    really has at that size.  A crop rectangle is mapped onto the canvas with
    the mapping the microphone itself uses, so it frames the same part of the
    design at every size.
    """
    mark, fill = tray_mark(name)
    rgba = icon.render_tray(size, mark, fill)
    if crop is None:
        left = top = 0.0
        width = height = float(size)
    else:
        scale = icon.tray_mapping(size)
        left = size / 2.0 + (crop[0] - icon.HEAD_CENTER_X) * scale
        top = (crop[1] - icon.HEAD_TOP) * scale
        width = (crop[2] - crop[0]) * scale
        height = (crop[3] - crop[1]) * scale
    cell_w = max(1, int(round(width * zoom)))
    cell_h = max(1, int(round(height * zoom)))
    pixels = bytearray(SHEET_PIXEL * (cell_w * cell_h))
    for py in range(cell_h):
        sy = int(math.floor(top + py / zoom))
        for px in range(cell_w):
            sx = int(math.floor(left + px / zoom))
            if not (0 <= sx < size and 0 <= sy < size):
                continue                     # outside the canvas stays background
            source = (sy * size + sx) * 4
            alpha = rgba[source + 3]
            o = (py * cell_w + px) * 4
            for i in range(3):
                pixels[o + i] = (SHEET_BACKGROUND * (255 - alpha)
                                 + rgba[source + i] * alpha) // 255
    return cell_w, cell_h, pixels


def cmd_sheet(args):
    """Stitch the tray icons into one image: a row per name, a column per size."""
    if args.zoom < 1:
        raise SystemExit('--zoom must be at least 1')
    sizes = [int(s) for s in args.sizes.split(',') if s.strip()]
    names = [n for n in args.names.split(',') if n.strip()]
    if not sizes or not names:
        raise SystemExit('nothing to render')
    crop = None
    if args.crop:
        crop = tuple(float(v) for v in args.crop.split(','))
        if len(crop) != 4:
            raise SystemExit('--crop needs left,top,right,bottom')
    gap = 8
    grid = [[tray_cell(size, name, crop, args.zoom) for size in sizes]
            for name in names]
    width = max(sum(cell[0] for cell in row) + gap * (len(row) - 1) for row in grid)
    height = (sum(max(cell[1] for cell in row) for row in grid)
              + gap * (len(grid) - 1))
    pixels = bytearray(SHEET_PIXEL * (width * height))
    top = 0
    for row in grid:
        left = 0
        for cell_w, cell_h, tile in row:
            for y in range(cell_h):
                start = ((top + y) * width + left) * 4
                pixels[start:start + cell_w * 4] = \
                    tile[y * cell_w * 4:(y + 1) * cell_w * 4]
            left += cell_w + gap
        top += max(cell[1] for cell in row) + gap
    if args.out:
        path = args.out
    else:
        label = '%s-%s-%gx' % ('+'.join(names),
                               '_'.join(str(size) for size in sizes), args.zoom)
        if crop is not None:
            label += '-crop'
        path = os.path.join(args.dir, 'sheet-%s.png' % label)
    print(write_file(path, icon.png_bytes(width, pixels, height)))
    print('%dx%d rows %s, columns %s' % (width, height, ' '.join(names),
                                         ' '.join(str(size) for size in sizes)))


# --- geometry probing ---------------------------------------------------------

PARTS = ('mic', 'head', 'cradle', 'stem', 'base')


def part_geometry(part, stretch=1.0):
    """Return (distance function, bounding box) of one part of the microphone."""
    head_radius, inner, outer, stem_half, stem_top = icon.stretched_geometry(stretch)
    centre = icon.HEAD_CENTER_X
    if part == 'head':
        return (lambda x, y: icon.head_distance(x, y, head_radius),
                (centre - head_radius, icon.HEAD_TOP,
                 centre + head_radius, icon.CRADLE_CENTER_Y + head_radius))
    if part == 'cradle':
        return (lambda x, y: icon.cradle_distance(x, y, (inner + outer) / 2.0,
                                                  (outer - inner) / 2.0),
                (icon.CRADLE_CENTER_X - outer, icon.ARM_TOP - 5.0,
                 icon.CRADLE_CENTER_X + outer, icon.CRADLE_CENTER_Y + outer))
    if part == 'stem':
        return (lambda x, y: icon.rounded_rect_distance(
                    x, y, centre - stem_half, stem_top,
                    centre + stem_half, icon.STEM_BOTTOM, 0.0),
                (centre - stem_half, stem_top - 2.0,
                 centre + stem_half, icon.STEM_BOTTOM))
    if part == 'base':
        return (lambda x, y: icon.rounded_rect_distance(
                    x, y, icon.BASE_LEFT, icon.BASE_TOP,
                    icon.BASE_RIGHT, icon.BASE_BOTTOM, icon.BASE_RADIUS),
                (icon.BASE_LEFT, icon.BASE_TOP, icon.BASE_RIGHT, icon.BASE_BOTTOM))
    return (lambda x, y: icon.microphone_distance(x, y, stretch),
            (icon.BASE_LEFT, icon.HEAD_TOP, icon.BASE_RIGHT, icon.BASE_BOTTOM))


def row_spans(distance, y, left, right, step):
    """Intervals of x where the shape is solid along the row y."""
    spans = []
    start = None
    x = left
    while x <= right + step:
        inside = distance(x, y) < INSIDE
        if inside and start is None:
            start = x
        elif not inside and start is not None:
            spans.append((start, x))
            start = None
        x += step
    return spans


def column_spans(distance, x, top, bottom, step):
    """Intervals of y where the shape is solid along the column x."""
    spans = []
    start = None
    y = top
    while y <= bottom + step:
        inside = distance(x, y) < INSIDE
        if inside and start is None:
            start = y
        elif not inside and start is not None:
            spans.append((start, y))
            start = None
        y += step
    return spans


def extremum(part='mic', axis='y', direction='min', stretch=1.0, tol=0.01):
    """Smallest/largest coordinate of the part, measured on the distance field."""
    distance, (left, top, right, bottom) = part_geometry(part, stretch)
    if axis == 'y':
        low, high = top, bottom
        inside = lambda c: bool(row_spans(distance, c, left, right, 0.25))
    else:
        low, high = left, right
        inside = lambda c: bool(column_spans(distance, c, top, bottom, 0.25))

    coarse = 0.5
    step = -coarse if direction == 'max' else coarse
    c = low if direction == 'min' else high
    previous = c
    while (c <= high) if direction == 'min' else (c >= low):
        if inside(c):
            break
        previous = c
        c += step
    else:
        return None

    best = c
    c -= step / abs(step) * tol
    while (c >= previous) if direction == 'min' else (c <= previous):
        if not inside(c):
            break
        best = c
        c -= step / abs(step) * tol
    return best


def cmd_probe(args):
    distance, (left, top, right, bottom) = part_geometry(args.part, args.stretch)
    print('%s, stretch %g, box (%g, %g) - (%g, %g)'
          % (args.part, args.stretch, left, top, right, bottom))
    if args.extremum:
        axis, direction = args.extremum.split(':')
        value = extremum(args.part, axis, direction, args.stretch, args.tol)
        print('%s %s: %s' % (axis, direction,
                             'none' if value is None else '%.3f' % value))
    if args.y is not None:
        print('y=%g spans: %s'
              % (args.y, _format_spans(row_spans(distance, args.y, left, right,
                                                args.step))))
    if args.x is not None:
        print('x=%g spans: %s'
              % (args.x, _format_spans(column_spans(distance, args.x, top, bottom,
                                                   args.step))))


def _format_spans(spans):
    if not spans:
        return 'none'
    return ', '.join('%.2f..%.2f (%.2f)' % (a, b, b - a) for a, b in spans)


# --- invariants ---------------------------------------------------------------

def _top_of_lower_mass(x, stretch, step=0.01):
    """Top of the solid mass below the head, scanning up from the base."""
    y = icon.STEM_BOTTOM - 1.0
    while y > icon.HEAD_TOP:
        if icon.microphone_distance(x, y, stretch) >= INSIDE:
            return y + step
        y -= step
    return None


def invariants(stretches, verbose=True):
    """Check the design invariants; returns a list of failure messages."""
    failures = []
    base_gap = icon.CRADLE_INNER_RADIUS - icon.HEAD_RADIUS
    base_thickness = icon.CRADLE_OUTER_RADIUS - icon.CRADLE_INNER_RADIUS
    rows = []
    for stretch in stretches:
        head_radius, inner, outer, stem_half, stem_top = icon.stretched_geometry(stretch)
        gap = inner - head_radius
        thickness = outer - inner
        top = extremum('cradle', 'y', 'min', stretch)
        rows.append((stretch, 2 * head_radius, 2 * outer, 2 * stem_half, gap, top,
                     stem_top))

        if abs(gap - base_gap) > 1e-9:
            failures.append('stretch %g: head/cradle gap %g != %g'
                            % (stretch, gap, base_gap))
        if abs(thickness - base_thickness) > 1e-9:
            failures.append('stretch %g: cradle thickness %g != %g'
                            % (stretch, thickness, base_thickness))
        if not stem_top > icon.CRADLE_CENTER_Y + inner:
            failures.append('stretch %g: stem top %g is not hidden behind the cradle'
                            % (stretch, stem_top))
        cap_limit = (icon.CRADLE_CENTER_Y + head_radius - icon.HEAD_TOP) / 2.0
        if head_radius > cap_limit:
            failures.append('stretch %g: cap radius %g is clamped to %g'
                            % (stretch, head_radius, cap_limit))
        if top is None or abs(top - icon.ARM_TOP) > 0.05:
            failures.append('stretch %g: cradle top %s != ARM_TOP %g'
                            % (stretch, top, icon.ARM_TOP))

        for ratio in (0.2, 0.5, 0.8, 0.95):
            dx = stem_half * ratio
            x = icon.HEAD_CENTER_X + dx
            expected = icon.CRADLE_CENTER_Y + math.sqrt(inner ** 2 - dx ** 2)
            measured = _top_of_lower_mass(x, stretch)
            if measured is None or abs(measured - expected) > 0.05:
                failures.append(
                    'stretch %g: stem top edge exposed at x=%g (measured %s, expected %g)'
                    % (stretch, x, measured, expected))

        for y in (icon.CRADLE_CENTER_Y + inner + 0.5, icon.STEM_BOTTOM - 1.0,
                  icon.BASE_TOP + 1.0):
            if icon.microphone_distance(icon.HEAD_CENTER_X, y, stretch) >= INSIDE:
                failures.append('stretch %g: stem not welded at y=%g' % (stretch, y))

    if verbose:
        print('%-8s %9s %9s %9s %9s %12s %9s'
              % ('stretch', 'head w', 'cradle w', 'stem w', 'gap', 'cradle top',
                 'stem top'))
        for stretch, head_w, cradle_w, stem_w, gap, top, stem_top in rows:
            print('%-8g %9.2f %9.2f %9.2f %9.2f %12.2f %9.2f'
                  % (stretch, head_w, cradle_w, stem_w, gap, top, stem_top))
    return failures


def cmd_invariants(args):
    stretches = [float(s) for s in args.stretch.split(',') if s.strip()]
    failures = invariants(stretches)
    for message in failures:
        print('FAIL ' + message)
    print('invariants: %s' % ('FAIL' if failures else 'OK'))
    return 1 if failures else 0


# --- consistency check --------------------------------------------------------

def expected_files():
    """Freshly generated contents of every file that ships with the application."""
    files = {
        'DeMic.ico': icon.ico_bytes([(size, icon.render(size)) for size in icon.SIZES]),
        'app-logo.png': icon.png_bytes(icon.APP_LOGO_SIZE, icon.render(icon.APP_LOGO_SIZE)),
    }
    for stem, name, fill in icon.tray_icons():
        files[stem + '.ico'] = icon.ico_bytes(
            [(size, icon.render_tray(size, name, fill)) for size in icon.TRAY_SIZES])
    return files


def unused_constants(path):
    tree = ast.parse(open(path, encoding='utf-8').read())
    assigned = {}
    for node in tree.body:
        if isinstance(node, ast.Assign):
            for target in node.targets:
                if isinstance(target, ast.Name):
                    assigned[target.id] = node.lineno
    used = set()
    for node in ast.walk(tree):
        if isinstance(node, ast.Name) and isinstance(node.ctx, ast.Load):
            used.add(node.id)
    return sorted((line, name) for name, line in assigned.items() if name not in used)


def run_check(update_golden=False, verbose=True):
    """Compare the files on disk with the script; returns a list of failures."""
    failures = []
    files = expected_files()
    produced = {}
    for name, data in files.items():
        produced[name] = digest(data)
        path = os.path.join(ROOT, name)
        if not os.path.exists(path):
            failures.append('%s: missing' % name)
            continue
        with open(path, 'rb') as f:
            on_disk = f.read()
        if compare_bytes(data, on_disk, name):
            if verbose:
                print('%s: OK' % name)
        else:
            failures.append('%s: differs from what generate_icon.py produces' % name)

    golden = {}
    if os.path.exists(GOLDEN):
        with open(GOLDEN, encoding='utf-8') as f:
            golden = json.load(f)
    if update_golden:
        golden.update(produced)
        write_file(GOLDEN, json.dumps(golden, indent=2, sort_keys=True).encode() + b'\n')
        print('golden baseline: updated')
    elif not golden:
        print('golden baseline: absent (create it with --update-golden)')
    else:
        changed = [name for name in sorted(produced)
                   if golden.get(name) != produced[name]]
        for name in changed:
            failures.append('%s: differs from the golden baseline' % name)
        print('golden baseline: %s' % ('FAIL' if changed else 'OK'))

    for line, name in unused_constants(os.path.join(HERE, 'generate_icon.py')):
        failures.append('generate_icon.py:%d: unused constant %s' % (line, name))
    return failures


def cmd_check(args):
    failures = run_check(args.update_golden)
    failures.extend(invariants([1.0, icon.TRAY_STRETCH], verbose=False))
    for message in failures:
        print('FAIL ' + message)
    print('check: %s' % ('FAIL' if failures else 'OK'))
    return 1 if failures else 0


VERIFY_VIEWS = (
    ('full', (0, 0, 256, 256), 2.0, 1.0, 'outline'),
    ('junction', (105, 155, 151, 195), 16.0, 1.0, 'both'),
    ('tray-shape', (56, 105, 200, 190), 6.0, icon.TRAY_STRETCH, 'outline'),
)


def cmd_verify(args):
    """Run the whole verification of one editing round and print a report."""
    if not args.no_previews:
        print('--- previews (%s) ---' % args.dir)
        for label, box, zoom, stretch, mode in VERIFY_VIEWS:
            width, height, pixels = render_silhouette(box[0], box[1], box[2], box[3],
                                                      zoom, stretch, mode)
            path = write_file(os.path.join(args.dir, 'verify-%s.png' % label),
                              icon.png_bytes(width, pixels))
            print('%-10s %4dx%-4d stretch %-5g %-7s %s'
                  % (label, width, height, stretch, mode, path))
        for stem, name, fill in icon.tray_icons():
            for size in args.sizes:
                path = write_file(
                    os.path.join(args.dir, 'verify-tray-%s-%d.png' % (stem, size)),
                    icon.png_bytes(size, icon.render_tray(size, name, fill)))
                print('%-10s %4dx%-4d %s' % ('tray', size, size, path))

    print('--- files on disk ---')
    failures = run_check(verbose=True)
    print('--- geometry ---')
    failures.extend(invariants([1.0, icon.TRAY_STRETCH], verbose=True))

    for message in failures:
        print('FAIL ' + message)
    print('verify: %s' % ('FAIL' if failures else 'OK'))
    return 1 if failures else 0


# --- command line -------------------------------------------------------------

def _sizes(text):
    return [int(value) for value in text.split(',') if value.strip()]


def main(argv=None):
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest='command', required=True)

    frames = sub.add_parser('frames', help='unpack .ico frames to PNGs')
    frames.add_argument('files', nargs='*')
    frames.add_argument('--dir', default=PREVIEW_DIR)
    frames.add_argument('--sizes', default=None, help='comma separated subset')

    preview = sub.add_parser('preview', help='render the microphone silhouette')
    preview.add_argument('--crop', default='0,0,256,256', help='left,top,right,bottom')
    preview.add_argument('--zoom', type=float, default=2.0, help='pixels per design unit')
    preview.add_argument('--stretch', type=float, default=1.0)
    preview.add_argument('--mode', choices=('fill', 'outline', 'both'), default='fill')
    preview.add_argument('--out', default=None)

    tray = sub.add_parser('tray', help='render tray icons')
    tray.add_argument('--sizes', default='16,24,32,64')
    tray.add_argument('--names', default=','.join(TRAY_STEMS))
    tray.add_argument('--dir', default=PREVIEW_DIR)

    sheet = sub.add_parser('sheet', help='stitch tray icons into one image')
    sheet.add_argument('--sizes', default='16,24,32,64')
    sheet.add_argument('--names', default=','.join(TRAY_STEMS))
    sheet.add_argument('--zoom', type=int, default=4,
                       help='integer nearest neighbour upscale factor')
    sheet.add_argument('--crop', default=None,
                       help='design units left,top,right,bottom; default whole canvas')
    sheet.add_argument('--dir', default=PREVIEW_DIR)
    sheet.add_argument('--out', default=None)

    probe = sub.add_parser('probe', help='scan a row/column or find an extreme point')
    probe.add_argument('--y', type=float, default=None)
    probe.add_argument('--x', type=float, default=None)
    probe.add_argument('--extremum', default=None, help='axis:direction, e.g. y:min')
    probe.add_argument('--part', default='mic', choices=PARTS)
    probe.add_argument('--stretch', type=float, default=1.0)
    probe.add_argument('--step', type=float, default=0.05)
    probe.add_argument('--tol', type=float, default=0.01)

    table = sub.add_parser('invariants', help='print and assert the design dimensions')
    table.add_argument('--stretch', default=','.join([str(1.0), str(icon.TRAY_STRETCH)]))

    check = sub.add_parser('check', help='verify the files on disk')
    check.add_argument('--update-golden', action='store_true')

    verify = sub.add_parser(
        'verify', help='run the whole verification of one editing round')
    verify.add_argument('--dir', default=PREVIEW_DIR)
    verify.add_argument('--sizes', type=_sizes, default=[16, 64],
                        help='tray sizes to render, default 16,64')
    verify.add_argument('--no-previews', action='store_true',
                        help='skip the preview images, only check files and geometry')

    args = parser.parse_args(argv)
    handlers = {
        'frames': cmd_frames,
        'preview': cmd_preview,
        'tray': cmd_tray,
        'sheet': cmd_sheet,
        'probe': cmd_probe,
        'invariants': cmd_invariants,
        'check': cmd_check,
        'verify': cmd_verify,
    }
    return handlers[args.command](args) or 0


if __name__ == '__main__':
    sys.exit(main())

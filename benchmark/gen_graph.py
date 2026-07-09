#!/usr/bin/env python3
"""Generate benchmark graph from results.csv"""
import csv
import math
import os
import struct
import zlib
from collections import defaultdict
from statistics import median

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
CSV_PATH = os.path.join(SCRIPT_DIR, 'results.csv')
OUT_DIR = SCRIPT_DIR

BG = (21, 23, 28)
FG = (236, 239, 244)
MUTED = (174, 180, 190)
GRID = (43, 47, 56)
BAR = (250, 255, 105)

FONT = {
    ' ': ('00000', '00000', '00000', '00000', '00000', '00000', '00000'),
    '.': ('00000', '00000', '00000', '00000', '00000', '00000', '00100'),
    ',': ('00000', '00000', '00000', '00000', '00100', '00100', '01000'),
    ':': ('00000', '00100', '00000', '00000', '00000', '00100', '00000'),
    '-': ('00000', '00000', '00000', '11111', '00000', '00000', '00000'),
    '_': ('00000', '00000', '00000', '00000', '00000', '00000', '11111'),
    '/': ('00001', '00010', '00010', '00100', '01000', '01000', '10000'),
    '(': ('00010', '00100', '01000', '01000', '01000', '00100', '00010'),
    ')': ('01000', '00100', '00010', '00010', '00010', '00100', '01000'),
    '?': ('01110', '10001', '00001', '00010', '00100', '00000', '00100'),
    '0': ('01110', '10001', '10011', '10101', '11001', '10001', '01110'),
    '1': ('00100', '01100', '00100', '00100', '00100', '00100', '01110'),
    '2': ('01110', '10001', '00001', '00010', '00100', '01000', '11111'),
    '3': ('11110', '00001', '00001', '01110', '00001', '00001', '11110'),
    '4': ('00010', '00110', '01010', '10010', '11111', '00010', '00010'),
    '5': ('11111', '10000', '10000', '11110', '00001', '00001', '11110'),
    '6': ('01110', '10000', '10000', '11110', '10001', '10001', '01110'),
    '7': ('11111', '00001', '00010', '00100', '01000', '01000', '01000'),
    '8': ('01110', '10001', '10001', '01110', '10001', '10001', '01110'),
    '9': ('01110', '10001', '10001', '01111', '00001', '00001', '01110'),
    'a': ('01110', '10001', '10001', '11111', '10001', '10001', '10001'),
    'b': ('11110', '10001', '10001', '11110', '10001', '10001', '11110'),
    'c': ('01111', '10000', '10000', '10000', '10000', '10000', '01111'),
    'd': ('11110', '10001', '10001', '10001', '10001', '10001', '11110'),
    'e': ('11111', '10000', '10000', '11110', '10000', '10000', '11111'),
    'f': ('11111', '10000', '10000', '11110', '10000', '10000', '10000'),
    'g': ('01111', '10000', '10000', '10011', '10001', '10001', '01111'),
    'h': ('10001', '10001', '10001', '11111', '10001', '10001', '10001'),
    'i': ('01110', '00100', '00100', '00100', '00100', '00100', '01110'),
    'j': ('00111', '00010', '00010', '00010', '10010', '10010', '01100'),
    'k': ('10001', '10010', '10100', '11000', '10100', '10010', '10001'),
    'l': ('10000', '10000', '10000', '10000', '10000', '10000', '11111'),
    'm': ('10001', '11011', '10101', '10101', '10001', '10001', '10001'),
    'n': ('10001', '11001', '10101', '10011', '10001', '10001', '10001'),
    'o': ('01110', '10001', '10001', '10001', '10001', '10001', '01110'),
    'p': ('11110', '10001', '10001', '11110', '10000', '10000', '10000'),
    'q': ('01110', '10001', '10001', '10001', '10101', '10010', '01101'),
    'r': ('11110', '10001', '10001', '11110', '10100', '10010', '10001'),
    's': ('01111', '10000', '10000', '01110', '00001', '00001', '11110'),
    't': ('11111', '00100', '00100', '00100', '00100', '00100', '00100'),
    'u': ('10001', '10001', '10001', '10001', '10001', '10001', '01110'),
    'v': ('10001', '10001', '10001', '10001', '10001', '01010', '00100'),
    'w': ('10001', '10001', '10001', '10101', '10101', '10101', '01010'),
    'x': ('10001', '10001', '01010', '00100', '01010', '10001', '10001'),
    'y': ('10001', '10001', '01010', '00100', '00100', '00100', '00100'),
    'z': ('11111', '00001', '00010', '00100', '01000', '10000', '11111'),
}


def load_results():
    """Parse CSV, return keyed run times"""
    data = defaultdict(list)
    with open(CSV_PATH, newline='') as f:
        for row in csv.DictReader(f):
            key = (row['category'], row['test_name'], row['engine'])
            data[key].append(float(row['time_ms']))
    return data


def build_comparison_data(data):
    """Group by test, compute median for each engine"""
    tests = defaultdict(dict)
    for (cat, test, engine), times in data.items():
        tests[(cat, test)][engine] = median(times)
    return tests


def _rows_by_speedup(tests):
    """Return comparable rows sorted by speedup"""
    rows = []
    for (cat, test), engines in tests.items():
        re2_t = engines.get('re2', 0)
        pg_t = engines.get('pg_builtin', 0)
        if re2_t > 0 and pg_t > 0:
            rows.append((f'{cat}: {test}', re2_t, pg_t, pg_t / re2_t))
    rows.sort(key=lambda r: r[3])
    return rows


def _new_image(width, height, color=BG):
    return bytearray(bytes(color) * width * height)


def _fill_rect(pixels, width, height, x, y, rect_w, rect_h, color):
    x0 = max(0, int(round(x)))
    y0 = max(0, int(round(y)))
    x1 = min(width, int(round(x + rect_w)))
    y1 = min(height, int(round(y + rect_h)))
    if x0 >= x1 or y0 >= y1:
        return

    row = bytes(color) * (x1 - x0)
    for yy in range(y0, y1):
        offset = (yy * width + x0) * 3
        pixels[offset:offset + len(row)] = row


def _draw_text(pixels, width, height, x, y, text, color=FG, scale=2):
    cursor = x
    for char in text.lower():
        glyph = FONT.get(char, FONT['?'])
        for gy, line in enumerate(glyph):
            for gx, bit in enumerate(line):
                if bit == '1':
                    _fill_rect(
                        pixels,
                        width,
                        height,
                        cursor + gx * scale,
                        y + gy * scale,
                        scale,
                        scale,
                        color,
                    )
        cursor += (4 if char == ' ' else 6) * scale


def _text_width(text, scale=2):
    if not text:
        return 0
    return sum((4 if char == ' ' else 6) * scale for char in text) - scale


def _tick_values(max_value, count=5):
    if max_value <= 0:
        return [0, 1]

    raw_step = max_value / count
    magnitude = 10 ** math.floor(math.log10(raw_step))
    normalized = raw_step / magnitude
    if normalized <= 1:
        step = magnitude
    elif normalized <= 2:
        step = 2 * magnitude
    elif normalized <= 5:
        step = 5 * magnitude
    else:
        step = 10 * magnitude

    top = math.ceil(max_value / step) * step
    return [i * step for i in range(int(round(top / step)) + 1)]


def _save_png(path, width, height, pixels):
    raw_rows = []
    row_bytes = width * 3
    for y in range(height):
        start = y * row_bytes
        raw_rows.append(b'\x00' + pixels[start:start + row_bytes])

    def chunk(kind, data):
        checksum = zlib.crc32(kind + data) & 0xffffffff
        return struct.pack('!I', len(data)) + kind + data + struct.pack('!I', checksum)

    header = struct.pack('!IIBBBBB', width, height, 8, 2, 0, 0, 0)
    png = (
        b'\x89PNG\r\n\x1a\n'
        + chunk(b'IHDR', header)
        + chunk(b'IDAT', zlib.compress(b''.join(raw_rows), level=9))
        + chunk(b'IEND', b'')
    )
    with open(path, 'wb') as f:
        f.write(png)


def plot_speedup(tests, filename='graph.png',
                 title='re2 speedup over postgresql builtin regex',
                 axis_cap=None):
    """Render horizontal speedup bars; axis_cap clips outliers to given max"""
    rows = _rows_by_speedup(tests)
    path = os.path.join(OUT_DIR, filename)

    if not rows:
        width = 640
        height = 160
        pixels = _new_image(width, height)
        _draw_text(pixels, width, height, 24, 28, 'no comparable rows', FG, 3)
        _save_png(path, width, height, pixels)
        print(f"Saved {path}")
        return

    display_rows = list(reversed(rows))
    speedups = [r[3] for r in rows]
    max_speedup = max(speedups)
    target = max_speedup
    if axis_cap:
        target = min(target, axis_cap)
    ticks = _tick_values(target)
    axis_max = max(ticks)

    label_width = max(_text_width(label, 2) for label, _, _, _ in rows)
    left = label_width + 36
    right = 40
    plot_width = 820
    row_step = 32
    bar_height = 18
    top = 82
    bottom = 68
    width = left + plot_width + right
    height = top + len(rows) * row_step + bottom
    chart_top = top - 10
    chart_bottom = top + len(rows) * row_step - 6

    pixels = _new_image(width, height)
    _draw_text(
        pixels,
        width,
        height,
        24,
        24,
        title,
        FG,
        3,
    )

    for tick in ticks:
        x = left + tick / axis_max * plot_width
        tick_label = f'{tick:g}'
        _draw_text(
            pixels,
            width,
            height,
            x - _text_width(tick_label, 2) / 2,
            chart_bottom + 14,
            tick_label,
            MUTED,
            2,
        )

    for row, (label, _, _, speedup) in enumerate(display_rows):
        y = top + row * row_step
        bar_width = speedup / axis_max * plot_width
        _draw_text(pixels, width, height, 8, y + 2, label, MUTED, 2)
        _fill_rect(pixels, width, height, left, y, bar_width, bar_height, BAR)

        value = f'{speedup:.1f}x'
        value_width = _text_width(value, 2)
        if value_width + 16 <= bar_width:
            _draw_text(pixels, width, height, left + 8, y + 2, value, BG, 2)
        else:
            _draw_text(pixels, width, height, left + bar_width + 6, y + 2, value, FG, 2)

    if 1 <= axis_max:
        x = left + plot_width / axis_max
        _fill_rect(
            pixels, width, height, x, chart_top, 2, chart_bottom - chart_top, BG
        )

    _fill_rect(pixels, width, height, left, chart_bottom, plot_width, 1, GRID)
    _draw_text(
        pixels,
        width,
        height,
        left,
        height - 30,
        'speedup factor (pg builtin time / re2 time)',
        MUTED,
        2,
    )

    _save_png(path, width, height, pixels)
    print(f"Saved {path}")


def _split_index(tests):
    """Partition tests into (throughput, index) by category prefix"""
    throughput = {}
    index = {}
    for key, engines in tests.items():
        (index if key[0].startswith('idx_') else throughput)[key] = engines
    return throughput, index


if __name__ == '__main__':
    data = load_results()
    tests = build_comparison_data(data)
    throughput, index = _split_index(tests)
    plot_speedup(throughput, 'graph.png')
    if index:
        plot_speedup(index, 'graph_index.png',
                     're2 index scan speedup over postgresql', axis_cap=20)
    print("Done.")

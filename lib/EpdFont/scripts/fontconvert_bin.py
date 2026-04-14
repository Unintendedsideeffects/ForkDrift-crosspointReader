#!python3
import freetype
import math
import argparse
import struct
from collections import namedtuple

# Based on fontconvert.py, but outputs binary CPF format for CrossPoint Reader.

parser = argparse.ArgumentParser(description="Generate a binary .cpf font file from a TTF/OTF font.")
parser.add_argument("size", type=int, help="font size to use.")
parser.add_argument("fontstack", action="store", nargs='+', help="list of font files, ordered by descending priority.")
parser.add_argument("--output", dest="output", action="store", required=True, help="output .cpf file path.")
parser.add_argument("--2bit", dest="is2Bit", action="store_true", help="generate 2-bit greyscale bitmap instead of 1-bit black and white.")
parser.add_argument("--additional-intervals", dest="additional_intervals", action="append", help="Additional code point intervals to export as min,max. This argument can be repeated.")
args = parser.parse_args()

GlyphProps = namedtuple("GlyphProps", ["width", "height", "advance_x", "left", "top", "data_length", "data_offset", "code_point"])
# Matches C++ EpdGlyph layout (16 bytes, little-endian):
# B=width, B=height, B=advanceX, x=pad, h=left, h=top, H=dataLength, xx=pad, I=dataOffset
GLYPH_STRUCT = "<BBBxhhHxxI"
assert struct.calcsize(GLYPH_STRUCT) == 16

font_stack = [freetype.Face(f) for f in args.fontstack]
is2Bit = args.is2Bit
size = args.size

# inclusive unicode code point intervals
intervals = [
    (0x0000, 0x007F), # Basic Latin
    (0x0080, 0x00FF), # Latin-1 Supplement
    (0x0100, 0x017F), # Latin Extended-A
    (0x2000, 0x206F), # General Punctuation
    (0x2010, 0x203A), # Basic Symbols
    (0x2040, 0x205F), # misc punctuation
    (0x20A0, 0x20CF), # common currency symbols
    (0x0300, 0x036F), # Combining Diacritical Marks
    (0x0400, 0x04FF), # Cyrillic
    (0x2070, 0x209F), # Superscripts and Subscripts
    (0x2200, 0x22FF), # General math operators
    (0x2190, 0x21FF), # Arrows
    (0xFFFD, 0xFFFD), # Replacement Character
]

add_ints = []
if args.additional_intervals:
    add_ints = [tuple([int(n, base=0) for n in i.split(",")]) for i in args.additional_intervals]

def norm_floor(val):
    return int(math.floor(val / (1 << 6)))

def norm_ceil(val):
    return int(math.ceil(val / (1 << 6)))

def load_glyph(code_point):
    for face in font_stack:
        glyph_index = face.get_char_index(code_point)
        if glyph_index > 0:
            face.load_glyph(glyph_index, freetype.FT_LOAD_RENDER)
            return face
    return None

unmerged_intervals = sorted(intervals + add_ints)
intervals = []
unvalidated_intervals = []
for i_start, i_end in unmerged_intervals:
    if len(unvalidated_intervals) > 0 and i_start + 1 <= unvalidated_intervals[-1][1]:
        unvalidated_intervals[-1] = (unvalidated_intervals[-1][0], max(unvalidated_intervals[-1][1], i_end))
        continue
    unvalidated_intervals.append((i_start, i_end))

for i_start, i_end in unvalidated_intervals:
    start = i_start
    for code_point in range(i_start, i_end + 1):
        face = load_glyph(code_point)
        if face is None:
            if start < code_point:
                intervals.append((start, code_point - 1))
            start = code_point + 1
    if start != i_end + 1:
        intervals.append((start, i_end))

for face in font_stack:
    face.set_char_size(size << 6, size << 6, 150, 150)

total_bitmap_size = 0
all_glyphs_data = []
all_glyphs_props = []

for i_start, i_end in intervals:
    for code_point in range(i_start, i_end + 1):
        face = load_glyph(code_point)
        bitmap = face.glyph.bitmap

        pixels4g = []
        px = 0
        for i, v in enumerate(bitmap.buffer):
            x = i % bitmap.width
            if x % 2 == 0:
                px = (v >> 4)
            else:
                px = px | (v & 0xF0)
                pixels4g.append(px)
                px = 0
            if x == bitmap.width - 1 and bitmap.width % 2 > 0:
                pixels4g.append(px)
                px = 0

        if is2Bit:
            pixels2b = []
            px = 0
            pitch = (bitmap.width // 2) + (bitmap.width % 2)
            for y in range(bitmap.rows):
                for x in range(bitmap.width):
                    px = px << 2
                    bm = pixels4g[y * pitch + (x // 2)]
                    bm = (bm >> ((x % 2) * 4)) & 0xF
                    if bm >= 12: px += 3
                    elif bm >= 8: px += 2
                    elif bm >= 4: px += 1
                    if (y * bitmap.width + x) % 4 == 3:
                        pixels2b.append(px)
                        px = 0
            if (bitmap.width * bitmap.rows) % 4 != 0:
                px = px << (4 - (bitmap.width * bitmap.rows) % 4) * 2
                pixels2b.append(px)
            pixels = pixels2b
        else:
            pixelsbw = []
            px = 0
            pitch = (bitmap.width // 2) + (bitmap.width % 2)
            for y in range(bitmap.rows):
                for x in range(bitmap.width):
                    px = px << 1
                    bm = pixels4g[y * pitch + (x // 2)]
                    px += 1 if ((x & 1) == 0 and bm & 0xE > 0) or ((x & 1) == 1 and bm & 0xE0 > 0) else 0
                    if (y * bitmap.width + x) % 8 == 7:
                        pixelsbw.append(px)
                        px = 0
            if (bitmap.width * bitmap.rows) % 8 != 0:
                px = px << (8 - (bitmap.width * bitmap.rows) % 8)
                pixelsbw.append(px)
            pixels = pixelsbw

        packed = bytes(pixels)
        glyph = GlyphProps(
            width = bitmap.width,
            height = bitmap.rows,
            advance_x = norm_floor(face.glyph.advance.x),
            left = face.glyph.bitmap_left,
            top = face.glyph.bitmap_top,
            data_length = len(packed),
            data_offset = total_bitmap_size,
            code_point = code_point,
        )
        total_bitmap_size += len(packed)
        all_glyphs_props.append(glyph)
        all_glyphs_data.append(packed)

face = load_glyph(ord('|'))
advance_y = norm_ceil(face.size.height)
ascender = norm_ceil(face.size.ascender)
descender = norm_floor(face.size.descender)

with open(args.output, "wb") as f:
    # 4 bytes: Magic "CPF\x01"
    f.write(b"CPF\x01")
    # 1 byte: advanceY, 4 bytes (int32): ascender, 4 bytes (int32): descender, 1 byte: is2Bit
    f.write(struct.pack("<Biib", advance_y, ascender, descender, 1 if is2Bit else 0))
    # 4 bytes (uint32): intervalCount, 4 bytes (uint32): totalGlyphs, 4 bytes (uint32): bitmapSize
    f.write(struct.pack("<III", len(intervals), len(all_glyphs_props), total_bitmap_size))
    
    # Intervals: uint32 first, uint32 last, uint32 offset
    offset = 0
    for i_start, i_end in intervals:
        f.write(struct.pack("<III", i_start, i_end, offset))
        offset += i_end - i_start + 1
        
    # Glyphs: uint8 width, uint8 height, uint8 advanceX, 1-byte padding,
    # int16 left, int16 top, uint16 dataLength, 2-byte padding, uint32 dataOffset
    for g in all_glyphs_props:
        f.write(struct.pack(GLYPH_STRUCT, g.width, g.height, g.advance_x, g.left, g.top, g.data_length, g.data_offset))
        
    # Bitmap data
    for data in all_glyphs_data:
        f.write(data)

print(f"Generated {args.output}: {len(all_glyphs_props)} glyphs, {total_bitmap_size} bytes bitmap data.")

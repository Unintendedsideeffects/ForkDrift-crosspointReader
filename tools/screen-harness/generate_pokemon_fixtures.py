#!/usr/bin/env python3
import struct
from pathlib import Path

SIZE = 96
OUT_DIR = Path(__file__).resolve().parent / "fixtures" / "pokemon"


def write_1bit_bmp(path: Path, pixels):
    row_bytes = (SIZE + 31) // 32 * 4
    pixel_data_size = row_bytes * SIZE
    file_size = 14 + 40 + 8 + pixel_data_size
    palette = bytes([0, 0, 0, 0, 255, 255, 255, 0])

    with path.open("wb") as f:
        f.write(b"BM")
        f.write(struct.pack("<I", file_size))
        f.write(struct.pack("<HH", 0, 0))
        f.write(struct.pack("<I", 14 + 40 + 8))
        f.write(struct.pack("<I", 40))
        f.write(struct.pack("<i", SIZE))
        f.write(struct.pack("<i", -SIZE))
        f.write(struct.pack("<HH", 1, 1))
        f.write(struct.pack("<I", 0))
        f.write(struct.pack("<I", pixel_data_size))
        f.write(struct.pack("<ii", 0, 0))
        f.write(struct.pack("<II", 2, 0))
        f.write(palette)

        for y in range(SIZE):
            row = bytearray(row_bytes)
            for x in range(SIZE):
                if pixels[y][x]:
                    row[x // 8] |= 0x80 >> (x % 8)
            f.write(row)


def shape_pixels(species_id: int):
    pixels = [[False] * SIZE for _ in range(SIZE)]
    cx = cy = SIZE // 2
    radius = 18 + (species_id % 4) * 3
    for y in range(SIZE):
        for x in range(SIZE):
            dx = x - cx
            dy = y - cy
            if dx * dx + dy * dy <= radius * radius:
                pixels[y][x] = True
            if species_id % 2 == 0 and abs(dx) <= 4 and abs(dy + 8) <= 10:
                pixels[y][x] = True
            if species_id % 3 == 0 and y > cy + 6 and abs(dx) <= 8:
                pixels[y][x] = True
    return pixels


def main():
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    for species_id in range(1, 7):
        write_1bit_bmp(OUT_DIR / f"sprite_{species_id}.bmp", shape_pixels(species_id))
    print(f"Wrote pokemon sprite fixtures to {OUT_DIR}")


if __name__ == "__main__":
    main()

#pragma once
#include <cstdint>

// size: 24x24
// Calendar icon: outer border, header area, 5 rows of 3 date columns separated by grid lines.
// Bit encoding: 1 = background (white), 0 = foreground (black), MSB-first per byte.
static const uint8_t Calendar24Icon[] = {
    // Row 0-1: top margin
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    // Row 2: top border
    0x00,
    0x00,
    0x00,
    // Row 3-4: header interior (1px side borders)
    0x7F,
    0xFF,
    0xFE,
    0x7F,
    0xFF,
    0xFE,
    // Row 5: header separator
    0x00,
    0x00,
    0x00,
    // Rows 6-7: date cell group 1 (3 cols: 7px | 7px | 6px, with 1px borders/separators)
    0x7F,
    0x7F,
    0x7E,
    0x7F,
    0x7F,
    0x7E,
    // Row 8: row divider
    0x00,
    0x00,
    0x00,
    // Rows 9-10: date cell group 2
    0x7F,
    0x7F,
    0x7E,
    0x7F,
    0x7F,
    0x7E,
    // Row 11: row divider
    0x00,
    0x00,
    0x00,
    // Rows 12-13: date cell group 3
    0x7F,
    0x7F,
    0x7E,
    0x7F,
    0x7F,
    0x7E,
    // Row 14: row divider
    0x00,
    0x00,
    0x00,
    // Rows 15-16: date cell group 4
    0x7F,
    0x7F,
    0x7E,
    0x7F,
    0x7F,
    0x7E,
    // Row 17: row divider
    0x00,
    0x00,
    0x00,
    // Rows 18-19: date cell group 5
    0x7F,
    0x7F,
    0x7E,
    0x7F,
    0x7F,
    0x7E,
    // Row 20: bottom border
    0x00,
    0x00,
    0x00,
    // Rows 21-23: bottom margin
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
};

# Cache File Formats

This document describes the binary serialization formats of the book metadata (`book.bin`) and section layout (`section.bin`) cache files.

---

## 1. Metadata Cache (`book.bin`)

### Version 9

The `book.bin` file contains parsed EPUB metadata, including the Table of Contents (TOC) and Spine entry mappings, to avoid parsing the zip package and XML at every startup.

### File Structure Layout

```text
┌─────────────────────────────────────────────────────────┐
│ HEADER                                                  │
│ - version (u8) [value = 9]                              │
│ - lutOffset (u32)                                       │
│ - spineCount (u16)                                      │
│ - tocCount (u16)                                        │
├─────────────────────────────────────────────────────────┤
│ METADATA                                                │
│ - title (String)                                        │
│ - author (String)                                       │
│ - language (String)                                     │
│ - coverItemHref (String)                                │
│ - textReferenceHref (String)                            │
├─────────────────────────────────────────────────────────┤
│ LOOKUP TABLES (LUTs)                                    │
│ - spineLut (u32[spineCount])                            │
│ - tocLut (u32[tocCount])                                │
├─────────────────────────────────────────────────────────┤
│ DATA ENTRIES                                            │
│ - Spines: SpineEntry[spineCount]                        │
│ - TOC: TocEntry[tocCount]                               │
└─────────────────────────────────────────────────────────┘
```

### Primitive Types
- **`u8` / `s8`**: 1-byte unsigned / signed integer
- **`u16` / `s16`**: 2-byte unsigned / signed integer (little-endian)
- **`u32` / `s32`**: 4-byte unsigned / signed integer (little-endian)
- **`String`**: Length-prefixed UTF-8 string:
  - `length`: `u32` length prefix
  - `data`: `char[length]` string content (not null-terminated)

---

### Field Specifications

#### Header
| Field Name | Type | Description |
|---|---|---|
| `version` | `u8` | Format version. Must be **`6`**. |
| `lutOffset` | `u32` | File offset of the start of the Lookup Tables (LUTs) from the beginning of the file. |
| `spineCount` | `u16` | Total number of spine entries. |
| `tocCount` | `u16` | Total number of Table of Contents entries. |

#### Metadata
All strings are written sequentially using the `String` length-prefixed representation.
- `title` (String): The title of the book.
- `author` (String): The author's name.
- `language` (String): The book's language code (e.g., `"en"`).
- `coverItemHref` (String): Internal EPUB path/href to the cover image.
- `textReferenceHref` (String): Reference to the first readable text section.

#### Lookup Tables (LUTs)
Located at `lutOffset`. Contains the absolute file offsets of each data entry block for fast random access.
- `spineLut`: `u32[spineCount]` containing the absolute offset of each `SpineEntry` in `book.bin`.
- `tocLut`: `u32[tocCount]` containing the absolute offset of each `TocEntry` in `book.bin`.

#### Data Entries

##### SpineEntry
Defined as:
| Field Name | Type | Description |
|---|---|---|
| `href` | `String` | Relative path of the HTML resource within the EPUB zip. |
| `cumulativeSize` | `u32` | Cumulative size (in bytes) of all spine items up to this entry. Used for global progress percentage calculation. |
| `tocIndex` | `s16` | Index of the corresponding entry in the TOC list (`-1` if none). |

##### TocEntry
Defined as:
| Field Name | Type | Description |
|---|---|---|
| `title` | `String` | Chapter or section title. |
| `href` | `String` | Path of the target resource. |
| `anchor` | `String` | Target HTML fragment/anchor (empty if none). |
| `level` | `u8` | Nesting level of the TOC entry (0 = top level). |
| `spineIndex` | `s16` | Index of the corresponding item in the Spine array (`-1` if none). |

---

## 2. Section Layout Cache (`section.bin`)

### Version 29

The `section.bin` caches pre-computed typesetting details (word positions, styles, line breaks, page sizes) for a single spine section under a specific set of layout parameters. It is recalculated automatically if any cache-busting setting is altered.

### File Structure Layout

```text
┌─────────────────────────────────────────────────────────┐
│ HEADER (38 bytes)                                       │
│ - version (u8) [value = 29]                             │
│ - fontId (s32)                                          │
│ - lineCompression (float)                               │
│ - ... (other layout/typesetting flags)                  │
│ - pageCount (u16)                                       │
│ - lutOffset (u32)                                       │
│ - anchorMapOffset (u32)                                 │
│ - paragraphLutOffset (u32)                              │
│ - liLutOffset (u32)                                     │
├─────────────────────────────────────────────────────────┤
│ PAGES                                                   │
│ - pageData: Page[pageCount]                             │
├─────────────────────────────────────────────────────────┤
│ LOOKUP TABLES & MAPS                                    │
│ - pageLut: u32[pageCount]                               │
│ - anchorMap:                                            │
│   - anchorCount (u16)                                   │
│   - anchors ( { String anchor, u16 page }[] )           │
│ - paragraphLut (u16[pageCount])                         │
│ - liLut (u16[pageCount])                                │
└─────────────────────────────────────────────────────────┘
```

---

### Header (38 Bytes)

The header contains all rendering and layout parameters. Any mismatch during load invalidates the cache.

| Offset | Field Name | Type | Description |
|---|---|---|---|
| 0 | `version` | `u8` | Format version. Must be **`29`**. |
| 1 | `fontId` | `s32` | Built-in or custom font ID. |
| 5 | `lineCompression` | `float` | Line spacing factor (tight, normal, wide compression). |
| 9 | `extraParagraphSpacing` | `bool` (u8) | Extra space added between paragraphs. |
| 10 | `forceParagraphIndents` | `bool` (u8) | Force first-line indentation on paragraphs. |
| 11 | `paragraphAlignment` | `u8` | Typesetting alignment preference (Justify, Left, Center, Right). |
| 12 | `viewportWidth` | `u16` | Screen typesetting width. |
| 14 | `viewportHeight` | `u16` | Screen typesetting height. |
| 16 | `hyphenationEnabled` | `bool` (u8) | If hyphenation is enabled during rendering. |
| 17 | `embeddedStyle` | `bool` (u8) | If book's embedded HTML/CSS styles are respected. |
| 18 | `imageRendering` | `u8` | Image mode (0 = Display, 1 = Placeholder, 2 = Suppress). |
| 19 | `focusReadingEnabled` | `bool` (u8) | Bionic/focus reading bolding mode enabled. |
| 20 | `guideReadingEnabled` | `bool` (u8) | Guide dots enabled. |
| 21 | `pageCount` | `u16` | Total pages generated in this section. |
| 23 | `lutOffset` | `u32` | Absolute file offset of the Page Lookup Table. |
| 27 | `anchorMapOffset` | `u32` | Absolute file offset of the Anchor Map. |
| 31 | `paragraphLutOffset` | `u32` | Absolute file offset of the Paragraph Lookup Table. |
| 35 | `liLutOffset` | `u32` | Absolute file offset of the List Item Lookup Table. |

---

### Page Structure (`Page`)

Pages are laid out sequentially. Each Page block is structured as follows:

| Field Name | Type | Description |
|---|---|---|
| `elementCount` | `u16` | Number of layout elements on this page. |
| `elements` | `PageElement[elementCount]` | Array of typeset page elements (Text, Images, Rules). |
| `footnoteCount` | `u16` | Number of footnotes anchored on this page. |
| `footnotes` | `FootnoteEntry[footnoteCount]` | Footnote data structures. |

#### FootnoteEntry
- **`number`**: `char[32]` - String representation of the footnote index/indicator.
- **`href`**: `char[96]` - Target HTML reference anchor link.

---

### Page Elements

Every typeset element begins with a **`u8 tag`** defining its layout subclass:
- **`1`** = `PageLine` (Text block)
- **`2`** = `PageImage` (Bitmap image wrapper)
- **`3`** = `PageTableFragment` (Tabular layout fragment)
- **`4`** = `PageHorizontalRule` (Visual divider line)

---

#### 1. PageLine (Text Block)

| Field Name | Type | Description |
|---|---|---|
| `xPos` | `s16` | Relative starting horizontal position. |
| `yPos` | `s16` | Relative starting vertical position. |
| `wordCount` | `u16` | Number of words in the text block. |
| `words` | `String[wordCount]` | Array of words. |
| `wordXpos` | `s16[wordCount]` | Absolute typesetting offsets for each word relative to `xPos`. |
| `wordStyles` | `u8[wordCount]` | EpdFontFamily styles for each word. Bitmask values: `REGULAR=0`, `BOLD=1`, `ITALIC=2`, `UNDERLINE=4`, `STRIKETHROUGH=8`. |
| `hasFocus` | `u8` | Focus/Bionic splitting presence flag (`1` = yes, `0` = no). |
| `wordFocusBoundary` | `u8[wordCount]` | *Only if `hasFocus == 1`*. Byte length of the bolded prefix for each word. |
| `wordFocusSuffixX` | `u16[wordCount]` | *Only if `hasFocus == 1`*. Pixel offset to start drawing the regular suffix style. |
| `alignment` | `u8` | Typesetting alignment. |
| `textAlignDefined` | `bool` (u8) | True if text-align CSS was set. |
| `marginTop` | `s16` | Top margin in pixels. |
| `marginBottom` | `s16` | Bottom margin in pixels. |
| `marginLeft` | `s16` | Left margin in pixels. |
| `marginRight` | `s16` | Right margin in pixels. |
| `paddingTop` | `s16` | Top padding in pixels. |
| `paddingBottom` | `s16` | Bottom padding in pixels. |
| `paddingLeft` | `s16` | Left padding in pixels. |
| `paddingRight` | `s16` | Right padding in pixels. |
| `textIndent` | `s16` | First line indent. |
| `textIndentDefined`| `bool` (u8) | True if text-indent CSS was set. |

---

#### 2. PageImage (Image Wrapper)

| Field Name | Type | Description |
|---|---|---|
| `xPos` | `s16` | Horizontal offset. |
| `yPos` | `s16` | Vertical offset. |
| `imagePath` | `String` | Relative path to image file in EPUB zip. |
| `width` | `s16` | typeset image width. |
| `height` | `s16` | typeset image height. |

*Note: The actual decoded grayscale/dithered image pixels are stored separately as a compressed pixel cache file (`.pxc`) with a `width` (uint16), `height` (uint16), and packed 2-bit pixels (4 pixels/byte).*

---

#### 3. PageTableFragment (Tabular Layout)

| Field Name | Type | Description |
|---|---|---|
| `xPos` | `s16` | Horizontal offset. |
| `yPos` | `s16` | Vertical offset. |
| `width` | `u16` | Total table typesetting width. |
| `columnCount` | `u8` | Number of columns in table. |
| `cellPadding` | `u8` | Typeset cell padding. |
| `lineHeight` | `u16` | Typeset line height. |
| `rowCount` | `u8` | Number of rows in this page fragment. |
| `rows` | `TableFragmentRow[rowCount]` | Row structures. |

##### TableFragmentRow
- **`height`**: `u16` - Row typesetting height.
- **`headerSeparator`**: `bool` (u8) - Draw a horizontal divider below this row.
- **`cellCount`**: `u8` - Number of cells in this row.
- **`cells`**: `TableFragmentCell[cellCount]`
  - **`isHeader`**: `bool` (u8) - If cell belongs to `<thead>`.
  - **`lineCount`**: `u8` - Number of text lines in cell.
  - **`lines`**: `TextBlock[lineCount]` - Text content block layout (identical format to `TextBlock` in `PageLine`).

---

#### 4. PageHorizontalRule (Visual Divider)

| Field Name | Type | Description |
|---|---|---|
| `xPos` | `s16` | Starting horizontal position. |
| `yPos` | `s16` | Vertical position. |
| `width` | `u16` | Horizontal line width. |
| `thickness` | `u8` | Line stroke thickness. |

---

### Lookup Tables & Maps

These maps follow after all Page data blocks and speed up navigation.

#### Page Lookup Table
Located at `lutOffset`. Contains `pageLut`: `u32[pageCount]`, containing the absolute file offset for each `Page` structure in `section.bin` for instant page-jump indexing.

#### Anchor Map
Located at `anchorMapOffset`. Maps HTML IDs/anchors to specific page indices.
- **`anchorCount`**: `u16` (2 bytes)
- **`anchors`**: Array of size `anchorCount` containing:
  - `anchor`: `String` (length-prefixed HTML id attribute value)
  - `pageIndex`: `u16` (2-byte index of page containing anchor)

#### Paragraph Lookup Table
Located at `paragraphLutOffset`.
- **`lutSize`**: `u16` (2 bytes, equal to `pageCount`)
- **`paragraphIndexes`**: `u16[lutSize]` array mapping each page to its starting typeset HTML paragraph index.

#### List Item Lookup Table
Located at `liLutOffset`.
- **`listItemIndexes`**: `u16[lutSize]` array mapping each page to its starting typeset HTML list item index.

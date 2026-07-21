# Webserver Endpoints

This document describes the HTTP, WebSocket, WebDAV, and discovery endpoints
available while CrossPoint Reader is in File Transfer or Calibre Wireless mode.

> [!NOTE]
> **Maintenance Note:** To verify or regenerate the list of endpoints documented here, run this command from the repository root:
> `grep -rhoE '"(/api/[a-z0-9_/-]+)"' src/network | sort -u`

- [Webserver Endpoints](#webserver-endpoints)
  - [Overview](#overview)
  - [HTTP Endpoints](#http-endpoints)
    - [GET `/` - Home Page](#get----home-page)
    - [GET `/files` - File Browser Page](#get-files---file-browser-page)
    - [GET `/remote-input` - Remote Keyboard Page](#get-remote-input---remote-keyboard-page)
    - [GET `/api/status` - Device Status](#get-apistatus---device-status)
    - [GET `/api/plugins` - Compile-Time Feature Manifest](#get-apiplugins---compile-time-feature-manifest)
    - [GET `/api/remote-keyboard/session` - Active Remote Keyboard Session](#get-apiremote-keyboardsession---active-remote-keyboard-session)
    - [POST `/api/remote-keyboard/claim` - Claim Remote Keyboard Session](#post-apiremote-keyboardclaim---claim-remote-keyboard-session)
    - [POST `/api/remote-keyboard/submit` - Submit Remote Keyboard Text](#post-apiremote-keyboardsubmit---submit-remote-keyboard-text)
    - [GET `/api/files` - List Files](#get-apifiles---list-files)
    - [GET `/api/recent` - Recent Books](#get-apirecent---recent-books)
    - [GET `/api/book-progress` - Book Progress](#get-apibook-progress---book-progress)
    - [GET `/api/book-pokemon` - Book Pokemon Metadata](#get-apibook-pokemon---book-pokemon-metadata)
    - [PUT `/api/book-pokemon` - Save Book Pokemon Metadata](#put-apibook-pokemon---save-book-pokemon-metadata)
    - [DELETE `/api/book-pokemon` - Clear Book Pokemon Metadata](#delete-apibook-pokemon---clear-book-pokemon-metadata)
    - [POST `/api/todo/entry` - Add TODO or Agenda Entry](#post-apitodoentry---add-todo-or-agenda-entry)
    - [GET `/api/todo/today` - Read Daily Planner Entries](#get-apitodotoday---read-daily-planner-entries)
    - [POST `/api/todo/today` - Save Daily Planner Entries](#post-apitodotoday---save-daily-planner-entries)
    - [POST `/api/notes/entry` - Add Note](#post-apinotesentry---add-note)
    - [GET `/api/notes` - Read Notes](#get-apinotes---read-notes)
    - [POST `/api/notes` - Save Notes](#post-apinotes---save-notes)
    - [GET `/download` - Download File](#get-download---download-file)
    - [POST `/upload` - Upload File](#post-upload---upload-file)
    - [GET `/api/fonts` - List Installed Font Families](#get-apifonts---list-installed-font-families)
    - [POST `/api/fonts/upload` - Upload Font File](#post-apifontsupload---upload-font-file)
    - [POST `/api/fonts/delete` - Delete Font Family](#post-apifontsdelete---delete-font-family)
    - [GET `/api/sleep-images` - List Sleep Images](#get-apisleep-images---list-sleep-images)
    - [GET `/api/sleep-cover` - Get Pinned Sleep Cover](#get-apisleep-cover---get-pinned-sleep-cover)
    - [POST `/api/sleep-cover/pin` - Pin Sleep Cover](#post-apisleep-coverpin---pin-sleep-cover)
    - [POST `/mkdir` - Create Folder](#post-mkdir---create-folder)
    - [POST `/delete` - Delete File or Folder](#post-delete---delete-file-or-folder)
    - [GET `/api/anki/cards` - Export Anki Cards](#get-apiankicards---export-anki-cards)
    - [GET `/api/cover` - Get Book Cover BMP](#get-apicover---get-book-cover-bmp)
    - [POST `/api/koreader/use-opds` - KOReader Sync Setup](#post-apikoreaderuse-opds---koreader-sync-setup)
    - [POST `/api/maintenance/clear-cache` - Clear Reading Cache](#post-apimaintenanceclear-cache---clear-reading-cache)
    - [POST `/api/maintenance/clear-crashes` - Clear Crash Reports](#post-apimaintenanceclear-crashes---clear-crash-reports)
    - [POST `/api/maintenance/clear-logs` - Clear Debug Logs](#post-apimaintenanceclear-logs---clear-debug-logs)
    - [POST `/api/maintenance/reset-settings` - Reset Persisted Settings](#post-apimaintenancereset-settings---reset-persisted-settings)
    - [POST `/api/maintenance/validate-sleep-images` - Validate Sleep Images](#post-apimaintenancevalidate-sleep-images---validate-sleep-images)
    - [GET `/api/opds` - List OPDS Servers](#get-apiopds---list-opds-servers)
    - [POST `/api/opds` - Add or Update OPDS Server](#post-apiopds---add-or-update-opds-server)
    - [POST `/api/opds/delete` - Remove OPDS Server](#post-apiopdsdelete---remove-opds-server)
    - [POST `/api/opds/test` - Test OPDS Server Connectivity](#post-apiopdstest---test-opds-server-connectivity)
    - [POST `/api/open-book` - Open Book Remotely](#post-apiopen-book---open-book-remotely)
    - [POST `/api/pokemon-sprite` - Cache Pokemon Sprite](#post-apipokemon-sprite---cache-pokemon-sprite)
    - [GET `/api/pokemon-team` - Get Pokemon Team](#get-apipokemon-team---get-pokemon-team)
    - [PUT `/api/pokemon-team` - Save Pokemon Team](#put-apipokemon-team---save-pokemon-team)
    - [POST `/api/remote/button` - Press Remote Button](#post-apiremotebutton---press-remote-button)
    - [POST `/api/screenshot` - Trigger Screenshot](#post-apiscreenshot---trigger-screenshot)
    - [GET `/api/settings` - List Settings Descriptors](#get-apisettings---list-settings-descriptors)
    - [POST `/api/settings` - Update Settings](#post-apisettings---update-settings)
    - [GET `/api/settings/raw` - Get Raw Settings Configuration](#get-apisettingsraw---get-raw-settings-configuration)
    - [POST `/api/time` - Set Device Time](#post-apitime---set-device-time)
    - [GET `/api/wifi` - List Saved Wi-Fi Credentials](#get-apiwifi---list-saved-wi-fi-credentials)
    - [POST `/api/wifi` - Save or Update Wi-Fi Credential](#post-apiwifi---save-or-update-wi-fi-credential)
    - [POST `/api/wifi/delete` - Delete Saved Wi-Fi Credential](#post-apiwifidelete---delete-saved-wi-fi-credential)
    - [POST `/api/wifi/forget-all` - Forget All Wi-Fi Credentials](#post-apiwififorget-all---forget-all-wi-fi-credentials)
  - [WebSocket Endpoint](#websocket-endpoint)
    - [Port 81 - Fast Binary Upload](#port-81---fast-binary-upload)
  - [Network Modes](#network-modes)
    - [Station Mode (STA)](#station-mode-sta)
    - [Access Point Mode (AP)](#access-point-mode-ap)
  - [Notes](#notes)

Examples use `crosspoint.local`. If mDNS does not resolve on your network, use
the IP address shown on the device screen.

## HTTP Pages

| Method | Path | Purpose |
|--------|------|---------|
| `GET` | `/` | Home/status page |
| `GET` | `/files` | File manager page |
| `GET` | `/settings` | Web settings page |
| `GET` | `/fonts` | SD-card font manager page |
| `GET` | `/js/jszip.min.js` | JavaScript asset used by the file manager |

## Device Status

Device hostname is dynamic via mDNS (`crosspoint-{deviceName}` or `crosspoint-{last4mac}`). The curl examples below use `crosspoint.local` as a shorthand; replace it with your device IP or actual mDNS name if it does not resolve.

---

## HTTP Endpoints

### GET `/` - Home Page

Serves the home page HTML interface.

**Request:**
```bash
curl http://crosspoint.local/
```

**Response:** HTML page (200 OK)

---

### GET `/files` - File Browser Page

Serves the file browser HTML interface.

**Request:**
```bash
curl http://crosspoint.local/files
```

**Response:** HTML page (200 OK)

---

### GET `/remote-input` - Remote Keyboard Page

Serves the browser fallback page for remote text entry.

**Request:**
```bash
curl http://crosspoint.local/remote-input
```

**Response:** HTML page (200 OK)

**Notes:**
- Only registered when `remote_keyboard_input` is enabled.
- Intended for phone and desktop browsers after scanning the QR code shown on-device.
- The page polls and claims the active remote keyboard session via the JSON APIs below.
- If no Wi-Fi is available, the device will start a temporary hotspot to serve this page.

---

### GET `/api/status` - Device Status

Returns JSON with device status information.

**Request:**
```bash
curl http://crosspoint.local/api/status
```

Response:

```json
{
  "version": "1.0.0",
  "protocolVersion": 1,
  "ip": "192.168.1.100",
  "mode": "STA",
  "rssi": -45,
  "freeHeap": 123456,
  "uptime": 3600,
  "device": "X4"
}
```

| Field             | Type   | Description                                               |
| ----------------- | ------ | --------------------------------------------------------- |
| `version`         | string | CrossPoint firmware version                               |
| `protocolVersion` | number | Transport schema version for app integrations             |
| `ip`              | string | Device IP address                                         |
| `mode`            | string | `"STA"` (connected to WiFi) or `"AP"` (access point mode) |
| `rssi`            | number | WiFi signal strength in dBm (0 in AP mode)                |
| `freeHeap`        | number | Free heap memory in bytes                                 |
| `uptime`          | number | Seconds since device boot                                 |

---

### GET `/api/plugins` - Compile-Time Feature Manifest

Returns JSON booleans describing which compile-time features are included in this firmware build.

**Request:**
```bash
curl http://crosspoint.local/api/plugins
```

**Response (200 OK, example):**
```json
{
  "markdown": true,
  "remote_keyboard_input": true,
  "remote_open_book": true,
  "remote_page_turn": true,
  "todo_planner": true,
  "pokemon_wallpaper_plugin": false
}
```

---

### GET `/api/remote-keyboard/session` - Active Remote Keyboard Session

Returns the currently active remote keyboard session, if any.

**Request:**
```bash
curl http://crosspoint.local/api/remote-keyboard/session
```

**Response (200 OK, no active session):**
```json
{
  "active": false
}
```

**Response (200 OK, active session):**
```json
{
  "active": true,
  "id": 42,
  "title": "WiFi Password",
  "text": "draft",
  "maxLength": 64,
  "isPassword": true,
  "claimedBy": "android",
  "lastClaimAt": 123456
}
```

| Field | Type | Description |
| ----- | ---- | ----------- |
| `active` | boolean | Whether the device is currently waiting for remote text input |
| `id` | number | Active session ID when `active` is `true` |
| `title` | string | On-device prompt title |
| `text` | string | Current draft text |
| `maxLength` | number | Maximum allowed length, or `0` for unlimited |
| `isPassword` | boolean | Whether the session represents password-style input |
| `claimedBy` | string | Optional last client ID that claimed the session |
| `lastClaimAt` | number | Optional timestamp of the most recent claim |

---

### POST `/api/remote-keyboard/claim` - Claim Remote Keyboard Session

Marks the current session as owned by a client and returns the updated snapshot.

**Request:**
```bash
curl -X POST http://crosspoint.local/api/remote-keyboard/claim \
  -H "Content-Type: application/json" \
  -d '{"id":42,"client":"android"}'
```

**Response (200 OK):**
```json
{
  "active": true,
  "id": 42,
  "title": "WiFi Password",
  "text": "draft",
  "maxLength": 64,
  "isPassword": true,
  "claimedBy": "android",
  "lastClaimAt": 123456
}
```

**Error responses:**
- `400` for missing or invalid JSON
- `404` when the session ID is missing or no longer active

---

### POST `/api/remote-keyboard/submit` - Submit Remote Keyboard Text

Completes the active remote keyboard session and returns the text to the device UI.

**Request:**
```bash
curl -X POST http://crosspoint.local/api/remote-keyboard/submit \
  -H "Content-Type: application/json" \
  -d '{"id":42,"text":"hunter2"}'
```

**Response (200 OK):**
```json
{
  "ok": true
}
```

**Error responses:**
- `400` for missing/invalid JSON or text that exceeds the session length limit
- `404` when the session ID is missing or no longer active

## File Management

### `GET /api/files`

Lists files and folders under a directory.

```bash
curl "http://crosspoint.local/api/files?path=/Books"
```

Query parameters:

| Parameter | Required | Default | Description |
|-----------|----------|---------|-------------|
| `path` | No | `/` | Directory to list |

Response:

```json
[
  {"name":"MyBook.epub","size":1234567,"isDirectory":false,"isEpub":true},
  {"name":"Notes","size":0,"isDirectory":true,"isEpub":false}
]
```

Hidden dotfiles are omitted unless the device setting `showHiddenFiles` is
enabled. `System Volume Information` and `XTCache` are always hidden/protected.

### `GET /download`

Downloads a file from the SD card.

### GET `/api/recent` - Recent Books

Returns the current recent-books list with derived reading progress. When the
Pokemon Party module is compiled in and a book has an assignment, the same
response also includes the saved Pokemon metadata for that book.

**Request:**
```bash
curl http://crosspoint.local/api/recent
```

**Response (200 OK, example):**
```json
[
  {
    "path": "/Books/MyBook.epub",
    "title": "My Book",
    "author": "A. Author",
    "last_position": "Ch 3 4/12 27%",
    "last_opened": 0,
    "hasCover": true,
    "progress": {
      "format": "epub",
      "percent": 27.32,
      "page": 4,
      "pageCount": 12,
      "position": "Ch 3 4/12 27%",
      "spineIndex": 2
    },
    "pokemon": {
      "id": 4,
      "name": "charmander"
    }
  }
]
```

| Field | Type | Description |
| ----- | ---- | ----------- |
| `path` | string | Absolute SD path of the recent book |
| `title` | string | Cached display title |
| `author` | string | Cached author |
| `last_position` | string | Human-readable cached progress label, or `""` when no progress is cached |
| `last_opened` | number | Compatibility placeholder, currently always `0` |
| `hasCover` | boolean | `true` when the recent-book cache already has a cover BMP |
| `progress` | object or `null` | Derived progress payload from the book cache |
| `pokemon` | object | Saved Pokemon metadata when `ENABLE_POKEMON_PARTY` is enabled and the book has an assignment |

**`progress` fields:**

| Field | Type | Description |
| ----- | ---- | ----------- |
| `format` | string | One of `epub`, `txt`, `markdown`, or `xtc` |
| `percent` | number | Reading progress percentage, rounded to 2 decimals |
| `page` | number | Current 1-based display page |
| `pageCount` | number | Total pages for the current section/book |
| `position` | string | Same formatted progress label used by `last_position` |
| `spineIndex` | number | EPUB-only 0-based spine/chapter index |

**Notes:**
- `pokemon` is omitted entirely unless the Pokemon Party API is compiled in and
  metadata exists for that book.
- Supported cached progress sources are `.epub`, `.txt`, `.md`, `.xtc`, and `.xtch`.

---

### GET `/api/book-progress` - Book Progress

Returns normalized cached progress for a single supported book file.

**Request:**
```bash
curl "http://crosspoint.local/api/book-progress?path=/Books/MyBook.epub"
```

**Query Parameters:**

| Parameter | Required | Description |
| --------- | -------- | ----------- |
| `path` | Yes | Absolute SD path to a supported book |

**Response (200 OK, example):**
```json
{
  "path": "/Books/MyBook.epub",
  "progress": {
    "format": "epub",
    "percent": 27.32,
    "page": 4,
    "pageCount": 12,
    "position": "Ch 3 4/12 27%",
    "spineIndex": 2
  }
}
```

If no cached progress exists yet, the endpoint still returns `200 OK` with:

```json
{
  "path": "/Books/MyBook.epub",
  "progress": null
}
```

**Error Responses:**

| Status | Body | Cause |
| ------ | ---- | ----- |
| 400 | `Missing path` | `path` parameter missing |
| 400 | `Invalid path` | Path fails SD path validation |
| 400 | `Unsupported book type` | File extension is not one of `.epub`, `.txt`, `.md`, `.xtc`, `.xtch` |
| 403 | `Cannot access protected items` | Path targets protected storage |
| 404 | `Book not found` | File does not exist |

**Notes:**
- `page` is always 1-based for display.
- `spineIndex` is present only for EPUB progress and remains 0-based.

---

### GET `/api/book-pokemon` - Book Pokemon Metadata

Returns saved Pokemon metadata for a single supported book.

This route is only registered when `ENABLE_POKEMON_PARTY` is enabled. When the
feature is disabled the route does not exist and the webserver returns `404`.

**Request:**
```bash
curl "http://crosspoint.local/api/book-pokemon?path=/Books/MyBook.epub"
```

**Response (200 OK, example):**
```json
{
  "path": "/Books/MyBook.epub",
  "pokemon": {
    "id": 25,
    "name": "pikachu",
    "types": ["electric"],
    "evolutionChain": [
      {"id": 172, "name": "pichu", "minLevel": null, "trigger": "friendship"},
      {"id": 25, "name": "pikachu", "minLevel": null, "trigger": null},
      {"id": 26, "name": "raichu", "minLevel": null, "trigger": "use-item"}
    ]
  }
}
```

If the book has no assignment yet, the endpoint returns `200 OK` with
`"pokemon": null`.

**Error Responses:**

| Status | Body | Cause |
| ------ | ---- | ----- |
| 400 | `Missing path` | `path` parameter missing |
| 400 | `Invalid path` | Path fails SD path validation |
| 400 | `Unsupported book type` | File extension is not one of `.epub`, `.txt`, `.md`, `.xtc`, `.xtch` |
| 403 | `Cannot access protected items` | Path targets protected storage |
| 404 | `Book not found` | File does not exist |

---

### PUT `/api/book-pokemon` - Save Book Pokemon Metadata

Stores or replaces Pokemon metadata for a supported book. The payload is saved
in the book's cache sidecar (`pokemon.json`).

This route is only registered when `ENABLE_POKEMON_PARTY` is enabled.

**Request:**
```bash
curl -X PUT \
  -H "Content-Type: application/json" \
  -d '{"path":"/Books/MyBook.epub","pokemon":{"id":1,"name":"bulbasaur","types":["grass","poison"]}}' \
  http://crosspoint.local/api/book-pokemon
```

**JSON Body:**

| Field | Required | Description |
| ----- | -------- | ----------- |
| `path` | Yes | Absolute SD path to a supported book |
| `pokemon` | Yes | Arbitrary JSON object to persist as the book's Pokemon metadata |

**Response (200 OK):**
```json
{
  "ok": true,
  "path": "/Books/MyBook.epub",
  "pokemon": {
    "id": 1,
    "name": "bulbasaur",
    "types": ["grass", "poison"]
  }
}
```

**Error Responses:**

| Status | Body | Cause |
| ------ | ---- | ----- |
| 400 | `Missing body` | No request body was sent |
| 400 | `Invalid JSON body` | Request body is not valid JSON |
| 400 | `Missing path` | `path` missing or empty |
| 400 | `Missing pokemon object` | `pokemon` is absent or not an object |
| 400 | `Invalid path` | Path fails SD path validation |
| 400 | `Unsupported book type` | File extension is not one of `.epub`, `.txt`, `.md`, `.xtc`, `.xtch` |
| 403 | `Cannot access protected items` | Path targets protected storage |
| 404 | `Book not found` | File does not exist |
| 500 | `Failed to save pokemon data` | Sidecar write failed |

---

### DELETE `/api/book-pokemon` - Clear Book Pokemon Metadata

Deletes the saved Pokemon assignment for a supported book.

This route is only registered when `ENABLE_POKEMON_PARTY` is enabled.

**Request:**
```bash
curl -X DELETE "http://crosspoint.local/api/book-pokemon?path=/Books/MyBook.epub"
```

**Response (200 OK):**
```json
{
  "ok": true,
  "path": "/Books/MyBook.epub"
}
```

**Error Responses:**

| Status | Body | Cause |
| ------ | ---- | ----- |
| 400 | `Missing path` | `path` parameter missing |
| 400 | `Invalid path` | Path fails SD path validation |
| 400 | `Unsupported book type` | File extension is not one of `.epub`, `.txt`, `.md`, `.xtc`, `.xtch` |
| 403 | `Cannot access protected items` | Path targets protected storage |
| 404 | `Book not found` | File does not exist |
| 500 | `Failed to delete pokemon data` | Sidecar delete failed |

---

### POST `/api/todo/entry` - Add TODO or Agenda Entry

Appends an entry to today's daily TODO file when the TODO planner feature is compiled in.

**Request:**
```bash
# Add a task entry
curl -X POST -d "type=todo&text=Buy milk" http://crosspoint.local/api/todo/entry

# Add an agenda/note entry
curl -X POST -d "type=agenda&text=Meeting at 14:00" http://crosspoint.local/api/todo/entry
```

**Form Parameters:**

| Parameter | Required | Description |
| --------- | -------- | ----------- |
| `text`    | Yes      | Entry text (1-300 chars, newlines are normalized to spaces) |
| `type`    | No       | `todo` (default) or `agenda` |

**Storage selection:**
- If today's `.md` file exists, append there.
- Else if today's `.txt` file exists, append there.
- Else create `.md` when markdown support is enabled, or `.txt` when markdown support is disabled.

**Response (200 OK):**
```json
{"ok":true}
```

**Error Responses:**

| Status | Body | Cause |
| ------ | ---- | ----- |
| 400 | `Missing text` | `text` parameter missing |
| 400 | `Invalid text` | Empty or too long text |
| 404 | `TODO planner disabled` | Feature is not compiled in |
| 503 | `Date unavailable` | Device date could not be resolved |
| 500 | `Failed to write TODO entry` | SD write failed |

---

### GET `/api/todo/today` - Read Daily Planner Entries

Returns today's TODO/agenda entries in structured form for web UI editing.

**Request:**
```bash
curl http://crosspoint.local/api/todo/today
```

**Response (200 OK):**
```json
{
  "ok": true,
  "date": "2026-02-27",
  "path": "/daily/2026-02-27.md",
  "items": [
    {"text": "Buy milk", "checked": false, "isHeader": false},
    {"text": "Meeting at 14:00", "checked": false, "isHeader": true}
  ]
}
```

| Field | Type | Description |
| ----- | ---- | ----------- |
| `date` | string | Current device date used for daily file selection |
| `path` | string | Resolved daily planner file path (`.md`/`.txt`) |
| `items[].text` | string | Entry text |
| `items[].checked` | boolean | Checkbox state for TODO entries |
| `items[].isHeader` | boolean | `true` for agenda/note entries |

**Error Responses:**

| Status | Body | Cause |
| ------ | ---- | ----- |
| 404 | `TODO planner disabled` | Feature is not compiled in |
| 503 | `Date unavailable` | Device date could not be resolved |

---

### POST `/api/todo/today` - Save Daily Planner Entries

Rewrites today's TODO/agenda list from a structured JSON payload.

**Request:**
```bash
curl -X POST \
  -H "Content-Type: application/json" \
  -d '{"items":[{"text":"Buy milk","checked":false,"isHeader":false},{"text":"Meeting at 14:00","isHeader":true}]}' \
  http://crosspoint.local/api/todo/today
```

**JSON Body:**

| Field | Required | Description |
| ----- | -------- | ----------- |
| `items` | Yes | Array of planner entries |
| `items[].text` | Yes | Entry text (trimmed, max 300 chars, empty entries ignored) |
| `items[].checked` | No | Checkbox state for TODO entries |
| `items[].isHeader` | No | `true` for agenda/note entries |

**Response (200 OK):**
```json
{"ok":true,"date":"2026-02-27","path":"/daily/2026-02-27.md"}
```

**Error Responses:**

| Status | Body | Cause |
| ------ | ---- | ----- |
| 400 | `Missing body` | Request body missing |
| 400 | `Invalid JSON body` | Invalid JSON payload |
| 400 | `Missing items array` | `items` key missing/not an array |
| 404 | `TODO planner disabled` | Feature is not compiled in |
| 503 | `Date unavailable` | Device date could not be resolved |
| 500 | `Failed to write TODO file` | SD write failed |

---

### POST `/api/notes/entry` - Add Note

Appends a note to `/notes.txt` when the Notes feature is compiled in.

**Request:**
```bash
curl -X POST -d "text=Remember page 42" http://crosspoint.local/api/notes/entry
```

**Form Parameters:**

| Parameter | Required | Description |
| --------- | -------- | ----------- |
| `text` | Yes | Note text (1-120 chars, newlines are normalized to spaces) |

**Response (200 OK):**
```json
{"ok":true}
```

**Error Responses:**

| Status | Body | Cause |
| ------ | ---- | ----- |
| 400 | `Invalid text` | Empty text |
| 404 | `Notes disabled` | Feature is not compiled in |
| 500 | `Failed to write note` | SD write failed |

---

### GET `/api/notes` - Read Notes

Returns line-based notes from `/notes.txt`.

**Request:**
```bash
curl http://crosspoint.local/api/notes
```

**Response (200 OK):**
```json
{
  "ok": true,
  "path": "/notes.txt",
  "items": [
    {"text": "Remember page 42"}
  ]
}
```

---

### POST `/api/notes` - Save Notes

Rewrites `/notes.txt` from a JSON payload. `items` may contain strings or objects with a `text` field.

**Request:**
```bash
curl -X POST \
  -H "Content-Type: application/json" \
  -d '{"items":["Remember page 42",{"text":"Look up author interview"}]}' \
  http://crosspoint.local/api/notes
```

**Response (200 OK):**
```json
{"ok":true,"path":"/notes.txt"}
```

**Error Responses:**

| Status | Body | Cause |
| ------ | ---- | ----- |
| 400 | `Missing body` | Request body missing |
| 400 | `Invalid JSON body` | Invalid JSON payload |
| 400 | `Missing items array` | `items` key missing/not an array |
| 404 | `Notes disabled` | Feature is not compiled in |
| 500 | `Failed to write notes` | SD write failed |

---

### GET `/download` - Download File

Downloads a file from the SD card.

**Request:**
```bash
# Download from root
curl -L "http://crosspoint.local/download?path=/mybook.epub" -o mybook.epub

# Download from a nested folder
curl -L "http://crosspoint.local/download?path=/Books/Fiction/mybook.epub" -o mybook.epub
```

**Query Parameters:**

| Parameter | Required | Description              |
| --------- | -------- | ------------------------ |
| `path`    | Yes      | Absolute file path on SD |

**Response (200 OK):**
- Binary file stream (`application/octet-stream` for most files)
- `application/epub+zip` for `.epub` files
- `Content-Disposition: attachment; filename="..."`

**Error Responses:**

| Status | Body                                | Cause                           |
| ------ | ----------------------------------- | ------------------------------- |
| 400    | `Missing path`                      | `path` parameter not provided   |
| 400    | `Invalid path`                      | Invalid or root path            |
| 400    | `Path is a directory`               | Attempted to download a folder  |
| 403    | `Cannot access system files`        | Hidden file (starts with `.`)   |
| 403    | `Cannot access protected items`     | Protected system file/folder    |
| 404    | `Item not found`                    | Path does not exist             |
| 500    | `Failed to open file`               | SD card access/open error       |

**Protected Items:**
- Files/folders starting with `.`
- `System Volume Information`
- `XTCache`

---

### POST `/upload` - Upload File

Uploads a file to the SD card via multipart form data.

**Request:**
```bash
curl -OJ "http://crosspoint.local/download?path=/Books/MyBook.epub"
```

Query parameters:

| Parameter | Required | Description |
|-----------|----------|-------------|
| `path` | Yes | File path to download |

Protected dotfiles, `System Volume Information`, and `XTCache` cannot be
downloaded. EPUB files are served as `application/epub+zip`; other files use
`application/octet-stream`.

### `POST /upload`

Uploads a file with HTTP multipart form data.

```bash
curl -X POST -F "file=@mybook.epub" "http://crosspoint.local/upload?path=/Books"
```

Query parameters:

| Parameter | Required | Default | Description |
|-----------|----------|---------|-------------|
| `path` | No | `/` | Destination directory |

Successful response:

```text
File uploaded successfully: mybook.epub
```

Notes:

- Existing files with the same name are overwritten.
- EPUB cache data for the uploaded path is cleared after a successful upload.
- HTTP upload uses a 4 KB write buffer before flushing to the SD card.

### `POST /mkdir`

Creates a folder.

### GET `/api/fonts` - List Installed Font Families

Returns a JSON object listing installed font families, available sizes, and their files on the SD card.

**Request:**
```bash
curl http://crosspoint.local/api/fonts
```

**Response (200 OK):**
```json
{
  "maxFamilies": 6,
  "families": [
    {
      "name": "MyFont",
      "sizes": [12, 14, 16],
      "files": [
        {"name": "MyFont-Regular.cpfont", "size": 12345}
      ]
    }
  ]
}
```

---

### POST `/api/fonts/upload` - Upload Font File

Uploads a `.cpfont` font file for a specific family via multipart form data.

**Request:**
```bash
curl -X POST -F "file=@MyFont-Regular.cpfont" "http://crosspoint.local/api/fonts/upload?family=MyFont"
```

**Query Parameters:**

| Parameter | Required | Description |
| --------- | -------- | ----------- |
| `family`  | Yes      | The name of the font family |

**Response (200 OK):**
```json
{
  "ok": true
}
```

**Error Responses:**

| Status | Body | Cause |
| ------ | ---- | ----- |
| 400 | `{"error":"Invalid .cpfont file"}` | The uploaded file is invalid or too small to validate magic header |

---

### POST `/api/fonts/delete` - Delete Font Family

Deletes an installed font family from the SD card.

**Request:**
```bash
curl -X POST -H "Content-Type: application/json" -d '{"family":"MyFont"}' http://crosspoint.local/api/fonts/delete
```

**JSON Body:**

| Field | Required | Description |
| ----- | -------- | ----------- |
| `family` | Yes | The name of the font family to delete |

**Response (200 OK):**
```json
{
  "ok": true
}
```

**Error Responses:**

| Status | Body | Cause |
| ------ | ---- | ----- |
| 400 | `{"error":"Invalid request"}` | Missing family parameter or invalid JSON |
| 500 | `{"error":"Delete failed"}` | Failed to delete the font family directory/files from SD card |

---

### GET `/api/sleep-images` - List Sleep Images

Returns a JSON array of images in the `/sleep/` folder on the SD card.

**Request:**
```bash
curl http://crosspoint.local/api/sleep-images
```

**Response (200 OK):**
```json
[
  {"path": "/sleep/foo.bmp", "name": "foo.bmp"},
  {"path": "/sleep/bar.bmp", "name": "bar.bmp"}
]
```

**Notes:**
- Returns an empty array `[]` if the `/sleep/` folder does not exist or contains no images.

---

### GET `/api/sleep-cover` - Get Pinned Sleep Cover

Returns the currently pinned sleep cover image path.

**Request:**
```bash
curl http://crosspoint.local/api/sleep-cover
```

**Response (200 OK):**
```json
{
  "path": "/sleep/foo.bmp",
  "name": "foo.bmp"
}
```

**Notes:**
- If no image is pinned, `path` and `name` will be empty strings.

---

### POST `/api/sleep-cover/pin` - Pin Sleep Cover

Sets a specific image or book cover to be displayed every time the device sleeps.

**Request (Mode A - Pin Sleep Folder Image):**
```bash
# Pin an image from the /sleep/ folder
curl -X POST -H "Content-Type: application/json" -d '{"path": "/sleep/foo.bmp"}' http://crosspoint.local/api/sleep-cover/pin

# Clear the pin (revert to random rotation)
curl -X POST -H "Content-Type: application/json" -d '{"path": ""}' http://crosspoint.local/api/sleep-cover/pin
```

**Request (Mode B - Pin Book Cover):**
```bash
# Pin the cover of a specific book
curl -X POST -H "Content-Type: application/json" -d '{"bookPath": "/books/mybook.epub"}' http://crosspoint.local/api/sleep-cover/pin
```

**Response (200 OK):**
```json
{
  "pinnedPath": "/sleep/foo.bmp"
}
```

**Notes:**
- **Mode A (Image Path):** Pins the specified file. If an empty path is sent, the pin is cleared. On success, the clearing request returns the plain text "Cleared".
- **Mode B (Book Path):** Copies the book's cover BMP to `/sleep/.pinned-cover.bmp` and sets that as the pinned image.
- When a cover is pinned, it will be used regardless of the random rotation setting, provided the **Sleep Screen** is set to **Custom**.

---

### POST `/mkdir` - Create Folder

Creates a new folder on the SD card.

**Request:**
```bash
curl -X POST -H "Content-Type: application/json" \
  -d '{"name":"NewFolder","path":"/"}' \
  http://crosspoint.local/mkdir
```

**JSON Body:**

| Parameter | Required | Default | Description |
|-----------|----------|---------|-------------|
| `name` | Yes | - | New folder name |
| `path` | No | `/` | Parent folder |

### `POST /rename`

Renames a file.

```bash
curl -X POST -d "path=/Books/old.epub&name=new.epub" http://crosspoint.local/rename
```

Form parameters:

| Parameter | Required | Description |
|-----------|----------|-------------|
| `path` | Yes | Existing file path |
| `name` | Yes | New file name, not a path |

Only files can be renamed through this endpoint. The old EPUB cache path is
cleared before the rename.

### `POST /move`

Moves a file into an existing folder.

```bash
curl -X POST -d "path=/Books/mybook.epub&dest=/Read" http://crosspoint.local/move
```

Form parameters:

| Parameter | Required | Description |
|-----------|----------|-------------|
| `path` | Yes | Existing file path |
| `dest` | Yes | Existing destination folder |

Only files can be moved through this endpoint. The old EPUB cache path is
cleared before the move.

### `POST /delete`

Deletes one or more files or empty folders.

| Status | Body                          | Cause                         |
| ------ | ----------------------------- | ----------------------------- |
| 400    | `Use JSON name/path body`     | Legacy form fields were sent  |
| 400    | `Missing JSON body`           | Request body was missing      |
| 400    | `Invalid JSON body`           | Request body was invalid JSON |
| 400    | `Missing folder name`         | `name` parameter not provided |
| 400    | `Folder name cannot be empty` | Empty folder name             |
| 400    | `Folder already exists`       | Folder with same name exists  |
| 500    | `Failed to create folder`     | SD card error                 |

---

### POST `/delete` - Delete File or Folder

Deletes one or more files or empty folders from the SD card.

**Request:**
```bash
# Delete one or more items
curl -X POST -H "Content-Type: application/json" \
  -d '["/Books/old.epub","/OldFolder"]' \
  http://crosspoint.local/delete
```

**JSON Body:** array of absolute paths to delete.

```text
Applied 2 setting(s)
```

## Font Management API

| Status | Body                                        | Cause                              |
| ------ | ------------------------------------------- | ---------------------------------- |
| 400    | `Missing JSON body`                         | Request body was missing           |
| 400    | `Invalid JSON body`                         | Request body was invalid JSON      |
| 400    | `Use paths JSON array`                      | Body was not an array, or legacy form fields were sent |
| 400    | `No paths provided`                         | `paths` was an empty JSON array    |
| 500    | `Failed to delete some items: ...`          | One or more paths could not be deleted |

**Protected Items:**
- `System Volume Information`
- `XTCache`

---

### GET `/api/anki/cards` - Export Anki Cards

Returns the in-memory Anki cards database as a JSON array.

**Request:**
```bash
curl http://crosspoint.local/api/anki/cards
```

**Response (200 OK):**
```json
[
  {
    "front": "example word",
    "back": "definition of the word",
    "context": "sentence context"
  }
]
```

**Notes:**
- Only registered when `ENABLE_ANKI_SUPPORT` is enabled.

---

### GET `/api/cover` - Get Book Cover BMP

Serves a BMP cover image for a book specified by `path`.

**Request:**
```bash
curl "http://crosspoint.local/api/cover?path=/Books/MyBook.epub" -o cover.bmp
```

**Query Parameters:**

| Parameter | Required | Description |
| --------- | -------- | ----------- |
| `path`    | Yes      | Absolute SD path to a book |

**Response (200 OK):**
- Binary BMP image stream (`image/bmp`)

---

### POST `/api/koreader/use-opds` - KOReader Sync Setup

Configures the reader sync engine to use the credentials of a specific OPDS server for KOSync.

**Request:**
```bash
curl -X POST -H "Content-Type: application/json" -d '{"index":0}' http://crosspoint.local/api/koreader/use-opds
```

**Response (200 OK):**
```text
OK
```

---

### POST `/api/maintenance/clear-cache` - Clear Reading Cache

Clears cached book rendering files from `XTCache` on the SD card.

**Request:**
```bash
curl -X POST http://crosspoint.local/api/maintenance/clear-cache
```

**Response (200 OK):**
```json
{
  "removed": 42,
  "failed": 0,
  "message": "Cache cleared"
}
```

---

### POST `/api/maintenance/clear-crashes` - Clear Crash Reports

Deletes crash reports from the SD card and clears panic flags.

**Request:**
```bash
curl -X POST http://crosspoint.local/api/maintenance/clear-crashes
```

**Response (200 OK):**
```json
{
  "ok": true
}
```

---

### POST `/api/maintenance/clear-logs` - Clear Debug Logs

Clears the debug log file from the SD card and empties the in-memory log buffer.

**Request:**
```bash
curl -X POST http://crosspoint.local/api/maintenance/clear-logs
```

**Response (200 OK):**
```json
{
  "ok": true
}
```

---

### POST `/api/maintenance/reset-settings` - Reset Persisted Settings

Resets all persisted reader settings to their default values (Wi-Fi credentials, recent books, and progress are preserved).

**Request:**
```bash
curl -X POST http://crosspoint.local/api/maintenance/reset-settings
```

**Response (200 OK):**
```json
{
  "ok": true
}
```

---

### POST `/api/maintenance/validate-sleep-images` - Validate Sleep Images

Scans and validates all images in the `/sleep/` directory on the SD card.

**Request:**
```bash
curl -X POST http://crosspoint.local/api/maintenance/validate-sleep-images
```

**Response (200 OK):**
```json
{
  "valid": 10,
  "invalid": 1,
  "message": "Validation complete"
}
```

---

### GET `/api/opds` - List OPDS Servers

Returns a JSON array of all configured OPDS catalog servers.

**Request:**
```bash
curl http://crosspoint.local/api/opds
```

**Response (200 OK):**
```json
[
  {
    "index": 0,
    "name": "My OPDS Server",
    "url": "http://192.168.1.50/opds",
    "username": "user",
    "hasPassword": true
  }
]
```

---

### POST `/api/opds` - Add or Update OPDS Server

Adds a new OPDS catalog server, or updates an existing one if `index` is specified in the JSON body.

**Request:**
```bash
curl -X POST -H "Content-Type: application/json" \
  -d '{"name":"My OPDS","url":"http://192.168.1.50/opds","username":"user","password":"pass"}' \
  http://crosspoint.local/api/opds
```

**JSON Body:**

| Field | Required | Description |
| ----- | -------- | ----------- |
| `name` | Yes | Display name for the OPDS server |
| `url` | Yes | OPDS server URL |
| `username` | No | Optional username |
| `password` | No | Optional password |
| `index` | No | Optional index to update an existing server |

**Response (200 OK):**
```text
OK
```

---

### POST `/api/opds/delete` - Remove OPDS Server

Deletes a configured OPDS server by index.

**Request:**
```bash
curl -X POST -H "Content-Type: application/json" -d '{"index":0}' http://crosspoint.local/api/opds/delete
```

**Response (200 OK):**
```text
OK
```

---

### POST `/api/opds/test` - Test OPDS Server Connectivity

Tests network connectivity and credentials for an OPDS server.

**Request:**
```bash
curl -X POST -H "Content-Type: application/json" \
  -d '{"name":"My OPDS","url":"http://192.168.1.50/opds","username":"user","password":"pass"}' \
  http://crosspoint.local/api/opds/test
```

**Response (200 OK):**
```text
Connection successful
```

---

### POST `/api/open-book` - Open Book Remotely

Commands the reader to open a specific book from the SD card.

**Request:**
```bash
curl -X POST -H "Content-Type: application/json" \
  -d '{"path":"/Books/MyBook.epub"}' \
  http://crosspoint.local/api/open-book
```

**Response (202 Accepted):**
```json
{
  "status": "opening"
}
```

**Notes:**
- Gated by `ENABLE_REMOTE_CONTROL`

---

### POST `/api/pokemon-sprite` - Cache Pokemon Sprite

Caches a base64-encoded 1-bit BMP sprite for a given Pokemon species ID on the device for offline rendering.

**Request:**
```bash
curl -X POST -H "Content-Type: application/json" \
  -d '{"speciesId":25,"bmpBase64":"Qk0eAAAAAAAAAD4AAAAoAAAA..."}' \
  http://crosspoint.local/api/pokemon-sprite
```

**Response (200 OK):**
```json
{
  "ok": true
}
```

**Notes:**
- Gated by `ENABLE_POKEMON_PARTY`

---

### GET `/api/pokemon-team` - Get Pokemon Team

Returns the prebaked roster of up to six Pokemon in the current party.

**Request:**
```bash
curl http://crosspoint.local/api/pokemon-team
```

**Response (200 OK):**
```json
[
  {
    "speciesId": 25,
    "name": "pikachu",
    "level": 5,
    "evolutionChain": []
  }
]
```

**Notes:**
- Gated by `ENABLE_POKEMON_PARTY`

---

### PUT `/api/pokemon-team` - Save Pokemon Team

Saves the prebaked team roster of up to six Pokemon. Only base-form Pokemon may be added.

**Request:**
```bash
curl -X PUT -H "Content-Type: application/json" \
  -d '{"team":[{"speciesId":25,"name":"pikachu","level":5}]}' \
  http://crosspoint.local/api/pokemon-team
```

**Response (200 OK):**
```json
{
  "ok": true
}
```

**Notes:**
- Gated by `ENABLE_POKEMON_PARTY`

---

### POST `/api/remote/button` - Press Remote Button

Simulates a page-turn button event (e.g. forward, back) remotely.

**Request:**
```bash
curl -X POST -H "Content-Type: application/json" \
  -d '{"button":"page_forward"}' \
  http://crosspoint.local/api/remote/button
```

**Response (202 Accepted):**
```json
{
  "status": "ok"
}
```

**Notes:**
- Gated by `ENABLE_REMOTE_CONTROL`
- Valid button values: `page_forward`, `next`, `page_back`, `prev`, `previous`

---

### POST `/api/screenshot` - Trigger Screenshot

Triggers an on-device screenshot capture, saving it to the SD card.

**Request:**
```bash
curl -X POST http://crosspoint.local/api/screenshot
```

**Response (202 Accepted):**
```json
{
  "status": "ok"
}
```

---

### GET `/api/settings` - List Settings Descriptors

Lists editable reader settings metadata. Returns as a chunked transfer-encoded JSON array of settings descriptors.

**Request:**
```bash
curl http://crosspoint.local/api/settings
```

**Response (200 OK):**
```json
[
  {
    "key": "fontSize",
    "name": "Font Size",
    "category": "reader",
    "type": "value",
    "value": 3,
    "min": 1,
    "max": 10,
    "step": 1
  }
]
```

---

### POST `/api/settings` - Update Settings

Applies settings mutations using a partial JSON object.

**Request:**
```bash
curl -X POST -H "Content-Type: application/json" \
  -d '{"fontSize":4,"darkMode":true}' \
  http://crosspoint.local/api/settings
```

**Response (200 OK):**
```text
Applied 2 setting(s)
```

---

### GET `/api/settings/raw` - Get Raw Settings Configuration

Returns raw setting values as a flat key-value JSON map.

**Request:**
```bash
curl http://crosspoint.local/api/settings/raw
```

**Response (200 OK):**
```json
{
  "fontSize": 3,
  "darkMode": true,
  "deviceName": "crosspoint-reader"
}
```

---

### POST `/api/time` - Set Device Time

Sets the device clock via epoch timestamp.

**Request:**
```bash
curl -X POST -H "Content-Type: application/json" \
  -d '{"time":1783819200}' \
  http://crosspoint.local/api/time
```

**Response (200 OK):**
```json
{
  "ok": true
}
```

**Notes:**
- Gated by `ENABLE_WIFI_CLOCK`

---

### GET `/api/wifi` - List Saved Wi-Fi Credentials

Returns a JSON array of all saved Wi-Fi network credentials (SSIDs, index, connection status).

**Request:**
```bash
curl http://crosspoint.local/api/wifi
```

**Response (200 OK):**
```json
[
  {
    "index": 0,
    "ssid": "MyWiFiNetwork",
    "hasPassword": true,
    "isLastConnected": true
  }
]
```

---

### POST `/api/wifi` - Save or Update Wi-Fi Credential

Adds a new saved Wi-Fi credential, or updates an existing one if `index` is specified.

**Request:**
```bash
curl -X POST -H "Content-Type: application/json" \
  -d '{"ssid":"MyWiFiNetwork","password":"my-wifi-password"}' \
  http://crosspoint.local/api/wifi
```

**JSON Body:**

| Field | Required | Description |
| ----- | -------- | ----------- |
| `ssid` | Yes | Wi-Fi network SSID |
| `password` | No | Wi-Fi network password (optional) |
| `index` | No | Optional index to update an existing credential |

**Response (200 OK):**
```text
OK
```

---

### POST `/api/wifi/delete` - Delete Saved Wi-Fi Credential

Removes a saved Wi-Fi credential by SSID.

**Request:**
```bash
curl -X POST -H "Content-Type: application/json" \
  -d '{"ssid":"MyWiFiNetwork"}' \
  http://crosspoint.local/api/wifi/delete
```

**Response (200 OK):**
```text
OK
```

---

### POST `/api/wifi/forget-all` - Forget All Wi-Fi Credentials

Removes all saved Wi-Fi network credentials from the device.

**Request:**
```bash
curl -X POST http://crosspoint.local/api/wifi/forget-all
```

**Response (200 OK):**
```json
{
  "ok": true
}
```

---

## WebSocket Endpoint

### Port 81 - Fast Binary Upload

A WebSocket endpoint for high-speed binary file uploads. More efficient than HTTP multipart for large files.

**Connection:**
```

Response:

```json
{
  "maxFamilies": 128,
  "families": [
    {
      "name": "Literata",
      "sizes": [12, 14, 16, 18],
      "files": [
        {"name": "Literata_12.cpfont", "size": 123456}
      ]
    }
  ]
}
```

### `POST /api/fonts/upload`

Uploads one `.cpfont` file into a family folder.

```bash
curl -X POST \
  -F "family=Literata" \
  -F "file=@Literata_12.cpfont" \
  http://crosspoint.local/api/fonts/upload
```

The handler validates the family name, `.cpfont` filename, and `CPFONT` magic
bytes before accepting the file.

Successful response:

```json
{"ok":true}
```

### `POST /api/fonts/delete`

Deletes an installed font family.

```bash
curl -X POST \
  -H "Content-Type: application/json" \
  -d '{"family":"Literata"}' \
  http://crosspoint.local/api/fonts/delete
```

Successful response:

```json
{"ok":true}
```

## OPDS Server API

### `GET /api/opds`

Lists saved OPDS servers. Passwords are never returned.

```bash
curl http://crosspoint.local/api/opds
```

Response:

```json
[
  {
    "index": 0,
    "name": "My Catalog",
    "url": "http://calibre.local:8080/opds",
    "username": "reader",
    "hasPassword": true
  }
]
```

### `POST /api/opds`

Adds or updates an OPDS server. Include `index` to update an existing entry.
If `password` is omitted during an update, the existing password is preserved.

```bash
curl -X POST \
  -H "Content-Type: application/json" \
  -d '{"name":"My Catalog","url":"http://calibre.local:8080/opds","username":"reader","password":"secret"}' \
  http://crosspoint.local/api/opds
```

### `POST /api/opds/delete`

Deletes an OPDS server by index.

```bash
curl -X POST \
  -H "Content-Type: application/json" \
  -d '{"index":0}' \
  http://crosspoint.local/api/opds/delete
```

## Wi-Fi Credential API

### `GET /api/wifi`

Lists saved Wi-Fi networks. Passwords are never returned.

```bash
curl http://crosspoint.local/api/wifi
```

Response:

```json
[
  {
    "index": 0,
    "ssid": "HomeWiFi",
    "hasPassword": true,
    "isLastConnected": true
  }
]
```

### `POST /api/wifi`

Adds or updates a saved Wi-Fi network. Include `index` to update an existing
entry. If `password` is omitted during an update, the existing password is
preserved.

```bash
curl -X POST \
  -H "Content-Type: application/json" \
  -d '{"ssid":"HomeWiFi","password":"secret"}' \
  http://crosspoint.local/api/wifi
```

### `POST /api/wifi/delete`

Deletes a saved Wi-Fi network by index.

```bash
curl -X POST \
  -H "Content-Type: application/json" \
  -d '{"index":0}' \
  http://crosspoint.local/api/wifi/delete
```

## WebSocket Upload

### Port 81

The WebSocket path is used for fast binary uploads from the file manager and
Calibre plugin workflows.

Connection:

```text
ws://crosspoint.local:81/
```

Protocol:

1. Client sends text: `START:<filename>:<size>:<path>`
2. Server replies `READY`
3. Client sends binary chunks
4. Server sends `PROGRESS:<received>:<total>` every 64 KB or at completion
5. Server sends `DONE` when complete or `ERROR:<message>` on failure

Example session:

```text
Client -> START:mybook.epub:1234567:/Books
Server -> READY
Client -> [binary chunk]
Server -> PROGRESS:65536:1234567
...
Server -> DONE
```

Error messages include:

| Message | Cause |
|---------|-------|
| `ERROR:Upload already in progress` | A second upload was started before the first completed |
| `ERROR:Invalid START format` | Malformed START message or invalid size token |
| `ERROR:Failed to create file` | Destination file could not be opened |
| `ERROR:No upload in progress` | Binary data arrived without a matching START |
| `ERROR:Upload overflow` | Client sent more bytes than declared |
| `ERROR:Write failed - disk full?` | SD write failed |

Incomplete WebSocket uploads are deleted on disconnect or error.

## WebDAV

The same HTTP server registers a WebDAV-compatible handler for file manager clients.

Supported methods:

```text
OPTIONS, GET, HEAD, PUT, DELETE, PROPFIND, MKCOL, MOVE, COPY, LOCK, UNLOCK
```

Notes:

- `PUT` writes to a temporary `.davtmp` file first, then renames it into place.
- Protected paths are rejected.
- `LOCK` and `UNLOCK` are accepted for client compatibility only. The server
  does not implement full WebDAV Class 2 locking semantics such as persistent
  locks or lock discovery.

## UDP Discovery

The server listens on UDP port `8134`. When it receives the text payload
`hello`, it replies to the sender with:

```text
crosspoint (on <hostname>);81
```

The final field is the WebSocket upload port.

## Network Modes

### Station Mode (STA)

- Device joins an existing 2.4 GHz Wi-Fi network.
- `crosspoint.local` is advertised with mDNS when available.
- `/api/status` returns `"mode": "STA"` and RSSI in dBm.

### Access Point Mode (AP)

- Device creates an open hotspot named `CrossPoint-Reader`.
- The device shows a Wi-Fi QR code and URL QR code.
- The fallback IP is typically `192.168.4.1`.
- `/api/status` returns `"mode": "AP"` and `"rssi": 0`.

### Calibre Wireless

Calibre Wireless starts the same web server in STA mode and displays setup
instructions plus WebSocket upload progress on the device screen.

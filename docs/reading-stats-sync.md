---
title: Reading Stats Sync
nav_order: 7
---

# Reading Stats Sync

CrossInk can sync all-time reading stats between nearby readers running CrossInk from **File Transfer > Sync Stats**. The sync is direct reader-to-reader; it does not use a server, account, or one "main" reader. This only works with CrossInk reading stats. This will _NOT_ work with any other firmware's reading stats.

## What Gets Synced

Nearby Stats Sync shares only each reader's all-time counters:

- total reading sessions
- total reading time
- total pages turned
- completed books

It does not sync per-book reading position, bookmarks, recent books, files, KOReader progress, settings, WiFi passwords, or OPDS servers.

Each device owns one record, and the Reading Stats screen sums all the records it can read. The device does not merge another reader's totals into its local `global_stats.bin`.

## How Nearby Sync Works

1. Open **File Transfer > Sync Stats** on both readers.
2. Press **Sync** on one reader only.
3. The readers find each other over ESP-NOW and exchange their local
   `/.crosspoint/global_stats.bin` payloads.
4. Each reader saves the other reader's payload under
   `/.crosspoint/synced_stats/device_<mac>.bin`.
5. Future Reading Stats views show this reader's local stats plus the other
   valid files in `/.crosspoint/synced_stats/`.

The sync screen creates `/.crosspoint/synced_stats/` the first time you use it. That folder stores snapshots received from other readers. This reader's own all-time totals stay in `/.crosspoint/global_stats.bin`; CrossInk does not write this reader's own `device_<mac>.bin` file into `synced_stats/`.

## SD Card Folder Structure

The reading-stats files live under `.crosspoint` on the SD card:

```text
.crosspoint/
├── global_stats.bin
├── global_stats.bin.bak
└── synced_stats/
    ├── device_aabbccddeeff.bin
    ├── device_112233445566.bin
    └── manual-copy.bin
```

`global_stats.bin` is this reader's local all-time stats. The `.bak` file is a
backup used if the main file is corrupt.

`synced_stats/` contains one contribution file per other synced reader. Each file is a snapshot from the last time this reader received that device's stats; it is not a live copy of that other reader's current `global_stats.bin`. Files created by nearby sync use the peer reader's hardware MAC address in the file
name: `device_<mac>.bin`, with no colons or dashes.

When displaying aggregated stats, CrossInk reads every non-folder file in `synced_stats/` that has a valid CrossInk stats payload. The file name does not have to match `device_<mac>.bin` for manual imports. If a manually copied file matches this reader's own `device_<local-mac>.bin`, CrossInk skips it so local stats are not counted twice.

## Manually Removing Synced Stats

Back up the SD card first if the stats matter to you.

To remove one old reader from the aggregate total, delete that reader's file from `/.crosspoint/synced_stats/` on every device that has it. For example:

```text
/.crosspoint/synced_stats/device_aabbccddeeff.bin
```

To stop using synced aggregate totals on a reader, delete the whole `/.crosspoint/synced_stats/` folder from that reader. As long as the folder is gone, local stats saves will not recreate it. Opening Sync Stats again will recreate the folder.

To reset only this reader's local all-time totals, delete both:

```text
/.crosspoint/global_stats.bin
/.crosspoint/global_stats.bin.bak
```

Do not delete `/.crosspoint/epub_<hash>/stats.bin` or versioned files such as
`/.crosspoint/epub_<hash>/stats_v5.bin` unless you also want to remove the
per-book stats for that book.

If a sync was interrupted, a temporary `*.part` file may be left in `synced_stats/`. CrossInk ignores invalid stats files while aggregating, so it is safe to delete leftover `.part` files.

## Manual Copying Between Devices

You can manually copy a valid stats file into another reader's `/.crosspoint/synced_stats/` folder. A descriptive file name such as `old_reader.bin` can work because CrossInk validates the file contents, not the name.

Use normal `device_<mac>.bin` names when possible. Do not copy this reader's own local `global_stats.bin` into `synced_stats/` under a different name; CrossInk will treat it like another reader and the all-time total will be too high.

## Manual Server Upload

Open **Settings > KOReader Sync**, configure the account and server, then choose
**Upload Stats**. Review the destination and press **Upload** (or tap its button).
The default server is `https://sync.crosspointreader.com`; self-hosted CrossPoint
Sync servers also work. Ordinary KOReader-only servers do not support this API.

Each invocation uploads this device's saved overall stats and saved EPUB/XTC stats
for books in its existing Library index. Refresh Library first to include newly
added books. Missing files, unreadable book stats, and books with tracking disabled
are skipped. Overall **1** means the overall snapshot was accepted; **0** means
it was not uploaded. Book and skipped counts remain visible after errors. If the
Library index is unavailable, only the overall upload is attempted and the screen
asks you to refresh Library. Nearby readers' totals are never uploaded as your own.

This action uploads **only** after manual confirmation. There are no uploads on
book close, sleep, or a timer. Device-wide tracking must be enabled;
this action never enables it. It does not change reading positions or download stats.
Historical totals already included in overall stats remain part of that snapshot,
even if tracking for an individual book is now disabled.

Book matching uses the existing Filename/Binary setting. Keep it consistent across
readers; changing it or renaming a file can create another server record. Titles
and authors are not sent by this action; books already sent through progress sync
with metadata can be named in the server UI. Stats-only books may not appear in
that UI's progress-based book list, although their snapshots are stored.

Press Back to stop between requests. A network request has a bounded timeout.
Accepted snapshots remain on the server after cancellation or a later failure;
retrying replaces them rather than adding the same reading time again.

Local deletion does not delete server snapshots. Removing stats files causes
uploads to skip them; an explicit device-wide stats reset writes an empty valid
snapshot, which replaces that device's overall server totals on the next upload.
Do not copy another reader's local stats files onto this reader and upload both:
those copied histories cannot be distinguished from reading performed locally.

No cache-format migration or cache clearing is needed. Stats uploads use the same
TLS policy as the existing KOReader progress-sync client: HTTPS encrypts traffic,
but server certificates are not verified. No additional SDK changes or custom CA
file are required. A configured plain HTTP server sends the authentication key
and stats without encryption.


## Progress and Stats Together

Enable **Settings > KOReader Sync > Sync Stats** to upload saved overall and
current-book stats during the reader's manual **Sync Progress** action. This
option defaults to off, including for existing accounts. It also runs when Smart
Sync finds equal progress or applies a newer server position. Disabled tracking,
missing stats, and invalid stats files do not upload replacement zeros. Other
readers' stats are never merged into local files.

These are separate API requests within the same user action, not one transaction.
Stats and clippings are attempted independently. If one upload fails, the other
is still attempted and the screen reports each result. Folder sync also attempts
these saved snapshots when progress sync fails. Retry the manual sync to resend the current snapshots.

## Catch Up a Folder

In **File Browser**, long-press a folder (hold Confirm on button devices), select
**Sync Folder Progress**, then confirm. This syncs saved EPUB positions in that
folder and its subfolders. It reads the SD folders directly; no Library refresh is
needed. The global sync method applies to every book: **Smart** keeps the further
progress, uploading local progress or applying remote progress as needed; **Ask**
shows a choice for each book, with its filename at the top.

Books need saved progress; missing EPUB metadata is rebuilt before syncing.
Unread books and old or invalid progress without a usable page count skip position sync;
their saved stats and clippings are still considered when enabled.
Metadata or mapping failures show an error after saved stats and clippings have
been attempted. Choose **Skip book** to continue, or Back to stop. Hidden entries and
non-EPUB files are excluded. The folder's
name does not mark books as finished, and this cannot reconstruct deleted history.
Traversal is bounded to 16 folder levels and 1024-byte paths; exceeding a limit or
an unreadable folder stops the sync rather than silently reporting success.

**Sync Stats** and **Upload Clippings** apply to each synced book when enabled.
Overall stats are attempted once after confirmation and Wi-Fi connection, even if
the folder is empty or every book skips or fails position sync. A failure does not
block per-book uploads. The result shows overall stats, book stats, and clippings
separately; a successful position may also appear in the failed-book count if an
extra upload failed. Missing data and disabled tracking show no upload rather
than replacing server data with zeros. **Send Metadata** controls title,
author, and filename sharing just as it does for normal progress uploads.
Press Back to stop, including from a per-book prompt; a book with many clippings
can take longer. Completed uploads and downloads remain applied if a later book
fails or you cancel.

## Clippings Upload

Enable **Settings > KOReader Sync > Upload Clippings** to include the current
book's saved clippings in manual progress sync and folder catch-up. This option
also defaults to off. The first implementation is upload-only: it does not download
server clippings or propagate local deletions. Notes and colors already on the
server are preserved. Server-deleted IDs stay deleted; this uploader never revives
a server tombstone.

IDs follow the server's SHA-256 rule using the uploaded creation value and exact
text, so unchanged clippings keep the same ID across retries. New clippings save
UTC creation time from the available clock, or zero when no clock is available.
Older uptime-based timestamps are exported as zero (unknown), not as dates near
1970. Identical undated quote text within one book shares one server ID; local
records remain separate. No historical dates are invented and no clipping file
migration is performed. One clipping is read and sent at a
time, including its chapter and saved layout hints, without loading all clippings
into memory. The server's 4096-byte text limit matches the local clipping limit.

### Sync a selected book

Choose **Sync Progress** from an EPUB’s context menu in **Library** or **File Browser**. This uses the same sync behavior as the reader menu, including **Sync Stats** and **Upload Clippings** only when enabled. If account credentials are missing, account settings open first; return to the book menu to retry after setup. Sync resumes in the selected book’s reader. Other file formats do not show this action.

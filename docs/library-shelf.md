# Library shelf on Home — design

Goal: books from the connected Grimoire/Booklore (OPDS) library appear on the
Home screen alongside on-device recents, in every theme, without per-theme work.

## Architecture

```
OpdsBookBrowserActivity ──(root feed loaded)──┐
BackgroundWifiService ──(one-shot fetch)──────┼──> LibraryShelfStore ──> /.crosspoint/library_shelf.json
                                              │         ▲ (std::mutex; bg task writes, main task reads)
HomeActivity::onEnter ── loadShelf() ─────────┘         │
        └─ merge: shelf entries appended to the recents vector handed to the
           theme as RecentBook{ path = "opds://<href>", title, author, cover="" }
```

- **Cache**: `LibraryShelfStore` (src/util/), singleton like RecentBooksStore,
  mutex-guarded (`std::mutex` — bg-task writer, main-task reader). JSON file
  `/.crosspoint/library_shelf.json`: `{ "server": <name>, "entries": [{t,a,h}] }`,
  capped at 6 BOOK-type entries.
- **Writers** (two, both opportunistic — no scheduled wakeups, no RTC dependency):
  1. The OPDS browser: whenever it renders the ROOT feed of the FIRST stored
     server, it snapshots the first 6 book entries into the store.
  2. The Always-mode background server: once per service session, after the web
     server is up and heap allows, fetch server[0]'s root feed (same capped
     HttpDownloader path the browser uses) and refresh the store. Runs on the
     bg task; never from an activity (avoids the task-lifetime race class).
- **Render**: HomeActivity appends shelf entries (paths prefixed `opds://`)
  to the recents vector passed to `drawRecentBookCover`. All themes render
  them as coverless cards (book icon) with title/author — zero theme changes.
  The cover ensure/thumb pipeline and progress lookups skip `opds://` paths.
- **Activation**: selecting a shelf entry opens the OPDS catalog (root) for
  server[0] — the same launch the Library menu entry uses.

## Deliberate v1 limits / future work
- No cover downloads for shelf entries (bandwidth + heap; revisit with a
  thumb-download step in the bg fetch).
- Activation opens the catalog root, not the specific book (`href` for a book
  entry is the download URL; deep-linking needs a detail-page navigation hook).
- Shelf source is the feed's natural order ("recent" when Booklore serves it
  that way); no per-feed selection UI.
- Staleness: the cache persists across reboots and refreshes on browse or bg
  session; there is no TTL (no trustworthy wall clock without WiFi anyway).

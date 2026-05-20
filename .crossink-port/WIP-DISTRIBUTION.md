# WIP distribution — lane worktrees

Generated: 2026-05-19  
Integration HEAD: `4261fc0a` (`feature/absorb-crossink`)

All five lane worktrees were created/updated at integration tip `4261fc0a`.
WIP was moved off the integration checkout into lane trees only.

## Worktree status

| Lane | Path | Branch | WIP (uncommitted) |
|------|------|--------|-------------------|
| home | `worktrees/crosspoint-reader-home` | `port/home` | `RecentBooksGridActivity.*` (new), `ActivityManager.cpp`, i18n keys for grid |
| reader | `worktrees/crosspoint-reader-reader` | `port/reader` | `EpubReaderActivity.cpp`, `EpubReaderMenuActivity.cpp`, theme tweaks |
| file | `worktrees/crosspoint-reader-file` | `port/file` | **Clean** — `e803b890` / `49c123f9` already on integration |
| web | `worktrees/crosspoint-reader-web` | `port/web` | `BaseTheme.cpp` disabled-key fill, `FilesPage.html`, generated HTML headers |
| settings | `worktrees/crosspoint-reader-settings` | `port/settings` | `recentBooksView` setting in `CrossPointSettings.h`, `JsonSettingsIO`, `SettingsList` |

Integration checkout: **clean** (no lane source edits).

## Stash → lane mapping

| Stash | Label | Disposition |
|-------|-------|-------------|
| `stash@{0}` | `integration-lane: pre-merge WIP` (port/web) | Absorbed into web worktree WIP |
| `stash@{1}` | `integration-lane: pre-merge WIP` (port/file) | Dropped — superseded by integration file-browser commits |
| `stash@{2}` | `integration-lane: pre-merge WIP` (port/reader) | Absorbed into reader worktree WIP |
| `stash@{3}` | `integration-lane: pre-merge WIP` (port/home) | Absorbed into home worktree WIP |
| `stash@{4}` | `port-audit-20260519-lane-home-grid-post-audit` | Settings hunks → `port/settings`; `ActivityManager` → `port/home` |
| `stash@{5}` | `integration: reader WIP off wrong tree` | Absorbed into `port/reader` |
| `stash@{6}` | `port-audit-20260519-lane-reader-i18n` | Partially absorbed; i18n conflicts resolved to integration base |
| `stash@{7}` | `port-audit-20260519-lane-web-baseTheme` | Absorbed into `port/web` |
| `stash@{8+}` | file-browser / all-sources / overlay / unrelated | **Do not apply whole** — superseded or mixed; see `AUDIT.md` |

## Preserved patches (backup only)

| Patch | Target lane |
|-------|-------------|
| `preserved-patches/wip-lane-web-baseTheme-disabled-key-20260519.patch` | web |
| `preserved-patches/wip-lane-file-worktree-20260519.patch` | file (superseded by integration) |
| `preserved-patches/wip-main-integration-20260519.patch` | web |
| `preserved-patches/wip-main-dirty-20260519.patch` | split reference — do not apply wholesale |

## Already on integration (do not re-port)

| CrossInk hash | Commit |
|---------------|--------|
| `91f11a03` | Minimal theme |
| `684946c5` | RoundedRaff padding |
| `8ab81889` | Menu item resize |
| `004b893e` | Base theme overlay removal |
| `642402f8` | Home progress bar |
| `e803b890` | Hidden files toggle |
| `49c123f9` | Delete on hold not release |

## Next steps per lane

- **home**: finish grid view (`ea9568c3`, `a45de2df`); commit on `port/home`
- **reader**: finish reader controls/status bar WIP; commit on `port/reader`
- **file**: pick next tracker item (`a27ee056` mark finished, etc.)
- **web**: finish shift-key UX (`3cc346a1`); regenerate HTML headers; commit on `port/web`
- **settings**: finish `recentBooksView` + Phase 4 power-button cluster; commit on `port/settings`

See `MERGE-PROTOCOL.md` for integration merge loop.

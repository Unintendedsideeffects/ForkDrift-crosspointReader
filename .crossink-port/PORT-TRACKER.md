# CrossInk → ForkDrift feature absorption tracker

> **Subagents:** read `.crossink-port/SUBAGENT.md` before any edit.

Branch: `feature/absorb-crossink` (off `fork-drift`)
Source: `crossink/main` (https://github.com/uxjulia/CrossInk), CrossInk own commits since upstream merge-base `43b20bd8`.
Strategy: **Curated manual port**, NOT bulk merge. Forks diverged 1100+ files. Cherry-picks
mostly will NOT apply (ForkDrift uses a modular `src/features/*/Registration.*` architecture
CrossInk lacks; themes are renamed: ForkDrift `ForkDriftTheme`/`LyraTheme` vs CrossInk
`LyraCarouselTheme`). Each feature is reimplemented against ForkDrift structure,
RAM-justified (380KB ceiling), and build-tested before commit.

Per-commit: `git -C . show <hash>` to read intent, adapt to ForkDrift, build
(`./.crossink-port/port-build.sh` — see `BUILD-COORDINATOR.md`), commit with
`port(crossink): <subj> [<hash>].

**Parallel port agents MUST use a git worktree per lane** — see `WORKTREES.md` and
`./.crossink-port/port-worktree.sh create <lane>` (`home|reader|file|web|settings`).
Integration merges happen only on `feature/absorb-crossink` in the parent repo.

Legend: [ ] todo  [~] in progress  [x] done  [-] skipped (reason)  [≈] subsumed

PORTING PRINCIPLE: theme/component files are ported from `crossink/main` TIP
(cumulative state), so later commits that only tweak an already-ported file are
subsumed by the initial port — mark [≈], don't re-apply. Per-commit items remain
listed for audit; only their non-ported-file churn (if any feature-relevant) needs
separate attention.

## Skipped (not features / inappropriate for ForkDrift)
- [-] 29a05602 OTA url → points at CrossInk's repo; ForkDrift has own OTA target
- [-] e4bb4439 catalog with 3 build variants → CrossInk release infra
- [-] 1b31168c crossink logo / remove version numbers → CrossInk branding (logo asset);
      revisit only the version-number-removal part if wanted
- [-] 3195e385 simulator sleep shortcut → ForkDrift host-sim harness differs (see memory)

## Phase 1 — Themes & home screen (foundational; later Lyra commits depend on it)
- [x] 91f11a03 add minimal theme — ported as MinimalTheme:LyraTheme, UI_THEME::MINIMAL=5,
      adapted to ForkDrift vtable (no isHeader/allowInvertedText/stats-on-cover;
      localized iconForName + compact-row helper; 2-arg getCoverThumbPath).
      Build SUCCESS, RAM 44.5%.
- [≈] c0a56228 style: minimal theme category font size — MinimalTheme.cpp tab-bar
      UI_12→UI_10 already in tip port (d8853c6d). HomeActivity/fontIds churn is
      CrossInk font-id restructuring, out of scope (ForkDrift has own font system).
### Lyra Carousel cluster — DEFERRED (hard prerequisites)
CrossInk LyraCarouselTheme is not standalone: overrides a `drawCarouselBorder`
virtual absent from ForkDrift BaseTheme, needs `setPreRenderIndex` + exact-
thumbnail plumbing in HomeActivity, and a stats footer (BookReadingStats::
sessionCount). Port AFTER: (1) Phase 3 reading-stats; (2) drawRecentBookCover
stats/progress signature extension across all themes (642402f8/8154c9a5);
(3) HomeActivity carousel integration. Resequenced below Phase 1 independents.
- [x] 642402f8 home progress bar for current book (Lyra) #28 — extends
      drawRecentBookCover signature (stats/progressPercent) across ALL themes
      (BaseTheme+Lyra+ForkDrift+Minimal); do before carousel + with Phase 3.
- [ ] 971a62ee LyraCarouselTheme carousel style tweak [carousel-dep]
- [ ] 8154c9a5 stats on lyra carousel home, title above cover [carousel-dep, P3]
- [ ] 9a9054cb perf: reduce Lyra Carousel RAM frame cache [carousel-dep]
- [ ] bdc65f27 update Lyra Carousel progress footer [carousel-dep, P3]
- [ ] 0c7e11a3 style: lyra reading stat bar width [carousel-dep]
- [x] 684946c5 fix(theme): roundedraff padding — applied exact deltas manually
      (forks diverged 124/63 but patched regions pristine): contentSidePadding
      20→15, kInteractiveInsetX 20→15, +kHomeMenuSidePadding/kListSidePadding/
      kTabHorizontalInset, even-distribution tab bar, menu rowHeight +20→+15,
      kRowPaddingX 40→30, list sidePadding→kListSidePadding.
- [x] 8ab81889 resize menu items to fit homescreen without scrolling — metric
      reductions applied: BaseTheme.h 45/8→38/6, LyraTheme.h + ForkDriftTheme.h
      64/8→56/6. Lyra3CoversTheme inherits LyraMetrics (auto-follows). The
      maxVisibleItems 6→7 part is N/A: ForkDrift drawButtonMenu has no item cap
      (no scrolling problem). ForkDriftTheme matched for intent consistency.
NOTE: commits may land inside concurrent user commits (e.g. 004b893e landed in
b243c895) — verify each port is in git history, not necessarily as own commit.
- [x] 004b893e remove text overlay on current book in base theme — ForkDrift
      relocated this into BaseTheme::drawBookMetadata (drawRecentBookCover
      refactored into computeBookCardRect/drawBookCard/drawBookMetadata).
      Re-expressed intent: keep empty-state placeholder, drop the title/author/
      box/Continue-Reading overlay. Affects Classic theme only (Lyra/ForkDrift/
      Minimal override drawRecentBookCover).
- [x] ea9568c3 toggleable recent books grid view
- [x] a45de2df show progress in recent books grid title

## Phase 2 — Fonts & text rendering
- [-] e742d76f add DM Sans UI font — ForkDrift uses Ubuntu UI fonts; DM Sans binary assets absent
- [-] e6a02fa9 add emoji support — requires binary emoji font asset; no conversion pipeline
- [-] b4299ecf no-emoji build variant — depends on e6a02fa9 emoji
- [-] 2b06b34c 3 build variants (font/emoji) — depends on DM Sans + emoji
- [x] 8723f154 strikethrough + wider underline — STRIKETHROUGH=8 bit in EpdFontFamily::Style;
      LineThrough in CssTextDecoration; line-through in CssParser::interpretDecoration;
      strikethrough fields in StyleStackEntry + effectiveStrikethrough; all 3 startElement
      CSS-decoration sites + STRIKETHROUGH_TAGS handler; endElement willClearStrikethrough
      + shouldFlush + reset; TextBlock draws 3px strike at mid-ascender (mirrors underline
      em-space logic); underline widened 1→3px; Section version 23→24. Build SUCCESS.
- [≈] 97c39de7 reduce wide line-heights — ForkDrift WIDE already 1.0–1.1 (CrossInk post-change target 1.2–1.3)
- [-] be13d667 PHM unicode range — requires font binary recompilation; no fontsrc pipeline
- [-] 9ca4b058 full PHM special-char support — font binary; same as above
- [-] 0b8d4e8e fonts --pnum — font conversion flag; no pipeline in ForkDrift
- [-] 426c9998 darker fonts via conversion — font conversion; no pipeline
- [-] ab07283b teensy font size — new font binaries required
- [-] 3302d4bb inter for teensy size — uses Inter font not present in ForkDrift
- [-] 11be5470 huge font size — new font binaries required
- [x] ade89cdf force paragraph indentation when none — forceParagraphIndents bool in
      CrossPointSettings + SettingsList + STR_FORCE_PARAGRAPH_INDENTS; ParsedText constructor
      + 3 firstLineIndent sites + applyParagraphIndent use forceParagraphIndents;
      ChapterHtmlSlimParser: constructor param, startNewTextBlock, force-indent on <p>,
      strip margins/padding when !embeddedStyle (paragraphs + headers); Section h/cpp
      version 24→25 + header field; HtmlSection h/cpp version 1→2 + field + ChapterHtmlSlimParser;
      MarkdownSection h/cpp version 1→2 + field (cache key); EpubReaderActivity 4 sites;
      MarkdownReaderActivity 5 sites + hash; MarkdownRenderer ParsedText call fixed.
      Build SUCCESS, RAM 44.5%.
- [x] 250b24c3 add guide dots — guideReadingEnabled in CrossPointSettings + SettingsList
      (STR_GUIDE_READING); ParsedText.addWord inserts U+00B7 (·) before non-continuation
      words when enabled and alignment != Justify; ChapterHtmlSlimParser + Section v26
      + MarkdownSection v3 (cache keys); HtmlSection/MarkdownRenderer pass false.
      Build SUCCESS, RAM 44.5%.
- [x] 05a6691a guide dots when justify — remove Justify exclusion; Section v26→27.
- [x] c5a615b5 improved css parser for descendant elements — CssAncestorEntry struct;
      selectorMatchesElement helper; DescendantRule storage in processRuleBlockWithStyle
      (two-part selector split, merge-or-append, MAX_DESCENDANT_RULES cap); resolveStyle
      cascade step 2 (ancestor matching); saveToCache/loadFromCache extended with
      writeStyle/readStyle lambdas and descendant rule section; CSS_CACHE_VERSION 4→5;
      ChapterHtmlSlimParser: ancestorStack_ member + reserve(32) in parseAndBuildPages;
      7 push sites in startElement (non-skip table/tr/td/img/alt/a/general); pop in
      endElement after depth-=1; all 3 resolveStyle calls pass ancestorStack_.
      Build SUCCESS, RAM 44.5%.
- [x] b34b4448 display <hr> tags — PageHorizontalRule class (Page.h/Page.cpp):
      render/serialize/deserialize + TAG_PageHorizontalRule=4 (TAG_PageTableFragment=3
      reserved); Section v28; ChapterHtmlSlimParser: emitHorizontalRule() (width=25%
      viewport centered, 2px thick, lineHeight/2 spacing); <hr> inside table skips
      (ForkDrift has no currentTableBuffer); <hr> outside table calls emitHorizontalRule
      with embedded-style CSS support. Build SUCCESS, RAM 44.5%.
- [x] 61c8d78f improve table rendering (#89)
- [x] 32e189ef draw borders for simple tables
- [x] 431430aa colSpan header/footer rows (#90)
- [x] ecf19d3e grayscale images even when AA off
- [x] 081f170d improve cover rendering
- [x] f39a11fe progressive jpeg cover support

## Phase 3 — Reading stats & finished tracking
- [x] 5331fe82 improve reading stats display
- [x] b03c834d update UI for reading stats
- [x] 7f4705e0 global book stats match per-book
- [x] cebb6cbf mark books finished + track stats
- [x] 60415e57 ask if finished at 99%
- [x] 815f2fbd move epubs to read folder when finished
- [x] a27ee056 file-browser mark finished action
- [x] d4f644eb session-count threshold
- [x] 3535f228 sleep screen reading stats option
- [ ] 7aca0245 asterisk for favorited sleep images
- [x] 993215b8 pin a favorite sleep image
- [ ] 0ac24e61 hide battery on transparent overlay while sleeping
- [x] 996c37c9 page overlay translations
- [ ] 9689ea5c page overlay logging  ← reader lane: remaining Phase 3

## Phase 4 — Controls, power button, pickers
- [x] efe9a5c1 quick font size/family via side buttons — sideButtonLongPress enum
      (SIDE_LONG_CHAPTER_SKIP/FONT_SIZE/OFF) in CrossPointSettings; fromSideBtn added
      to PageTurnResult / detectPageTurn; SIDE_LONG_FONT_SIZE handler in Epub+XtcReader;
      chapter-skip and orientation-change gated on fromSideBtn; SettingsList entry added.
- [x] 8ba6be19 long press menu action + settings section headers — LONG_PRESS_MENU_ACTION
      enum + longPressMenuAction field; executeLongPressMenuAction() in EpubReaderActivity
      (CHANGE_FONT/REFRESH/SYNC/SCREENSHOT; Phase-3 cases no-op); SECTION_HEADER SettingType
      + SectionHeader factory; controls tab restructured with General/In-Reader headers;
      isHeader param propagated to all 4 theme drawList overrides (Base/Lyra/Minimal/RoundedRaff)
      with variable-Y rendering; I18n 380→387 keys. Build SUCCESS, RAM 44.5%.
- [x] 1662fd86 more short power button options — SHORT_PWRBTN extended with
      TOGGLE_GUIDE_DOTS/TOGGLE_BIONIC_READING/TOGGLE_BOOKMARK/SYNC_PROGRESS/
      MARK_FINISHED/READING_STATS/SCREENSHOT/CYCLE_PAGE_TURN; LONG_MENU_CYCLE_PAGE_TURN
      added; executeReaderQuickAction(action) refactor unifies short-power + long-press;
      CYCLE_PAGE_TURN toggles auto page turn on/off; Phase-3 cases no-op.
      I18n 389 keys. Build SUCCESS, RAM 44.5%.
- [x] bf54096d long press power button customization + reorder controls —
      longPwrBtn field (SHORT_PWRBTN enum); LONG_MENU_SLEEP=1 inserted into
      LONG_PRESS_MENU_ACTION (shifts all values +1); POWER_BUTTON_WAKE_SHORT_MS +
      POWER_BUTTON_LONG_PRESS_MS constants; getPowerButtonWakeDuration() +
      getPowerButtonLongPressDuration(); executeLongPowerButtonAction(); short-press
      gated to held < longPressDuration; detectPageTurn short+long power differentiation;
      controls tab restructured: Power Button / Front Buttons / Side Buttons sections;
      I18n 393 keys. Build SUCCESS, RAM 44.5%.
- [≈] 9c20a784 short power press = Confirm outside reader — ForkDrift already has
      consumePowerConfirm() (single-tap=Confirm, double-tap=Back) via isPowerTapSelectEnabled()
      / isDualSideLayout(); works in all activities with no reader-mode gating. CrossInk's
      IGNORE-based fallback approach is architecturally different but subsumed.
- [≈] fe7e8554 short power = Confirm when no global action consumes it — subsumed by above
- [≈] 1a3f9a86 short power = Confirm in reader menus — subsumed; ForkDrift consumePowerConfirm
      has no reader-mode gate so already fires in reader sub-menus
- [x] 30916203 granular front button orientation modes — FRONT_BUTTON_ORIENTATION_AWARE
      enum + sideButtonOrientationAware bool; readerMode field + setReaderMode() added
      to MappedInputManager; orientation-aware helpers in mapButton/mapLabels;
      drawButtonHints allowInvertedText across all 4 themes; reader activities pass true.
      Build SUCCESS, RAM 44.5%.
- [-] ae547416 tilt page turn shortcuts — X4 has no IMU (halTiltSensor/tiltPageTurn absent from ForkDrift)
- [x] 84505799 file transfer shortcut option — FILE_TRANSFER=14/LONG_MENU_FILE_TRANSFER=12
      in CrossPointSettings; suppressNextBackRelease + suppressBackRelease in MappedInputManager;
      goToFileTransfer(returnBookPath)/goToReader(suppressBackRelease) in ActivityManager;
      exitToOrigin()/returnBookPath in CrossPointWebServerActivity; openFileTransfer() in
      EpubReaderActivity; FILE_TRANSFER in all 3 action dispatchers; SettingsList lists updated.
      Build SUCCESS, RAM 44.5%.
- [x] 30e0523d in-reader controls shortcut — ControlsOptionsActivity (12-item controls
      list: Power Button / Front Buttons / Side Buttons sections; reuses SettingsList +
      GUI.drawList + ButtonNavigator); wired into EpubReaderMenuActivity as CONTROLS_OPTIONS
      item after SELECT_CHAPTER; handler uses startActivityForResult + isCancelled result
      to suppress menu action on return. No reader-remap or tilt (ForkDrift lacks both).
- [x] 49c123f9 file browser long-press on delay not release
- [≈] c924a36d additional page turn intervals — subsumed by ac42b24e custom picker
- [x] ac42b24e custom auto page turn interval picker
- [x] 0cb91c3d custom sleep timer picker — IntervalSelectionActivity (generic picker,
      replaces EpubReaderAutoPageTurnIntervalActivity); sleepTimeoutMinutes uint8_t
      field added to CrossPointSettings with binary/JSON migration; SettingsList
      converts sleep from Enum to Value; SettingsActivity opens picker on select.
      Build SUCCESS, RAM 44.5%.
- [x] e803b890 toggle hidden files in file browser

## Phase 5 — Web/WiFi/misc
- [≈] f9be757b focus reading — engine fully in ForkDrift tip (ParsedText/Section/
      ChapterHtmlSlimParser/CrossPointSettings/EpubReaderActivity). Ported only
      the missing settings UI: STR_FOCUS_READING key + SettingsList Toggle entry.
- [x] 07bb45b4 status bar padding (CAUTION: ForkDrift reworked status bar — adapt, see memory)
- [≈] 41209464 bookmark menu + scrollbar >6 items — bookmark parts skipped (no
      BookmarkStore/BookmarksHomeActivity/EpubReaderBookmarkListActivity in ForkDrift).
      Scrollbar/paging ported to BaseTheme+LyraTheme+ForkDriftTheme drawButtonMenu
      (maxVisibleItems=6, arrow indicators; RoundedRaff already had paging).
      Build SUCCESS, RAM 44.5%.
- [-] 4c3aaa30 delete bookmarks from list — deferred: no BookmarkStore/BookmarksHomeActivity in ForkDrift
- [-] f17569ad delete book cache from file browser — skipped per lane scope
- [≈] 95d76b14 WiFi network management API + UI — already in ForkDrift tip
      (WifiCredentialStore + handleGet/Post/DeleteWifiNetwork in CrossPointWebServer).
- [≈] a66863ef user guide: web WiFi/OPDS settings — already in ForkDrift USER_GUIDE.md
      (section 3.6.6 Web Settings WiFi + OPDS).
- [x] 3cc346a1 gray out disabled shift key on URL entry
- [-] 071da53f web ui style tweak — skipped; ForkDrift HTML diverged substantially
      (different page structure + branding); CSS-only, no feature impact.
- [x] 66c8a9ec vietnamese language support — vietnamese.yaml added (183 own keys,
      214 English fallback); STR_CROSSPOINT/STR_CROSSINK adapted to "CrossPoint Reader".
      I18n: 397 keys, 23 languages.

Full ordered list with hashes: `.crossink-port/feature-commits.txt`

---

## Sprint dispatch — open items by lane

> Spin one subagent per lane. Each agent reads `SUBAGENT.md`, creates its worktree
> (`port-worktree.sh create <lane>`), ports each item below in order, builds after
> every commit, pushes `port/<lane>`. Integration agent merges in lane order after all
> lanes report done.

### `renderer` lane — 6 items (unblocked)

Port all against `feature/absorb-crossink` tip.

| Hash | Subject |
|------|---------|
| `61c8d78f` | improve table rendering (#89) |
| `32e189ef` | draw borders for simple tables |
| `431430aa` | colSpan header/footer rows (#90) |
| `ecf19d3e` | grayscale images even when AA off |
| `081f170d` | improve cover rendering |
| `f39a11fe` | progressive jpeg cover support |

Scope: `lib/Epub/`, `lib/GfxRenderer/`, `src/activities/reader/`

---

### `reader` lane — 11 items (Phase 3 core; unblocked)

These are the reading-stats and sleep-cover items. Phase 1 Lyra Carousel items depend
on this lane completing first.

| Hash | Subject |
|------|---------|
| `5331fe82` | improve reading stats display |
| `b03c834d` | update UI for reading stats |
| `7f4705e0` | global book stats match per-book |
| `cebb6cbf` | mark books finished + track stats |
| `60415e57` | ask if finished at 99% |
| `d4f644eb` | session-count threshold |
| `3535f228` | sleep screen reading stats option |
| `7aca0245` | asterisk for favorited sleep images |
| `993215b8` | pin a favorite sleep image |
| `0ac24e61` | hide battery on transparent overlay while sleeping |
| `9689ea5c` | page overlay logging |

Scope: `src/activities/reader/`, `src/activities/sleep/`, `CrossPointSettings.*`

---

### `file` lane — 3 items (Phase 3 file side; unblocked)

| Hash | Subject |
|------|---------|
| `815f2fbd` | move epubs to read folder when finished |
| `a27ee056` | file-browser mark finished action |
| `996c37c9` | page overlay translations (I18n YAML + SettingsList) |

Scope: `src/activities/file/`, `lib/I18n/translations/`

---

### `home` lane — 5 items (Lyra Carousel cluster; **blocked on `reader` lane**)

Do NOT start until `reader` lane is merged into `feature/absorb-crossink`.
Carousel items depend on `BookReadingStats::sessionCount` and `drawRecentBookCover`
stats/progressPercent signature from Phase 3.

| Hash | Subject |
|------|---------|
| `971a62ee` | LyraCarouselTheme carousel style tweak |
| `8154c9a5` | stats on lyra carousel home, title above cover |
| `9a9054cb` | perf: reduce Lyra Carousel RAM frame cache |
| `bdc65f27` | update Lyra Carousel progress footer |
| `0c7e11a3` | style: lyra reading stat bar width |

Scope: `src/activities/home/`, `lib/UITheme/LyraTheme.*`

---

### Integration order (after all lanes pushed)

`renderer` → `reader` → `file` → `home`

After each merge: `./.crossink-port/port-build.sh` must pass before the next merge.

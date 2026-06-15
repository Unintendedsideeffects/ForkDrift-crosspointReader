# Plan 001: File-manager action buttons can no longer execute attacker-controlled filenames as JavaScript

> **Executor instructions**: Follow this plan step by step. Run every
> verification command and confirm the expected result before moving to the
> next step. If anything in the "STOP conditions" section occurs, stop and
> report — do not improvise. When done, update the status row for this plan
> in `plans/README.md`.
>
> **Drift check (run first)**: `git diff --stat 47c7c0c4..HEAD -- src/network/html/FilesPage.html`
> If `src/network/html/FilesPage.html` changed since this plan was written,
> compare the "Current state" excerpt below against the live code before
> proceeding; on a mismatch, treat it as a STOP condition.

## Status

- **Priority**: P1
- **Effort**: S
- **Risk**: LOW
- **Depends on**: none
- **Category**: security
- **Planned at**: commit `47c7c0c4`, 2026-06-15

## Why this matters

The on-device web file manager renders file/folder names that originate from the
SD card (side-loaded files, OPDS downloads, WebDAV uploads). Four action-button
sinks interpolate `file.name` directly into an HTML `onclick="..."` attribute,
escaping **only** the single-quote (`'` → `\'`). They do **not** escape the
double-quote. A filename such as `a" onmouseover="someJs()` breaks out of the
double-quoted `onclick` attribute and runs arbitrary JavaScript in the file
manager UI the moment the directory listing renders. The web UI is LAN-only with
no auth by design, so this is a stored-XSS / defense-in-depth gap: any path that
lets a filename onto the SD card becomes a JS-execution vector inside the
trusted local UI. The same page already renders the *display* cells safely
(`escapeHtml(...)` / `encodeURIComponent(...)`); only the action buttons regressed.

## Current state

- `src/network/html/FilesPage.html` — the file manager page. **This `.html` is the
  source of truth.** A build step bakes it into `src/network/html/FilesPageHtml.generated.h`
  (a gitignored build artifact). You edit the `.html` only; the generated header
  regenerates from it.
- `escapeHtml(unsafe)` is defined in the same file at line ~1993 and escapes `&`,
  `<`, `>`, `"` (→ `&quot;`), and `'` (→ `&#039;`). It is in scope inside the
  directory-render function.
- The **safe pattern already used in this file**: the checkbox cells pass data via
  HTML data-attributes, e.g. line 2150/2163:
  ```html
  <input type="checkbox" class="select-item" data-path="${encodeURIComponent(folderPath)}" data-name="${escapeHtml(file.name)}" data-type="folder">
  ```
- The **vulnerable sinks** (current code, `src/network/html/FilesPage.html:2154,2170-2172`):
  ```js
  // line 2154 (folder delete):
  fileTableContent += `<td class="actions-col"><div class="action-icon-group"><button class="delete-btn" onclick="openDeleteModal('${file.name.replaceAll("'", "\\'")}', '${folderPath.replaceAll("'", "\\'")}', true)" title="Delete folder">🗑️</button></div></td>`;
  // lines 2170-2172 (file move / rename / delete):
  fileTableContent += `<button class="move-btn" onclick="openMoveModal('${file.name.replaceAll("'", "\\'")}', '${filePath.replaceAll("'", "\\'")}' )" title="Move file">📂</button>`;
  fileTableContent += `<button class="rename-btn" onclick="openRenameModal('${file.name.replaceAll("'", "\\'")}', '${filePath.replaceAll("'", "\\'")}' )" title="Rename file">✏️</button>`;
  fileTableContent += `<button class="delete-btn" onclick="openDeleteModal('${file.name.replaceAll("'", "\\'")}', '${filePath.replaceAll("'", "\\'")}', false)" title="Delete file">🗑️</button>`;
  ```
- The handler signatures these call (do not change them):
  - `function openDeleteModal(name, path, isFolder)` (defined ~line 3075)
  - `function openRenameModal(name, path)` (defined ~line 5497)
  - `function openMoveModal(name, path)` (defined ~line 5609)
- The table rows are emitted into `fileTableContent`, then assigned once via
  `fileTable.innerHTML = fileTableContent;` at line ~2179.

## Commands you will need

| Purpose | Command | Expected on success |
|---------|---------|---------------------|
| Drift check | `git diff --stat 47c7c0c4..HEAD -- src/network/html/FilesPage.html` | empty (or you reconcile) |
| Regenerate header from HTML | `uv run python scripts/build_html.py` | exit 0; `src/network/html/FilesPageHtml.generated.h` regenerates |
| Syntax sanity (optional) | `node --check <(sed -n '/<script>/,/<\/script>/p' src/network/html/FilesPage.html)` | may not be feasible inline — skip if it errors on the extraction, not on your JS |
| Confirm no stray sinks remain | `grep -n 'onclick="open.*replaceAll' src/network/html/FilesPage.html` | no matches |

If `scripts/build_html.py` is not the correct generator name, run
`grep -rn "FilesPageHtml.generated" scripts/ platformio.ini` to find the generator
and use that; if you cannot find it, a full `uv run pio run -e default` also
regenerates the header as a `pre:` build step — but **do not** run a PlatformIO
build if another build may be running concurrently (see STOP conditions).

## Scope

**In scope** (the only file you should modify):
- `src/network/html/FilesPage.html`

**Out of scope** (do NOT touch):
- `src/network/html/FilesPageHtml.generated.h` — generated artifact; never hand-edit, never commit it.
- The `openMoveModal` / `openRenameModal` / `openDeleteModal` function bodies — their
  `(name, path[, isFolder])` contract stays exactly the same.
- Any other `onclick="..."` handler that takes **no** interpolated data (e.g.
  `onclick="openUploadModal()"`). Those are static and safe.
- The display cells (`escapeHtml`/`encodeURIComponent` already correct).

## Git workflow

- Branch: `fix/filemanager-xss` (only if you are asked to branch; otherwise edit in place and do NOT commit).
- Do NOT commit, push, or open a PR unless the operator explicitly instructs it.
  The pre-commit hook runs a firmware build; committing here is unnecessary and slow.

## Steps

### Step 1: Replace the four `onclick`-with-interpolation buttons with data-attribute buttons

In `src/network/html/FilesPage.html`, change the four sinks so the filename/path
are carried in `data-*` attributes (escaped with `escapeHtml`, exactly like the
checkbox cells already do) instead of being interpolated into an `onclick` JS
string. Use a stable class to hook a delegated listener.

Replace line 2154 (folder delete button) with:
```js
fileTableContent += `<td class="actions-col"><div class="action-icon-group"><button class="delete-btn js-delete-btn" data-name="${escapeHtml(file.name)}" data-path="${escapeHtml(folderPath)}" data-folder="1" title="Delete folder">🗑️</button></div></td>`;
```

Replace lines 2170-2172 (file move/rename/delete buttons) with:
```js
fileTableContent += `<button class="move-btn js-move-btn" data-name="${escapeHtml(file.name)}" data-path="${escapeHtml(filePath)}" title="Move file">📂</button>`;
fileTableContent += `<button class="rename-btn js-rename-btn" data-name="${escapeHtml(file.name)}" data-path="${escapeHtml(filePath)}" title="Rename file">✏️</button>`;
fileTableContent += `<button class="delete-btn js-delete-btn" data-name="${escapeHtml(file.name)}" data-path="${escapeHtml(filePath)}" data-folder="0" title="Delete file">🗑️</button>`;
```

Note: `data-*` attribute values read back via `element.dataset.*` are
automatically HTML-decoded by the browser, so `escapeHtml` here is correct and
round-trips the original filename to the handlers unchanged.

**Verify**: `grep -n 'js-move-btn\|js-rename-btn\|js-delete-btn' src/network/html/FilesPage.html` → 4 matches (one move, one rename, two delete).

### Step 2: Add one delegated click listener that calls the existing handlers

Immediately **after** the line `fileTable.innerHTML = fileTableContent;` (line ~2179),
attach a single delegated listener on `fileTable` that reads the dataset and calls
the unchanged handler functions:

```js
fileTable.querySelectorAll('.js-move-btn').forEach(btn =>
  btn.addEventListener('click', () => openMoveModal(btn.dataset.name, btn.dataset.path)));
fileTable.querySelectorAll('.js-rename-btn').forEach(btn =>
  btn.addEventListener('click', () => openRenameModal(btn.dataset.name, btn.dataset.path)));
fileTable.querySelectorAll('.js-delete-btn').forEach(btn =>
  btn.addEventListener('click', () => openDeleteModal(btn.dataset.name, btn.dataset.path, btn.dataset.folder === '1')));
```

Place this block inside the same function scope where `fileTable` and the
`openMoveModal`/etc. functions are visible (they are top-level functions in the
same `<script>`, so they are in scope). Match the file's existing indentation
(2-space).

**Verify**: `grep -n "addEventListener('click', () => openMoveModal" src/network/html/FilesPage.html` → 1 match.

### Step 3: Confirm no interpolated-onclick sinks remain and regenerate the header

**Verify**:
- `grep -n 'onclick="open.*replaceAll' src/network/html/FilesPage.html` → **no matches**.
- `grep -n 'onclick=.*\${file\.name' src/network/html/FilesPage.html` → **no matches**.
- Regenerate the baked header: `uv run python scripts/build_html.py` → exit 0.
- `git status --short src/network/html/` should show `FilesPage.html` modified; the
  `.generated.h` may also show as modified but it is gitignored — do not stage it.

## Test plan

There is no JS unit-test harness in this repo, so verification is by inspection +
a manual reasoning check (state it in your report):

1. Confirm the four buttons now use `data-name`/`data-path` with `escapeHtml`, and a
   delegated listener calls the original handlers.
2. Reason through the attack string `a" onmouseover="x` as a filename: with the new
   code it becomes `data-name="a&quot; onmouseover=&quot;x"`, which is inert — the
   `&quot;` cannot close the attribute, and `btn.dataset.name` returns the literal
   `a" onmouseover="x` string to the handler. Include this in your report.
3. If a simulator or local dev server for the configurator/web UI is available, load
   `/files` and confirm the move/rename/delete buttons still open their modals for a
   normally-named file. If no such environment is available, say so — do not block on it.

## Done criteria

ALL must hold:

- [ ] `grep -n 'onclick="open.*replaceAll' src/network/html/FilesPage.html` returns no matches
- [ ] `grep -n 'js-move-btn\|js-rename-btn\|js-delete-btn' src/network/html/FilesPage.html` returns 4 matches
- [ ] `grep -n "addEventListener('click', () => open" src/network/html/FilesPage.html` returns 3 matches (move, rename, delete)
- [ ] `uv run python scripts/build_html.py` exits 0
- [ ] Only `src/network/html/FilesPage.html` is modified in `git status` (the `.generated.h` is gitignored and must not be staged)
- [ ] `plans/README.md` status row updated

## STOP conditions

Stop and report back (do not improvise) if:

- The "Current state" excerpt at lines 2154/2170-2172 does not match the live code
  (the page has been refactored since this plan was written).
- The handler signatures `openMoveModal(name, path)` / `openRenameModal(name, path)` /
  `openDeleteModal(name, path, isFolder)` differ from what's documented here.
- You cannot find the HTML generator and a PlatformIO build appears to already be
  running (a concurrent `pio` build corrupts the shared cache — never start a second one).
- Regenerating the header produces a diff far larger than the lines you changed.

## Maintenance notes

- Any new dynamic action button added to the file table must use the same
  data-attribute + delegated-listener pattern; never interpolate `file.name`/paths
  into an `onclick` string again.
- A reviewer should confirm the delegated listeners are attached *after* each
  `innerHTML` assignment that rebuilds the table (if the table is re-rendered on
  navigation, the listeners must be re-attached — they are, because they live right
  after the `innerHTML` write).
- Deferred (out of scope here): a broader sweep of other dynamically-built `innerHTML`
  blocks in this file (e.g. the todo-planner and upload-log sections) for the same
  pattern. Those already use `escapeHtml`/`escapeTodoValue` for *text* content; only
  the action-button `onclick` sinks had the attribute-breakout bug.

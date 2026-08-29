# Heap and fragmentation analysis

Supporting analyses behind [../HEAP_ANALYSIS.md](../HEAP_ANALYSIS.md), which is the
summary and the place to start. These are the long-form working documents.

All of them were produced against the device on 2026-08-28 and cite `file:line`.
They were reviewed before being filed, and several of their claims did not survive
that review — where a document is wrong, the correction is recorded in
[../FINDINGS.md](../FINDINGS.md) rather than by editing the document.

**Exception — occupancy diet, 2026-08-29.** Hunt-3 P0–P2c landed. Home+Always is
no longer 11.5 KB / 8.2 KB; measured serving idle is 29–36 KB / 17 KB after UDP
`"hello"`. Current numbers live in [../HEAP_ANALYSIS.md](../HEAP_ANALYSIS.md)
(occupancy addendum) and FINDINGS 2026-08-29T10:16Z. The 2026-08-28 hunt/lens
bodies stay as the diagnosis that justified the diet.

## Decision

| Document | What it is |
|---|---|
| [architecture-verdict.md](architecture-verdict.md) | **Start here for inflate/STORED.** Adjudicates the four angles below, selects the STORED-shadow approach, rejects the framebuffer-loan route as a first fix, and sets Gate 0 — the measurement that must pass before any of it is built. |
| [hunt-3-server-diet.md](hunt-3-server-diet.md) | **Start here for Home+Always occupancy.** P0 dispatch table, lazy mDNS/WS, start budget 4,504. Landed 2026-08-29; measured 29–36 KB serving free. |
| [linker-analysis.md](linker-analysis.md) | Resolves the ~20 KB gap between linker DRAM and the heap pool (TLSF bookkeeping, ~16 B per live block, not reclaimable), and ranks the static-RAM reductions actually available (~7.6-11 KB). Its "what not to do" list is the more useful half. |

## Candidate angles

Four independent takes on "what would change the shape of this problem".

| Document | Angle | Outcome |
|---|---|---|
| [angle-1-pooling.md](angle-1-pooling.md) | Allocation census, arena/pool design | Second-stage option, not first |
| [angle-2-framebuffer.md](angle-2-framebuffer.md) | Put the inflate dictionary in the idle 52 KB framebuffer | Rejected as first fix — the locking obligation is far wider than it looks |
| [angle-3-contiguity.md](angle-3-contiguity.md) | Allocation ordering, reboot-as-defrag | Small independent boot-policy cleanup |
| [angle-4-representation.md](angle-4-representation.md) | Rewrite books as STORED so no dictionary is ever needed | **Selected**, gated on Gate 0 |

## Diagnostic lenses

The earlier survey that produced the measured picture in the first place.

| Document | Lens |
|---|---|
| [lens-1-allocation-census.md](lens-1-allocation-census.md) | Where the bytes go; reclaim opportunities |
| [lens-2-threshold-audit.md](lens-2-threshold-audit.md) | Every heap threshold in the codebase, audited. The origin of most of the fixes shipped on 2026-08-28. |
| [lens-3-fragmentation.md](lens-3-fragmentation.md) | Why the largest block collapses |
| [lens-4-budget.md](lens-4-budget.md) | Static vs heap accounting, per-state budget |

## Raw data

Serial traces, the measurement harness and the per-run CSVs are under
`plans/heap-2026-08-28/` (untracked — they are evidence, not documentation).

## Measuring this device

`CMD:HEAPPROF` returns free/largest/min/total plus block counts; `device_walk.py`
has a `heap <label>` walk verb that writes `heap.csv`. Before trusting an A/B:

- Reboot between every run — the heap does not recover within a session.
- Confirm the reader actually opened, and which book.
- Discard runs that log `Cache not found, building` (an index legitimately claims
  the inflate window) or whose `t_ms` goes backwards (a silent defrag reboot resets
  the heap mid-walk and flatters that arm).
- **Classify runs by whether the inflate window reservation succeeded.** It fails
  intermittently, and a failed run reads ~32 KB higher on free heap. That is a
  bimodal outcome, not variance, and mixing the two modes makes the data look far
  noisier than it is. With the modes separated, free heap repeats to about 2 KB;
  largest block still spans ~10 KB, so use a median of five or more runs.

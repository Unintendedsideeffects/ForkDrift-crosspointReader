# Fork Strategy & Branch Management

This document outlines the relationship between this repository and the upstream [CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader) project, and explains the purpose of the `fork-drift` branch.

## Why This Fork Exists

While the upstream CrossPoint Reader project provides a stable and high-quality firmware for the Xteink X4, this fork exists to:

1.  **Maintain the Feature Picker:** Support the [web-based build configurator](https://unintendedsideeffects.github.io/ForkDrift-crosspointReader/configurator/) which allows users to generate custom firmware builds.
2.  **Accelerate Feature Iteration:** Provide a staging ground for experimental features, community-driven enhancements, and rapid bug fixes that may not yet be ready for the main upstream repository.
3.  **Experimental Drift:** Enable architectural experiments and "drifting" features that prioritize specific community needs or hardware optimizations.

## Upstream Relationship

This repository maintains a close relationship with the [official upstream repository](https://github.com/crosspoint-reader/crosspoint-reader).

-   The `master` branch is kept synchronized with the upstream `master` branch.
-   Stable features developed in this fork are periodically submitted to the upstream project via Pull Requests.
-   Upstream changes are regularly merged into the `fork-drift` branch to ensure compatibility.

## Branch Strategy

### `master`
The `master` branch is the stable release branch of this fork. While it incorporates upstream stability, it also includes the core "Fork Drift" capabilities (configurator, modular flags, etc.) that have been validated in `fork-drift`. Use this branch for a stable, feature-complete Fork Drift experience.

### `fork-drift`
The `fork-drift` branch is the primary active development branch for this fork. It contains experimental features and rapid iterations before they are merged into `master`.
...
---
**Last Updated:** 2026-03-24

## Contribution Workflow

If you would like to contribute to this fork:

1.  **Target the `fork-drift` branch:** All new features and bug fixes should be submitted as Pull Requests against the `fork-drift` branch.
2.  **Synchronize with Upstream:** If your change is a general improvement intended for all CrossPoint users, we will work with you to prepare it for upstreaming after it has been validated in `fork-drift`.
3.  **Issue Tracking:** Please refer to [GitHub Issues](https://github.com/Unintendedsideeffects/ForkDrift-crosspointReader/issues) for current priorities and known regressions being addressed in this fork.

## Behavioral Drifts

These are changes to existing upstream behaviour rather than wholly new features. They require no feature flag and are always active.

### Captive portal redirect in AP/hotspot mode

**Upstream behaviour:** `CrossPointWebServer::handleNotFound()` returns a plain-text 404 for any unrecognised URL.

**Fork drift:** When the web server is running in AP mode (hotspot), `handleNotFound()` returns `302 → http://<ap-ip>/` instead of 404.

**Why:** The DNS server already resolves every domain to the device IP via a wildcard entry. OS captive-portal probes — Apple iOS/macOS (`/hotspot-detect.html`, `/library/test/success.html`), Android (`/generate_204`), Windows (`/ncsi.txt`, `/connecttest.txt`) — all hit the web server and previously received a 404, so the OS never surfaced the "Sign in to network" notification. The 302 redirect completes the second layer of the captive portal handshake, causing every major OS to automatically open the CrossPoint web UI when a user connects to the hotspot.

The redirect uses the raw AP IP address rather than the `.local` mDNS hostname because mDNS is typically blocked on clients until after the captive portal is dismissed.

**Files changed:** `src/network/CrossPointWebServer.cpp` (`handleNotFound`)

---

For more details on building custom firmware, see [BUILD_CONFIGURATION.md](BUILD_CONFIGURATION.md).

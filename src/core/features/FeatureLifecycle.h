#pragma once

class GfxRenderer;

namespace core {

/**
 * FeatureLifecycle provides explicit startup hook points for optional features.
 *
 * main.cpp calls each stage once, in order, without knowing which features are
 * compiled in. Each hook internally gates its work behind compile-time feature
 * flags so that disabled features cost nothing at runtime.
 *
 * Lifecycle stages (called in this order during boot):
 *  1. onStorageReady   – SD card is mounted; features may scan storage resources.
 *  2. onSettingsLoaded – CrossPointSettings has been loaded; features apply their
 *                        persisted configuration to hardware or runtime state.
 *  3. onFontSetup      – Called from setupDisplayAndFonts() after builtin font
 *                        families are registered; feature-provided font families
 *                        are registered with the renderer.
 */
class FeatureLifecycle {
 public:
  /**
   * Called once the SD card is successfully mounted, before settings are loaded.
   * Features that index or pre-scan storage resources (e.g. user font families)
   * perform that work here.
   */
  static void onStorageReady();

  /**
   * Called immediately after CrossPointSettings::loadFromFile().
   * Features apply their settings to hardware or runtime state (dark mode,
   * integration credential stores, user font loading with fallback).
   *
   * @param renderer  The active GfxRenderer (for display-state features).
   */
  static void onSettingsLoaded(GfxRenderer& renderer);

  /**
   * Called at the end of font setup, after all builtin font families are
   * registered with the renderer. Feature-provided font families (e.g. user
   * SD fonts) are registered here.
   *
   * @param renderer  The active GfxRenderer to register font families into.
   */
  static void onFontSetup(GfxRenderer& renderer);

  /**
   * Called after WiFi association succeeds and before a background web server
   * allocates its routes. Network integrations with large temporary heap needs
   * can do bounded work here without competing with the server.
   */
  static void onBackgroundNetworkReady();

  /**
   * Called each time the background web server first transitions to the RUNNING
   * state (WiFi connected, server up). Features may use this for work that
   * specifically depends on registered web routes being available.
   * May be called more than once per session if the server restarts.
   */
  static void onBackgroundServerStarted();
  // Periodic while a background server runs; handlers self-gate on interval.
  static void onBackgroundServerTick();
};

}  // namespace core

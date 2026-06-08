#pragma once

#include <string>

// Shared, per-species cache of 1-bit Pokémon sprites rendered for the e-ink
// panel. Sprites are keyed by species id (not by book) so every book that
// assigns the same species — and every evolution stage — reuses one BMP.
//
// The source art is the pixel sprite from the PokéAPI; the web plugin stores its
// URL in pokemon.json. On device we lazily download that PNG and convert it to a
// 1-bit BMP (PngToBmpConverter) the first time it is needed and WiFi is up.
// Rendering code that has no cached sprite falls back to a drawn placeholder, so
// the party UI always works offline.
namespace PokemonSpriteCache {

// PokéAPI pixel sprites are 96x96 native; caching at that size keeps the BMP
// small and lets the renderers scale up cleanly for larger party/sleep slots.
constexpr int kDefaultSpriteSize = 96;

// Absolute SD path where a species' 1-bit sprite BMP lives (whether or not it
// exists yet). Empty only on allocation failure.
std::string spritePath(int speciesId);

// True when the converted sprite BMP is already cached on the SD card.
bool isCached(int speciesId);

// Ensure a cached sprite exists for speciesId, downloading spriteUrl and
// converting it to a 1-bit BMP when missing. Returns true if the sprite is (now)
// cached. Requires an active network connection to fetch a missing sprite; when
// offline and uncached it returns false without blocking. Safe to call when
// already cached (cheap existence check).
bool ensureSprite(int speciesId, const std::string& spriteUrl, int targetWidth, int targetHeight);

// Convenience: ensureSprite using the canonical PokéAPI pixel-sprite URL derived
// from speciesId, so callers don't need the URL from pokemon.json.
bool ensureSpriteById(int speciesId, int targetWidth, int targetHeight);

}  // namespace PokemonSpriteCache

#pragma once

namespace cacheload {
// Deserialization fails for two reasons that need opposite responses: bytes that
// are actually bad (drop the cache and rebuild) and an allocation the heap
// refused (keep the cache — it is fine, we simply could not read it now). The
// return type is a plain null pointer either way, so the reason is recorded
// here instead.
//
// Single-threaded by construction: all section loads run under SpiBusMutex.
void beginLoad();        // clear the flag before a load
void markOutOfMemory();  // record that a failure was an allocation refusal
bool wasOutOfMemory();   // true if any allocation was refused during this load
}  // namespace cacheload

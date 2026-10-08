#pragma once

#include <Epub/hyphenation/LanguageRegistry.h>
#include <HyphenationPack.h>

namespace HyphenationPackStore {
constexpr char DIRECTORY[] = "/.crosspoint/hyphenation";
bool begin();
bool lookup(const char* code, ExternalHyphenationPatterns& out);
bool isInstalled(const char* code);
bool hasSource(const char* code);
hyphenation_pack::Result install(const char* code);
hyphenation_pack::Result remove(const char* code);
}  // namespace HyphenationPackStore

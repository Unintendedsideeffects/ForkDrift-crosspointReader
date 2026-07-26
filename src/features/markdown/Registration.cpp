#include "features/markdown/Registration.h"

#include <FeatureFlags.h>
#include <Logging.h>

#include <memory>
#include <new>
#include <string>

#include "Markdown.h"
#include "activities/reader/MarkdownReaderActivity.h"
#include "core/registries/ReaderLoader.h"
#include "core/registries/ReaderRegistry.h"

namespace features::markdown {
namespace {

bool isSupported(const char* path) {
  (void)path;
  return true;
}

Activity* createActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const std::string& path,
                         void* callbackCtx, void (*onBackToLibrary)(void* ctx, const std::string& path),
                         void (*onBackHome)(void* ctx)) {
  auto markdown = core::loadDocumentNoThrow<Markdown>(path, "Markdown");
  if (!markdown) {
    return nullptr;
  }

  auto* activity = new (std::nothrow)
      MarkdownReaderActivity(renderer, mappedInput, std::move(markdown), callbackCtx, onBackToLibrary, onBackHome);
  if (!activity) {
    LOG_ERR("READER", "Failed to allocate MarkdownReaderActivity");
    return nullptr;
  }

  return activity;
}

}  // namespace

void registerFeature() {
#if ENABLE_MARKDOWN
  core::ReaderEntry entry{
      ".md",
      isSupported,
      "Markdown support disabled in this build",
      "Markdown support\nnot available\nin this build",
      createActivity,
  };
  core::ReaderRegistry::add(entry);
#endif
}

}  // namespace features::markdown

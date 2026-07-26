#include "features/epub/Registration.h"

#include <Epub.h>
#include <FeatureFlags.h>
#include <Logging.h>

#include <memory>
#include <new>
#include <string>

#include "activities/reader/EpubReaderActivity.h"
#include "core/registries/ReaderLoader.h"
#include "core/registries/ReaderRegistry.h"

namespace features::epub {
namespace {

bool isSupported(const char* path) {
  (void)path;
  return true;
}

Activity* createActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const std::string& path,
                         void* callbackCtx, void (*onBackToLibrary)(void* ctx, const std::string& path),
                         void (*onBackHome)(void* ctx)) {
  (void)callbackCtx;
  (void)onBackToLibrary;
  (void)onBackHome;

  auto epub = core::loadDocumentNoThrow<Epub>(path, "EPUB");
  if (!epub) {
    return nullptr;
  }

  auto* activity =
      core::createActivityNoThrow<EpubReaderActivity>("EpubReaderActivity", renderer, mappedInput, std::move(epub));
  if (!activity) {
    LOG_ERR("READER", "Failed to allocate EpubReaderActivity");
    return nullptr;
  }

  return activity;
}

}  // namespace

void registerFeature() {
#if ENABLE_EPUB_SUPPORT
  core::ReaderEntry entry{
      ".epub",        isSupported, "EPUB support disabled in this build", "EPUB support\nnot available\nin this build",
      createActivity,
  };
  core::ReaderRegistry::add(entry);
#endif
}

}  // namespace features::epub

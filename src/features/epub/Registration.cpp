#include "features/epub/Registration.h"

#include <Epub.h>
#include <Epub/StoredEpubCache.h>
#include <FeatureFlags.h>
#include <HalStorage.h>
#include <HeapGuard.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>

#include <memory>
#include <new>
#include <string>

#include "SpiBusMutex.h"
#include "activities/RenderLock.h"
#include "activities/reader/EpubReaderActivity.h"
#include "components/UITheme.h"
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

  std::string archivePath = path;
  {
    RenderLock renderLock;
    GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
    renderer.displayBuffer();
    {
      GfxRenderer::FrameBufferLoan loan(renderer);
      archivePath = stored_epub::prepare(path.c_str());
      if (archivePath.empty()) {
        archivePath = path;
      }
    }
  }

  SpiBusMutex::Guard guard;
  if (!Storage.exists(path.c_str())) {
    LOG_ERR("READER", "File does not exist: %s", path.c_str());
    return nullptr;
  }
  if (!heapguard::canAllocate(sizeof(Epub), 0)) {
    LOG_ERR("READER", "READER_ALLOC_REJECT kind=document type=EPUB bytes=%u free=%u largest=%u",
            static_cast<unsigned int>(sizeof(Epub)), static_cast<unsigned int>(heapguard::freeBytes()),
            static_cast<unsigned int>(heapguard::largestBlock()));
    return nullptr;
  }
  auto epub = makeUniqueNoThrow<Epub>(path, "/.crosspoint");
  if (!epub) {
    LOG_ERR("READER", "Failed to allocate EPUB object");
    return nullptr;
  }
  epub->setArchivePath(archivePath);
  if (!epub->load()) {
    LOG_ERR("READER", "Failed to load EPUB");
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

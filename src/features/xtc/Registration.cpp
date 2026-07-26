#include "features/xtc/Registration.h"

#include <FeatureFlags.h>
#include <Logging.h>
#include <Xtc.h>

#include <memory>
#include <new>
#include <string>

#include "activities/reader/XtcReaderActivity.h"
#include "core/registries/ReaderLoader.h"
#include "core/registries/ReaderRegistry.h"

namespace features::xtc {
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

  auto xtc = core::loadDocumentNoThrow<Xtc>(path, "XTC");
  if (!xtc) {
    return nullptr;
  }

  auto* activity =
      core::createActivityNoThrow<XtcReaderActivity>("XtcReaderActivity", renderer, mappedInput, std::move(xtc));
  if (!activity) {
    LOG_ERR("READER", "Failed to allocate XtcReaderActivity");
    return nullptr;
  }

  return activity;
}

void addEntry(const char* extension) {
  core::ReaderEntry entry{
      extension,      isSupported, "XTC support disabled in this build", "XTC support\nnot available\nin this build",
      createActivity,
  };
  core::ReaderRegistry::add(entry);
}

}  // namespace

void registerFeature() {
#if ENABLE_XTC_SUPPORT
  addEntry(".xtc");
  addEntry(".xtch");
#endif
}

}  // namespace features::xtc

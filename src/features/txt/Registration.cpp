#include "features/txt/Registration.h"

#include <Logging.h>

#include <memory>
#include <new>
#include <string>

#include "Txt.h"
#include "activities/reader/TxtReaderActivity.h"
#include "core/registries/ReaderLoader.h"
#include "core/registries/ReaderRegistry.h"

namespace features::txt {
namespace {

Activity* createActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const std::string& path,
                         void* callbackCtx, void (*onBackToLibrary)(void* ctx, const std::string& path),
                         void (*onBackHome)(void* ctx)) {
  (void)callbackCtx;
  (void)onBackToLibrary;
  (void)onBackHome;

  auto txt = core::loadDocumentNoThrow<Txt>(path, "TXT");
  if (!txt) {
    return nullptr;
  }

  auto* activity =
      core::createActivityNoThrow<TxtReaderActivity>("TxtReaderActivity", renderer, mappedInput, std::move(txt));
  if (!activity) {
    LOG_ERR("READER", "Failed to allocate TxtReaderActivity");
    return nullptr;
  }

  return activity;
}

}  // namespace

void registerFeature() {
  core::ReaderEntry entry{
      ".txt", nullptr, nullptr, nullptr, createActivity,
  };
  core::ReaderRegistry::add(entry);
}

}  // namespace features::txt

#include "AnnotationStore.h"

#if ENABLE_ANNOTATIONS

#include <HalStorage.h>
#include <Logging.h>
#include <Serialization.h>

namespace {
constexpr uint8_t kFileVersion = 1;
constexpr char kFileName[] = "/annotations.bin";
}  // namespace

bool AnnotationStore::loadForBook(const std::string& cachePath) {
  unload();
  filePath = cachePath + kFileName;
  loaded = true;

  HalFile file;
  if (!Storage.openFileForRead("ANN", filePath, file)) {
    return true;  // no file yet: empty store
  }

  serialization::BufferedReader reader(file);
  uint8_t version = 0;
  uint16_t count = 0;
  if (!serialization::readPod(reader, version) || version != kFileVersion ||
      !serialization::readPod(reader, count) || count > kMaxAnnotationsPerBook) {
    LOG_ERR("ANN", "Invalid annotations file, starting empty");
    return true;
  }
  annotations.reserve(count);
  for (uint16_t i = 0; i < count; i++) {
    Annotation a;
    if (!serialization::readPod(reader, a.spineIndex) || !serialization::readPod(reader, a.page) ||
        !serialization::readPod(reader, a.startWord) || !serialization::readPod(reader, a.endWord) ||
        !serialization::readString(reader, a.text) || a.text.size() > kMaxTextBytes) {
      LOG_ERR("ANN", "Truncated annotations file at entry %u", i);
      break;
    }
    annotations.push_back(std::move(a));
  }
  LOG_DBG("ANN", "Loaded %u annotations", static_cast<unsigned>(annotations.size()));
  return true;
}

void AnnotationStore::unload() {
  if (dirty) {
    saveToFile();
  }
  annotations.clear();
  annotations.shrink_to_fit();
  filePath.clear();
  loaded = false;
  dirty = false;
}

bool AnnotationStore::add(const Annotation& annotation) {
  if (!loaded || annotations.size() >= kMaxAnnotationsPerBook || annotation.text.empty() ||
      annotation.text.size() > kMaxTextBytes) {
    return false;
  }
  annotations.push_back(annotation);
  dirty = true;
  saveToFile();
  return true;
}

int AnnotationStore::removeAt(const uint16_t spineIndex, const uint16_t page, const uint16_t wordIdx) {
  int removed = 0;
  for (auto it = annotations.begin(); it != annotations.end();) {
    if (it->spineIndex == spineIndex && it->page == page && it->startWord <= wordIdx && wordIdx <= it->endWord) {
      it = annotations.erase(it);
      removed++;
    } else {
      ++it;
    }
  }
  if (removed > 0) {
    dirty = true;
    saveToFile();
  }
  return removed;
}

bool AnnotationStore::removeCandidateAt(const uint16_t spineIndex, const uint16_t page,
                                        const uint16_t wordIdx) const {
  for (const auto& a : annotations) {
    if (a.spineIndex == spineIndex && a.page == page && a.startWord <= wordIdx && wordIdx <= a.endWord) {
      return true;
    }
  }
  return false;
}

bool AnnotationStore::hasAnyFor(const uint16_t spineIndex, const uint16_t page) const {
  for (const auto& a : annotations) {
    if (a.spineIndex == spineIndex && a.page == page) {
      return true;
    }
  }
  return false;
}

std::vector<const Annotation*> AnnotationStore::forPage(const uint16_t spineIndex, const uint16_t page) const {
  std::vector<const Annotation*> out;
  for (const auto& a : annotations) {
    if (a.spineIndex == spineIndex && a.page == page) {
      out.push_back(&a);
    }
  }
  return out;
}

void AnnotationStore::saveToFile() {
  if (!loaded || filePath.empty()) {
    return;
  }
  HalFile file;
  if (!Storage.openFileForWrite("ANN", filePath, file)) {
    LOG_ERR("ANN", "Failed to open %s for write", filePath.c_str());
    return;
  }
  serialization::BufferedWriter writer(file);
  serialization::writePod(writer, kFileVersion);
  serialization::writePod(writer, static_cast<uint16_t>(annotations.size()));
  for (const auto& a : annotations) {
    serialization::writePod(writer, a.spineIndex);
    serialization::writePod(writer, a.page);
    serialization::writePod(writer, a.startWord);
    serialization::writePod(writer, a.endWord);
    serialization::writeString(writer, a.text);
  }
  if (!writer.flush()) {
    LOG_ERR("ANN", "Failed to flush annotations");
    return;
  }
  dirty = false;
}

#endif  // ENABLE_ANNOTATIONS

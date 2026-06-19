#pragma once

#include <memory>
#include <string>
#include <vector>

#include "../Activity.h"
#include "HalStorage.h"
#include "util/ButtonNavigator.h"
#include "util/DictionaryLookup.h"

class HalFileDictionaryAccessor : public DictionaryAccessor {
 public:
  explicit HalFileDictionaryAccessor(std::unique_ptr<HalFile> file) : file_(std::move(file)) {}

  bool seek(size_t pos) override { return file_->seek(pos); }

  size_t read(void* buf, size_t size) override { return file_->read(buf, size); }

  size_t size() const override { return file_->size(); }

 private:
  std::unique_ptr<HalFile> file_;
};

class DictionaryActivity final : public Activity {
 public:
  explicit DictionaryActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string pageWords,
                              std::string contextText);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&& lock) override;

 private:
  void lookupWord(const std::string& word);
  void renderList(RenderLock& lock);
  void renderDefinition(RenderLock& lock);

  std::string pageWords;
  std::string contextText;
  std::vector<std::string> wordsList;
  ButtonNavigator navigator;
  int selectedIndex = 0;

  bool showingDefinition = false;
  std::string currentDefinition;
  std::vector<std::string> definitionLines;
  int scrollLine = 0;

  std::unique_ptr<HalFileDictionaryAccessor> accessor;
  bool dictMissing = false;
};

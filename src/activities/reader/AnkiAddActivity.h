#pragma once

#include <string>

#include "../Activity.h"

class AnkiAddActivity final : public Activity {
 public:
  explicit AnkiAddActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string frontText,
                           std::string backText, std::string contextText);

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  std::string frontText;
  std::string backText;
  std::string contextText;
  bool saved = false;
};

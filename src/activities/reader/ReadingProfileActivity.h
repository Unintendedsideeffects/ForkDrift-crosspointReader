#pragma once

#include "../Activity.h"

class ReadingProfileActivity final : public Activity {
 public:
  ReadingProfileActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
};

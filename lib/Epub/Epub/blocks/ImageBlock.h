#pragma once
#include <Serialization.h>

#include <memory>
#include <string>

#include "Block.h"

class ImageBlock final : public Block {
 public:
  ImageBlock(const std::string& imagePath, int16_t width, int16_t height);
  ~ImageBlock() override = default;

  const std::string& getImagePath() const { return imagePath; }
  int16_t getWidth() const { return width; }
  int16_t getHeight() const { return height; }

  bool imageExists() const;

  BlockType getType() override { return IMAGE_BLOCK; }
  bool isEmpty() override { return false; }

  void render(GfxRenderer& renderer, const int x, const int y);

  // Forget this session's decode failures so a transient memory/storage problem
  // is retried. The reader calls this on entry. Ported from upstream/develop.
  static void clearSessionRenderFailures();
  bool serialize(serialization::BufferedWriter& file);
  static std::unique_ptr<ImageBlock> deserialize(serialization::BufferedReader& file);

 private:
  void renderPlaceholder(GfxRenderer& renderer, int x, int y) const;

  std::string imagePath;
  int16_t width;
  int16_t height;
};

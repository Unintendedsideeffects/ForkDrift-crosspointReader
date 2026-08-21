#include <HalStorage.h>
#include <Logging.h>
#include <Serialization.h>

#include "ImageBlock.h"

ImageBlock::ImageBlock(const std::string& imagePath, const int16_t width, const int16_t height)
    : imagePath(imagePath), width(width), height(height) {}

bool ImageBlock::imageExists() const { return Storage.exists(imagePath.c_str()); }

bool ImageBlock::serialize(serialization::BufferedWriter& file) {
  serialization::writeString(file, imagePath);
  serialization::writePod(file, width);
  serialization::writePod(file, height);
  return true;
}

std::unique_ptr<ImageBlock> ImageBlock::deserialize(serialization::BufferedReader& file) {
  std::string path;
  if (!serialization::readString(file, path)) {
    LOG_ERR("IMG", "Deserialization failed: could not read image path");
    return nullptr;
  }
  int16_t width = 0;
  int16_t height = 0;
  if (!serialization::readPod(file, width) || !serialization::readPod(file, height)) {
    LOG_ERR("IMG", "Deserialization failed: truncated image metadata");
    return nullptr;
  }
  auto* block = new (std::nothrow) ImageBlock(path, width, height);
  if (!block) {
    LOG_ERR("IMG", "OOM: ImageBlock");
    return nullptr;
  }
  return std::unique_ptr<ImageBlock>(block);
}

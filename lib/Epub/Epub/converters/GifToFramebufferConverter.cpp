#include "GifToFramebufferConverter.h"

#if ENABLE_EPUB_GIF

#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <HeapGuard.h>
#include <Logging.h>

#include "DirectPixelWriter.h"
#include "DitherUtils.h"
#include "GifRaster.h"
#include "ImageDimsProbe.h"
#include "PixelCache.h"
#include "PngRowExpansion.h"

namespace {

struct GifRender {
  GfxRenderer* renderer{nullptr};
  const RenderConfig* config{nullptr};
  DirectPixelWriter pw;
  PixelCache cache;
  bool caching{false};
  int srcWidth{0};
  int srcHeight{0};
  int dstWidth{0};
  int dstHeight{0};
  int lastDstY{-1};
  int screenWidth{0};
  int screenHeight{0};
  bool interlaced{false};
};

int gifFileRead(void* ctx, uint8_t* dst, int n) {
  auto* file = static_cast<HalFile*>(ctx);
  if (!file || n <= 0) {
    return 0;
  }
  const int got = file->read(dst, static_cast<size_t>(n));
  return got < 0 ? 0 : got;
}

bool computeDestSize(const gifraster::DecodeInfo& info, const RenderConfig& config, int& dstWidth, int& dstHeight) {
  if (info.width <= 0 || info.height <= 0) {
    return false;
  }
  if (config.useExactDimensions && config.maxWidth > 0 && config.maxHeight > 0) {
    dstWidth = config.maxWidth;
    dstHeight = config.maxHeight;
    return dstWidth > 0 && dstHeight > 0;
  }
  float scaleX = (config.maxWidth > 0) ? static_cast<float>(config.maxWidth) / info.width : 1.0f;
  float scaleY = (config.maxHeight > 0) ? static_cast<float>(config.maxHeight) / info.height : 1.0f;
  float scale = (scaleX < scaleY) ? scaleX : scaleY;
  if (scale > 1.0f) {
    scale = 1.0f;
  }
  dstWidth = static_cast<int>(info.width * scale);
  dstHeight = static_cast<int>(info.height * scale);
  return dstWidth > 0 && dstHeight > 0;
}

bool onBegin(void* ctx, const gifraster::DecodeInfo& info) {
  auto* render = static_cast<GifRender*>(ctx);
  if (!render || !render->config || !render->renderer) {
    return false;
  }
  if (!computeDestSize(info, *render->config, render->dstWidth, render->dstHeight)) {
    LOG_ERR("GIF", "Degenerate output dimensions for %dx%d GIF", info.width, info.height);
    return false;
  }
  render->srcWidth = info.width;
  render->srcHeight = info.height;
  render->interlaced = info.interlaced;
  render->lastDstY = -1;
  render->pw.init(*render->renderer);
  render->caching = !render->config->cachePath.empty() && !info.interlaced;
  if (render->caching) {
    if (!render->cache.begin(render->config->cachePath, render->dstWidth, render->dstHeight, render->config->x,
                             render->config->y, 1)) {
      LOG_ERR("GIF", "Failed to start cache stream, continuing without caching");
      render->caching = false;
    }
  }
  LOG_DBG("GIF", "GIF %dx%d -> %dx%d%s", info.width, info.height, render->dstWidth, render->dstHeight,
          info.interlaced ? " [interlaced]" : "");
  return true;
}

bool onRow(void* ctx, int srcY, const uint8_t* gray, int width) {
  auto* render = static_cast<GifRender*>(ctx);
  if (!render || !render->config || !gray || width != render->srcWidth) {
    return false;
  }

  int firstDstY = 0;
  int endDstY = 0;
  const int lastPainted = render->interlaced ? -1 : render->lastDstY;
  pngrow::computeDstRowRange(srcY, render->srcHeight, render->dstHeight, lastPainted, &firstDstY, &endDstY);
  if (firstDstY >= endDstY) {
    return true;
  }

  const int outXBase = render->config->x;
  int dstXStart = 0;
  if (outXBase < 0) {
    dstXStart = -outXBase;
  }
  int dstXEnd = render->dstWidth;
  if (outXBase + render->dstWidth > render->screenWidth) {
    dstXEnd = render->screenWidth - outXBase;
  }
  if (dstXStart >= dstXEnd) {
    render->lastDstY = endDstY - 1;
    return true;
  }

  const bool useDithering = render->config->useDithering;
  for (int dstY = firstDstY; dstY < endDstY; dstY++) {
    render->lastDstY = dstY;
    const int outY = render->config->y + dstY;
    if (outY >= render->screenHeight) {
      continue;
    }
    render->pw.beginRow(outY);

    bool caching = render->caching;
    DirectCacheWriter cw;
    if (caching) {
      if (!render->cache.advanceTo(dstY)) {
        caching = false;
        render->caching = false;
      } else {
        cw.init(render->cache.buffer, render->cache.bytesPerRow, render->cache.bandRows, render->cache.originX);
        cw.beginRow(outY, render->config->y + render->cache.bandStart);
      }
    }

    int srcX = 0;
    int error = 0;
    for (int dstX = 0; dstX < dstXStart; dstX++) {
      error += render->srcWidth;
      while (error >= render->dstWidth) {
        error -= render->dstWidth;
        srcX++;
      }
    }

    for (int dstX = dstXStart; dstX < dstXEnd; dstX++) {
      const int outX = outXBase + dstX;
      uint8_t dithered;
      if (useDithering) {
        dithered = applyBayerDither4Level(gray[srcX], outX, outY);
      } else {
        dithered = gray[srcX] / 85;
        if (dithered > 3) {
          dithered = 3;
        }
      }
      render->pw.writePixel(outX, dithered);
      if (caching) {
        cw.writePixel(outX, dithered);
      }
      error += render->srcWidth;
      while (error >= render->dstWidth) {
        error -= render->dstWidth;
        srcX++;
      }
    }
  }
  return true;
}

}  // namespace

bool GifToFramebufferConverter::getDimensionsStatic(const std::string& imagePath, ImageDimensions& out) {
  return imagedims::probeFromFile(imagePath, out);
}

bool GifToFramebufferConverter::decodeToFramebuffer(const std::string& imagePath, GfxRenderer& renderer,
                                                    const RenderConfig& config) {
  LOG_DBG("GIF", "Decoding GIF: %s (free=%u largest=%u)", imagePath.c_str(),
          static_cast<unsigned>(heapguard::freeBytes()), static_cast<unsigned>(heapguard::largestBlock()));

  HalFile file;
  if (!Storage.openFileForRead("GIF", imagePath, file)) {
    LOG_ERR("GIF", "Failed to open %s", imagePath.c_str());
    return false;
  }

  GifRender render;
  render.renderer = &renderer;
  render.config = &config;
  render.screenWidth = renderer.getScreenWidth();
  render.screenHeight = renderer.getScreenHeight();

  gifraster::ByteSource src{gifFileRead, &file};
  const gifraster::Status status = gifraster::decode(src, onBegin, onRow, &render);
  if (status != gifraster::Status::Ok) {
    LOG_ERR("GIF", "Decode failed (%s): %s", gifraster::statusText(status), imagePath.c_str());
    if (render.caching) {
      render.cache.abort();
    }
    return false;
  }

  LOG_DBG("GIF", "GIF decoding complete (free=%u largest=%u)", static_cast<unsigned>(heapguard::freeBytes()),
          static_cast<unsigned>(heapguard::largestBlock()));

  if (render.caching) {
    render.cache.finalize();
  }
  return true;
}

bool GifToFramebufferConverter::supportsFormat(const std::string& extension) {
  return FsHelpers::hasGifExtension(extension);
}

#endif

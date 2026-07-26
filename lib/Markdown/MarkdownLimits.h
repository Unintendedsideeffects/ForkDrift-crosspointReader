#pragma once

#include <cstddef>

namespace markdown::limits {

constexpr size_t kMaxSourceBytes = 96 * 1024;
constexpr size_t kMaxPreprocessedBytes = 128 * 1024;
constexpr size_t kMaxLineBytes = 8 * 1024;
constexpr size_t kMaxEmbedBytes = 32 * 1024;
constexpr size_t kMaxEmbeddedAggregateBytes = 64 * 1024;
constexpr size_t kMaxEmbedCount = 16;
constexpr int kMaxEmbedDepth = 3;
constexpr size_t kParserHeadroomBytes = 48 * 1024;

enum class AdmissionStatus {
  Ok,
  Empty,
  SourceTooLarge,
  InsufficientHeap,
  FragmentedHeap,
};

inline AdmissionStatus admitSource(const size_t sourceBytes, const size_t freeBytes, const size_t largestBlock) {
  if (sourceBytes == 0) return AdmissionStatus::Empty;
  if (sourceBytes > kMaxSourceBytes) return AdmissionStatus::SourceTooLarge;
  if (sourceBytes + 1 > largestBlock) return AdmissionStatus::FragmentedHeap;
  if (sourceBytes > (static_cast<size_t>(-1) - kParserHeadroomBytes) / 2 ||
      freeBytes < sourceBytes * 2 + kParserHeadroomBytes) {
    return AdmissionStatus::InsufficientHeap;
  }
  return AdmissionStatus::Ok;
}

}  // namespace markdown::limits

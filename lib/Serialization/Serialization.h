#pragma once
#include <HalStorage.h>
#include <Memory.h>

#include <cstring>
#include <iostream>

namespace serialization {

// Buffered sequential reader over HalFile. Every HalFile call takes the global
// storage mutex and crosses into SdFat; page deserialization issues ~1000 field
// reads of 1-4 bytes each. Batching them into sector-sized chunks takes the
// mutex once per 512 bytes instead of once per field. Falls back to unbuffered
// passthrough if the chunk buffer cannot be allocated.
// Mix direct HalFile access while a reader is live only after seek(), which
// drops the window and then seeks the file.
class BufferedReader {
 public:
  static constexpr size_t kBufSize = 512;

  explicit BufferedReader(HalFile& file, size_t cap = kBufSize)
      : file_(file), cap_(cap), buf_(cap > 0 ? makeUniqueNoThrow<uint8_t[]>(cap) : nullptr) {}
  BufferedReader(const BufferedReader&) = delete;
  BufferedReader& operator=(const BufferedReader&) = delete;

  int read(void* dst, size_t count) {
    if (!buf_ || cap_ == 0) {
      return static_cast<int>(file_.read(static_cast<uint8_t*>(dst), count));
    }
    auto* out = static_cast<uint8_t*>(dst);
    size_t total = 0;
    while (count > 0) {
      const size_t avail = fill_ - pos_;
      if (avail > 0) {
        const size_t n = avail < count ? avail : count;
        memcpy(out, bufData() + pos_, n);
        pos_ += n;
        out += n;
        total += n;
        count -= n;
        continue;
      }
      if (count >= cap_) {
        const int n = file_.read(out, count);
        if (n > 0) {
          total += static_cast<size_t>(n);
        }
        return static_cast<int>(total);
      }
      const int n = file_.read(bufData(), cap_);
      if (n <= 0) {
        break;
      }
      fill_ = static_cast<size_t>(n);
      pos_ = 0;
    }
    return static_cast<int>(total);
  }

  size_t position() const {
    if (!buf_ || cap_ == 0) {
      return file_.position();
    }
    const size_t unread = fill_ - pos_;
    const size_t filePos = file_.position();
    return filePos >= unread ? filePos - unread : 0;
  }

  bool seek(size_t pos) {
    fill_ = 0;
    pos_ = 0;
    return file_.seek(pos);
  }

 private:
  uint8_t* bufData() { return buf_.get(); }

  HalFile& file_;
  size_t cap_ = kBufSize;
  std::unique_ptr<uint8_t[]> buf_;
  size_t fill_ = 0;
  size_t pos_ = 0;
};

// Buffered sequential writer over HalFile; same rationale as BufferedReader for
// the page-serialization write path during section indexing. Callers must check
// flush() (or failed()) before trusting the file contents. Only valid for
// sequential writes: flush() before seeking the underlying file.
class BufferedWriter {
 public:
  static constexpr size_t kBufSize = 512;

  explicit BufferedWriter(HalFile& file, size_t cap = kBufSize)
      : file_(file), cap_(cap), buf_(cap > 0 ? makeUniqueNoThrow<uint8_t[]>(cap) : nullptr) {}
  BufferedWriter(const BufferedWriter&) = delete;
  BufferedWriter& operator=(const BufferedWriter&) = delete;
  ~BufferedWriter() { flush(); }

  size_t write(const void* src, size_t count) {
    if (!buf_ || cap_ == 0) {
      const size_t n = file_.write(reinterpret_cast<const uint8_t*>(src), count);
      if (n != count) {
        failed_ = true;
      }
      return n;
    }
    if (count >= cap_) {
      if (!flush()) {
        return 0;
      }
      const size_t n = file_.write(reinterpret_cast<const uint8_t*>(src), count);
      if (n != count) {
        failed_ = true;
      }
      return n;
    }
    if (fill_ + count > cap_ && !flush()) {
      return 0;
    }
    memcpy(bufData() + fill_, src, count);
    fill_ += count;
    return count;
  }

  bool flush() {
    if (fill_ > 0) {
      const size_t n = file_.write(bufData(), fill_);
      if (n != fill_) {
        failed_ = true;
      }
      fill_ = 0;
    }
    return !failed_;
  }

  size_t position() const { return file_.position() + fill_; }

  bool failed() const { return failed_; }

 private:
  uint8_t* bufData() { return buf_.get(); }

  HalFile& file_;
  size_t cap_ = kBufSize;
  std::unique_ptr<uint8_t[]> buf_;
  size_t fill_ = 0;
  bool failed_ = false;
};

template <typename T>
void writePod(std::ostream& os, const T& value) {
  os.write(reinterpret_cast<const char*>(&value), sizeof(T));
}

template <typename T>
static void writePod(HalFile& file, const T& value) {
  file.write(reinterpret_cast<const uint8_t*>(&value), sizeof(T));
}

template <typename T>
static void writePod(BufferedWriter& writer, const T& value) {
  writer.write(reinterpret_cast<const uint8_t*>(&value), sizeof(T));
}

template <typename T>
static bool readPod(std::istream& is, T& value) {
  is.read(reinterpret_cast<char*>(&value), sizeof(T));
  return is.gcount() == static_cast<std::streamsize>(sizeof(T));
}

template <typename T>
static bool readPod(HalFile& file, T& value) {
  return file.read(reinterpret_cast<uint8_t*>(&value), sizeof(T)) == sizeof(T);
}

template <typename T>
static bool readPod(BufferedReader& reader, T& value) {
  return reader.read(reinterpret_cast<uint8_t*>(&value), sizeof(T)) == sizeof(T);
}

inline void writeString(std::ostream& os, const std::string& s) {
  const uint32_t len = s.size();
  writePod(os, len);
  os.write(s.data(), len);
}

static void writeString(HalFile& file, const std::string& s) {
  const uint32_t len = s.size();
  writePod(file, len);
  file.write(reinterpret_cast<const uint8_t*>(s.data()), len);
}

static void writeString(BufferedWriter& writer, const std::string& s) {
  const uint32_t len = s.size();
  writePod(writer, len);
  writer.write(reinterpret_cast<const uint8_t*>(s.data()), len);
}

static bool readString(std::istream& is, std::string& s) {
  uint32_t len;
  if (!readPod(is, len)) return false;
  if (len > 65536) return false;  // Sanity check: max 64KB for metadata strings
  if (len == 0) {
    s.clear();
    return true;
  }
  s.resize(len);
  is.read(&s[0], len);
  return is.gcount() == static_cast<std::streamsize>(len);
}

static bool readString(HalFile& file, std::string& s) {
  uint32_t len;
  if (!readPod(file, len)) return false;
  if (len > 65536) return false;  // Sanity check: max 64KB for metadata strings
  if (len == 0) {
    s.clear();
    return true;
  }
  s.resize(len);
  return file.read(reinterpret_cast<uint8_t*>(&s[0]), len) == len;
}

static bool readString(BufferedReader& reader, std::string& s) {
  uint32_t len;
  if (!readPod(reader, len)) return false;
  if (len > 65536) return false;  // Sanity check: max 64KB for metadata strings
  if (len == 0) {
    s.clear();
    return true;
  }
  s.resize(len);
  return reader.read(reinterpret_cast<uint8_t*>(&s[0]), len) == static_cast<int>(len);
}
}  // namespace serialization

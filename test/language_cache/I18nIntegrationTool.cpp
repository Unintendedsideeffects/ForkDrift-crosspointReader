#include <HalStorage.h>
#include <I18n.h>
#include <I18nStrings.h>
#include <Memory.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

int main(int argc, char** argv) {
  if (argc < 2) return 2;
  if (std::strcmp(argv[1], "format") == 0) {
    if (argc != 4) return 2;
    std::printf("%d", language_cache::compatibleFormat(argv[2], argv[3]));
    return 0;
  }
  const char* code = argc > 2 ? argv[2] : "EN";
  const uint64_t generation = argc > 3 ? std::strtoull(argv[3], nullptr, 10) : 0;
  I18N.begin(code, generation);
  if (std::strcmp(argv[1], "boot") == 0) {
    if (HalStorage::opens || HalFile::reads) return 3;
    for (int i = 0; i < 10000; ++i) (void)I18N.get(StrId::STR_SETTINGS_TITLE);
    if (HalStorage::opens || HalFile::reads) return 4;
    std::printf("%s|%s|%s|%u\n", I18N.getCode(), I18N.getName(), tr(STR_SETTINGS_TITLE), I18N.isRightToLeft());
    I18N.benchmark();
  } else if (std::strcmp(argv[1], "lookup") == 0) {
    if (argc < 5) return 2;
    for (size_t i = 0; i < static_cast<size_t>(StrId::_COUNT); ++i) {
      if (std::strcmp(TRANSLATION_KEYS[i].name, argv[4]) == 0) {
        std::printf("%s", I18N.get(static_cast<StrId>(TRANSLATION_KEYS[i].id)));
        return 0;
      }
    }
    return 2;
  } else if (std::strcmp(argv[1], "install") == 0) {
    if (argc < 5) return 2;
    const char* old = tr(STR_SETTINGS_TITLE);
    const std::string before(old);
    language_cache::Installed installed;
    const auto result = I18N.prepare(argv[4], installed);
    if (result != language_cache::Result::Ok) {
      std::fprintf(stderr, "%s\n", language_cache::resultName(result));
      return 5;
    }
    if (std::strcmp(old, before.c_str()) != 0 || tr(STR_SETTINGS_TITLE) != old) return 6;
    std::printf("%s|%llu\n", installed.metadata.code, static_cast<unsigned long long>(installed.generation));
  } else if (std::strcmp(argv[1], "scan") == 0) {
    HeapObject<I18n::Catalog> catalog;
    catalog.init(MemoryPool::None);
    const auto status = catalog ? I18N.discover(*catalog.get()) : language_cache::Result::Memory;
    if (status != language_cache::Result::Ok) {
      std::fprintf(stderr, "%s\n", language_cache::resultName(status));
      return 5;
    }
    for (size_t i = 0; i < catalog->count; ++i) {
      I18n::Option item;
      catalog->select(i, item);
      std::printf("%s|%s|%d|%s\n", item.code, item.name, item.duplicate, item.path);
    }
  } else
    return 2;
}

#include <ObfuscationUtils.h>

namespace obfuscation {

void xorTransform(std::string&) {}

String obfuscateToBase64(const std::string& plaintext) { return String(plaintext.c_str()); }

std::string deobfuscateFromBase64(const char* encoded, bool* ok) {
  const bool present = encoded != nullptr && encoded[0] != '\0';
  if (ok) {
    *ok = present;
  }
  return present ? std::string(encoded) : std::string();
}

void selfTest() {}

}  // namespace obfuscation

#include <fstream>
#include <string>

#include "doctest/doctest.h"
#include "include/FeatureFlags.h"
#include "src/core/features/FeatureCatalog.h"

TEST_CASE("testStatusBarToggleTranslations") {
  struct TranslationExpectation {
    const char* path;
    const char* show;
    const char* hide;
  };

  const TranslationExpectation expectations[] = {
      {"lib/I18n/translations/belarusian.yaml", "Паказаць", "Схаваць"},
      {"lib/I18n/translations/czech.yaml", "Zobrazit", "Skrýt"},
      {"lib/I18n/translations/danish.yaml", "Vis", "Skjul"},
      {"lib/I18n/translations/finnish.yaml", "Näytä", "Piilota"},
      {"lib/I18n/translations/french.yaml", "Afficher", "Masquer"},
      {"lib/I18n/translations/italian.yaml", "Mostra", "Nascondi"},
      {"lib/I18n/translations/portuguese.yaml", "Mostrar", "Ocultar"},
  };

  for (const TranslationExpectation& expectation : expectations) {
    std::ifstream file(expectation.path);
    CHECK(file.is_open());

    const std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    CHECK(content.find(std::string("STR_SHOW: \"") + expectation.show + "\"") != std::string::npos);
    CHECK(content.find(std::string("STR_HIDE: \"") + expectation.hide + "\"") != std::string::npos);
  }
}

TEST_CASE("testFeatureCatalogApi") {
  size_t featureCount = 0;
  const core::FeatureDescriptor* features = core::FeatureCatalog::all(featureCount);
  CHECK(features != nullptr);
  CHECK(featureCount > 0);
  CHECK(core::FeatureCatalog::totalCount() == featureCount);

  CHECK(core::FeatureCatalog::isEnabled("epub_support") == (ENABLE_EPUB_SUPPORT != 0));
  CHECK(core::FeatureCatalog::isEnabled("home_media_picker") == (ENABLE_HOME_MEDIA_PICKER != 0));
  CHECK(core::FeatureCatalog::isEnabled("pokemon_party") == (ENABLE_POKEMON_PARTY != 0));
  CHECK(core::FeatureCatalog::isEnabled("remote_keyboard_input") == (ENABLE_REMOTE_KEYBOARD_INPUT != 0));
  CHECK(core::FeatureCatalog::isEnabled("missing_feature") == false);
  CHECK(core::FeatureCatalog::find("missing_feature") == nullptr);
  const core::FeatureDescriptor* backgroundServerOnCharge = core::FeatureCatalog::find("background_server_on_charge");
  CHECK(backgroundServerOnCharge != nullptr);
  CHECK(backgroundServerOnCharge->requiresAllCount == 0);
  CHECK(backgroundServerOnCharge->requiresAnyCount == 0);
  const core::FeatureDescriptor* backgroundServerAlways = core::FeatureCatalog::find("background_server_always");
  CHECK(backgroundServerAlways != nullptr);
  CHECK(backgroundServerAlways->requiresAllCount == 0);
  CHECK(backgroundServerAlways->requiresAnyCount == 0);
  const core::FeatureDescriptor* focusReading = core::FeatureCatalog::find("focus_reading");
  REQUIRE(focusReading != nullptr);
  CHECK(focusReading->requiresAllCount == 0);
  CHECK(focusReading->requiresAnyCount == 2);
  CHECK(std::string(focusReading->requiresAny[0]) == "epub_support");
  CHECK(std::string(focusReading->requiresAny[1]) == "markdown");
  const core::FeatureDescriptor* guideDots = core::FeatureCatalog::find("guide_dots");
  REQUIRE(guideDots != nullptr);
  CHECK(guideDots->requiresAllCount == 0);
  CHECK(guideDots->requiresAnyCount == 2);
  CHECK(std::string(guideDots->requiresAny[0]) == "epub_support");
  CHECK(std::string(guideDots->requiresAny[1]) == "markdown");

  const String json = core::FeatureCatalog::toJson();
  CHECK(!json.isEmpty());
  CHECK(json.indexOf("\"extended_fonts\":") == -1);
  CHECK(json.indexOf("\"epub_support\":") != -1);
  CHECK(json.indexOf("\"pokemon_party\":") != -1);
  CHECK(json.indexOf("\"remote_keyboard_input\":") != -1);
  CHECK(json.indexOf("\"todo_planner\":") != -1);

  const String buildString = core::FeatureCatalog::buildString();
  CHECK(!buildString.isEmpty());
  CHECK(buildString.indexOf("extended_fonts") == -1);

  String dependencyError;
  CHECK(core::FeatureCatalog::validate(&dependencyError));
  CHECK(dependencyError.isEmpty());
}

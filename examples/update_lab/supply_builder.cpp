#include "model.h"
#include "apptraverse/distill.h"
namespace update_lab {
ae::RamDomainStorage BuildSupply(unsigned release) {
  ae::RamDomainStorage storage;
  ae::Domain domain{storage};
  auto p = TextObject::ptr::Create(ae::CreateWith{domain}.with_id(kPrefabId));
  p->name = "A";
#if UPDATE_LAB_SCHEMA
  p->badge = "S1 / prefab R3";
#endif
  p->SetObjectScope({kPrefabId, {}, true});
  auto definition = SupplyDefinition::ptr::Create(ae::CreateWith{domain}.with_id(kSupplyId));
  definition->release = release;
  definition->languages = release < 2 ? std::vector<std::string>{"en", "ru"}
                                    : std::vector<std::string>{"en", "ru", "de"};
  definition->SetObjectScope({kSupplyId, {}, true});
  p.Save(); definition.Save();
  if (release < 3) storage.state[kPrefabId]->at(TextObject::kClassId).erase(1);
  return storage;
}
LanguagePack BuildLanguage(unsigned release, std::string const& language) {
  LanguagePack p;
  p.locale = language;
  if (language == "en") p.text = {"Update laboratory", release == 0 ? "Hello! Original translation." : "Hello! Revised translation.", "Language", "Version", "Portable note", "Save note", "Add clone", "Clone", "Downloading", "Ready", "Download failed", "Slow connection", "Restarting", "Prefab", "Try broken update", "Export USER", "Import USER"};
  if (language == "ru") p.text = {"Лаборатория обновлений", release == 0 ? "Привет! Исходный перевод." : "Здравствуйте! Обновлённый перевод.", "Язык", "Версия", "Переносимая заметка", "Сохранить заметку", "Добавить клон", "Клон", "Загрузка", "Готово", "Ошибка загрузки", "Медленное соединение", "Перезапуск", "Префаб", "Проверить сбой обновления", "Экспорт USER", "Импорт USER"};
  if (language == "de") p.text = {"Update-Labor", "Hallo! Neu hinzugefügte Sprache.", "Sprache", "Version", "Übertragbare Notiz", "Notiz speichern", "Klon hinzufügen", "Klon", "Herunterladen", "Bereit", "Download fehlgeschlagen", "Langsame Verbindung", "Neustart", "Vorlage", "Fehlerhaftes Update testen", "USER exportieren", "USER importieren"};
  if (language == "ru" && release >= 3) p.text[Ready] = "Готово к работе";
  // Deliberate demo ballast, not part of the compression benchmark corpus.
  p.payload.resize(language == "en" ? 0 : 384 * 1024);
  std::uint32_t value = 0x6539b731;
  for (auto& byte : p.payload) { value ^= value << 13; value ^= value >> 17; value ^= value << 5; byte = static_cast<std::uint8_t>(value); }
  return p;
}
namespace {
ae::ObjId LanguageRoot(std::string const& locale) {
  if (locale == "en") return ae::ObjId{0x7b200001};
  if (locale == "ru") return ae::ObjId{0x7b200002};
  if (locale == "de") return ae::ObjId{0x7b200003};
  Fail("unknown publisher locale");
}
void AddLocalization(ae::RamDomainStorage& storage, unsigned release, std::string const& locale) {
  ae::Domain domain{storage};
  auto state = LocalizationState::ptr::Create(ae::CreateWith{domain}.with_id(LanguageRoot(locale)));
  auto data = BuildLanguage(release, locale);
  state->locale = locale; state->fixture_data = std::move(data.payload);
  state->SetObjectScope({state.id(), {}, true});
  auto first_string = 0x7b300000U + (state.id().id() & 0xffU) * 256;
  for (unsigned i = 0; i < data.text.size(); ++i) {
    auto string = LocalizedString::ptr::Create(ae::CreateWith{domain}.with_id(ae::ObjId{first_string + i}));
    string->value = std::move(data.text[i]);
    string->SetObjectScope({state.id(), {}, true});
    string.Save();
    state->strings.push_back(std::move(string));
  }
  state.Save();
}
}
ae::RamDomainStorage BuildCompleteSupply(unsigned release) {
  auto storage = BuildSupply(release);
  AddLocalization(storage, release, "en"); AddLocalization(storage, release, "ru");
  if (release >= 2) AddLocalization(storage, release, "de");
  return storage;
}
DistilledRelease DistillRelease(unsigned release) {
  DistilledRelease result;
  auto graph = BuildCompleteSupply(release);
  ae::Domain domain{graph};
  auto supply = SupplyDefinition::ptr::Declare(ae::CreateWith{domain}.with_id(kSupplyId)).Load();
  result.definition = {release, release >= 3 ? 1U : 0U, "supply/" + std::to_string(release), {}};
  // Explicit distillation policy. Unmarked ownership roots stay in the complete base.
  std::map<ae::ObjId, std::string> optional;
  for (auto const& locale : supply->languages) {
    auto root = LanguageRoot(locale);
    if (locale == "ru" || locale == "de") optional[root] = locale;
    result.definition.languages.push_back({locale, {}, root.id(), release});
  }
  for (auto& [id, layers] : graph.state) {
    auto scope = graph.object_scopes.find(id);
    auto owner = scope == graph.object_scopes.end() ? id : scope->second.root_id;
    auto found = optional.find(owner);
    auto& partition = found == optional.end() ? result.embedded : result.downloads[found->second];
    partition.state[id] = std::move(layers);
    if (scope != graph.object_scopes.end()) partition.object_scopes[id] = scope->second;
  }
  for (auto& resource : result.definition.languages)
    if (result.downloads.contains(resource.locale)) resource.full = LanguageId(release, resource.locale);
  return result;
}
}

#include "wire.h"
#include <algorithm>

namespace update_lab {
std::string ExeId(std::string const& platform, unsigned version) {
  return "executable/" + platform + "/" + std::to_string(version);
}
std::string DeltaId(unsigned from, unsigned to, std::string const& locale) {
  auto id = "delta/" + std::to_string(from) + "/" + std::to_string(to);
  return locale.empty() ? id : id + "/" + locale;
}
Resource const* FindLanguage(Release const& release, std::string const& locale) {
  for (auto const& language : release.languages) if (language.locale == locale) return &language;
  return nullptr;
}
std::string Resolve(Catalog const& catalog, ResolveRequest const& q, UpdatePlan& output) {
  UpdatePlan plan{};
  auto platform = catalog.platforms.find(q.platform);
  if (platform != catalog.platforms.end()) plan.available = platform->second;
  std::sort(plan.available.begin(), plan.available.end());
  if (plan.available.empty()) return "No releases for " + q.platform;
  plan.intent = q.target;
  if (plan.intent.release == kLatest) plan.intent.release = plan.available.back();
  auto target = FindRelease(catalog, plan.intent.release);
  if (!target || !std::binary_search(plan.available.begin(), plan.available.end(), target->id))
    return "Release unavailable for " + q.platform;
  switch (q.target.mode) {
    case InstallMode::Preferred: plan.executable = target->executable; break;
    case InstallMode::BrokenFixture: plan.executable = 2; break;
    case InstallMode::DataOnly: {
      auto compatible = catalog.compatible_executables.find(target->id);
      if (q.executable != target->executable && (compatible == catalog.compatible_executables.end() ||
          std::find(compatible->second.begin(), compatible->second.end(), q.executable) == compatible->second.end()))
        return "This release requires a different executable";
      plan.executable = q.executable;
      break;
    }
    default: return "Unknown update mode";
  }
  if (!FindLanguage(*target, plan.intent.language)) {
    if (q.current == target->id) return "Language unavailable";
    plan.intent.language = "en";
  }
  plan.image.release = *target;
  if (plan.intent.mode == InstallMode::DataOnly) plan.image.release.executable = plan.executable;
  auto current = FindRelease(catalog, q.current);
  if (!current && q.current != kLatest) return "Unknown installed release";
  plan.supply = {current ? current->supply : "", target->supply, target->id, {}};
  if (!current) plan.supply.steps.push_back(target->supply);
  else for (unsigned r = current->id; r != target->id;) {
    unsigned next = r < target->id ? r + 1 : r - 1;
    if (!FindRelease(catalog, next)) return "Missing intermediate release R" + std::to_string(next);
    plan.supply.steps.push_back(DeltaId(r, next));
    r = next;
  }

  for (auto const& language : target->languages) {
    if (language.full.empty()) continue;
    auto installed = q.installed.find(language.locale);
    if (installed == q.installed.end() && language.locale != plan.intent.language) continue;
    Chain chain{"", language.full, language.revision, {}};
    if (installed == q.installed.end() || installed->second == kLatest) {
      chain.steps.push_back(language.full);
    } else {
      unsigned revision = installed->second;
      chain.base = LanguageId(revision, language.locale);
      for (unsigned r = revision; r != language.revision;) {
        unsigned next = r < language.revision ? r + 1 : r - 1;
        auto release = FindRelease(catalog, next);
        auto resource = release ? FindLanguage(*release, language.locale) : nullptr;
        if (!resource) return "Missing resource history: " + language.locale;
        if (resource->revision != revision) {
          chain.steps.push_back(DeltaId(revision, resource->revision, language.locale));
          revision = resource->revision;
        }
        r = next;
      }
    }
    plan.image.resources[language.locale] = language.revision;
    plan.resources.push_back(std::move(chain));
  }

  if (plan.executable != q.executable) plan.executable_id = ExeId(q.platform, plan.executable);
  auto ids = plan.supply.steps;
  for (auto const& chain : plan.resources) ids.insert(ids.end(), chain.steps.begin(), chain.steps.end());
  if (!plan.executable_id.empty()) ids.push_back(plan.executable_id);
  for (auto const& id : ids) {
    auto file = FindComponent(catalog, id);
    if (!file) return "Missing component in update chain: " + id;
    auto cached = q.received.find(id);
    auto received = cached == q.received.end() ? 0 : std::min(cached->second, file->size);
    plan.bytes += file->size;
    plan.remaining += file->size - received;
    plan.files.push_back({id, file->size, {}});
  }
  output = std::move(plan);
  return {};
}
}

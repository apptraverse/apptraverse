#include "window_catalog_list_row_text.h"

#include <cassert>

namespace apptraverse {
namespace {

void ReplaceAll(std::string& text, std::string_view needle,
                std::string_view replacement) {
  if (needle.empty()) {
    return;
  }
  std::size_t pos = 0;
  while ((pos = text.find(needle, pos)) != std::string::npos) {
    text.replace(pos, needle.size(), replacement);
    pos += replacement.size();
  }
}

std::string ResolveCatalogLocalized(ui::UiLocalizationString& localized,
                                    int language_index) {
  auto& slot = localized.strings[static_cast<std::size_t>(language_index)];
  if (!slot.is_loaded()) {
    slot.Load();
  }
  return slot->utf8;
}

}  // namespace

bool IsWindowCatalogHolderWindowOpen(WindowHolder const& holder) {
  if (!holder.window.is_valid()) {
    return false;
  }
  ae::ObjFlags const flags = holder.window.flags();
  return (flags & ae::ObjFlags::kUnloaded) == ae::ObjFlags{} &&
         (flags & ae::ObjFlags::kUnloadedByDefault) == ae::ObjFlags{};
}

std::string FormatWindowCatalogListRowText(WindowCatalogTexts const& texts,
                                           int language_index,
                                           std::uint32_t window_number,
                                           bool window_open) {
  auto& mutable_texts = const_cast<WindowCatalogTexts&>(texts);
  mutable_texts.list_row_title_format.Load();
  std::string format = ResolveCatalogLocalized(
      *mutable_texts.list_row_title_format, language_index);
  std::string suffix;
  if (!window_open) {
    mutable_texts.list_row_title_closed_suffix.Load();
    suffix = ResolveCatalogLocalized(
        *mutable_texts.list_row_title_closed_suffix, language_index);
  }
  ReplaceAll(format, "{0}", std::to_string(window_number));
  ReplaceAll(format, "{1}", suffix);
  return format;
}

std::string ResolveWindowCatalogHolderListRowText(WindowHolder const& holder,
                                                WindowCatalog const& catalog,
                                                int language_index) {
  return FormatWindowCatalogListRowText(
      catalog.persisted.texts, language_index, holder.window_number,
      IsWindowCatalogHolderWindowOpen(holder));
}

std::string ResolveWindowCatalogListRowPrimaryText(
    ui::UiListRow::ptr const& row, WindowCatalog const& catalog,
    int language_index) {
  row.Load();
  auto const& holder = static_cast<WindowHolder const&>(*row);
  return ResolveWindowCatalogHolderListRowText(holder, catalog, language_index);
}

std::vector<std::string> ResolveWindowCatalogListRowPrimaryTexts(
    ui::UiListContainer const& list, WindowCatalog const& catalog,
    int language_index) {
  std::vector<std::string> labels;
  labels.reserve(list.rows.size());
  for (auto const& row : list.rows) {
    labels.push_back(
        ResolveWindowCatalogListRowPrimaryText(row, catalog, language_index));
  }
  return labels;
}

std::string ResolveWindowCatalogChildWindowTitle(WindowCatalog const& catalog,
                                                 ae::ObjId window_obj_id,
                                                 int language_index) {
  catalog.persisted.list.Load();
  for (auto const& row : catalog.persisted.list->rows) {
    row.Load();
    auto const& holder = static_cast<WindowHolder const&>(*row);
    if (!holder.window.is_valid() || holder.window.id() != window_obj_id) {
      continue;
    }
    catalog.persisted.texts.window_title_prefix.Load();
    std::string prefix = ResolveCatalogLocalized(
        *catalog.persisted.texts.window_title_prefix, language_index);
    return prefix + std::to_string(holder.window_number);
  }
  return catalog.persisted.texts.window_title_prefix->Resolve(language_index);
}

}  // namespace apptraverse

#include <filesystem>

#include "chat_ui_session.h"

int main(int argc, char** argv) {
  if (argc != 2) return 2;
  using namespace apptraverse::chat::gui;
  EnsureUiRegistration();
  ConfigureUiFactories(HeadlessUiFactories());
  return ExportUiSupply(std::filesystem::path{argv[1]});
}

#include <iostream>
#include "apptraverse/chat_supply_export.h"
int main(int argc, char** argv) {
  if (argc != 2) {
    std::cerr << "Usage: chat-supply-exporter OUTPUT.h\n";
    return 2;
  }
  return apptraverse::chat::ExportChatInitialSupply(argv[1]) ? 0 : 1;
}

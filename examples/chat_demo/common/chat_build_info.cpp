#include "chat_build_info.h"

#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include "chat_build_info_generated.h"

namespace apptraverse::example::chat_demo {
namespace {

bool WriteUtf8File(std::string const& path, std::string const& text) {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out) {
    return false;
  }
  out.write(text.data(), static_cast<std::streamsize>(text.size()));
  return static_cast<bool>(out);
}

}  // namespace

ChatBuildInfo GetChatBuildInfo() {
  ChatBuildInfo info;
  info.source_sha = APPTRAVERSE_CHAT_SOURCE_SHA;
  info.compile_fingerprint = APPTRAVERSE_CHAT_COMPILE_FINGERPRINT;
  info.source_dirty = (APPTRAVERSE_CHAT_SOURCE_DIRTY != 0);
  info.source_dirty_flag = APPTRAVERSE_CHAT_SOURCE_DIRTY_FLAG;
  info.configuration = APPTRAVERSE_CHAT_BUILD_CONFIGURATION;
  info.cxx_compiler = APPTRAVERSE_CHAT_CXX_COMPILER;
  info.aether_client_sha = APPTRAVERSE_CHAT_AETHER_CLIENT_SHA;
  info.aether_objects_sha = APPTRAVERSE_CHAT_AETHER_OBJECTS_SHA;
  info.aether_miscpp_sha = APPTRAVERSE_CHAT_AETHER_MISCPP_SHA;
  info.objects_scope_patch = APPTRAVERSE_CHAT_OBJECTS_SCOPE_PATCH;
  return info;
}

std::string FormatChatBuildInfoText(ChatBuildInfo const& info) {
  std::string out;
  out += "binary_source_sha=";
  out += info.source_sha;
  out += '\n';
  out += "compile_fingerprint=";
  out += info.compile_fingerprint;
  out += '\n';
  out += "source_dirty=";
  out += info.source_dirty_flag;
  out += '\n';
  out += "configuration=";
  out += info.configuration;
  out += '\n';
  out += "cxx_compiler=";
  out += info.cxx_compiler;
  out += '\n';
  out += "aether_client_sha=";
  out += info.aether_client_sha;
  out += '\n';
  out += "aether_objects_sha=";
  out += info.aether_objects_sha;
  out += '\n';
  out += "aether_miscpp_sha=";
  out += info.aether_miscpp_sha;
  out += '\n';
  out += "objects_scope_patch=";
  out += info.objects_scope_patch;
  out += '\n';
  return out;
}

bool TryHandleBuildInfoArgs(std::vector<std::string> const& args, int* exit_code) {
  bool want_stdout = false;
  std::string out_file;
  for (std::size_t i = 0; i < args.size(); ++i) {
    std::string_view const arg = args[i];
    if (arg == "--build-info") {
      want_stdout = true;
    } else if (arg == "--build-info-file") {
      if (i + 1 >= args.size()) {
        if (exit_code != nullptr) {
          *exit_code = 1;
        }
        return true;
      }
      out_file = args[++i];
    }
  }
  if (!want_stdout && out_file.empty()) {
    return false;
  }
  std::string const text = FormatChatBuildInfoText(GetChatBuildInfo());
  if (!out_file.empty()) {
    if (!WriteUtf8File(out_file, text)) {
      if (exit_code != nullptr) {
        *exit_code = 1;
      }
      return true;
    }
  }
  if (want_stdout) {
    std::cout << text << std::flush;
  }
  if (exit_code != nullptr) {
    *exit_code = 0;
  }
  return true;
}

bool TryHandleBuildInfoArgs(int argc, char const* const* argv, int* exit_code) {
  std::vector<std::string> args;
  for (int i = 1; i < argc; ++i) {
    args.emplace_back(argv[i]);
  }
  return TryHandleBuildInfoArgs(args, exit_code);
}

}  // namespace apptraverse::example::chat_demo

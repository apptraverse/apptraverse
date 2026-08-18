#include "runtime_jsonl.h"

#include <cstdlib>
#include <iomanip>
#include <sstream>

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#else
#  include <unistd.h>
#endif

namespace apptraverse::examples {
namespace {

constexpr char kSchemaVersion[] = "apptraverse.runtime_event/1";
constexpr char kDefaultRunId[] = "run-default";
constexpr char kDefaultInstance[] = "win-default";

std::string ReadEnv(std::string_view name) {
  if (name.empty()) {
    return {};
  }
  char const* value = std::getenv(std::string(name).c_str());
  if (value == nullptr) {
    return {};
  }
  return value;
}

std::uint32_t CurrentProcessIdValue() {
#if defined(_WIN32)
  return static_cast<std::uint32_t>(::GetCurrentProcessId());
#else
  return static_cast<std::uint32_t>(::getpid());
#endif
}

void AppendEscapedChar(std::string& out, char ch) {
  switch (ch) {
    case '"':
      out += "\\\"";
      break;
    case '\\':
      out += "\\\\";
      break;
    case '\b':
      out += "\\b";
      break;
    case '\f':
      out += "\\f";
      break;
    case '\n':
      out += "\\n";
      break;
    case '\r':
      out += "\\r";
      break;
    case '\t':
      out += "\\t";
      break;
    default:
      if (static_cast<unsigned char>(ch) < 0x20) {
        std::ostringstream stream;
        stream << "\\u" << std::hex << std::uppercase << std::setw(4)
               << std::setfill('0')
               << static_cast<int>(static_cast<unsigned char>(ch));
        out += stream.str();
      } else {
        out.push_back(ch);
      }
      break;
  }
}

}  // namespace

std::string JsonEscape(std::string_view text) {
  std::string out;
  out.reserve(text.size() + 8);
  for (char ch : text) {
    AppendEscapedChar(out, ch);
  }
  return out;
}

RuntimeJsonlLogger::Field RuntimeJsonlLogger::Field::String(std::string key,
                                                            std::string_view value) {
  Field field;
  field.key = std::move(key);
  field.kind = Kind::kString;
  field.string_value.assign(value);
  return field;
}

RuntimeJsonlLogger::Field RuntimeJsonlLogger::Field::Bool(std::string key,
                                                          bool value) {
  Field field;
  field.key = std::move(key);
  field.kind = Kind::kBool;
  field.bool_value = value;
  return field;
}

RuntimeJsonlLogger::Field RuntimeJsonlLogger::Field::Int(std::string key,
                                                         std::int64_t value) {
  Field field;
  field.key = std::move(key);
  field.kind = Kind::kInt;
  field.int_value = value;
  return field;
}

RuntimeJsonlLogger::Field RuntimeJsonlLogger::Field::UInt(std::string key,
                                                          std::uint64_t value) {
  Field field;
  field.key = std::move(key);
  field.kind = Kind::kUInt;
  field.uint_value = value;
  return field;
}

RuntimeJsonlLogger::Field RuntimeJsonlLogger::Field::Null(std::string key) {
  Field field;
  field.key = std::move(key);
  field.kind = Kind::kNull;
  return field;
}

std::unique_ptr<RuntimeJsonlLogger> RuntimeJsonlLogger::TryOpenFromEnvironment() {
  auto const path = ReadEnv("APPTRAVERSE_RUNTIME_JSONL");
  if (path.empty()) {
    return nullptr;
  }
  auto run_id = ReadEnv("APPTRAVERSE_RUN_ID");
  if (run_id.empty()) {
    run_id = kDefaultRunId;
  }
  auto instance = ReadEnv("APPTRAVERSE_INSTANCE");
  if (instance.empty()) {
    instance = kDefaultInstance;
  }
  return std::unique_ptr<RuntimeJsonlLogger>(
      new RuntimeJsonlLogger(path, run_id, instance, "windows",
                             CurrentProcessIdValue()));
}

RuntimeJsonlLogger::RuntimeJsonlLogger(std::string path, std::string run_id,
                                       std::string instance,
                                       std::string platform, std::uint32_t pid)
    : run_id_(std::move(run_id)),
      instance_(std::move(instance)),
      platform_(std::move(platform)),
      pid_(pid),
      start_mono_(std::chrono::steady_clock::now()) {
  stream_.open(path, std::ios::out | std::ios::app | std::ios::binary);
}

RuntimeJsonlLogger::~RuntimeJsonlLogger() {
  if (stream_.is_open()) {
    stream_.flush();
  }
}

std::uint64_t RuntimeJsonlLogger::NextSeq() { return ++seq_; }

std::int64_t RuntimeJsonlLogger::WallUs() const {
  return std::chrono::duration_cast<std::chrono::microseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

std::uint64_t RuntimeJsonlLogger::MonoUs() const {
  return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
                                        std::chrono::steady_clock::now() - start_mono_)
                                        .count());
}

void RuntimeJsonlLogger::AppendDataObject(
    std::string& out, std::initializer_list<Field> const& fields) const {
  out += "\"data\":{";
  bool first = true;
  for (auto const& field : fields) {
    if (!first) {
      out += ',';
    }
    first = false;
    out += '"';
    out += JsonEscape(field.key);
    out += "\":";
    switch (field.kind) {
      case Field::Kind::kString:
        out += '"';
        out += JsonEscape(field.string_value);
        out += '"';
        break;
      case Field::Kind::kBool:
        out += field.bool_value ? "true" : "false";
        break;
      case Field::Kind::kInt:
        out += std::to_string(field.int_value);
        break;
      case Field::Kind::kUInt:
        out += std::to_string(field.uint_value);
        break;
      case Field::Kind::kNull:
        out += "null";
        break;
    }
  }
  out += '}';
}

void RuntimeJsonlLogger::Emit(std::string_view event,
                              std::initializer_list<Field> fields) {
  if (!stream_.is_open()) {
    return;
  }
  std::string line;
  line.reserve(256);
  line += '{';
  line += "\"schema_version\":\"";
  line += kSchemaVersion;
  line += "\",\"run_id\":\"";
  line += JsonEscape(run_id_);
  line += "\",\"seq\":";
  line += std::to_string(NextSeq());
  line += ",\"event\":\"";
  line += JsonEscape(event);
  line += "\",\"platform\":\"";
  line += JsonEscape(platform_);
  line += "\",\"instance\":\"";
  line += JsonEscape(instance_);
  line += "\",\"pid\":";
  line += std::to_string(pid_);
  line += ",\"t_us\":";
  line += std::to_string(WallUs());
  line += ",\"mono_us\":";
  line += std::to_string(MonoUs());
  line += ',';
  AppendDataObject(line, fields);
  line += '}';
  stream_ << line << '\n';
  stream_.flush();
}

}  // namespace apptraverse::examples

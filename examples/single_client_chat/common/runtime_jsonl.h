#ifndef APPTRAVERSE_EXAMPLES_RUNTIME_JSONL_H_
#define APPTRAVERSE_EXAMPLES_RUNTIME_JSONL_H_

#include <chrono>
#include <cstdint>
#include <fstream>
#include <initializer_list>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace apptraverse::examples {

std::string JsonEscape(std::string_view text);

class RuntimeJsonlLogger {
 public:
  struct Field {
    enum class Kind : std::uint8_t {
      kString,
      kBool,
      kInt,
      kUInt,
      kNull,
    };

    std::string key;
    Kind kind{Kind::kNull};
    std::string string_value;
    bool bool_value{false};
    std::int64_t int_value{0};
    std::uint64_t uint_value{0};

    static Field String(std::string key, std::string_view value);
    static Field Bool(std::string key, bool value);
    static Field Int(std::string key, std::int64_t value);
    static Field UInt(std::string key, std::uint64_t value);
    static Field Null(std::string key);
  };

  static std::unique_ptr<RuntimeJsonlLogger> TryOpenFromEnvironment();

  RuntimeJsonlLogger(RuntimeJsonlLogger const&) = delete;
  RuntimeJsonlLogger& operator=(RuntimeJsonlLogger const&) = delete;
  ~RuntimeJsonlLogger();

  bool enabled() const { return stream_.is_open(); }

  void Emit(std::string_view event,
            std::initializer_list<Field> fields = {});

 private:
  RuntimeJsonlLogger(std::string path, std::string run_id, std::string instance,
                     std::string platform, std::uint32_t pid);

  std::uint64_t NextSeq();
  std::int64_t WallUs() const;
  std::uint64_t MonoUs() const;
  void AppendDataObject(std::string& out,
                        std::initializer_list<Field> const& fields) const;

  std::string run_id_;
  std::string instance_;
  std::string platform_;
  std::uint32_t pid_{0};
  std::uint64_t seq_{0};
  std::chrono::steady_clock::time_point start_mono_;
  std::ofstream stream_;
};

}  // namespace apptraverse::examples

#endif  // APPTRAVERSE_EXAMPLES_RUNTIME_JSONL_H_

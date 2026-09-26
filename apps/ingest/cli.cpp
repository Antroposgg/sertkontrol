#include "cli.hpp"

#include <optional>
#include <string>

namespace sk::ingest {

namespace {

std::optional<Mode> mode_of(std::string_view arg) {
  if (arg == "--once") {
    return Mode::kOnce;
  }
  if (arg == "--daemon") {
    return Mode::kDaemon;
  }
  if (arg == "--demo") {
    return Mode::kDemo;
  }
  if (arg == "--help" || arg == "-h") {
    return Mode::kHelp;
  }
  return std::nullopt;
}

}  // namespace

std::string_view usage() noexcept {
  return "Использование: ingest (--once | --daemon | --demo | --help)\n"
         "              [--snapshot-dir <путь>] [--demo-dir <путь>]\n";
}

Result<Options> parse_args(std::span<const std::string_view> args) {
  Options opts;
  std::optional<Mode> mode;
  for (std::size_t i = 0; i < args.size(); ++i) {
    const auto arg = args[i];
    if (const auto m = mode_of(arg)) {
      if (mode && *mode != *m) {
        return Error{ErrorCode::kInvalidArgument, "указано больше одного режима"};
      }
      mode = m;
      continue;
    }
    if (arg == "--snapshot-dir" || arg == "--demo-dir") {
      if (i + 1 >= args.size()) {
        return Error{ErrorCode::kInvalidArgument, std::string{arg} + ": нет значения"};
      }
      auto& target = arg == "--snapshot-dir" ? opts.snapshot_dir : opts.demo_dir;
      target = std::filesystem::path{args[++i]};
      continue;
    }
    return Error{ErrorCode::kInvalidArgument, "неизвестный аргумент: " + std::string{arg}};
  }
  if (!mode) {
    return Error{ErrorCode::kInvalidArgument, "не указан режим"};
  }
  opts.mode = *mode;
  return opts;
}

}  // namespace sk::ingest

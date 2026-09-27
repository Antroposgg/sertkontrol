#include "demo_pair.hpp"

#include <string>
#include <utility>

#include "demo_source.hpp"

namespace sk::ingest {

namespace {

/// Регистрирует, собирает и возвращает версию. Публикация — отдельно: для N+1 сначала нужен diff.
Result<BuildResult> build_demo_version(RegistryDb& db, const std::filesystem::path& source_file,
                                       const std::filesystem::path& snapshot_dir, std::uint64_t version,
                                       std::int16_t stage) {
  auto source = DemoTsvSource::open(source_file);
  if (!source) {
    return source.error();
  }
  const VersionInfo info{.version = version,
                         .source = std::string{DemoTsvSource::name()},
                         .source_date = source.value().source_date(),
                         .file_path = snapshot_file(snapshot_dir, version).string(),
                         .is_demo = true,
                         .demo_stage = stage};
  if (auto r = db.mark_building(info); !r) {
    return r.error();
  }
  auto built = build_snapshot(source.value(), snapshot_dir, version, true);
  if (!built) {
    (void)db.mark_failed(version, built.error().detail);
    return built.error();
  }
  return built;
}

}  // namespace

Result<DemoPairResult> run_demo_pair(RegistryDb& db, const std::filesystem::path& demo_dir,
                                     const std::filesystem::path& snapshot_dir) {
  DemoPairResult out;
  auto base = build_demo_version(db, demo_dir / kDemoBaseFile, snapshot_dir, kDemoBaseVersion, 0);
  if (!base) {
    return base.error();
  }
  out.base = std::move(base).value();
  if (auto r = db.publish(kDemoBaseVersion, out.base, {}, nullptr); !r) {
    return r.error();
  }

  auto next = build_demo_version(db, demo_dir / kDemoNextFile, snapshot_dir, kDemoNextVersion, 1);
  if (!next) {
    return next.error();
  }
  out.next = std::move(next).value();
  // Diff по только что записанным файлам — тем же, что откроет certd (АРХ §4, поток B, шаг 2).
  const auto before = snapshot::open_snapshot(out.base.file);
  const auto after = snapshot::open_snapshot(out.next.file);
  if (!before || !after) {
    const auto& err = before ? after.error() : before.error();
    (void)db.mark_failed(kDemoNextVersion, err.detail);
    return err;
  }
  out.diff = snapshot::diff(*before.value(), *after.value(),
                            [&out](const snapshot::DocChange& c) { out.changes.push_back(c); });
  if (auto r = db.publish(kDemoNextVersion, out.next, out.changes, &out.diff); !r) {
    return r.error();
  }
  return out;
}

}  // namespace sk::ingest

/// @file memory_ports.hpp
/// @brief Порты `Outbox` и `InboundLog` в памяти — для тестов бота и webhook.
#pragma once

#include <map>
#include <mutex>
#include <set>
#include <utility>
#include <vector>

#include "ports.hpp"

namespace sk::certd {

class MemoryOutbox final : public Outbox {
 public:
  struct Entry {
    maxapi::OutgoingMessage msg{};
    int priority{0};
  };

  drogon::Task<Result<Ok>> enqueue(maxapi::OutgoingMessage msg, int priority) override {
    if (auto v = maxapi::validate(msg); !v) {
      co_return v.error();
    }
    const std::scoped_lock lock(mutex_);
    entries_.push_back({std::move(msg), priority});
    co_return Ok{};
  }

  [[nodiscard]] std::vector<Entry> entries() const {
    const std::scoped_lock lock(mutex_);
    return entries_;
  }
  void clear() {
    const std::scoped_lock lock(mutex_);
    entries_.clear();
  }

 private:
  mutable std::mutex mutex_;
  std::vector<Entry> entries_;
};

class MemoryInboundLog final : public InboundLog {
 public:
  drogon::Task<Result<bool>> first_seen(std::string dedup_key) override {
    const std::scoped_lock lock(mutex_);
    co_return seen_.insert(std::move(dedup_key)).second;
  }
  drogon::Task<void> mark_processed(std::string dedup_key, std::string error) override {
    const std::scoped_lock lock(mutex_);
    processed_[std::move(dedup_key)] = std::move(error);
    co_return;
  }
  [[nodiscard]] std::map<std::string, std::string> processed() const {
    const std::scoped_lock lock(mutex_);
    return processed_;
  }

 private:
  mutable std::mutex mutex_;
  std::set<std::string> seen_;
  std::map<std::string, std::string> processed_;
};

}  // namespace sk::certd

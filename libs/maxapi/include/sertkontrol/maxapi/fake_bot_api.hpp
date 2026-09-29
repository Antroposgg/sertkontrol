/// @file fake_bot_api.hpp
/// @brief `RecordingBotApi` — BotApi в памяти для тестов бота и outbox: записывает вызовы, отдаёт заданные
/// файлы.
#pragma once

#include <map>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "sertkontrol/maxapi/bot_api.hpp"

namespace sk::maxapi {

class RecordingBotApi final : public BotApi {
 public:
  struct Answer {
    std::string callback_id{};
    std::string notification{};
  };

  drogon::Task<Result<BotInfo>> get_me() override {
    const std::scoped_lock lock(mutex_);
    co_return me_;
  }

  drogon::Task<Result<Ok>> send_message(OutgoingMessage msg) override {
    const std::scoped_lock lock(mutex_);
    if (fail_next_) {
      fail_next_ = false;
      co_return Error{fail_code_, "тестовый отказ"};
    }
    if (auto v = validate(msg); !v) {
      co_return v.error();
    }
    messages_.push_back(std::move(msg));
    co_return Ok{};
  }

  drogon::Task<Result<Ok>> answer_callback(std::string callback_id, std::string notification) override {
    const std::scoped_lock lock(mutex_);
    answers_.push_back({std::move(callback_id), std::move(notification)});
    co_return Ok{};
  }

  drogon::Task<Result<std::vector<std::byte>>> download(std::string url, std::size_t max_bytes) override {
    const std::scoped_lock lock(mutex_);
    const auto it = files_.find(url);
    if (it == files_.end()) {
      co_return Error{ErrorCode::kNotFound, "нет файла " + url};
    }
    if (it->second.size() > max_bytes) {
      co_return Error{ErrorCode::kFileTooLarge, "вложение больше лимита"};
    }
    co_return it->second;
  }

  /// Ответ `get_me`: данные бота или ошибка (по умолчанию — бот без username).
  void set_me(Result<BotInfo> me) {
    const std::scoped_lock lock(mutex_);
    me_ = std::move(me);
  }
  void add_file(std::string url, std::vector<std::byte> bytes) {
    const std::scoped_lock lock(mutex_);
    files_[std::move(url)] = std::move(bytes);
  }
  /// Следующая отправка вернёт ошибку `code` (`kInternal` — как 5xx, `kRateLimited` — как 429).
  void fail_next_send(ErrorCode code = ErrorCode::kInternal) {
    const std::scoped_lock lock(mutex_);
    fail_next_ = true;
    fail_code_ = code;
  }
  [[nodiscard]] std::vector<OutgoingMessage> messages() const {
    const std::scoped_lock lock(mutex_);
    return messages_;
  }
  [[nodiscard]] std::vector<Answer> answers() const {
    const std::scoped_lock lock(mutex_);
    return answers_;
  }

 private:
  mutable std::mutex mutex_;
  std::vector<OutgoingMessage> messages_;
  std::vector<Answer> answers_;
  std::map<std::string, std::vector<std::byte>> files_;
  Result<BotInfo> me_{BotInfo{}};
  bool fail_next_{false};
  ErrorCode fail_code_{ErrorCode::kInternal};
};

}  // namespace sk::maxapi

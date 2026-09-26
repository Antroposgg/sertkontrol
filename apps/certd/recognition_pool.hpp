/// @file recognition_pool.hpp
/// @brief Отдельный пул потоков распознавания (ADR-0004): IO-потоки Drogon ждут результат через `co_await`.
#pragma once

#include <drogon/utils/coroutine.h>

#include <atomic>
#include <cstddef>
#include <functional>
#include <span>
#include <vector>

#include <trantor/net/EventLoopThreadPool.h>

#include "sertkontrol_contracts.hpp"

namespace sk::certd {

/// Функция распознавания (C5). В проде — `recog::recognize`; в тестах — подмена.
using Recognizer = std::function<Result<std::vector<recog::Found>>(std::span<const std::byte>,
                                                                   recog::MediaType, const recog::Limits&)>;

/// Пул с ограниченной очередью: больше `max_in_flight` одновременных задач → `kRateLimited`
/// («попробуйте через минуту», АРХ §10).
class RecognitionPool {
 public:
  RecognitionPool(std::size_t threads, std::size_t max_in_flight, Recognizer recognizer);
  RecognitionPool(const RecognitionPool&) = delete;
  RecognitionPool& operator=(const RecognitionPool&) = delete;
  RecognitionPool(RecognitionPool&&) = delete;
  RecognitionPool& operator=(RecognitionPool&&) = delete;
  ~RecognitionPool();

  /// Распознаёт файл в пуле. Параметры по значению — живут в кадре корутины.
  drogon::Task<Result<std::vector<recog::Found>>> recognize(std::vector<std::byte> bytes,
                                                            recog::MediaType type);

 private:
  trantor::EventLoopThreadPool pool_;
  std::size_t max_in_flight_;
  std::atomic<std::size_t> in_flight_{0};
  Recognizer recognizer_;
};

}  // namespace sk::certd

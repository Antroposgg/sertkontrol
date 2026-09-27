#include "recognition_pool.hpp"

#include <utility>

namespace sk::certd {

RecognitionPool::RecognitionPool(std::size_t threads, std::size_t max_in_flight, Recognizer recognizer)
    : pool_(threads, "recog"), max_in_flight_(max_in_flight), recognizer_(std::move(recognizer)) {
  pool_.start();
}

RecognitionPool::~RecognitionPool() {
  for (auto* loop : pool_.getLoops()) {
    loop->quit();
  }
  pool_.wait();
}

drogon::Task<Result<std::vector<recog::Found>>> RecognitionPool::recognize(std::vector<std::byte> bytes,
                                                                           recog::MediaType type) {
  if (in_flight_.fetch_add(1) >= max_in_flight_) {
    in_flight_.fetch_sub(1);
    co_return Error{ErrorCode::kRateLimited, "очередь распознавания заполнена, попробуйте через минуту"};
  }
  auto* loop = pool_.getNextLoop();
  // std::function, а не лямбда в кадре корутины: тип лямбды без связывания ломает unity-сборку (-Wsubobject-linkage).
  std::function<Result<std::vector<recog::Found>>()> task = [this, &bytes, type] {
    return recognizer_(bytes, type, recog::Limits{});
  };
  auto result = co_await drogon::queueInLoopCoro<Result<std::vector<recog::Found>>>(loop, std::move(task));
  in_flight_.fetch_sub(1);
  co_return result;
}

}  // namespace sk::certd

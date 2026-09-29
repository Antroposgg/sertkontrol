#include "curl_http.hpp"

#include <memory>
#include <mutex>

#include <curl/curl.h>

namespace sk::maxapi::detail {

namespace {

void global_init() {
  static std::once_flag once;
  std::call_once(once, [] { curl_global_init(CURL_GLOBAL_DEFAULT); });
}

struct Sink {
  std::string* body{nullptr};
  std::size_t limit{0};
  bool too_large{false};
};

std::size_t on_data(char* data, std::size_t size, std::size_t count, void* user) {
  auto* sink = static_cast<Sink*>(user);
  const auto n = size * count;
  if (sink->body->size() + n > sink->limit) {
    sink->too_large = true;
    return 0;  // прерывает передачу
  }
  sink->body->append(data, n);
  return n;
}

struct CurlDeleter {
  void operator()(CURL* c) const { curl_easy_cleanup(c); }
};
struct SlistDeleter {
  void operator()(curl_slist* l) const { curl_slist_free_all(l); }
};

}  // namespace

bool tls_available() noexcept {
  global_init();
  const auto* info = curl_version_info(CURLVERSION_NOW);
  return info != nullptr && (info->features & CURL_VERSION_SSL) != 0;
}

HttpResponse perform(const HttpRequest& request) {
  global_init();
  HttpResponse out;
  const std::unique_ptr<CURL, CurlDeleter> curl{curl_easy_init()};
  if (!curl) {
    out.error = "curl_easy_init";
    return out;
  }
  std::unique_ptr<curl_slist, SlistDeleter> headers;
  for (const auto& h : request.headers) {
    headers.reset(curl_slist_append(headers.release(), h.c_str()));
  }
  Sink sink{.body = &out.body, .limit = request.max_response_bytes};
  const char* protocols = request.allow_http ? "http,https" : "https";
  auto* c = curl.get();
  // NOLINTBEGIN(cppcoreguidelines-pro-type-vararg): curl_easy_setopt — C API с переменным числом аргументов.
  curl_easy_setopt(c, CURLOPT_URL, request.url.c_str());
  curl_easy_setopt(c, CURLOPT_PROTOCOLS_STR, protocols);
  curl_easy_setopt(c, CURLOPT_REDIR_PROTOCOLS_STR, "https");
  curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, request.follow_redirects ? 1L : 0L);
  curl_easy_setopt(c, CURLOPT_MAXREDIRS, 3L);
  curl_easy_setopt(c, CURLOPT_SSL_VERIFYPEER, 1L);
  curl_easy_setopt(c, CURLOPT_SSL_VERIFYHOST, 2L);
  if (!request.ca_file.empty()) {
    curl_easy_setopt(c, CURLOPT_CAINFO, request.ca_file.c_str());
  }
  curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
  curl_easy_setopt(c, CURLOPT_TIMEOUT_MS, static_cast<long>(request.timeout_seconds * 1000));
  curl_easy_setopt(c, CURLOPT_CUSTOMREQUEST, request.method.c_str());
  if (request.method == "POST") {
    curl_easy_setopt(c, CURLOPT_POSTFIELDS, request.body.data());
    curl_easy_setopt(c, CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(request.body.size()));
  }
  curl_easy_setopt(c, CURLOPT_HTTPHEADER, headers.get());
  curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, on_data);
  curl_easy_setopt(c, CURLOPT_WRITEDATA, &sink);
  const auto code = curl_easy_perform(c);
  curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &out.status);
  // NOLINTEND(cppcoreguidelines-pro-type-vararg)
  out.too_large = sink.too_large;
  if (code != CURLE_OK) {
    out.error = curl_easy_strerror(code);
    if (!out.too_large) {
      out.status = 0;
    }
  }
  return out;
}

}  // namespace sk::maxapi::detail

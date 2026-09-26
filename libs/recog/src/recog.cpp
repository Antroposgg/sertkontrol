/// @file recog.cpp
/// @brief C5 `recog::recognize`: номера из PDF — QR со страниц с конца, затем текстовый слой (ADR-0004).
///
/// Фото и OCR — F7 (этап 4). Все лимиты АРХ §10 проверяются до тяжёлой работы; время — между страницами.
#include <algorithm>
#include <chrono>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#include <ZXing/ReadBarcode.h>
#include <poppler/cpp/poppler-document.h>
#include <poppler/cpp/poppler-global.h>
#include <poppler/cpp/poppler-image.h>
#include <poppler/cpp/poppler-page-renderer.h>
#include <poppler/cpp/poppler-page.h>

#include "sertkontrol_contracts.hpp"

namespace sk::recog {

namespace {

/// Разрешение рендера для QR: в спайке 150 dpi + zxing — 31–34 мс на страницу (АРХ §1).
constexpr double kQrDpi = 150.0;
constexpr double kPointsPerInch = 72.0;
constexpr std::size_t kMaxNumbers = 20;

using Clock = std::chrono::steady_clock;

/// poppler пишет предупреждения о битых PDF в stderr; глушим один раз на процесс.
void silence_poppler() {
  static std::once_flag once;
  std::call_once(once, [] { poppler::set_debug_error_function([](const std::string&, void*) {}, nullptr); });
}

bool is_pdf(std::span<const std::byte> file) {
  // Сигнатура «%PDF-» допускается в первых 1024 байтах (так её ищут просмотрщики).
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): байты файла как текст сигнатуры.
  const std::string_view head{reinterpret_cast<const char*>(file.data()),
                              std::min<std::size_t>(file.size(), 1024)};
  return head.find("%PDF-") != std::string_view::npos;
}

bool is_url(std::string_view s) {
  return s.starts_with("https://") || s.starts_with("http://");
}

struct Collected {
  std::vector<Found> found;
  std::unordered_set<std::string> seen;
  std::optional<std::string> registry_url;

  void add(std::string raw, Source source) {
    const auto canonical = canon::canonicalize(raw);
    if (!canonical || found.size() >= kMaxNumbers || !seen.insert(*canonical).second) {
      return;
    }
    found.push_back({.raw = std::move(raw), .source = source});
  }
};

/// QR со страниц с конца: выписка обычно несёт QR на последней странице. Останавливаемся на первой
/// странице, где QR найден, — бюджет этапа ≤ 100 мс (АРХ §4).
std::optional<Error> scan_qr(const poppler::document& doc, int pages, const Limits& limits,
                             Clock::time_point deadline, Collected& out) {
  poppler::page_renderer renderer;
  renderer.set_image_format(poppler::image::format_gray8);
  ZXing::ReaderOptions options;
  options.setFormats(ZXing::BarcodeFormat::QRCode);
  options.setTryHarder(true);
  for (int i = pages - 1; i >= 0; --i) {
    if (Clock::now() > deadline) {
      return Error{ErrorCode::kNumberNotRecognized, "превышено время распознавания"};
    }
    const std::unique_ptr<poppler::page> page{doc.create_page(i)};
    if (!page) {
      continue;
    }
    const auto rect = page->page_rect();
    const auto w = rect.width() * kQrDpi / kPointsPerInch;
    const auto h = rect.height() * kQrDpi / kPointsPerInch;
    if (w <= 0 || h <= 0 || w * h > static_cast<double>(limits.max_pixels)) {
      return Error{ErrorCode::kFileTooLarge,
                   "страница больше " + std::to_string(limits.max_pixels / 1'000'000) + " Мпикс при 150 dpi"};
    }
    const auto img = renderer.render_page(page.get(), kQrDpi, kQrDpi);
    if (!img.is_valid() || img.format() != poppler::image::format_gray8) {
      continue;
    }
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): байты gray8 из poppler для zxing.
    const auto* pixels = reinterpret_cast<const std::uint8_t*>(img.const_data());
    const ZXing::ImageView view{pixels, img.width(), img.height(), ZXing::ImageFormat::Lum,
                                img.bytes_per_row()};
    const auto barcodes = ZXing::ReadBarcodes(view, options);
    for (const auto& b : barcodes) {
      const auto text = b.text();
      if (is_url(text)) {
        if (!out.registry_url) {
          out.registry_url = text;
        }
        continue;
      }
      for (auto& raw : canon::find_numbers(text, kMaxNumbers)) {
        out.add(std::move(raw), Source::kQr);
      }
    }
    if (!barcodes.empty()) {
      break;
    }
  }
  return std::nullopt;
}

std::optional<Error> scan_text(const poppler::document& doc, int pages, Clock::time_point deadline,
                               Collected& out) {
  for (int i = 0; i < pages; ++i) {
    if (Clock::now() > deadline) {
      return Error{ErrorCode::kNumberNotRecognized, "превышено время распознавания"};
    }
    const std::unique_ptr<poppler::page> page{doc.create_page(i)};
    if (!page) {
      continue;
    }
    const auto bytes = page->text().to_utf8();
    const std::string text{bytes.begin(), bytes.end()};
    for (auto& raw : canon::find_numbers(text, kMaxNumbers)) {
      out.add(std::move(raw), Source::kTextLayer);
    }
  }
  return std::nullopt;
}

Result<std::vector<Found>> recognize_pdf(std::span<const std::byte> file, const Limits& limits) {
  silence_poppler();
  const auto deadline = Clock::now() + limits.timeout;
  // poppler копирует данные не всегда, поэтому буфер живёт до конца функции.
  std::vector<char> data(file.size());
  std::memcpy(data.data(), file.data(), file.size());
  const std::unique_ptr<poppler::document> doc{
      poppler::document::load_from_raw_data(data.data(), static_cast<int>(data.size()))};
  if (!doc) {
    return Error{ErrorCode::kUnsupportedMediaType, "не удалось открыть PDF: файл повреждён или это не PDF"};
  }
  if (doc->is_locked()) {
    return Error{ErrorCode::kUnsupportedMediaType, "PDF защищён паролем"};
  }
  const int pages = doc->pages();
  if (pages > static_cast<int>(limits.max_pages)) {
    return Error{ErrorCode::kFileTooLarge, "в PDF больше " + std::to_string(limits.max_pages) + " страниц"};
  }
  Collected out;
  if (auto err = scan_qr(*doc, pages, limits, deadline, out)) {
    return *err;
  }
  if (auto err = scan_text(*doc, pages, deadline, out)) {
    return *err;
  }
  if (out.found.empty()) {
    return Error{ErrorCode::kNumberNotRecognized,
                 out.registry_url ? "в PDF есть QR со ссылкой на реестр, но нет номера документа"
                                  : "в PDF не найден номер документа"};
  }
  // Ссылка из QR относится к документу — прикладываем её к первому найденному номеру.
  out.found.front().registry_url = out.registry_url;
  return out.found;
}

}  // namespace

Result<std::vector<Found>> recognize(std::span<const std::byte> file, MediaType type, const Limits& limits) {
  if (file.size() > limits.max_bytes) {
    return Error{ErrorCode::kFileTooLarge,
                 "файл больше " + std::to_string(limits.max_bytes / (std::size_t{1024} * 1024)) + " МБ"};
  }
  if (file.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
    return Error{ErrorCode::kFileTooLarge, "файл слишком большой"};
  }
  switch (type) {
    case MediaType::kPdf:
      if (!is_pdf(file)) {
        return Error{ErrorCode::kUnsupportedMediaType, "файл не является PDF"};
      }
      return recognize_pdf(file, limits);
    case MediaType::kJpeg:
    case MediaType::kPng:
      break;
  }
  return Error{ErrorCode::kUnsupportedMediaType,
               "фото и сканы пока не поддерживаются — пришлите PDF-выписку или номер текстом"};
}

}  // namespace sk::recog

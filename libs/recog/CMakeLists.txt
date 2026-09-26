# C5 — распознавание номеров в файлах (R3): PDF → QR → текстовый слой. OCR — этап 4 (F7).
find_package(PkgConfig REQUIRED)
pkg_check_modules(POPPLER_CPP REQUIRED IMPORTED_TARGET poppler-cpp)
find_package(ZXing CONFIG REQUIRED)

add_library(sk_recog STATIC src/recog.cpp)
add_library(sk::recog ALIAS sk_recog)
target_link_libraries(sk_recog PUBLIC sk::contracts PRIVATE sk::canon PkgConfig::POPPLER_CPP ZXing::ZXing)
sk_set_warnings(sk_recog)

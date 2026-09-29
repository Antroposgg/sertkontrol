if(SK_COVERAGE)
  if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    message(FATAL_ERROR "SK_COVERAGE поддержан только для GCC (gcovr)")
  endif()
  # Атомарные счётчики: тесты гоняют код из нескольких потоков (concurrency_test, пулы certd), и гонки обычных
  # счётчиков gcov дают отрицательные значения ветвлений, на которых gcovr останавливается (GCC PR 68080).
  add_compile_options(--coverage -fprofile-update=atomic -O0 -g)
  add_link_options(--coverage -fprofile-update=atomic)
endif()

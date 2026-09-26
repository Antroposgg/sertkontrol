# Предупреждения для всех собственных целей. Подключать через sk_set_warnings(<target>).
function(sk_set_warnings target)
  target_compile_options(${target} PRIVATE
    -Wall -Wextra -Wpedantic
    -Wshadow -Wconversion -Wsign-conversion -Wold-style-cast
    -Wnon-virtual-dtor -Woverloaded-virtual -Wnull-dereference
    -Wdouble-promotion -Wformat=2 -Wimplicit-fallthrough)
  if(SK_WERROR)
    target_compile_options(${target} PRIVATE -Werror)
  endif()
endfunction()

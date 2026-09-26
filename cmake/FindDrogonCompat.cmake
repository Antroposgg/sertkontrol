# DrogonConfig.cmake 1.8.7 из Ubuntu 24.04 (FindJsoncpp.cmake) вызывает exec_program,
# запрещённый политикой CMP0153 при cmake_minimum_required(3.28). Политику возвращаем
# в OLD только на время find_package внутри отдельной области видимости политик.
macro(sk_find_drogon)
  cmake_policy(PUSH)
  cmake_policy(SET CMP0153 OLD)
  find_package(Drogon CONFIG REQUIRED)
  cmake_policy(POP)
endmacro()

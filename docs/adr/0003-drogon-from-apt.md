# ADR-0003: Drogon 1.8.7 из apt

- Статус: принят
- Дата: 2026-09-26
- Связано: АРХ §9, ADR-0011

## Контекст
Лимит `docker build` — 5 минут (КЕЙС §4 п.5). Нужны HTTP-сервер с корутинами, асинхронный PostgreSQL и `LISTEN`.

## Решение
`libdrogon-dev` 1.8.7 из Ubuntu 24.04.

## Альтернативы
Drogon 1.9.x из исходников (+минуты сборки), userver (тяжелее в освоении), Boost.Beast (нет PG и корутинного фреймворка).

## Последствия
Ставится за секунды. `DrogonConfig.cmake` из пакета тянет dev-пакеты sqlite, mariadb, hiredis, yaml-cpp, boost и вызывает `exec_program`, запрещённый политикой CMP0153 в CMake 3.28, — поэтому `find_package` обёрнут в `cmake/FindDrogonCompat.cmake` (политика OLD только на время поиска).

## Когда пересмотреть
Понадобится функциональность 1.9.x.

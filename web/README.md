# web — мини-приложение MAX (React + TypeScript, владелец R4)

**Статус:** этап 0 — каркас: оболочка с навигацией, пометка «Тестовые данные», API-клиент, состояния экранов.

## Назначение и границы
- Делает: экраны «Портфель», «Документ», «Добавить», «Импорт CSV», «Данные» (АРХ §8) поверх REST `/api/v1`.
- НЕ делает: доменную логику — всё через `certd`. Не хранит токены: авторизация — `initData` на каждый запрос (ADR-0006).
- Раздаётся самим `certd` с того же origin — CORS не нужен.

## Ключевые файлы
| Файл | Что внутри |
|---|---|
| `src/App.tsx`, `src/screens.ts` | Оболочка и список экранов |
| `src/api/client.ts` | `createApiClient` — `X-Max-Init-Data`, JSON, ошибки → `ApiError` |
| `src/api/problem.ts` | RFC 9457: `Problem`, `ProblemCode` (= `sk::ErrorCode`), `toProblem`, `problemMessage` |
| `src/max/bridge.ts` | `getWebApp`, `getInitData` — доступ к MAX Bridge, безопасный вне MAX |
| `src/components/StateView.tsx` | Обязательные состояния «загрузка / пусто / ошибка + повторить» |
| `src/components/DemoBadge.tsx` | Пометка тестовых данных |

## Куда добавлять экран
1. Строка в `src/screens.ts`.
2. Компонент в `src/screens/<Имя>.tsx`; данные — через `ApiClient`, отображение — через `StateView` (все три состояния обязательны — критерий UX КЕЙС §5.2).
3. Тест `*.test.tsx` рядом.

## Команды
```bash
cd web && npm ci && npm run dev          # разработка, прокси /api → localhost:8080
scripts/ci/web.sh                         # lint, tsc, тесты с покрытием ≥ 70% (дважды), build
```

## Зависимости
Версии зафиксированы в `package-lock.json`. Node ≥ 24 ([ADR-0007](../docs/adr/0007-node-24-lts.md)). `@maxhub/max-ui` и скрипт MAX Bridge добавляются на этапе 1 после проверки лицензии и сверки с документацией MAX.

## Ограничения
Экраны — заглушки до этапов 1–2; `openCodeReader` (F10) — этап 4.

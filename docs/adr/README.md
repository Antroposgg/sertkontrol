# Архитектурные решения (ADR)

Любое отклонение от [АРХ](../ARCHITECTURE.md) оформляется здесь до того, как попадёт в код.
Шаблон — [`template.md`](template.md). Номер — следующий свободный; принятые ADR не переписываются,
а заменяются новым со ссылкой «Заменяет ADR-NNNN».

| ADR | Решение | Статус | Источник |
|---|---|---|---|
| [0001](0001-registry-in-mmap-snapshot.md) | Реестр — в собственном неизменяемом снапшоте (mmap) | принят | АРХ §9 |
| [0002](0002-two-processes.md) | Два процесса: `certd` и `ingest` | принят | АРХ §9 |
| [0003](0003-drogon-from-apt.md) | Drogon 1.8.7 из apt | принят | АРХ §9 |
| [0004](0004-recognition-order.md) | Распознавание: QR → текстовый слой → OCR в отдельном пуле | принят | АРХ §9 |
| [0005](0005-queues-in-postgres.md) | Очереди, таймеры и outbox — в PostgreSQL | принят | АРХ §9 |
| [0006](0006-initdata-auth.md) | Авторизация — проверка initData на каждый запрос | принят | АРХ §9 |
| [0007](0007-node-24-lts.md) | Node.js 24 LTS вместо 20 | принят | этап 0 |
| [0008](0008-contracts-module.md) | Контрактный заголовок — модуль `libs/contracts` | принят | этап 0 |
| [0009](0009-local-run-without-secrets.md) | Локальный запуск без секретов: PostgreSQL `trust` во внутренней сети | принят | этап 0 |
| [0010](0010-explicit-member-initializers.md) | Явные `{}` у полей агрегатов; `readability-redundant-member-init` выключена | принят | этап 0 |
| [0011](0011-apt-mirror-build-arg.md) | Зеркало apt в Docker-сборке — аргумент `APT_MIRROR` | принят | этап 0 |
| [0012](0012-mintsifry-root-ca.md) | Корневой сертификат Минцифры в образе certd | принят | этап 1 |
| [0013](0013-dev-auth-without-max.md) | Мини-приложение без MAX при локальном запуске — dev-пользователь | принят | этап 1 |
| [0014](0014-history-and-demo-endpoints.md) | История — `GET /history?number=`; `simulate-update` → `200 {notified}`; `POST /demo/reset` | принят | этап 2 |

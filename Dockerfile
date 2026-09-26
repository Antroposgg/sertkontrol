# syntax=docker/dockerfile:1
# Образ certd + ingest по скелету АРХ §10. Отличия от скелета: node:24 (ADR-0007),
# пакеты из docker/apt-*.txt (один список для образа и CI), тесты не собираются,
# процесс работает не от root, зеркало apt задаётся аргументом (ADR-0011).

# Официальное зеркало Ubuntu. По умолчанию — в РФ (жюри и VPS в РФ): втрое быстрее archive.ubuntu.com.
# Подписи пакетов apt проверяет независимо от зеркала. Переопределение: --build-arg APT_MIRROR=...
ARG APT_MIRROR=http://mirror.yandex.ru/ubuntu

# ───── Сборка C++ ─────
FROM ubuntu:24.04 AS build
ENV DEBIAN_FRONTEND=noninteractive
ARG APT_MIRROR
RUN sed -i "s|http://archive.ubuntu.com/ubuntu|${APT_MIRROR}|g; s|http://security.ubuntu.com/ubuntu|${APT_MIRROR}|g" \
      /etc/apt/sources.list.d/ubuntu.sources
COPY docker/apt-build-deps.txt /tmp/apt-build-deps.txt
RUN apt-get update \
    && grep -vE '^\s*(#|$)' /tmp/apt-build-deps.txt | xargs apt-get install -y --no-install-recommends \
    && rm -rf /var/lib/apt/lists/*
COPY CMakeLists.txt CMakePresets.json /src/
COPY cmake /src/cmake
COPY libs /src/libs
COPY apps /src/apps
RUN cmake -S /src -B /b --preset docker && cmake --build /b

# ───── Сборка мини-приложения ─────
FROM node:24-slim AS web
WORKDIR /web
COPY web/package.json web/package-lock.json ./
RUN npm ci --no-audit --no-fund
COPY web ./
RUN npm run build

# ───── Рантайм ─────
FROM ubuntu:24.04
ENV DEBIAN_FRONTEND=noninteractive
ARG APT_MIRROR
RUN sed -i "s|http://archive.ubuntu.com/ubuntu|${APT_MIRROR}|g; s|http://security.ubuntu.com/ubuntu|${APT_MIRROR}|g" \
      /etc/apt/sources.list.d/ubuntu.sources
COPY docker/apt-runtime-deps.txt /tmp/apt-runtime-deps.txt
RUN apt-get update \
    && grep -vE '^\s*(#|$)' /tmp/apt-runtime-deps.txt | xargs apt-get install -y --no-install-recommends \
    && rm -rf /var/lib/apt/lists/* /tmp/apt-runtime-deps.txt \
    && useradd --system --uid 10001 --no-create-home --shell /usr/sbin/nologin sertkontrol \
    && mkdir -p /data/snapshots /data/sources \
    && chown sertkontrol:sertkontrol /data/snapshots /data/sources
COPY --from=build /b/apps/certd/certd /b/apps/ingest/ingest /usr/local/bin/
COPY --from=web /web/dist /srv/app
COPY data/demo /opt/sertkontrol/demo
USER sertkontrol
EXPOSE 8080
ENTRYPOINT ["certd"]

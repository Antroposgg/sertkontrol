# Образ для локального прогона ворот-проверки теми же скриптами, что и CI.
# Сборка: docker build -f docker/dev.Dockerfile -t sertkontrol-dev .
FROM ubuntu:24.04
COPY docker/apt-build-deps.txt docker/apt-dev-tools.txt /tmp/apt/
COPY scripts/ci/install-deps.sh /tmp/apt/
RUN mkdir -p /tmp/apt/docker && mv /tmp/apt/*.txt /tmp/apt/docker/ \
    && mkdir -p /tmp/apt/scripts/ci && mv /tmp/apt/install-deps.sh /tmp/apt/scripts/ci/ \
    && bash /tmp/apt/scripts/ci/install-deps.sh build dev \
    && rm -rf /var/lib/apt/lists/* /tmp/apt
RUN git config --system --add safe.directory '*'
WORKDIR /src

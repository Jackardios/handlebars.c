FROM ubuntu:24.04

# Установка зависимостей для сборки
RUN apt-get update && apt-get install -y \
    autoconf \
    automake \
    libtool \
    pkg-config \
    make \
    gcc \
    flex \
    bison \
    re2c \
    gperf \
    check \
    libtalloc-dev \
    libpcre3-dev \
    libjson-c-dev \
    libyaml-dev \
    liblmdb-dev \
    bats \
    valgrind \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# Копируем исходники
COPY . .

# Сборка проекта
RUN autoreconf -i \
    && ./configure --disable-Werror --enable-valgrind \
    && make

# По умолчанию запускаем тесты
CMD ["make", "check"]

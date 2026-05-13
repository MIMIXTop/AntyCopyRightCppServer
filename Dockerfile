FROM ubuntu:25.04 AS build

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    ca-certificates \
    cmake \
    git \
    ninja-build \
    pkg-config \
    libboost-json-dev \
    libboost-thread-dev \
    libboost-url-dev \
    libssl-dev \
    libicu-dev \
    libpqxx-dev \
    libpoppler-cpp-dev \
    zlib1g-dev \
    libbz2-dev \
    liblzma-dev \
    libzstd-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY . .

RUN cmake -S . -B build -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_TESTING=OFF \
    && cmake --build build --target AntyCopyRightCppServer -j 6

FROM ubuntu:25.04 AS runtime

ENV DEBIAN_FRONTEND=noninteractive
ENV SERVER_ADDRESS=0.0.0.0
ENV SERVER_PORT=8080

RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates \
    libboost-json-dev \
    libboost-thread-dev \
    libboost-url-dev \
    libssl-dev \
    libicu-dev \
    libpqxx-dev \
    libpoppler-cpp-dev \
    zlib1g \
    libbz2-1.0 \
    liblzma5 \
    libzstd1 \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY --from=build /src/build/AntyCopyRightCppServer /app/AntyCopyRightCppServer

RUN useradd --create-home --shell /usr/sbin/nologin appuser \
    && chown -R appuser:appuser /app

USER appuser
EXPOSE 8080

CMD ["/app/AntyCopyRightCppServer"]

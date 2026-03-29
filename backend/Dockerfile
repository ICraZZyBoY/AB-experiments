FROM ghcr.io/userver-framework/ubuntu-22.04-userver:latest AS build

WORKDIR /app

COPY CMakeLists.txt ./
COPY src ./src

RUN cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
    && cmake --build build --config Release -j"$(nproc)"

FROM ghcr.io/userver-framework/ubuntu-22.04-userver:latest

WORKDIR /app

COPY --from=build /app/build/ab_experiments_backend /app/ab_experiments_backend
COPY config /app/config

EXPOSE 8080 8081

CMD ["/app/ab_experiments_backend", "--config", "/app/config/static_config.yaml"]

# Build the server in Release mode.
FROM ubuntu:24.04 AS build
RUN apt-get update \
    && apt-get install -y --no-install-recommends build-essential cmake git ca-certificates \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY . .
RUN cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
    && cmake --build build --target marketsim_server -j"$(nproc)"

# Ship only the binary, its config and the web UI.
FROM ubuntu:24.04
WORKDIR /app
COPY --from=build /src/build/marketsim_server ./marketsim_server
COPY config ./config
COPY web ./web
EXPOSE 8080
CMD ["./marketsim_server", "8080", "config/symbols.json"]

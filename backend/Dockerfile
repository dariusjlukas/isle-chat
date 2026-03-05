FROM alpine:3.21 AS builder

RUN apk add --no-cache \
    build-base \
    cmake \
    git \
    openssl-dev \
    postgresql-dev \
    pkgconf \
    zlib-dev \
    linux-headers

WORKDIR /app

# Build libpqxx from source (not available in Alpine 3.21 repos)
RUN git clone --depth 1 --branch 7.9.2 https://github.com/jtv/libpqxx.git /tmp/libpqxx && \
    cd /tmp/libpqxx && mkdir build && cd build && \
    cmake .. -DCMAKE_BUILD_TYPE=Release -DSKIP_BUILD_TEST=ON -DBUILD_SHARED_LIBS=ON && \
    make -j$(nproc) && make install && \
    rm -rf /tmp/libpqxx

# Clone dependencies
RUN mkdir -p libs && \
    git clone --depth 1 --recurse-submodules https://github.com/uNetworking/uWebSockets.git libs/uWebSockets && \
    git clone --depth 1 --branch v3.11.3 https://github.com/nlohmann/json.git libs/json

# Copy source
COPY CMakeLists.txt .
COPY src/ src/

# Build
RUN mkdir build && cd build && \
    cmake .. -DCMAKE_BUILD_TYPE=Release && \
    make -j$(nproc)

# Runtime stage
FROM alpine:3.21

RUN apk add --no-cache \
    libssl3 \
    libpq \
    libstdc++ \
    libgcc

WORKDIR /app
COPY --from=builder /app/build/chat-server .
COPY --from=builder /usr/local/lib/libpqxx*.so* /usr/local/lib/
RUN ldconfig /usr/local/lib 2>/dev/null || true
RUN mkdir -p /data/uploads

EXPOSE 9001
CMD ["./chat-server"]

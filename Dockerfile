FROM debian:bookworm-slim AS build
RUN apt-get update && apt-get install -y --no-install-recommends cmake ninja-build g++ && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY CMakeLists.txt ./
COPY include include
COPY src src
COPY apps apps
COPY graphx.yaml ./graphx.yaml
RUN cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DGRAPHX_BUILD_TESTS=OFF \
 && cmake --build build

FROM debian:bookworm-slim
RUN apt-get update && apt-get install -y --no-install-recommends iproute2 && rm -rf /var/lib/apt/lists/*
RUN mkdir /captures && chown 65532:65532 /captures
COPY --from=build /src/build/graphx-generator /usr/local/bin/
COPY --from=build /src/build/graphx-transform /usr/local/bin/
COPY --from=build /src/build/graphx-sink /usr/local/bin/
COPY --from=build /src/build/graphx /usr/local/bin/
COPY --from=build /src/graphx.yaml /etc/graphx/graphx.yaml
ENV GRAPHX_CONFIG=/etc/graphx/graphx.yaml
USER 65532:65532
ENTRYPOINT ["/usr/local/bin/graphx-generator"]

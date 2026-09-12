# This image is a local verification tool, not a production runtime image.
FROM gcc:15-bookworm@sha256:9ca91b05c7b07d2979f16413e8b2cd6ec8a7c80ffca4121ccab0aeba33f90460 AS gcc-toolchain

FROM node:24-bookworm-slim@sha256:ba849c60be29959425b8734d57b8b4b7d56f98edd9504c9af091d5281095a71e AS node-runtime

FROM ubuntu:24.04@sha256:33ceb71981b602c1a7443a53469e4dba065f7503eab3078a2d7a57a2ab987517

ENV DEBIAN_FRONTEND=noninteractive

COPY --from=gcc-toolchain /usr/local/ /usr/local/
COPY --from=node-runtime /usr/local/ /usr/local/

RUN apt-get update \
 && apt-get install -y --no-install-recommends bash ca-certificates curl gnupg \
 && rm -rf /var/lib/apt/lists/*

# A private-organization root CA may be supplied at build time with:
#   --secret id=graphx_ca,src=/absolute/path/to/company-root-ca.crt
# Existing organization installers may also be supplied as a reviewed local file:
#   --secret id=graphx_cert_installer,src=/absolute/path/to/install-certs.sh
COPY docker/install-build-trust.sh /usr/local/libexec/graphx-install-build-trust
ARG GRAPHX_BUILD_TRUST_FINGERPRINT=graphx-trust-v1-none
RUN --mount=type=secret,id=graphx_ca,required=false \
    --mount=type=secret,id=graphx_cert_installer,required=false \
    /usr/bin/bash /usr/local/libexec/graphx-install-build-trust

RUN curl --fail --silent --show-error \
      https://apt.llvm.org/llvm-snapshot.gpg.key \
      --output /tmp/llvm-snapshot.gpg.key \
 && test "$(gpg --show-keys --with-colons /tmp/llvm-snapshot.gpg.key \
      | awk -F: '$1 == "fpr" { print $10; exit }')" \
      = "6084F3CF814B57C1CF12EFD515CF4D18AF4F7421" \
 && gpg --dearmor < /tmp/llvm-snapshot.gpg.key \
      > /usr/share/keyrings/apt.llvm.org.gpg \
 && echo "deb [signed-by=/usr/share/keyrings/apt.llvm.org.gpg] https://apt.llvm.org/noble/ llvm-toolchain-noble-21 main" \
      > /etc/apt/sources.list.d/llvm-21.list \
 && rm /tmp/llvm-snapshot.gpg.key \
 && apt-get update \
 && apt-get install -y --no-install-recommends \
      build-essential clang-21 clang-tools-21 clang-format-21 clang-tidy-21 \
      cmake cppcheck git libclang-rt-21-dev libfuzzer-21-dev libssl-dev llvm-21 \
      ninja-build openssl pkg-config python3 tshark xxd \
 && rm -rf /var/lib/apt/lists/*

ENV NODE_EXTRA_CA_CERTS=/etc/ssl/certs/ca-certificates.crt
ENV GRAPHX_BUILD_JOBS=4

WORKDIR /workspace
COPY . .

ENTRYPOINT ["/usr/bin/bash", "/workspace/scripts/linux-container-entrypoint.sh"]
CMD ["tls"]

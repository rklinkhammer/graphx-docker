# syntax=docker/dockerfile:1.7

# This image is a local verification tool, not a production runtime image.
FROM node:24-bookworm-slim@sha256:ba849c60be29959425b8734d57b8b4b7d56f98edd9504c9af091d5281095a71e AS node-runtime

FROM ubuntu:24.04@sha256:33ceb71981b602c1a7443a53469e4dba065f7503eab3078a2d7a57a2ab987517

ENV DEBIAN_FRONTEND=noninteractive

COPY --from=node-runtime /usr/local/ /usr/local/

RUN apt-get update \
 && apt-get install -y --no-install-recommends \
      bash build-essential ca-certificates clang-18 clang-format-18 clang-tidy-18 \
      cmake cppcheck curl git libssl-dev ninja-build openssl pkg-config xxd \
      libclang-rt-18-dev python3 tshark \
 && rm -rf /var/lib/apt/lists/*

# A public/private-organization root CA may be supplied at build time with:
#   --secret id=graphx_ca,src=/absolute/path/to/company-root-ca.crt
# The CA is intentionally copied into this local verifier image because npm,
# CMake FetchContent, and other verification tools may need it at runtime.
RUN --mount=type=secret,id=graphx_ca,required=false \
    if test -f /run/secrets/graphx_ca; then \
      install -m 0644 /run/secrets/graphx_ca \
        /usr/local/share/ca-certificates/graphx-local-root-ca.crt; \
      update-ca-certificates; \
    fi

# Existing organization installers can instead be supplied as a local file:
#   --secret id=graphx_cert_installer,src=/absolute/path/to/install-certs.sh
# The script is never copied into an image layer. Only its intended certificate
# store changes remain in this local verifier image.
RUN --mount=type=secret,id=graphx_cert_installer,required=false \
    if test -f /run/secrets/graphx_cert_installer; then \
      /usr/bin/bash /run/secrets/graphx_cert_installer; \
      update-ca-certificates; \
    fi

ENV NODE_EXTRA_CA_CERTS=/etc/ssl/certs/ca-certificates.crt
ENV GRAPHX_BUILD_JOBS=4

WORKDIR /workspace
COPY . .

ENTRYPOINT ["/usr/bin/bash", "/workspace/scripts/linux-container-entrypoint.sh"]
CMD ["tls"]

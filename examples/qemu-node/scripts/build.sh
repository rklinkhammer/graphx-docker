#!/usr/bin/env bash
set -euo pipefail

example_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
repo_dir="$(cd "$example_dir/../.." && pwd)"
source "$repo_dir/scripts/configure-build-trust.sh"

command -v docker >/dev/null || { echo "Missing required command: docker" >&2; exit 2; }
docker info >/dev/null 2>&1 || { echo "Docker is not available" >&2; exit 2; }

builder_image="${GRAPHX_QEMU_BUILDER_IMAGE:-graphx-qemu-builder:2025.02.17}"
build=(docker build --file "$example_dir/build-env/Dockerfile"
  --build-arg "GRAPHX_BUILD_TRUST_FINGERPRINT=$GRAPHX_BUILD_TRUST_FINGERPRINT"
  --tag "$builder_image")
if [[ -n "${GRAPHX_CA_CERT:-}" ]]; then
  build+=(--secret "id=graphx_ca,src=$GRAPHX_CA_CERT")
fi
if [[ -n "${GRAPHX_CERT_INSTALL_SCRIPT:-}" ]]; then
  build+=(--secret "id=graphx_cert_installer,src=$GRAPHX_CERT_INSTALL_SCRIPT")
fi
build+=("$repo_dir")
"${build[@]}"

mkdir -p "$example_dir/output" "$example_dir/dl"
docker run --rm \
  --user "$(id -u):$(id -g)" \
  --env HOME=/tmp \
  --volume "$repo_dir:/workspace" \
  --workdir /workspace \
  "$builder_image" -lc '
    set -euo pipefail
    example=/workspace/examples/qemu-node
    make -C /opt/buildroot O="$example/output" \
      BR2_EXTERNAL="$example/buildroot-external" graphx_qemu_x86_64_defconfig
    make -C /opt/buildroot O="$example/output" \
      BR2_EXTERNAL="$example/buildroot-external" \
      BR2_DL_DIR="$example/dl" graphx-qemu-node-dirclean
    make -C /opt/buildroot O="$example/output" \
      BR2_EXTERNAL="$example/buildroot-external" \
      BR2_DL_DIR="$example/dl" "-j${GRAPHX_QEMU_BUILD_JOBS:-4}"
  '

for image in bzImage rootfs.cpio.gz; do
  [[ -s "$example_dir/output/images/$image" ]] || {
    echo "Build completed without expected image: $image" >&2
    exit 1
  }
done
echo "QEMU guest images: $example_dir/output/images"

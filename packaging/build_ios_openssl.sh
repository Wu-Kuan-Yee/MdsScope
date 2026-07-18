#!/bin/bash
# Build OpenSSL static libraries for iOS (arm64 device).
# Run once; outputs libssl.a and libcrypto.a into packaging/ios_openssl/.
# The main CMakeLists.txt auto-detects them for SSH support.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
DEST="${SCRIPT_DIR}/ios_openssl"
OPENSSL_VERSION="${1:-3.4.0}"
SRC_DIR="${SCRIPT_DIR}/.openssl-src-${OPENSSL_VERSION}"

if [ -f "${DEST}/libssl.a" ] && [ -f "${DEST}/libcrypto.a" ]; then
    echo "OpenSSL already built: ${DEST}/"
    exit 0
fi

# Download
if [ ! -d "${SRC_DIR}" ]; then
    TAR="openssl-${OPENSSL_VERSION}.tar.gz"
    URL="https://www.openssl.org/source/${TAR}"
    echo "Downloading ${URL} ..."
    curl -fSL -o "/tmp/${TAR}" "${URL}"
    tar xzf "/tmp/${TAR}" -C "${SCRIPT_DIR}"
    mv "${SCRIPT_DIR}/openssl-${OPENSSL_VERSION}" "${SRC_DIR}"
fi

export IPHONEOS_DEPLOYMENT_TARGET="15.0"
export SDK_PATH="$(xcrun --sdk iphoneos --show-sdk-path)"

cd "${SRC_DIR}"

# Build for iOS arm64 device
echo "Configuring OpenSSL for iOS arm64 ..."
./Configure ios64-xcrun no-shared no-dso no-hw no-engine no-tests \
    --prefix="${DEST}" --openssldir="${DEST}"

echo "Building ..."
make -j"$(sysctl -n hw.ncpu 2>/dev/null || echo 4)"

echo "Installing ..."
make install_sw

# Flat structure expected by CMakeLists.txt
cp "${DEST}/lib/libssl.a" "${DEST}/libssl.a"
cp "${DEST}/lib/libcrypto.a" "${DEST}/libcrypto.a"

echo ""
echo "Done. Libraries:"
ls -lh "${DEST}/libssl.a" "${DEST}/libcrypto.a"

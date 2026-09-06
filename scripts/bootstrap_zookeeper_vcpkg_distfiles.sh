#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VERSION="3.8.5"
VCPKG_ROOT="${VCPKG_ROOT:-$ROOT/toolchains/vcpkg-tinyimx}"
DOWNLOAD_DIR="$VCPKG_ROOT/downloads"

SOURCE_NAME="apache-zookeeper-${VERSION}.tar.gz"
BINARY_NAME="apache-zookeeper-${VERSION}-bin.tar.gz"

SOURCE_URL="https://archive.apache.org/dist/zookeeper/zookeeper-${VERSION}/${SOURCE_NAME}"
BINARY_URL="https://archive.apache.org/dist/zookeeper/zookeeper-${VERSION}/${BINARY_NAME}"

SOURCE_SHA512="61c05f6064797994dc25c42df35d67d2c3839fd59a496924852a4d78b492b06746c8eb5445edb63cbc0107ef2b8b31babf23488f96a52b00682cd2e9b61be339"
BINARY_SHA512="ab9bf90649df19d8fd8378f2e8d9159bc8528d8e4c166a93d9fa4a9c98e39ee9de0279cc9dc58cd6d593141c0a45576d0df9db47d143d63951598a43efdc0a30"

fail() {
  echo "[ZooKeeper bootstrap][FAIL] $*" >&2
  exit 1
}

verify_sha512() {
  local path="$1"
  local expected="$2"
  local actual
  actual="$(sha512sum "$path" | awk '{print $1}')"
  [[ "$actual" == "$expected" ]]
}

download_verified() {
  local name="$1"
  local url="$2"
  local expected="$3"
  local path="$DOWNLOAD_DIR/$name"
  local tmp="$path.part.$$"

  if [[ -f "$path" ]]; then
    if verify_sha512 "$path" "$expected"; then
      echo "[ZooKeeper bootstrap][PASS] cached $name"
      return 0
    fi
    echo "[ZooKeeper bootstrap][WARN] invalid cached checksum, replacing: $name"
    rm -f "$path"
  fi

  command -v curl >/dev/null 2>&1 || fail "curl is required"
  echo "[ZooKeeper bootstrap] downloading $url"
  rm -f "$tmp"
  curl -fL --retry 3 --retry-delay 2 "$url" -o "$tmp"

  if ! verify_sha512 "$tmp" "$expected"; then
    rm -f "$tmp"
    fail "SHA512 mismatch for $name"
  fi

  mv "$tmp" "$path"
  chmod 0644 "$path"
  echo "[ZooKeeper bootstrap][PASS] downloaded and verified $name"
}

[[ -d "$VCPKG_ROOT" ]] || fail "VCPKG_ROOT does not exist: $VCPKG_ROOT"
command -v sha512sum >/dev/null 2>&1 || fail "sha512sum is required"
mkdir -p "$DOWNLOAD_DIR"

echo "=============================================="
echo " TinyIMX ZooKeeper vcpkg Distfile Bootstrap"
echo "=============================================="
echo "version      : $VERSION"
echo "vcpkg root   : $VCPKG_ROOT"
echo "download dir : $DOWNLOAD_DIR"

download_verified "$SOURCE_NAME" "$SOURCE_URL" "$SOURCE_SHA512"
download_verified "$BINARY_NAME" "$BINARY_URL" "$BINARY_SHA512"

echo "=============================================="
echo "[ZooKeeper bootstrap PASS] distfiles ready"
echo "=============================================="

#!/usr/bin/env bash
set -Eeuo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd -- "${SCRIPT_DIR}/.." && pwd)"

VERSION="${TINYIMX_ROCKETMQ_SERVER_VERSION:-5.5.1}"
BASE="${TINYIMX_ROCKETMQ_LOCAL_DIR:-${ROOT_DIR}/.local/rocketmq-${VERSION}}"
DIST_DIR="$BASE/dist"
RUN_DIR="$BASE/run"
UNPACK_DIR="$BASE/unpack"
ZIP="$BASE/rocketmq-all-${VERSION}-bin-release.zip"
SHA512_FILE="${ZIP}.sha512"

NAMESRV="${TINYIMX_ROCKETMQ_NAMESRV:-127.0.0.1:9876}"
PROXY_ENDPOINT="${TINYIMX_ROCKETMQ_ENDPOINT:-127.0.0.1:8081}"
CLUSTER="${TINYIMX_ROCKETMQ_CLUSTER:-DefaultCluster}"
TOPIC="${TINYIMX_ROCKETMQ_TOPIC:-tinyimx-message-events}"

PRIMARY_DOWNLOAD_URL="https://dist.apache.org/repos/dist/release/rocketmq/${VERSION}/rocketmq-all-${VERSION}-bin-release.zip"
ARCHIVE_DOWNLOAD_URL="https://archive.apache.org/dist/rocketmq/${VERSION}/rocketmq-all-${VERSION}-bin-release.zip"
DOWNLOAD_URL="${TINYIMX_ROCKETMQ_DOWNLOAD_URL:-$PRIMARY_DOWNLOAD_URL}"
SHA512_URL="${TINYIMX_ROCKETMQ_SHA512_URL:-${DOWNLOAD_URL}.sha512}"

log() { printf '[local-rocketmq] %s\n' "$*"; }
fail() { log "FAIL: $*"; exit 1; }

ensure_java_home() {
  local java_cmd java_real

  if [[ -n "${JAVA_HOME:-}" && -x "${JAVA_HOME}/bin/java" ]]; then
    export JAVA_HOME
    return 0
  fi

  java_cmd="$(command -v java 2>/dev/null || true)"
  [[ -n "$java_cmd" ]] || fail "java not found; RocketMQ 5.5.1 requires a JDK/JRE"

  java_real="$(readlink -f "$java_cmd" 2>/dev/null || true)"
  [[ -n "$java_real" ]] || fail "failed to resolve java executable: $java_cmd"

  JAVA_HOME="$(cd "$(dirname "$java_real")/.." && pwd)"
  export JAVA_HOME
  [[ -x "${JAVA_HOME}/bin/java" ]] || fail "resolved JAVA_HOME has no bin/java: $JAVA_HOME"
}

validate_topic_name() {
  [[ ${#TOPIC} -ge 1 && ${#TOPIC} -le 128 ]] || \
    fail "invalid RocketMQ topic length: $TOPIC"
  [[ "$TOPIC" =~ ^[A-Za-z0-9_-]+$ ]] || \
    fail "invalid RocketMQ topic '$TOPIC'; allowed characters: [A-Za-z0-9_-]"
}

port_open() {
  local host=${1%:*}
  local port=${1##*:}
  timeout 1 bash -c "</dev/tcp/$host/$port" >/dev/null 2>&1
}

curl_fetch() {
  local url=$1
  local dest=$2
  local allow_resume=${3:-0}
  local tmp="${dest}.part"
  local attempt rc

  mkdir -p "$(dirname "$dest")"

  for attempt in 1 2 3 4 5; do
    log "download attempt ${attempt}/5: $url"
    set +e
    if [[ "$allow_resume" == "1" && -s "$tmp" ]]; then
      curl --http1.1 --fail --location --connect-timeout 20 \
        --retry 2 --retry-all-errors --retry-delay 2 \
        --continue-at - --output "$tmp" "$url"
      rc=$?
      if [[ $rc -eq 33 ]]; then
        # Server rejected byte-range resume. Retry this attempt from scratch.
        rm -f "$tmp"
        curl --http1.1 --fail --location --connect-timeout 20 \
          --retry 2 --retry-all-errors --retry-delay 2 \
          --output "$tmp" "$url"
        rc=$?
      fi
    else
      rm -f "$tmp"
      curl --http1.1 --fail --location --connect-timeout 20 \
        --retry 2 --retry-all-errors --retry-delay 2 \
        --output "$tmp" "$url"
      rc=$?
    fi
    set -e

    if [[ $rc -eq 0 && -s "$tmp" ]]; then
      mv -f "$tmp" "$dest"
      return 0
    fi

    log "WARN: download failed rc=$rc; retrying after $((attempt * 2))s"
    sleep $((attempt * 2))
  done

  rm -f "$tmp"
  return 1
}

extract_expected_sha512() {
  local checksum_file=$1
  local contiguous grouped

  # Common GNU/BSD style: one contiguous 128-hex digest somewhere in the file.
  contiguous="$(grep -Eio '[0-9a-f]{128}' "$checksum_file" | head -n 1 | tr '[:upper:]' '[:lower:]' || true)"
  if [[ ${#contiguous} -eq 128 ]]; then
    printf '%s\n' "$contiguous"
    return 0
  fi

  # Apache RocketMQ release checksum files use ASF grouped format, e.g.:
  #   rocketmq-all-X.Y.Z-bin-release.zip: 01234567 89ABCDEF ...
  #                                       ...
  # The 128 hex digits are split into 8-char groups across multiple lines.
  # Strip the filename prefix from the first line, concatenate continuation
  # lines, then remove whitespace. Do not strip arbitrary non-hex characters
  # before removing the filename prefix because filenames may contain a-f.
  grouped="$(awk '''
    NR == 1 { sub(/^[^:]*:[[:space:]]*/, "") }
    {
      gsub(/[[:space:]\r\n]/, "")
      printf "%s", $0
    }
    END { printf "\n" }
  ''' "$checksum_file" | tr '[:upper:]' '[:lower:]')"

  if [[ "$grouped" =~ ^[0-9a-f]{128}$ ]]; then
    printf '%s\n' "$grouped"
    return 0
  fi

  return 1
}

verify_distribution_zip() {
  local expected actual

  if [[ ! -s "$SHA512_FILE" ]]; then
    curl_fetch "$SHA512_URL" "$SHA512_FILE" 0 || return 1
  fi

  expected="$(extract_expected_sha512 "$SHA512_FILE" || true)"
  [[ ${#expected} -eq 128 ]] || {
    log "WARN: invalid SHA512 metadata: $SHA512_FILE"
    return 1
  }

  actual="$(sha512sum "$ZIP" | awk '{print tolower($1)}')"
  if [[ "$actual" != "$expected" ]]; then
    log "WARN: SHA512 mismatch for $ZIP"
    log "WARN: expected=$expected"
    log "WARN: actual=$actual"
    return 1
  fi

  unzip -tq "$ZIP" >/dev/null || {
    log "WARN: zip integrity check failed: $ZIP"
    return 1
  }

  return 0
}

download_distribution() {
  local fallback_url fallback_sha

  # Reuse a complete cached archive only after cryptographic verification.
  if [[ -s "$ZIP" ]] && verify_distribution_zip; then
    log "PASS cached binary distribution SHA512 verified"
    return 0
  fi

  rm -f "$ZIP" "$SHA512_FILE" "${ZIP}.part" "${SHA512_FILE}.part"

  if curl_fetch "$DOWNLOAD_URL" "$ZIP" 1 && \
     curl_fetch "$SHA512_URL" "$SHA512_FILE" 0 && \
     verify_distribution_zip; then
    log "PASS binary distribution SHA512 verified"
    return 0
  fi

  # If the default current-release endpoint is unavailable, fall back to ASF archive.
  if [[ "$DOWNLOAD_URL" == "$PRIMARY_DOWNLOAD_URL" ]]; then
    fallback_url="$ARCHIVE_DOWNLOAD_URL"
    fallback_sha="${fallback_url}.sha512"
    log "WARN: primary release endpoint failed; falling back to $fallback_url"
    rm -f "$ZIP" "$SHA512_FILE" "${ZIP}.part" "${SHA512_FILE}.part"
    curl_fetch "$fallback_url" "$ZIP" 1 || return 1
    curl_fetch "$fallback_sha" "$SHA512_FILE" 0 || return 1
    verify_distribution_zip || return 1
    log "PASS archived binary distribution SHA512 verified"
    return 0
  fi

  return 1
}

setup() {
  local command
  for command in java unzip curl sha512sum find; do
    command -v "$command" >/dev/null 2>&1 || fail "missing command: $command"
  done

  mkdir -p "$BASE" "$RUN_DIR"

  if [[ ! -x "$DIST_DIR/bin/mqnamesrv" || ! -x "$DIST_DIR/bin/mqbroker" || ! -x "$DIST_DIR/bin/mqadmin" ]]; then
    download_distribution || fail "failed to download/verify RocketMQ ${VERSION} binary distribution"

    rm -rf "$DIST_DIR" "$UNPACK_DIR"
    mkdir -p "$DIST_DIR" "$UNPACK_DIR"
    unzip -q "$ZIP" -d "$UNPACK_DIR"

    # The official binary release is rooted at
    # rocketmq-all-${VERSION}-bin-release/bin/*.  Do not cap find at depth=2:
    # from UNPACK_DIR, mqnamesrv is normally at depth 3.
    local found home
    found="$(find "$UNPACK_DIR" -type f -path '*/bin/mqnamesrv' -print -quit)"
    [[ -n "$found" ]] || {
      log "archive top-level entries:"
      find "$UNPACK_DIR" -mindepth 1 -maxdepth 3 -printf '  %P\n' | head -n 80 >&2 || true
      fail "mqnamesrv not found in verified RocketMQ binary distribution"
    }

    home="$(cd "$(dirname "$found")/.." && pwd)"
    [[ -f "$home/bin/mqbroker" ]] || fail "mqbroker missing next to discovered mqnamesrv: $home"
    [[ -f "$home/bin/mqadmin" ]] || fail "mqadmin missing next to discovered mqnamesrv: $home"

    cp -a "$home/." "$DIST_DIR/"
    chmod +x "$DIST_DIR"/bin/mqnamesrv "$DIST_DIR"/bin/mqbroker "$DIST_DIR"/bin/mqadmin 2>/dev/null || true
    rm -rf "$UNPACK_DIR"
  fi

  [[ -x "$DIST_DIR/bin/mqnamesrv" ]] || fail "mqnamesrv is not executable after setup"
  [[ -x "$DIST_DIR/bin/mqbroker" ]] || fail "mqbroker is not executable after setup"
  [[ -x "$DIST_DIR/bin/mqadmin" ]] || fail "mqadmin is not executable after setup"

  log "PASS setup complete: $DIST_DIR"
}

start() {
  setup
  ensure_java_home
  export ROCKETMQ_HOME="$DIST_DIR"

  if ! port_open "$NAMESRV"; then
    (
      cd "$DIST_DIR"
      JAVA_OPT_EXT="${TINYIMX_RMQ_NAMESRV_JAVA_OPT:--Xms256m -Xmx256m -Xmn128m}" \
        nohup sh bin/mqnamesrv >"$RUN_DIR/namesrv.log" 2>&1 &
      echo $! >"$RUN_DIR/namesrv.pid"
    )
  fi

  for _ in $(seq 1 60); do
    port_open "$NAMESRV" && break
    sleep .5
  done
  port_open "$NAMESRV" || fail "NameServer did not become ready; see $RUN_DIR/namesrv.log"

  if ! port_open "$PROXY_ENDPOINT"; then
    (
      cd "$DIST_DIR"
      JAVA_OPT_EXT="${TINYIMX_RMQ_BROKER_JAVA_OPT:--Xms512m -Xmx512m -Xmn256m}" \
        nohup sh bin/mqbroker -n "$NAMESRV" --enable-proxy >"$RUN_DIR/broker-proxy.log" 2>&1 &
      echo $! >"$RUN_DIR/broker.pid"
    )
  fi

  for _ in $(seq 1 120); do
    port_open "$PROXY_ENDPOINT" && break
    sleep .5
  done
  port_open "$PROXY_ENDPOINT" || fail "Broker/Proxy endpoint did not become ready; see $RUN_DIR/broker-proxy.log"

  log "PASS started namesrv=$NAMESRV proxy=$PROXY_ENDPOINT"
}

create_topic() {
  local update_output topic_list

  validate_topic_name
  start
  ensure_java_home
  export ROCKETMQ_HOME="$DIST_DIR"

  set +e
  update_output="$(
    cd "$DIST_DIR" &&
    sh bin/mqadmin updateTopic \
      -n "$NAMESRV" \
      -t "$TOPIC" \
      -c "$CLUSTER" \
      -a +message.type=NORMAL 2>&1
  )"
  local update_rc=$?
  set -e
  printf '%s\n' "$update_output"

  if [[ $update_rc -ne 0 ]] || \
     grep -Eq 'SubCommandException|command failed|Caused by:|MQClientException' <<<"$update_output"; then
    fail "mqadmin updateTopic failed for topic: $TOPIC"
  fi

  set +e
  topic_list="$(cd "$DIST_DIR" && sh bin/mqadmin topicList -n "$NAMESRV" 2>&1)"
  local list_rc=$?
  set -e
  [[ $list_rc -eq 0 ]] || {
    printf '%s\n' "$topic_list" >&2
    fail "mqadmin topicList failed after topic creation"
  }

  if grep -Eq 'SubCommandException|command failed|Caused by:|MQClientException' <<<"$topic_list"; then
    printf '%s\n' "$topic_list" >&2
    fail "mqadmin topicList reported an exception"
  fi

  printf '%s\n' "$topic_list" | tr -d '\r' | grep -Fxq "$TOPIC" || {
    printf '%s\n' "$topic_list" >&2
    fail "topic was not observable after updateTopic: $TOPIC"
  }

  log "PASS NORMAL topic ready and observable: $TOPIC"
}

status() {
  printf 'namesrv=%s %s\n' "$NAMESRV" "$(port_open "$NAMESRV" && echo up || echo down)"
  printf 'proxy=%s %s\n' "$PROXY_ENDPOINT" "$(port_open "$PROXY_ENDPOINT" && echo up || echo down)"
  if [[ -x "$DIST_DIR/bin/mqadmin" ]] && port_open "$NAMESRV"; then
    ensure_java_home
    export ROCKETMQ_HOME="$DIST_DIR"
    (cd "$DIST_DIR" && sh bin/mqadmin clusterList -n "$NAMESRV") || true
  fi
}

stop() {
  local f pid
  for f in "$RUN_DIR/broker.pid" "$RUN_DIR/namesrv.pid"; do
    if [[ -f "$f" ]]; then
      pid="$(cat "$f" 2>/dev/null || true)"
      [[ "$pid" =~ ^[0-9]+$ ]] && kill "$pid" 2>/dev/null || true
      rm -f "$f"
    fi
  done

  if [[ -x "$DIST_DIR/bin/mqshutdown" ]]; then
    (cd "$DIST_DIR" && sh bin/mqshutdown broker >/dev/null 2>&1 || true)
    (cd "$DIST_DIR" && sh bin/mqshutdown namesrv >/dev/null 2>&1 || true)
  fi
  log "stopped"
}

clean() {
  stop
  rm -rf "$BASE"
  log "cleaned $BASE"
}

case "${1:-}" in
  setup) setup ;;
  start) start ;;
  create-topic|create_topic) create_topic ;;
  status|health) status ;;
  stop) stop ;;
  clean) clean ;;
  endpoint) echo "$PROXY_ENDPOINT" ;;
  *)
    echo "usage: $0 {setup|start|create-topic|status|stop|clean|endpoint}" >&2
    exit 2
    ;;
esac

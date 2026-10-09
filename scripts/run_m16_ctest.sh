#!/usr/bin/env bash

set +e
set +u
set +o pipefail

ROOT="${TINYIMX_ROOT:-$HOME/projects/TinyIMX_publish}"
BUILD_DIR="${TINYIMX_BUILD_DIR:-build/linux-debug}"

cd "$ROOT" || exit 90

MODE="${1:-unit}"

case "$MODE" in

    unit)
        TARGETS=(
            config_m16_b_tests
            outbox_relay_unit_tests
            m16_crash_window_contract_tests
        )

        LABEL='^unit$'
        ;;

    integration)
        TARGETS=(
            message_outbox_integration_tests
            unread_projection_reader_integration_tests
            unread_projection_cache_integration_tests
        )

        LABEL='^integration$'
        ;;

    list)
        ctest \
            --test-dir "$BUILD_DIR" \
            --print-labels

        exit $?
        ;;

    *)
        echo "usage: $0 {unit|integration|list}" >&2
        exit 64
        ;;
esac

echo "================================================"
echo "TinyIMX M16 CTest"
echo "================================================"
echo "mode=$MODE"
echo "build_dir=$BUILD_DIR"
echo "label=$LABEL"

cmake --build "$BUILD_DIR" \
    --target "${TARGETS[@]}" \
    -j2

BUILD_RC=$?

if [[ "$BUILD_RC" -ne 0 ]]; then
    echo "[FAIL] M16 ${MODE} build"
    exit "$BUILD_RC"
fi

ctest \
    --test-dir "$BUILD_DIR" \
    -L "$LABEL" \
    --output-on-failure \
    -j2

TEST_RC=$?

if [[ "$TEST_RC" -ne 0 ]]; then
    echo "[FAIL] M16 ${MODE} CTest"
    exit "$TEST_RC"
fi

echo
echo "[PASS] M16 ${MODE} CTest"

#!/bin/sh
# PERF-002: experimentally enlarge only ens33 rings, bounded and restored.
set -eu
test "$(cat /sys/class/net/ens33/address)" = '00:0c:29:9a:71:2e'
/diag/ethtool -g ens33
restore() {
    /diag/ethtool -G ens33 rx 256 tx 256
    echo RESTORED
    /diag/ethtool -g ens33
}
trap restore EXIT
trap 'exit 0' TERM INT
/diag/ethtool -G ens33 rx 4096 tx 4096
echo READY_RING_4096
/diag/ethtool -g ens33
sleep 120 &
wait $!

#!/bin/sh
# PERF-002: bounded checksum-offload control; TSO follows its dependency.
set -eu
test "$(cat /sys/class/net/ens33/address)" = '00:0c:29:9a:71:2e'
/diag/ethtool -k ens33 | grep '^tx-checksumming: on$'
/diag/ethtool -k ens33 | grep '^tcp-segmentation-offload: on$'
restore() {
    /diag/ethtool -K ens33 tx on
    /diag/ethtool -K ens33 tso on
    echo RESTORED
    /diag/ethtool -k ens33 | grep -E '^(tcp-segmentation-offload|generic-segmentation-offload|scatter-gather|tx-checksumming):'
}
trap restore EXIT
trap 'exit 0' TERM INT
/diag/ethtool -K ens33 tx off
echo READY_CHECKSUM_OFF
/diag/ethtool -k ens33 | grep -E '^(tcp-segmentation-offload|generic-segmentation-offload|scatter-gather|tx-checksumming):'
sleep 120 &
wait $!

Runtime TLS files live outside Git at `$TINYIMX_M21_STATE_DIR/tls/`.
Use `scripts/m21_generate_dev_tls.sh` only for local acceptance. Replace the
self-signed certificate with CA-issued material before Internet exposure.

"""Verify safe synchronous-send build provenance before SDK cache reuse.

The stamp is produced by the controlled SDK build; this is cache integrity,
not cryptographic attestation of an arbitrary third-party binary. No network,
source mutation, build, or installed-library replacement is performed.
"""
import argparse
import hashlib
import json
import re
from pathlib import Path


def sha(path):
    digest = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(block)
    return digest.hexdigest()


def verify(prefix, fragment, renderer, library=None):
    prefix = Path(prefix).resolve(strict=True)
    metadata = prefix / '.tinyimx-rocketmq-isolated-toolchain'
    fields = {}
    for line in metadata.read_text(encoding='utf-8').splitlines():
        if not line.strip():
            continue
        if '=' not in line:
            raise ValueError('Malformed SDK build metadata')
        key, value = line.split('=', 1)
        if key in fields:
            raise ValueError('Duplicate SDK build metadata field: ' + key)
        fields[key] = value
    expected = {
        'sync_send_result': 'owned-v1',
        'sync_send_fragment_sha256': sha(fragment),
        'sync_send_renderer_sha256': sha(renderer),
    }
    for key, value in expected.items():
        if fields.get(key) != value:
            raise ValueError('SDK safe-send build metadata absent or stale: ' + key)
    digest = fields.get('sdk_library_sha256', '')
    if not re.fullmatch('[0-9a-f]{64}', digest):
        raise ValueError('SDK library checksum is missing')
    candidates = [Path(library)] if library else [
        item for subdir in ('lib', 'lib64')
        for item in (prefix / subdir).glob('librocketmq.so*')
    ]
    if not candidates:
        raise ValueError('SDK shared library is absent')
    resolved = {item.resolve(strict=True) for item in candidates}
    if len(resolved) != 1:
        raise ValueError('SDK prefix contains ambiguous shared libraries')
    actual = resolved.pop()
    if not actual.is_file() or not actual.is_relative_to(prefix):
        raise ValueError('SDK library must resolve inside its isolated prefix')
    if sha(actual) != digest:
        raise ValueError('SDK library differs from its safe-send build metadata')
    return dict(status='SAFE_SYNC_SEND_BUILD_METADATA_AND_LIBRARY_CHECKSUM_PASS',
                library=str(actual), library_sha256=digest)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    for name in ('prefix', 'fragment', 'renderer'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--library', type=Path)
    args = parser.parse_args()
    try:
        print(json.dumps(verify(args.prefix, args.fragment, args.renderer, args.library)))
    except (OSError, ValueError) as error:
        parser.exit(1, 'RocketMQ safe-send SDK verification failed: ' + str(error) + '\n')

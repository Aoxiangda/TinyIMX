#!/usr/bin/env python3
"""Reproduce append/read race without any server, credentials, or user data."""
import pathlib, tempfile, threading, time
from capacity_run import complete_ledger_snapshot

def main():
    with tempfile.TemporaryDirectory(prefix='tinyimx-ledger-snapshot-') as tmp:
        path = pathlib.Path(tmp)/'owned-ledger.tsv'
        complete = b'delivery\t700001\t700002\t1\t2\towned\t3\n'
        path.write_bytes(complete[:20])
        def finish():
            time.sleep(.04)
            with path.open('ab') as f:
                f.write(complete[20:])
        writer = threading.Thread(target=finish)
        writer.start()
        raw, attempts = complete_ledger_snapshot(path)
        writer.join()
        assert raw == complete and attempts > 1, 'Must retain entire racing record'
        path.write_bytes(b'bad\tcomplete\n'+complete)
        try:
            complete_ledger_snapshot(path)
        except ValueError:
            pass
        else:
            raise AssertionError('Malformed complete record must fail')
        path.write_bytes(complete+b'delivery\t700001')
        try:
            complete_ledger_snapshot(path, timeout=.03)
        except TimeoutError:
            pass
        else:
            raise AssertionError('Permanent incomplete tail must fail without omission')
        path.write_bytes(b'')
        assert complete_ledger_snapshot(path) == (b'', 1), 'Valid hold ledger is empty'
    print('LEDGER_SNAPSHOT_PASS=4')

if __name__ == '__main__':
    main()

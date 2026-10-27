"""Apply the one-branch liveness fix to a verified new SDK source clone.

No network, dependency bootstrap, original SDK overwrite, early timeout return,
detached wait worker or change to success/ACK semantics is introduced.
"""
import argparse,hashlib,json
from pathlib import Path
BEFORE='3ab7b9a506283747960031c0a3da18308f488ab4a2211ded977e9cec6d1b69e0'
OLD=b'  if (endpoints.empty()) {\n    SPDLOG_WARN("No broker is available");\n    return;\n  }'
NEW=b'''  if (endpoints.empty()) {
    SPDLOG_WARN("No broker is available");
    // refreshAssignments() waits for this completion during startup. The
    // periodic refresh is installed only after that wait returns, so dropping
    // this callback prevents recovery even when a route becomes available.
    const std::error_code ec = ErrorCode::NotFound;
    cb(ec);
    return;
  }'''
def render(raw):
 assert hashlib.sha256(raw).hexdigest()==BEFORE,'Exact qualified1052 source required'
 assert raw.count(OLD)==1
 return raw.replace(OLD,NEW)
def main():
 p=argparse.ArgumentParser();p.add_argument('--source',required=True);p.add_argument('--proof',required=True);a=p.parse_args()
 root=Path(a.source).resolve(strict=True)
 # Qualified/retained SDK trees are immutable. Only the explicitly named new
 # candidate clone may be patched; test reconstruction uses render() directly.
 assert root.name=='source'and root.parent.name.startswith('tinyimx-sdk-no-route-r1695-')
 file=root/'source/rocketmq/SimpleConsumerImpl.cpp';assert file.is_file()and not file.is_symlink()
 raw=file.read_bytes();patched=render(raw);file.write_bytes(patched)
 proof=dict(issue_id='PERF-1695',target=str(file),before_sha256=BEFORE,after_sha256=hashlib.sha256(patched).hexdigest(),changes=1,callback_error='ErrorCode::NotFound',new_success_paths=0,original_SDK_overwritten=False,performance_claim=False)
 Path(a.proof).write_text(json.dumps(proof,indent=2));print(json.dumps(proof))
if __name__=='__main__':main()

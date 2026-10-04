#!/usr/bin/env bash
set -euo pipefail
umask 077
cd /home/jackson7/projects/TinyIMX_publish
python3 - <<'PY'
import pathlib,json,hashlib
old=pathlib.Path('.local/codex/session-select-counter-review-20261005/raw.tsv');raw=old.read_bytes();lines=raw.decode().splitlines()
d=pathlib.Path('.local/codex/session-select-counter-result-review-20261005');assert not d.exists();d.mkdir()
assert len(lines)==3 and lines[1]=='1' and lines[0].startswith('Com_select\t') and lines[2].startswith('Com_select\t')
before=int(lines[0].split('\t')[1]);after=int(lines[2].split('\t')[1]);assert after-before==1
x={'status':'SESSION_COM_SELECT_DELTA_SANITY_PASS','original_raw_sha256':hashlib.sha256(raw).hexdigest(),'initial_count':before,'after_select_one':after,'delta':after-before,'first_check_failure':'Incorrectly required absolute initial zero; actual existing mysql CLI session begins at1. Preserve original failure and raw. Product test must compare before/after, not absolute initial value.','new_SQL_or_mutations':False,'originals_preserved':True}
(d/'summary.json').write_text(json.dumps(x,indent=2)+'\n');print(json.dumps(x))
PY

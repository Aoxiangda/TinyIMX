#!/usr/bin/env bash
set -euo pipefail
umask 077
cd /home/jackson7/projects/TinyIMX_publish
python3 - <<'PY'
import pathlib,json,hashlib,subprocess,datetime,re,sys
r=pathlib.Path.cwd();b=r/'.local/codex';d=b/'private-cpu-symbol-analysis-20261005';assert not d.exists()
head=subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip();source=json.loads((b/'private-cpu-symbol-parser-source-20261005/summary.json').read_text());assert head==source['head']
assert subprocess.run(['pgrep','-f','^/home/jackson7/projects/TinyIMX_publish/build/linux-release/tinyimx_capacity_worker'],capture_output=True).returncode==1
parent=b/'private-cpu-symbol-snapshot-20261005-attempt2';capture=b/'private-cpu-load-profile-20261005';assert json.loads((parent/'helper-failed.json').read_text())['status']=='FAIL'
manifest=json.loads((parent/'symbol-snapshot-manifest.json').read_text());assert len(manifest['files'])==12 and not manifest['copied_whole_root'] and manifest['old_reports_function_labels_rejected'] and manifest['raw_samples_unchanged']
def sha(path):
 h=hashlib.sha256()
 with path.open('rb') as f:
  for chunk in iter(lambda:f.read(1024*1024),b''):h.update(chunk)
 return h.hexdigest()
for entry in manifest['files']:
 p=parent/entry['destination'];assert p.resolve().is_relative_to((parent/'bundle').resolve()) and not p.is_symlink() and p.stat().st_size==entry['size'] and sha(p)==entry['sha256']
 if entry['source_path']=='/opt/tinyimx/bin/gateway_demo':assert entry['sha256']=='c2894a21cf308ad35ef665c603d17b1a2577c9216aa9e74856772befa99d640f'
ref=json.loads((b/'private-batch-message-deployment-retained-on-20261005/runtime-after.json').read_text());cfgroot=pathlib.Path('/home/jackson7/.local/share/tinyimx/m21/config')
def runtime():
 names=subprocess.check_output(['docker','ps','--format','{{.Names}}'],text=True).splitlines();assert len(names)==19;cs=json.loads(subprocess.check_output(['docker','inspect',*names],text=True));return {'containers':{c['Name']:{'id':c['Id'],'image':c['Image'],'started':c['State']['StartedAt']} for c in cs},'config_sha256':{p.name:sha(p) for p in cfgroot.glob('*.json')}}
before=runtime();assert before==ref
settings={p:pathlib.Path('/proc/sys/kernel/'+p).read_text().strip() for p in ['perf_event_paranoid','kptr_restrict']};assert settings=={'perf_event_paranoid':'4','kptr_restrict':'1'}
inputs=[parent/'helper-failed.json',parent/'symbol-snapshot-manifest.json',parent/'cpu-profile-summary.json',capture/'profile-audit-before.json',b/'capacity-cpuprofile150/summary.json']+[parent/(label+'-cpu-symbols.txt') for label in ['gateway-a','gateway-b']]
input_sha={str(p.relative_to(b)):sha(p) for p in inputs};d.mkdir()
def preserve_failure(kind,error,traceback):
 (d/'helper-failed.json').write_text(json.dumps({'status':'FAIL','type':kind.__name__,'message':str(error),'head':head,'new_load':False})+'\n');sys.__excepthook__(kind,error,traceback)
sys.excepthook=preserve_failure
(d/'audit-before.json').write_text(json.dumps({'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'head':head,'operation':'Parse existingphysicalexactELF Gatewayreports, known5th placeholder IPCfield, no newload/copy/probes','inputs_sha256':input_sha,'runtime':before,'validation':'12 exactELF SHA/buildID manifest; headerSamples620/625/Lost0; strict5columns withIPC[-,-]; fullsamplerows, sortweights; unknownvDSOoffsetsnotinventednames','writes':'Freshownanalysis numeric/symbol JSON only','limits':'UserCPUflat notkernel orwallwait, instrumentedload1306.6msnotacceptance, Messagecapturefailed, oldprocrootsymbolswrong','rollback':'Alloldraw/report/ELF/stages byteexact preserved, no deletion'},indent=2)+'\n')
reports=[];all_rows={}
for label,count in [('gateway-a',620),('gateway-b',625)]:
 c=json.loads(subprocess.check_output(['docker','inspect','tinyimx-codex-snapshot-cpu-'+label+'-report-20261005-attempt2'],text=True))[0]
 assert c['State']['Status']=='exited' and c['State']['ExitCode']==0 and not c['HostConfig']['CapAdd'] and c['HostConfig']['PidMode']==''
 text=(parent/(label+'-cpu-symbols.txt')).read_text();assert re.search(r'^# Samples:\s*'+str(count)+r'\s',text,re.M) and '# Total Lost Samples: 0' in text;rows=[]
 for line in text.splitlines():
  fields=[f.strip() for f in line.split(';')]
  if not fields or not re.fullmatch(r'[0-9.]+%?',fields[0]):continue
  assert len(fields)==5 and fields[4].split()==['-','-'] and fields[1].isdigit() and fields[2] and fields[3].startswith('[.] ')
  rows.append({'cpu_sample_weight_pct':float(fields[0].rstrip('%')),'samples':int(fields[1]),'dso':fields[2],'symbol':fields[3][4:].strip()})
 assert len(rows)==({'gateway-a':187,'gateway-b':167}[label]) and sum(x['samples'] for x in rows)==count and abs(sum(x['cpu_sample_weight_pct'] for x in rows)-100)<2
 rows.sort(key=lambda x:x['cpu_sample_weight_pct'],reverse=True);all_rows[label]=rows
 dso={name:sum(x['cpu_sample_weight_pct'] for x in rows if x['dso']==name) for name in sorted({x['dso'] for x in rows})}
 reports.append({'target':label,'sample_count':count,'lost_samples':0,'symbol_rows':len(rows),'unknown_weight_pct':sum(x['cpu_sample_weight_pct'] for x in rows if x['symbol'].startswith('0x')),'dso_weight_pct':dso,'top_symbols':rows[:20],'limits':'vDSO0x768 publiccodeoffset stillunknown; exactothersymbolsphysicallypinned. UserCPUflat620/625 samplesnotcallchain/totalwall/baselineP99.'})
assert runtime()==before and input_sha=={str(p.relative_to(b)):sha(p) for p in inputs} and settings=={p:pathlib.Path('/proc/sys/kernel/'+p).read_text().strip() for p in settings}
(d/'all-symbol-rows.json').write_text(json.dumps(all_rows,indent=2)+'\n');x={'status':'PRIVATE_GATEWAY_CPU_EXACT_SYMBOL_ANALYSIS_PASS','head':head,'reports':reports,'physical_ELF_manifest_sha256':sha(parent/'symbol-snapshot-manifest.json'),'prior_procroot_function_labels_rejected':True,'message_cpu_complete':False,'message_exit':255,'new_load':False,'all19_runtime_configs_preserved':True,'parent_evidence_sha_preserved':True,'full_feature_acceptance':False};(d/'summary.json').write_text(json.dumps(x,indent=2)+'\n')
print(json.dumps({**x,'reports':[{**p,'top_symbols':[{**q,'symbol':q['symbol'][:300]} for q in p['top_symbols']]} for p in reports]},indent=2))
PY

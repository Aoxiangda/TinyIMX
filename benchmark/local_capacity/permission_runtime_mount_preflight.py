"""Validate exact existing readonly task binds before any service stop/recreation."""
import pathlib

def validate_mounts(proposed,original):
 for role,c in original.items():
  expected={(m['Destination'],str(pathlib.Path(m['Source']).resolve()),not m['RW']) for m in c['Mounts']}
  volumes=proposed[role].get('volumes',[]);assert len(volumes)==len(expected),'Unexpected or duplicate mount count'
  assert all(v['type']=='bind' for v in volumes),'Only original bind mounts authorised'
  actual={(v['target'],str(pathlib.Path(v['source']).resolve()),v.get('read_only',False)) for v in volumes}
  assert actual==expected,'Source/target/read-only mismatch before any mutation'
  assert all(pathlib.Path(v['source']).is_dir() for v in volumes),'Require every exact source existing directory'
  if c['HostConfig'].get('Binds'):
   assert all(v.get('bind',{}).get('create_host_path',True) is True and set(v.get('bind',{}))<={'create_host_path'} for v in volumes),'Retain original Binds API representation'

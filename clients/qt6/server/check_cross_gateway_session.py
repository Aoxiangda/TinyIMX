"""Exercise one owned account directly across both private Gateway endpoints."""
import datetime,json,pathlib,socket,struct,subprocess,time
ROOT=pathlib.Path('/home/jackson7/projects/TinyIMX_publish');out=ROOT/'.local/codex'/('desktop-session-check-'+datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%S%fZ'));out.mkdir()
profile=json.loads((ROOT/'.local/codex/feature-accounts-20261007T091056892760Z/demo-profile.json').read_text());account=profile['accounts'][2];checks=[]
def check(name,condition):
    checks.append({'name':name,'pass':bool(condition)})
    if not condition:raise AssertionError(name)
def endpoint(role):
    value=json.loads(subprocess.check_output(['docker','inspect','tinyimx-m21-gateway-'+role+'-1'],text=True))[0];return next(iter(value['NetworkSettings']['Networks'].values()))['IPAddress']
def send(s,type,seq,body):
    data=json.dumps(body,separators=(',',':')).encode();s.sendall(struct.pack('!IHHHHII',0x54494d58,1,type,0,0,seq,len(data))+data)
def exact(s,n):
    data=b''
    while len(data)<n:
        b=s.recv(n-len(data))
        if not b:raise EOFError('connection closed')
        data+=b
    return data
def receive(s):
    magic,version,type,flags,reserved,seq,size=struct.unpack('!IHHHHII',exact(s,20));assert magic==0x54494d58 and version==1 and size<=1024*1024;body=json.loads(exact(s,size));
    if type in [2019,2051]:send(s,2020 if type==2019 else 2052,seq,{'message_id':body['message_id']})
    return type,seq,body
def match(s,type,seq):
    for _ in range(100):
        result=receive(s)
        if result[0]==type and result[1]==seq:return result[2]
    raise AssertionError('response not observed')
old=new=None;error=''
try:
    old=socket.create_connection((endpoint('a'),9000),10);old.settimeout(10);send(old,1001,1,{'username':account['username'],'password':profile['password']});check('gateway_a_authenticates_owned_account',match(old,1002,1).get('success'))
    new=socket.create_connection((endpoint('b'),9000),10);new.settimeout(10);send(new,1001,1,{'username':account['username'],'password':profile['password']});check('gateway_b_reauthenticates_same_account',match(new,1002,1).get('success'))
    start=time.monotonic();send(old,9001,2,{});replaced=False
    for _ in range(100):
        type,seq,body=receive(old)
        if type==9999 and seq==0 and body.get('reason')=='login_replaced':replaced=True;break
    check('old_cross_gateway_session_notified_and_fenced',replaced);elapsed_ms=round((time.monotonic()-start)*1000,2)
    closed=False
    try:receive(old)
    except EOFError:closed=True
    check('old_socket_shutdown_after_notice',closed)
    send(new,2009,3,{'limit':100});check('new_owner_friend_rpc_survives_old_cleanup',match(new,2010,3).get('success'))
    send(new,9001,4,{});check('new_owner_heartbeat_survives_old_cleanup',match(new,9001,4).get('pong') is True)
except Exception as e:error=str(e)
finally:
    if old:old.close()
    if new:new.close()
    result={'status':'FAIL' if error else 'PASS','error':error,'checks':checks,'eviction_after_explicit_heartbeat_ms':locals().get('elapsed_ms'),'expectation':'positive mismatch at next heartbeat; no assertion of instantaneous login replacement','pressure':False,'account':account['user_id']};(out/'report.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps({**result,'audit':str(out)}))
raise SystemExit(1 if error else 0)

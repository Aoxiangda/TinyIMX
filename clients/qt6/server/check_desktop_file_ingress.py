"""Small functional integration checks with only the three owned feature accounts.
No load generator, SQL mutations, external messages, or deletions. Never log tokens.
"""
import base64,hashlib,json,pathlib,sys,urllib.request,urllib.error,uuid
profile=json.loads(pathlib.Path(sys.argv[1]).read_text(encoding='utf-8-sig'));out=pathlib.Path(sys.argv[2]);out.mkdir(exist_ok=False)
endpoint='http://'+profile['endpoint'].split(':')[0]+':18082/desktop';checks=[];tokens=[];files=[]
def rpc(op,args,token=None):
    headers={'Content-Type':'application/json'}
    if token:headers['Authorization']='Bearer '+token
    request=urllib.request.Request(endpoint,data=json.dumps({'op':op,'args':args}).encode(),headers=headers)
    try:
        with urllib.request.urlopen(request,timeout=20) as response:return response.status,json.load(response)
    except urllib.error.HTTPError as error:return error.code,json.load(error)
def check(name,ok):
    row={'name':name,'pass':bool(ok)};checks.append(row)
    with (out/'progress.jsonl').open('a') as f:f.write(json.dumps(row)+'\n')
    if not ok:raise AssertionError(name)
def begin(token,size,checksum):
    status,b=rpc('begin',{'client_upload_id':uuid.uuid4().hex,'file_name':'ingress-owned-check.bin','content_type':'application/octet-stream','total_size':str(size),'checksum_algorithm':'sha256','expected_checksum':checksum,'preferred_chunk_size':'262144'},token)
    check('begin_available_session_'+str(len(files)),status==200 and b['success']);files.append(b['file']['file_id']);return b
try:
    check('unauthenticated_denied',rpc('progress',{'upload_id':'1'})[0]==401)
    for account in profile['accounts']:
        status,b=rpc('login',{'username':account['username'],'password':profile['password']});check('authenticated_identity_'+account['user_id'],status==200 and b['user_id']==account['user_id']);tokens.append(b['token'])
    a,b,c=tokens;data=bytes(range(256))*3073;digest=hashlib.sha256(data).hexdigest();upload=begin(a,len(data),digest);session=upload['session']['upload_id'];file=upload['file']['file_id']
    check('actor_injection_rejected',rpc('progress',{'upload_id':session,'actor_user_id':profile['accounts'][0]['user_id']},b)[0]==400)
    check('other_user_upload_session_denied',rpc('progress',{'upload_id':session},b)[0] in (403,404))
    for index in [2,0,1,3]:
        chunk=data[index*262144:(index+1)*262144];status,response=rpc('chunk',{'upload_id':session,'chunk_index':str(index),'byte_offset':str(index*262144),'data':base64.b64encode(chunk).decode(),'checksum_algorithm':'sha256','checksum':hashlib.sha256(chunk).hexdigest()},a)
        check('out_of_order_chunk_'+str(index),status==200 and response['result']=='UPLOAD_CHUNK_RESULT_STORED')
    chunk=data[262144:524288];status,response=rpc('chunk',{'upload_id':session,'chunk_index':'1','byte_offset':'262144','data':base64.b64encode(chunk).decode(),'checksum_algorithm':'sha256','checksum':hashlib.sha256(chunk).hexdigest()},a);check('duplicate_chunk_reused',status==200 and response['result']=='UPLOAD_CHUNK_RESULT_REUSED')
    status,response=rpc('progress',{'upload_id':session},a);check('durable_resume_progress',status==200 and response['progress']['ready_to_finalize'])
    status,response=rpc('finalize',{'upload_id':session},a);check('finalize_sha256_available',status==200 and response['file']['status']=='FILE_STATUS_AVAILABLE' and response['verified_checksum']==digest)
    check('unshared_recipient_cannot_download',rpc('download_info',{'file_id':file},b)[0] in (403,404))
    status,share=rpc('share',{'file_id':file,'target_user_id':profile['accounts'][1]['user_id']},a);check('owner_issued_recipient_capability',status==200 and share['info']['verified_checksum']==digest);cap=share['capability']
    check('wrong_recipient_denied',rpc('download_info',{'file_id':file,'capability':cap},c)[0]==403)
    check('tampered_capability_denied',rpc('download_info',{'file_id':file,'capability':cap[:-1]+('0' if cap[-1]!='0' else '1')},b)[0]==403)
    check('oversized_read_range_denied',rpc('read_range',{'file_id':file,'capability':cap,'offset':'0','length':'65537'},b)[0]==400)
    result=bytearray();offset=0
    while offset<len(data):
        status,response=rpc('read_range',{'file_id':file,'capability':cap,'offset':str(offset),'length':str(min(65536,len(data)-offset))},b);chunk=base64.b64decode(response['data']);check('authorized_range_sha256_'+str(offset),status==200 and response['range_sha256']==hashlib.sha256(chunk).hexdigest());result.extend(chunk);offset=int(response['next_offset'])
    check('download_whole_file_exact',bytes(result)==data and hashlib.sha256(result).hexdigest()==digest)
    canceled=begin(a,len(data),digest);status,response=rpc('cancel',{'upload_id':canceled['session']['upload_id']},a);check('cancel_durable_session',status==200 and response['session']['status']=='UPLOAD_SESSION_STATUS_CANCELED')
    check('canceled_file_not_downloadable',rpc('download_info',{'file_id':canceled['file']['file_id']},a)[0]==409)
    status,response=rpc('logout',{},c);check('logout_revokes_authentication',status==200 and rpc('download_info',{'file_id':file},c)[0]==401)
    status='PASS';error=''
except Exception as e:status='FAIL';error=str(e)
finally:
    for token in tokens:rpc('logout',{},token)
    (out/'report.json').write_text(json.dumps({'status':status,'error':error,'checks':checks,'owned_file_ids':files,'capabilities_and_tokens_logged':False,'pressure':False},indent=2)+'\n')
print(json.dumps({'status':status,'checks':len(checks),'error':error}));sys.exit(0 if status=='PASS' else 1)

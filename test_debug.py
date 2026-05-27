import urllib.request
import json

# Test failed delete via Web API
req = urllib.request.Request(
    'http://127.0.0.1:8090/api/channel/delete',
    data=json.dumps({'channel_id': 99999}).encode(),
    headers={'Content-Type': 'application/json'},
    method='POST'
)
resp = urllib.request.urlopen(req)
result = json.loads(resp.read().decode())
print('Full result:', result)
print('status:', result.get('status'))
data = result.get('data', {})
print('data:', data)
print('data.success:', data.get('success'))
ok = result.get('status') == 'ok' and data.get('success', False)
print('ok:', ok)

# Check logs
logs = json.loads(urllib.request.urlopen('http://127.0.0.1:8090/api/logs').read().decode())
for log in logs['data']:
    if log['action'] == 'channel_delete':
        print('Log:', log)

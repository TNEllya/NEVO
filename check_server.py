import urllib.request
import json

# Check which server.py is running by checking a unique marker
req = urllib.request.Request(
    'http://127.0.0.1:8090/api/channel/delete',
    data=json.dumps({'channel_id': 99999}).encode(),
    headers={'Content-Type': 'application/json'},
    method='POST'
)
resp = urllib.request.urlopen(req)
result = json.loads(resp.read().decode())
print('API result:', result)

# Now check the file directly
with open('C:/Users/yzd20/Desktop/NEVO/test/server/web/server.py', 'r', encoding='utf-8') as f:
    content = f.read()
    marker = 'ok = result.get("status") == "ok" and result.get("data", {}).get("success", False)'
    if marker in content:
        print('File HAS the fix')
    else:
        print('File DOES NOT have the fix')

    # Find the delete handler
    idx = content.find('elif path == "/api/channel/delete"')
    if idx >= 0:
        print('Delete handler snippet:')
        print(content[idx:idx+500])

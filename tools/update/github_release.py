"""GitHub release transport: bounded reads, no credential redirects."""
import hashlib
import json
import re
import urllib.error
import urllib.parse
import urllib.request

from release_identity import PUBLIC_REPOSITORY, check_identity

REPOSITORIES = {'application': PUBLIC_REPOSITORY, 'system': PUBLIC_REPOSITORY}

class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        return None

class GitHubRelease:
    def __init__(self, repository, token):
        if repository not in REPOSITORIES.values() or not token:
            raise ValueError('registered repository and release-environment token required')
        self.repository, self.token = repository, token
        self.base = 'https://api.github.com/repos/' + repository
        self.opener = urllib.request.build_opener(NoRedirect)

    def request(self, url, method='GET', data=None, content_type='application/json', accept='application/vnd.github+json'):
        u = urllib.parse.urlparse(url)
        if u.scheme != 'https' or u.hostname not in {'api.github.com', 'uploads.github.com'} or u.username or u.password or u.port:
            raise ValueError('untrusted GitHub API URL')
        headers = {'Authorization':'Bearer '+self.token, 'Accept':accept, 'Content-Type':content_type, 'User-Agent':'Mimi-Release-Gate'}
        if hasattr(data,'fileno'):
            import os
            headers['Content-Length'] = str(os.fstat(data.fileno()).st_size)
        return self.opener.open(urllib.request.Request(url, method=method, data=data, headers=headers),timeout=120)

    def api(self, path, method='GET', data=None):
        with self.request(self.base+path,method,json.dumps(data).encode() if data is not None else None) as response:
            raw = response.read((8<<20)+1)
            if len(raw)>8<<20: raise ValueError('GitHub metadata too large')
            return json.loads(raw)

    def verify_tag(self, lock):
        ref = self.api('/git/ref/tags/'+urllib.parse.quote(lock['tag'],safe=''))['object']
        for _ in range(8):
            if ref['type'] != 'tag': break
            ref = self.api('/git/tags/'+ref['sha'])['object']
        if ref['type'] != 'commit' or ref['sha'] != lock['source_commit']:
            raise ValueError('remote immutable tag does not match release source')

    def find(self, tag):
        matches = []
        for page in range(1,11):
            chunk = self.api(f'/releases?per_page=100&page={page}')
            matches.extend(r for r in chunk if r['tag_name']==tag)
            if len(chunk)<100: break
        else: raise ValueError('release pagination exceeded')
        if len(matches)>1: raise ValueError('duplicate release tags')
        return matches[0] if matches else None

    def assets(self, release):
        result = {}
        for page in range(1,33):
            chunk = self.api(f"/releases/{release['id']}/assets?per_page=100&page={page}")
            for asset in chunk:
                if asset['name'] in result: raise ValueError('duplicate remote asset names')
                result[asset['name']] = asset
            if len(chunk)<100: return result
        raise ValueError('asset pagination exceeded')

    def download(self, asset, path, size, digest):
        if asset.get('size') != size: raise ValueError('remote asset size mismatch: '+asset['name'])
        try:
            response = self.request(asset['url'],accept='application/octet-stream')
        except urllib.error.HTTPError as error:
            if error.code != 302: raise
            url = error.headers['Location']; u = urllib.parse.urlparse(url)
            if u.scheme != 'https' or u.hostname not in {'release-assets.githubusercontent.com','objects.githubusercontent.com'} or u.username or u.password or u.port:
                raise ValueError('untrusted asset redirect')
            response = self.opener.open(urllib.request.Request(url),timeout=120)
        with response, path.open('xb') as output:
            received = 0; h = hashlib.sha256()
            for chunk in iter(lambda: response.read(1<<20),b''):
                received += len(chunk)
                if received>size: raise ValueError('oversized remote asset')
                h.update(chunk); output.write(chunk)
        if received != size or h.hexdigest()!=digest: raise ValueError('remote asset hash mismatch: '+asset['name'])

def verify_repository(lock, repository):
    check_identity(lock)
    if REPOSITORIES.get(lock['release_type']) != repository or lock.get('repository',repository) != repository:
        raise ValueError('release type/repository mismatch')

def asset_snapshot(assets):
    return {name:(a['id'],a['size'],a.get('updated_at')) for name,a in assets.items()}

def verify_remote(client, release, directory, lock, destination):
    from release_gate import sha
    expected = {**lock['assets'],'release-lock.json':sha(directory/'release-lock.json')}
    assets = client.assets(release)
    if set(assets) != set(expected): raise ValueError('remote asset inventory differs from reviewed release')
    for name,digest in expected.items():
        if not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9._-]{0,200}',name): raise ValueError('invalid release asset name')
        client.download(assets[name], destination/name,(directory/name).stat().st_size,digest)
    return asset_snapshot(assets)

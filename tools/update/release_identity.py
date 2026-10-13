"""One public release repository, separate application/system tag namespaces."""
import re

PUBLIC_REPOSITORY = 'dshanpi/SmartDesktopMimi'
HARDWARE_REPOSITORY = '100askTeam/A133-AIDesktop-Tina5'
PLATFORM = r'[a-z0-9]+(?:-[a-z0-9]+)*'
VERSION = r'(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)(?:-[0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*)?(?:\+[0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*)?'

def valid_version(version):
    if not isinstance(version,str) or not re.fullmatch(VERSION,version):return False
    core=version.split('+',1)[0]
    if '-' in core:
        for identifier in core.split('-',1)[1].split('.'):
            if identifier.isdigit() and len(identifier)>1 and identifier.startswith('0'):return False
    return True

def tag_for(kind, version, platform=None):
    if not valid_version(version):raise ValueError('invalid release version')
    if kind=='application':return 'v'+version
    if kind=='system' and isinstance(platform,str) and re.fullmatch(PLATFORM,platform):
        return 'system-'+platform+'-v'+version
    raise ValueError('unknown release type/platform')

def parse_tag(tag):
    if tag.startswith('v') and valid_version(tag[1:]):return 'application',None,tag[1:]
    m=re.fullmatch('system-('+PLATFORM+')-v('+VERSION+')',tag)
    if m and valid_version(m[2]):return 'system',m[1],m[2]
    raise ValueError('invalid release tag')

def check_identity(lock):
    if lock.get('repository',PUBLIC_REPOSITORY)!=PUBLIC_REPOSITORY:
        raise ValueError('release must use the unified repository')
    if lock['tag']!=tag_for(lock['release_type'],lock['version'],lock.get('platform_id')):
        raise ValueError('tag/version/type/platform mismatch')

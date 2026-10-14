"""Explicit supported SDK build tuples, not a platform qualification claim."""
def target_spec(platform_id, architecture, target_os):
    if platform_id == 'a133' and architecture == 'aarch64' and target_os in ('tina','debian','ubuntu'):
        prefix = 'aarch64-openwrt-linux-' if target_os == 'tina' else 'aarch64-linux-gnu-'
        return dict(compiler_prefix=prefix, multiarch='aarch64-linux-gnu', deb_architecture='arm64', native=False)
    if (platform_id, architecture, target_os) == ('ubuntu-host','x86_64','ubuntu'):
        return dict(compiler_prefix='x86_64-linux-gnu-', multiarch='x86_64-linux-gnu', deb_architecture='amd64', native=True)
    raise ValueError('Unsupported platform/architecture/OS combination')

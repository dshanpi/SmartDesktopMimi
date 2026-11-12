import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync, existsSync } from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { rewriteLink, sourceForGeneratedFile } from '../source-links.mjs';
import { routes, assets } from '../routes.mjs';
const root = fileURLToPath(new URL('../../', import.meta.url));

test('chapter navigation resolves under the project base and preserves anchors', () => {
  assert.equal(rewriteLink('architecture.md#阅读源码的入口', 'docs/tutorial/index.md'), '/SmartDesktopMimi/architecture/#阅读源码的入口');
  assert.equal(rewriteLink('index.md', 'docs/tutorial/install.md'), '/SmartDesktopMimi/');
});
test('existing evidence images become local site assets, source files stay on GitHub', () => {
  assert.equal(rewriteLink('../debug-evidence/a133-en-us-20260906/final/desktop-r3.png', 'docs/tutorial/gallery.md'), '/SmartDesktopMimi/screenshots/desktop.png');
  assert.equal(rewriteLink('../application-update-policy.md', 'docs/tutorial/ota.md'), 'https://github.com/dshanpi/SmartDesktopMimi/blob/main/docs/application-update-policy.md');
});
test('anchors and external URLs remain unchanged', () => {
  for (const url of ['#检查', 'https://github.com/dshanpi/SmartDesktopMimi', 'mailto:docs@example.com']) assert.equal(rewriteLink(url, 'docs/tutorial/index.md'), url);
});
test('Windows and POSIX generated paths resolve canonical Markdown', () => {
  assert.equal(sourceForGeneratedFile('G:\\repo\\docs\\src\\content\\docs\\install.md'), 'docs/tutorial/install.md');
  assert.equal(sourceForGeneratedFile('/repo/docs/src/content/docs/install.md'), 'docs/tutorial/install.md');
});
test('links cannot escape the repository', () => {
  assert.throws(() => rewriteLink('../../../outside.md', 'docs/tutorial/install.md'), /escapes repository/);
});
test('every canonical chapter and its relative links and images exist', () => {
  assert.equal(new Set(routes.map(p => p.slug)).size, routes.length);
  for (const page of routes) {
    const content = readFileSync(path.join(root, page.source), 'utf8');
    assert.match(content, /^# .+\n/);
    for (const match of content.matchAll(/!?\[[^\]]*\]\(([^)\s]+)\)/g)) {
      const url = match[1];
      if (/^(?:[a-z][a-z0-9+.-]*:|#|\/\/)/i.test(url)) continue;
      const target = path.resolve(root, path.dirname(page.source), decodeURIComponent(url.split(/[?#]/)[0]));
      assert.ok(existsSync(target), page.source + ' -> missing ' + url);
    }
  }
  for (const asset of assets) assert.ok(existsSync(path.join(root, asset.source)), asset.source);
});

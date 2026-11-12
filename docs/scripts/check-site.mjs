import { readFileSync, readdirSync, existsSync, statSync } from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { base, routes } from '../routes.mjs';
const dist = fileURLToPath(new URL('../dist/', import.meta.url));
const walk = (dir) => readdirSync(dir, { withFileTypes: true }).flatMap((entry) => {
  const file = path.join(dir, entry.name);
  return entry.isDirectory() ? walk(file) : [file];
});
const htmlFiles = walk(dist).filter((file) => file.endsWith('.html'));
const html = new Map(htmlFiles.map((file) => [file, readFileSync(file, 'utf8')]));
const failures = [];
for (const page of routes) {
  const file = path.join(dist, page.slug === 'index' ? '' : page.slug, 'index.html');
  if (!existsSync(file)) failures.push('Missing route: ' + page.slug);
}
for (const [file, text] of html) {
  const relative = path.relative(dist, file).replaceAll('\\', '/').replace(/index\.html$/, '');
  const current = new URL(base + '/' + relative, 'https://mimi.invalid');
  for (const match of text.matchAll(/\b(?:href|src)=["']([^"']+)["']/g)) {
    const value = match[1].replaceAll('&amp;', '&');
    let url;
    try { url = new URL(value, current); } catch { failures.push('Invalid URL: ' + value); continue; }
    if (url.origin !== current.origin) continue;
    if (url.pathname !== base && !url.pathname.startsWith(base + '/')) {
      failures.push(relative + ': missing base prefix: ' + value); continue;
    }
    let target = path.join(dist, decodeURIComponent(url.pathname.slice(base.length)));
    if (existsSync(target) && statSync(target).isDirectory()) target = path.join(target, 'index.html');
    if (!existsSync(target)) { failures.push(relative + ': missing target: ' + value); continue; }
    if (url.hash && html.has(target)) {
      const id = decodeURIComponent(url.hash.slice(1));
      const ids = [...html.get(target).matchAll(/\bid=["']([^"']+)["']/g)].map((item) => item[1]);
      if (!ids.includes(id)) failures.push(relative + ': missing anchor: ' + value);
    }
  }
}
if (failures.length) { console.error([...new Set(failures)].join('\n')); process.exit(1); }
console.log('PASS: ' + routes.length + ' routes; HTML links, anchors and assets');

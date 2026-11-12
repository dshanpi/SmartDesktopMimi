import { readFileSync, writeFileSync, mkdirSync, cpSync, readdirSync, unlinkSync } from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { routes, repository, assets } from '../routes.mjs';

const docsRoot = fileURLToPath(new URL('../', import.meta.url));
const repoRoot = path.dirname(docsRoot);
const output = path.join(docsRoot, 'src/content/docs');
export const sourcePaths = routes.map((page) => path.join(repoRoot, page.source));

export function syncContent() {
  mkdirSync(output, { recursive: true });
  const names = new Set(routes.map((page) => page.slug + '.md'));
  // Only remove obsolete generated Markdown inside this dedicated output folder.
  for (const name of readdirSync(output)) {
    if (name.endsWith('.md') && !names.has(name)) unlinkSync(path.join(output, name));
  }
  for (const page of routes) {
    const content = readFileSync(path.join(repoRoot, page.source), 'utf8').replace(/\r\n/g, '\n');
    const match = content.match(/^# ([^\n]+)\n/);
    if (!match) throw new Error('Expected one leading H1 in ' + page.source);
    const frontmatter = '---\ntitle: ' + JSON.stringify(page.title) +
      '\neditUrl: ' + JSON.stringify(repository + '/edit/main/' + page.source) + '\n---\n';
    const target = path.join(output, page.slug + '.md');
    const result = frontmatter + content.slice(match[0].length);
    let previous;
    try { previous = readFileSync(target, 'utf8'); } catch {}
    if (previous !== result) writeFileSync(target, result);
  }
}
export function prepare() {
  syncContent();
  mkdirSync(path.join(docsRoot, 'public/screenshots'), { recursive: true });
  for (const asset of assets) cpSync(path.join(repoRoot, asset.source), path.join(docsRoot, 'public/screenshots', asset.file));
  const vendor = path.join(docsRoot, 'public/vendor');
  mkdirSync(vendor, { recursive: true });
  for (const [from, to] of [
    ['mermaid/dist/mermaid.min.js', 'mermaid.min.js'],
  ]) cpSync(path.join(docsRoot, 'node_modules', from), path.join(vendor, to));
}
if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) prepare();

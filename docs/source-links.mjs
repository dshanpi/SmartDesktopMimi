import path from 'node:path';
import { base, repository, routes, assets } from './routes.mjs';

export function rewriteLink(url, source) {
  if (/^([a-z][a-z0-9+.-]*:|#|\/\/)/i.test(url)) return url;
  if (url.startsWith('assets/')) return base + '/' + url.slice('assets/'.length);
  const match = url.match(/^([^?#]*)(.*)$/);
  const pathname = match[1];
  const suffix = match[2];
  const target = path.posix.normalize(path.posix.join(path.posix.dirname(source), pathname));
  const asset = assets.find((item) => item.source === target);
  if (asset) return base + '/screenshots/' + asset.file + suffix;
  const page = routes.find((item) => item.source === target);
  if (page) return base + '/' + (page.slug === 'index' ? '' : page.slug + '/') + suffix;
  if (target.startsWith('docs/screenshots/')) {
    return base + '/screenshots/' + target.slice('docs/screenshots/'.length) + suffix;
  }
  if (target.startsWith('docs/public/')) {
    return base + '/' + target.slice('docs/public/'.length) + suffix;
  }
  if (target.startsWith('../') || path.posix.isAbsolute(target)) {
    throw new Error('Link escapes repository: ' + source + ' -> ' + url);
  }
  return repository + '/blob/main/' + target.split('/').map(encodeURIComponent).join('/') + suffix;
}

export function sourceForGeneratedFile(file) {
  const normalized = String(file).replaceAll('\\', '/');
  const slug = normalized.split('/src/content/docs/')[1]?.replace(/\.md$/, '');
  return routes.find((item) => item.slug === slug)?.source;
}

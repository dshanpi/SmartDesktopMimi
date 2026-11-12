// Architecture follows YuzukiNeko's remark extension; content is EinkOS-specific.
import { visit } from 'unist-util-visit';
import { rewriteLink, sourceForGeneratedFile } from '../../source-links.mjs';

const escapeHtml = (value) => value.replaceAll('&', '&amp;').replaceAll('<', '&lt;').replaceAll('>', '&gt;');
export function remarkDocs() {
  return (tree, file) => {
    const source = sourceForGeneratedFile(file.path);
    if (!source) return;
    visit(tree, (node, index, parent) => {
      if (node.type === 'code' && node.lang === 'mermaid' && parent && index !== undefined) {
        parent.children[index] = { type: 'html', value: '<pre class="mermaid">' + escapeHtml(node.value) + '</pre>' };
      } else if ((node.type === 'link' || node.type === 'image' || node.type === 'definition') && node.url) {
        node.url = rewriteLink(node.url, source);
      } else if (node.type === 'html') {
        node.value = node.value.replace(/\b(src|href|data-src)="([^"]+)"/g,
          (_, attribute, url) => attribute + '="' + rewriteLink(url, source) + '"');
      }
    });
  };
}

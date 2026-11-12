const diagrams = document.querySelectorAll('pre.mermaid');
if (diagrams.length) {
  const script = document.createElement('script');
  script.src = new URL('./vendor/mermaid.min.js', import.meta.url).href;
  script.onload = async () => {
    try {
      window.mermaid.initialize({ startOnLoad: false, securityLevel: 'strict', theme: document.documentElement.dataset.theme === 'dark' ? 'dark' : 'default' });
      await window.mermaid.run({ nodes: diagrams });
    } catch (error) { console.error('Diagram rendering failed', error); }
  };
  document.head.append(script);
}

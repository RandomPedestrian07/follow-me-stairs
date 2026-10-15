// Adds a "Set up Follow-Me" link to ESPHome's stock web page, opening the
// setup wizard the device serves at /followme. Loaded by the stock page as
// /0.js (web_server js_include in follow-me.yaml).
(() => {
  const ID = 'followme-setup-link';
  const add = () => {
    if (document.getElementById(ID)) return;
    const a = document.createElement('a');
    a.id = ID;
    a.href = '/followme';
    a.textContent = 'Set up Follow-Me';
    a.style.cssText = [
      'position:fixed',
      'right:20px',
      'bottom:20px',
      'z-index:9999',
      'padding:10px 16px',
      'background:#0e0e10',
      'color:#ece8df',
      'border:1px solid rgba(236,232,223,.35)',
      'border-radius:3px',
      "font:500 14px/1.2 'IBM Plex Sans',system-ui,sans-serif",
      'text-decoration:none',
    ].join(';');
    document.body.appendChild(a);
  };
  if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', add);
  else add();
})();

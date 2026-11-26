// Theme switch, XML highlighting and the image lightbox.
(function () {
  var root = document.documentElement;
  function current() {
    return root.dataset.theme || (matchMedia('(prefers-color-scheme: dark)').matches ? 'dark' : 'light');
  }
  document.querySelector('.theme').addEventListener('click', function () {
    var next = current() === 'dark' ? 'light' : 'dark';
    root.dataset.theme = next;
    try { localStorage.setItem('scener-theme', next); } catch (e) {}
  });

  // Highlight XML that is already escaped in the page: comments, tags, attributes,
  // quoted values, and {expressions} inside values.
  function esc(s) { return s.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;'); }
  function value(v) {
    return '<span class="val">' + esc(v).replace(/\{(?:[^{}]|\{[^{}]*\})*\}/g, function (m) { return '<span class="exp">' + m + '</span>'; }) + '</span>';
  }
  function tag(t) {
    var m = /^(<\/?)([\w:-]+)([\s\S]*?)(\/?>)$/.exec(t);
    if (!m) return esc(t);
    var attrs = m[3].replace(/([\w:.-]+)=("[^"]*")/g, function (all, name, v) {
      return '<span class="att">' + name + '</span>=' + value(v);
    });
    return '<span class="tag">' + esc(m[1]) + m[2] + '</span>' + attrs + '<span class="tag">' + esc(m[4]) + '</span>';
  }
  document.querySelectorAll('code.xml').forEach(function (el) {
    var src = el.textContent, out = '', re = /<!--[\s\S]*?-->|<[^>]+>/g, last = 0, m;
    while ((m = re.exec(src))) {
      out += esc(src.slice(last, m.index));
      out += m[0].startsWith('<!--') ? '<span class="com">' + esc(m[0]) + '</span>' : tag(m[0]);
      last = re.lastIndex;
    }
    el.innerHTML = out + esc(src.slice(last));
  });

  var box = document.querySelector('.lightbox');
  if (box && box.showModal) {
    var img = box.querySelector('img');
    document.querySelectorAll('a.zoom').forEach(function (a) {
      a.addEventListener('click', function (e) {
        e.preventDefault();
        var src = a.querySelector('img');
        img.src = a.href; img.alt = src ? src.alt : '';
        box.showModal();
      });
    });
    box.addEventListener('click', function (e) { if (e.target === box || e.target === img) box.close(); });
  }
})();

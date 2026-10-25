// Script library: tag chips are collected from the cards; search and tags filter together.
(function () {
  var lib = document.querySelector('[data-lib]');
  if (!lib) return;
  var input = lib.querySelector('input');
  var tagBox = lib.querySelector('.tags');
  var cards = Array.prototype.slice.call(lib.querySelectorAll('.card'));
  var count = lib.querySelector('.lib-count');
  var empty = lib.querySelector('.lib-empty');
  var countFmt = lib.getAttribute('data-count');
  var active = null;

  var seen = {};
  cards.forEach(function (card) {
    var tags = card.getAttribute('data-tags').split(',');
    var box = document.createElement('div');
    box.className = 'ctags';
    tags.forEach(function (t) {
      var s = document.createElement('span');
      s.textContent = '#' + t;
      box.appendChild(s);
      seen[t] = (seen[t] || 0) + 1;
    });
    card.appendChild(box);
  });
  Object.keys(seen).sort(function (a, b) { return seen[b] - seen[a] || a.localeCompare(b); }).forEach(function (t) {
    var b = document.createElement('button');
    b.type = 'button';
    b.className = 'tag';
    b.textContent = '#' + t;
    b.setAttribute('aria-pressed', 'false');
    b.addEventListener('click', function () {
      active = active === t ? null : t;
      Array.prototype.forEach.call(tagBox.children, function (x) {
        x.setAttribute('aria-pressed', String(x === b && active === t));
      });
      apply();
    });
    tagBox.appendChild(b);
  });

  function apply() {
    var q = input.value.trim().toLowerCase();
    var shown = 0;
    cards.forEach(function (card) {
      var tags = card.getAttribute('data-tags').split(',');
      var text = (card.textContent + ' ' + tags.join(' ')).toLowerCase();
      var ok = (!active || tags.indexOf(active) >= 0) && (!q || text.indexOf(q.replace(/^#/, '')) >= 0);
      card.hidden = !ok;
      if (ok) shown++;
    });
    count.textContent = countFmt.replace('%1', shown).replace('%2', cards.length);
    empty.hidden = shown > 0;
  }
  input.addEventListener('input', apply);
  apply();
})();

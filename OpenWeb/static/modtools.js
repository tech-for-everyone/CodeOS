/* OpenWeb ModTools.
 *
 * A small, dependency-free page workbench.  It has two modes:
 *
 *  - Live overrides: CSS, DOM rules and JavaScript applied to a page that
 *    OpenWeb can read (same-origin, local files, or an isolated preview).
 *  - Source preview: arbitrary pasted HTML rendered into a sandboxed frame,
 *    so sites that block cross-origin access can still be redesigned.
 *
 * The module deliberately exposes one global because the static UI loads it
 * as a plain script before app.js (the service worker caches both files).
 */
(function () {
  'use strict';

  var STORE_KEY = 'openweb.modtools.v1';
  var STYLE_ATTR = 'data-ow-modtools-css';
  var RUN_ATTR = 'data-ow-modtools-run';

  function hostOf(url) {
    var match = /^[a-z][a-z0-9+.-]*:\/\/([^/?#]+)/i.exec(String(url || ''));
    return match ? match[1].replace(/^.*@/, '') : '';
  }

  function stripHash(url) {
    return String(url || '').split('#')[0];
  }

  function emptyOverride() {
    return { enabled: true, css: '', js: '', rules: [], source: '' };
  }

  function normalizeOverride(value) {
    var base = emptyOverride();
    if (!value || typeof value !== 'object') return base;
    base.enabled = value.enabled !== false;
    base.css = typeof value.css === 'string' ? value.css : '';
    base.js = typeof value.js === 'string' ? value.js : '';
    base.source = typeof value.source === 'string' ? value.source : '';
    if (Array.isArray(value.rules)) {
      base.rules = value.rules.filter(function (rule) {
        return rule && typeof rule.selector === 'string' && rule.selector.trim();
      }).map(function (rule) {
        return {
          selector: rule.selector,
          action: typeof rule.action === 'string' ? rule.action : 'hide',
          value: rule.value == null ? '' : String(rule.value)
        };
      });
    }
    return base;
  }

  function load() {
    var state = { overrides: {} };
    var raw;
    try {
      raw = localStorage.getItem(STORE_KEY);
    } catch (err) {
      return state;
    }
    if (!raw) return state;
    try {
      var parsed = JSON.parse(raw);
      if (parsed && parsed.overrides && typeof parsed.overrides === 'object') {
        Object.keys(parsed.overrides).forEach(function (key) {
          state.overrides[key] = normalizeOverride(parsed.overrides[key]);
        });
      }
    } catch (err) {
      /* A corrupt override store must never break the browser. */
    }
    return state;
  }

  function save(state) {
    try {
      localStorage.setItem(STORE_KEY, JSON.stringify(state));
    } catch (err) {
      return false;
    }
    return true;
  }

  function keyFor(url, scope) {
    var clean = stripHash(url);
    if (scope === 'host') return 'host:' + (hostOf(clean) || '*');
    if (scope === 'all') return '*';
    return 'url:' + clean;
  }

  function scopeOf(key) {
    if (key === '*') return 'all';
    if (key.indexOf('host:') === 0) return 'host';
    return 'url';
  }

  function labelFor(key) {
    if (key === '*') return 'Every site (wildcard)';
    if (key.indexOf('host:') === 0) return 'All pages on ' + key.slice(5);
    if (key.indexOf('url:') === 0) return key.slice(4);
    return key;
  }

  /* Specific overrides win: wildcard, then host, then exact URL. */
  function resolve(url) {
    var state = load();
    var keys = ['*', 'host:' + (hostOf(url) || '*'), 'url:' + stripHash(url)];
    var merged = emptyOverride();
    var found = [];

    keys.forEach(function (key) {
      var override = state.overrides[key];
      if (!override || !override.enabled) return;
      found.push(key);
      if (override.css) merged.css += (merged.css ? '\n\n' : '') + override.css;
      if (override.js) merged.js += (merged.js ? '\n\n' : '') + override.js;
      if (override.source) merged.source = override.source;
      merged.rules = merged.rules.concat(override.rules);
    });
    merged.keys = found;
    return merged;
  }

  function writeOverride(url, scope, override) {
    var state = load();
    state.overrides[keyFor(url, scope)] = normalizeOverride(override);
    return save(state);
  }

  function deleteOverride(url, scope) {
    var state = load();
    var key = keyFor(url, scope);
    var existed = Object.prototype.hasOwnProperty.call(state.overrides, key);
    delete state.overrides[key];
    save(state);
    return existed;
  }

  function parseRules(text) {
    var parsed = JSON.parse(String(text || '[]'));
    if (!Array.isArray(parsed)) throw new Error('Rules must be a JSON array');
    return parsed.map(function (rule) {
      if (!rule || typeof rule.selector !== 'string' || !rule.selector.trim()) {
        throw new Error('Every rule needs a selector');
      }
      return normalizeOverride({ rules: [rule] }).rules[0];
    });
  }

  function ruleSummary(rule) {
    var label = {
      hide: 'Hide', remove: 'Remove', text: 'Text', html: 'HTML',
      attr: 'Attribute', class: 'Class', style: 'Style',
      before: 'Insert before', after: 'Insert after',
      prepend: 'Prepend', append: 'Append'
    }[rule.action] || rule.action;
    return label + ' ' + rule.selector;
  }

  function escapeRuleValue(value) {
    return String(value == null ? '' : value).replace(/"/g, '&quot;');
  }

  function applyRule(doc, rule) {
    var nodes = doc.querySelectorAll(rule.selector);
    var value = String(rule.value == null ? '' : rule.value);
    var pair, name;

    Array.prototype.forEach.call(nodes, function (node) {
      node.setAttribute('data-ow-modtools-rule', rule.action);
      switch (rule.action) {
        case 'remove':
          if (node.parentNode) node.parentNode.removeChild(node);
          break;
        case 'hide':
          node.style.setProperty('display', 'none', 'important');
          break;
        case 'text':
          node.textContent = value;
          break;
        case 'html':
          node.innerHTML = value;
          break;
        case 'attr':
          pair = value.indexOf('=');
          if (pair > 0) {
            name = value.slice(0, pair).trim();
            if (name) node.setAttribute(name, value.slice(pair + 1));
          }
          break;
        case 'class':
          value.split(/\s+/).forEach(function (cls) {
            if (cls && node.classList) node.classList.add(cls);
          });
          break;
        case 'style':
          node.style.cssText += ';' + value;
          break;
        case 'before':
        case 'after':
        case 'prepend':
        case 'append':
          node.insertAdjacentHTML(rule.action, value);
          break;
        default:
          throw new Error('Unknown action: ' + rule.action);
      }
    });
    return nodes.length;
  }

  function hash(text) {
    var value = 5381;
    var i;
    for (i = 0; i < text.length; i++) {
      value = ((value << 5) + value + text.charCodeAt(i)) | 0;
    }
    return String(value);
  }

  function applyOverride(frame, url, override) {
    var doc;
    try {
      doc = frame && frame.contentDocument;
    } catch (err) {
      return { ok: false, reason: 'cross-origin', errors: [] };
    }
    if (!doc) return { ok: false, reason: 'cross-origin', errors: [] };

    var keys = override && override.keys ? override.keys : [];
    override = normalizeOverride(override);
    var errors = [];
    var old;
    var i;
    var head = doc.head || doc.documentElement;
    var style;

    old = doc.querySelectorAll('style[' + STYLE_ATTR + ']');
    for (i = 0; i < old.length; i++) old[i].parentNode.removeChild(old[i]);

    if (override.css.trim() && head) {
      style = doc.createElement('style');
      style.setAttribute(STYLE_ATTR, 'true');
      style.textContent = override.css;
      head.appendChild(style);
    }

    override.rules.forEach(function (rule) {
      try {
        applyRule(doc, rule);
      } catch (err) {
        errors.push(rule.selector + ': ' + err.message);
      }
    });

    if (override.js.trim()) {
      var win = frame.contentWindow;
      var stamp = hash(stripHash(url) + '\n' + override.js);
      var runs = win.__openwebModToolsRuns;
      if (!runs) {
        runs = {};
        win.__openwebModToolsRuns = runs;
      }
      if (!runs[stamp]) {
        runs[stamp] = true;
        try {
          win.eval(override.js);
        } catch (err) {
          errors.push('JavaScript: ' + err.message);
        }
      }
    }

    return {
      ok: true,
      applied: !!(override.css.trim() || override.js.trim() || override.rules.length),
      keys: keys,
      errors: errors,
      cssLength: override.css.length,
      ruleCount: override.rules.length,
      jsLength: override.js.length
    };
  }

  function apply(frame, url) {
    return applyOverride(frame, url, resolve(url));
  }

  /* Undo what can be undone without a network request. DOM and JS changes
   * are restored by reloading the page, so callers should reload after reset. */
  function resetInDocument(frame) {
    var doc;
    try {
      doc = frame && frame.contentDocument;
    } catch (err) {
      return false;
    }
    if (!doc) return false;
    var old = doc.querySelectorAll('style[' + STYLE_ATTR + ']');
    var i;
    for (i = 0; i < old.length; i++) old[i].parentNode.removeChild(old[i]);
    if (frame.contentWindow) frame.contentWindow.__openwebModToolsRuns = {};
    return true;
  }

  function esc(text) {
    return String(text == null ? '' : text).replace(/[&<>"']/g, function (ch) {
      return { '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[ch];
    });
  }

  var BASE_STYLE = [
    ':root { color-scheme: dark; }',
    'html { background: #07101f; color: #e7f0ff;',
    '  font: 16px/1.7 system-ui, -apple-system, Segoe UI, sans-serif; }',
    'body { margin: 0 auto; padding: 2.5rem 1.25rem 5rem; max-width: 64rem; }',
    'a { color: #58e6d6; } a:hover { color: #a99bff; }',
    'img { max-width: 100%; height: auto; border-radius: 10px; }',
    'table { border-collapse: collapse; width: 100%; }',
    'th, td { border: 1px solid rgba(140,170,210,.28); padding: .6rem .75rem; }',
    'pre { overflow: auto; padding: 1rem; border-radius: 10px;',
    '  background: #0d1830; border: 1px solid rgba(140,170,210,.25); }',
    '.ow-modtools-banner { border-left: 4px solid #58e6d6;',
    '  background: linear-gradient(90deg, rgba(88,230,214,.12), rgba(169,155,255,.06));',
    '  padding: .75rem 1rem; border-radius: 0 10px 10px 0;',
    '  margin: 0 0 2rem; color: #9fb4d4; font-size: .85rem; }'
  ].join('\n');

  function buildDocument(source, override, banner) {
    var parsed;
    var doc;
    try {
      parsed = new DOMParser();
      doc = parsed.parseFromString(String(source || ''), 'text/html');
    } catch (err) {
      doc = document.implementation.createHTMLDocument('OpenWeb preview');
      doc.body.innerHTML = '<pre>' + esc(source) + '</pre>';
    }
    if (!doc.documentElement) return '<!doctype html><pre>' + esc(source) + '</pre>';

    var head = doc.head || doc.createElement('head');
    if (!doc.head) doc.documentElement.insertBefore(head, doc.body);
    var style = doc.createElement('style');
    style.textContent = BASE_STYLE + '\n\n' + (override && override.css ? override.css : '');
    head.appendChild(style);

    if (banner) {
      var note = doc.createElement('div');
      note.className = 'ow-modtools-banner';
      note.textContent = banner;
      if (doc.body.firstChild) doc.body.insertBefore(note, doc.body.firstChild);
      else doc.body.appendChild(note);
    }

    if (override && override.js && override.js.trim()) {
      var script = doc.createElement('script');
      /* Split the closing tag so this string cannot terminate its own script. */
      script.textContent = override.js.replace(/<\/script/gi, '<\\/script');
      doc.body.appendChild(script);
    }

    return '<!doctype html>\n' + doc.documentElement.outerHTML;
  }

  function preview(source, override) {
    return buildDocument(source, override,
      'ModTools isolated preview - changes here do not touch the original site');
  }

  function reader(markup, url) {
    var doc;
    try {
      doc = new DOMParser().parseFromString(String(markup || ''), 'text/html');
    } catch (err) {
      return buildDocument('<pre>' + esc(markup) + '</pre>', null, 'Reader view');
    }

    var remove = doc.querySelectorAll(
      'script,style,noscript,iframe,header,footer,nav,aside,form,button,' +
      '[role="navigation"],[role="banner"],[aria-hidden="true"]');
    var i;
    for (i = 0; i < remove.length; i++) {
      if (remove[i].parentNode) remove[i].parentNode.removeChild(remove[i]);
    }

    var candidates = doc.querySelectorAll(
      'article,main,[role="main"],.entry-content,.post-content,.article-body,#content');
    var main = candidates.length ? candidates[0] : doc.body;
    var title = (doc.querySelector('h1') || {}).textContent ||
                (doc.title || '') || hostOf(url) || 'Untitled page';
    var content = '';

    if (main && main.innerHTML) {
      content = main.innerHTML;
      /* A nested h1 would duplicate the reader header. */
      if (main.tagName && main.tagName.toLowerCase() === 'h1' && main.parentNode) {
        content = '';
      }
    } else {
      content = '<p>No readable content was found.</p>';
    }

    var sourceHost = hostOf(url) || url;
    var css = [
      ':root { color-scheme: dark; }',
      'html { background: radial-gradient(circle at 15% -10%, #173356, #07101f 55%);',
      '  color: #dce8fa; font: 17px/1.75 Georgia, "Times New Roman", serif; }',
      'body { margin: 0 auto; padding: 3rem 1.25rem 6rem; max-width: 46rem; }',
      '.ow-reader-head { font: 600 13px/1.4 system-ui, sans-serif;',
      '  letter-spacing: .16em; text-transform: uppercase; color: #58e6d6;',
      '  border-bottom: 1px solid rgba(140,170,210,.3); padding-bottom: 1rem;',
      '  margin-bottom: 2rem; }',
      '.ow-reader-head a { color: #a99bff; text-decoration: none; }',
      'h1 { font: 700 clamp(2rem, 5vw, 3.1rem)/1.12 system-ui, sans-serif;',
      '  letter-spacing: -.035em; color: #f6faff; margin: 0 0 2rem; }',
      'h2,h3 { font-family: system-ui, sans-serif; color: #f6faff; margin-top: 2.4rem; }',
      'p { margin: 0 0 1.35rem; }',
      'a { color: #6ee7d8; text-underline-offset: 3px; }',
      'img { max-width: 100%; height: auto; border-radius: 12px;',
      '  box-shadow: 0 18px 50px rgba(0,0,0,.45); }',
      'blockquote { margin: 2rem 0; padding: .2rem 0 .2rem 1.4rem;',
      '  border-left: 3px solid #a99bff; color: #b9c9e4; font-style: italic; }',
      'pre,code { font-family: ui-monospace, SFMono-Regular, Consolas, monospace; }',
      'pre { background: #0d1830; padding: 1rem; overflow: auto; border-radius: 10px; }',
      'table { width: 100%; border-collapse: collapse; font-family: system-ui, sans-serif; }',
      'th,td { border: 1px solid rgba(140,170,210,.28); padding: .5rem .65rem; }',
      'hr { border: 0; border-top: 1px solid rgba(140,170,210,.3); margin: 2.5rem 0; }'
    ].join('\n');

    return '<!doctype html>\n<html><head><meta charset="utf-8">' +
      '<base href="' + esc(stripHash(url)) + '">' +
      '<title>' + esc(title) + '</title><style>' + css + '</style></head><body>' +
      '<div class="ow-reader-head">OpenWeb Reader &middot; ' + esc(sourceHost) +
      ' &middot; <a href="' + esc(stripHash(url)) + '">Original page</a></div>' +
      '<article class="ow-reader"><h1>' + esc(title) + '</h1>' + content + '</article>' +
      '</body></html>';
  }

  function makeSelector(node) {
    if (!node || node.nodeType !== 1) return 'body';
    if (node.id && /^[A-Za-z][A-Za-z0-9_-]*$/.test(node.id)) return '#' + node.id;
    var parts = [];
    var cur = node;
    while (cur && cur.nodeType === 1 && parts.length < 4) {
      var tag = cur.tagName.toLowerCase();
      if (cur.id && /^[A-Za-z][A-Za-z0-9_-]*$/.test(cur.id)) {
        parts.unshift(tag + '#' + cur.id);
        break;
      }
      var classes = [];
      if (cur.classList) {
        Array.prototype.forEach.call(cur.classList, function (cls) {
          if (/^[A-Za-z][A-Za-z0-9_-]*$/.test(cls)) classes.push(cls);
        });
      }
      var part = tag;
      if (classes.length) part += '.' + classes.slice(0, 2).join('.');
      var parent = cur.parentNode;
      if (parent && parent.children && parent.children.length > 1 && cur.tagName !== 'BODY') {
        var index = 1;
        var sibling = cur;
        while (sibling.previousElementSibling) {
          sibling = sibling.previousElementSibling;
          index++;
        }
        part += ':nth-child(' + index + ')';
      }
      parts.unshift(part);
      if (cur.tagName === 'BODY' || !parent) break;
      cur = parent;
    }
    return parts.join(' > ');
  }

  function inspect(frame, onPick) {
    var doc;
    try {
      doc = frame && frame.contentDocument;
    } catch (err) {
      return { ok: false, reason: 'cross-origin' };
    }
    if (!doc) return { ok: false, reason: 'cross-origin' };

    stopInspect(frame);
    function handler(event) {
      event.preventDefault();
      event.stopPropagation();
      var info = {
        selector: makeSelector(event.target),
        tag: event.target.tagName ? event.target.tagName.toLowerCase() : '',
        id: event.target.id || '',
        classes: event.target.classList ? Array.prototype.join.call(event.target.classList, ' ') : ''
      };
      stopInspect(frame);
      onPick(info);
    }
    doc.addEventListener('click', handler, true);
    doc.__owModToolsInspect = handler;
    return { ok: true };
  }

  function stopInspect(frame) {
    var doc;
    try {
      doc = frame && frame.contentDocument;
    } catch (err) {
      return;
    }
    if (doc && doc.__owModToolsInspect) {
      doc.removeEventListener('click', doc.__owModToolsInspect, true);
      doc.__owModToolsInspect = null;
    }
  }

  window.OpenWebModTools = {
    hostOf: hostOf,
    load: load,
    save: save,
    keyFor: keyFor,
    scopeOf: scopeOf,
    labelFor: labelFor,
    resolve: resolve,
    writeOverride: writeOverride,
    deleteOverride: deleteOverride,
    parseRules: parseRules,
    ruleSummary: ruleSummary,
    escapeRuleValue: escapeRuleValue,
    apply: apply,
    applyOverride: applyOverride,
    resetInDocument: resetInDocument,
    preview: preview,
    reader: reader,
    inspect: inspect,
    stopInspect: stopInspect,
    makeSelector: makeSelector
  };
})();

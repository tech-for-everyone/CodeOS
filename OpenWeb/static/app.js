/* OpenWeb browser UI.
 *
 * The markup and stylesheet in this directory describe a complete browser;
 * this file is the behaviour layer referenced by index.html and sw.js.  It
 * keeps its state in localStorage so tabs, history, bookmarks and preferences
 * survive a reload without a server-side account.
 */
(() => {
  'use strict';

  const KEYS = {
    tabs: 'openweb.tabs',
    bookmarks: 'openweb.bookmarks',
    history: 'openweb.history',
    downloads: 'openweb.downloads',
    settings: 'openweb.settings'
  };

  const SEARCH_ENGINES = {
    google: { label: 'Google', url: 'https://www.google.com/search?q=' },
    ddg: { label: 'DuckDuckGo', url: 'https://html.duckduckgo.com/html/?q=' },
    bing: { label: 'Bing', url: 'https://www.bing.com/search?q=' }
  };

  const DEFAULT_SHORTCUTS = [
    { name: 'OpenWeb docs', url: 'https://github.com/CodeOS-Comunity/CodeOS', glyph: 'OW' },
    { name: 'Wikipedia', url: 'https://en.wikipedia.org', glyph: 'W' },
    { name: 'DuckDuckGo', url: 'https://duckduckgo.com', glyph: 'D' },
    { name: 'Example', url: 'https://example.com', glyph: 'E' }
  ];

  const NEW_TAB = 'openweb://newtab';
  const READER_PAGE = 'openweb://reader';
  const MODTOOLS_PAGE = 'openweb://modtools';

  const $ = (id) => document.getElementById(id);
  const MT = () => window.OpenWebModTools;

  const el = {
    form: $('navigation-form'),
    back: $('back-button'),
    forward: $('forward-button'),
    reload: $('reload-button'),
    home: $('home-button'),
    address: $('address-bar'),
    lock: $('lock-icon'),
    engine: $('engine-select'),
    go: $('go-button'),
    newTab: $('new-tab-button'),
    bookmarksButton: $('bookmarks-button'),
    historyButton: $('history-button'),
    downloadsButton: $('downloads-button'),
    settingsButton: $('settings-button'),
    modtoolsButton: $('modtools-button'),
    tabRow: $('tab-row'),
    view: $('browser-view'),
    newTabPage: $('new-tab-page'),
    ntpSearch: $('ntp-search-input'),
    shortcuts: $('shortcuts-grid'),
    status: $('status-text'),
    security: $('security-info'),
    zoom: $('zoom-level'),
    reader: $('reader-mode-btn'),
    bookmarksModal: $('bookmarks-modal'),
    bookmarksTree: $('bookmarks-tree'),
    bookmarkSearch: $('bookmark-search'),
    addBookmark: $('add-bookmark-btn'),
    importBookmarks: $('import-bookmarks-btn'),
    exportBookmarks: $('export-bookmarks-btn'),
    historyModal: $('history-modal'),
    historyList: $('history-list'),
    historySearch: $('history-search'),
    clearHistory: $('clear-history-btn'),
    downloadsModal: $('downloads-modal'),
    downloadsList: $('downloads-list'),
    settingsModal: $('settings-modal'),
    settingsNav: $('settings-nav'),
    settingsPanels: $('settings-panels'),
    addBookmarkModal: $('add-bookmark-modal'),
    addBookmarkForm: $('add-bookmark-form'),
    bmName: $('bm-name'),
    bmUrl: $('bm-url'),
    bmFolder: $('bm-folder'),
    modtoolsModal: $('modtools-modal'),
    modtoolsScope: $('modtools-scope'),
    modtoolsUrl: $('modtools-url'),
    modtoolsTabs: $('modtools-tabs'),
    modtoolsCss: $('modtools-css'),
    modtoolsRules: $('modtools-rules'),
    modtoolsRuleList: $('modtools-rule-list'),
    modtoolsSelector: $('modtools-selector'),
    modtoolsAction: $('modtools-action'),
    modtoolsValue: $('modtools-value'),
    modtoolsAddRule: $('modtools-add-rule'),
    modtoolsJs: $('modtools-js'),
    modtoolsSource: $('modtools-source'),
    modtoolsPreviewSource: $('modtools-preview-source'),
    modtoolsPick: $('modtools-pick'),
    modtoolsPicked: $('modtools-picked'),
    modtoolsStatus: $('modtools-status'),
    modtoolsPreview: $('modtools-preview'),
    modtoolsApply: $('modtools-apply'),
    modtoolsReset: $('modtools-reset')
  };

  function readStore(key, fallback) {
    try {
      const raw = localStorage.getItem(key);
      if (!raw) return fallback;
      const value = JSON.parse(raw);
      return value === null || value === undefined ? fallback : value;
    } catch (err) {
      return fallback;
    }
  }

  function writeStore(key, value) {
    try {
      localStorage.setItem(key, JSON.stringify(value));
    } catch (err) {
      setStatus('Storage is full; changes may not survive a reload');
    }
  }

  const settings = Object.assign({
    engine: 'google',
    home: 'https://example.com',
    rememberHistory: true,
    zoom: 100
  }, readStore(KEYS.settings, {}));

  let tabs = readStore(KEYS.tabs, []);
  let activeId = readStore('openweb.activeTab', null);
  let history = readStore(KEYS.history, []);
  let downloads = readStore(KEYS.downloads, []);
  let bookmarks = readStore(KEYS.bookmarks, null);
  if (!Array.isArray(history)) history = [];
  if (!Array.isArray(downloads)) downloads = [];

  if (!Array.isArray(tabs) || tabs.length === 0) {
    tabs = [{ id: 1, url: NEW_TAB, title: 'New Tab', back: [], forward: [] }];
    activeId = 1;
  }
  if (!bookmarks || !Array.isArray(bookmarks.items)) {
    bookmarks = {
      folders: ['Bookmarks bar', 'Reading list'],
      items: DEFAULT_SHORTCUTS.map((s) => ({
        name: s.name, url: s.url, folder: 'Bookmarks bar'
      }))
    };
  }
  if (!tabs.some((tab) => tab.id === activeId)) activeId = tabs[0].id;
  /* Tolerate tabs written by an older version of the UI. */
  tabs.forEach((tab) => {
    if (!Array.isArray(tab.back)) tab.back = [];
    if (!Array.isArray(tab.forward)) tab.forward = [];
    /* Reader/preview documents are in-memory only; reopen their real page. */
    if (isReader(tab.url) || isModTools(tab.url)) {
      const restore = tab.readerUrl || tab.modSourceUrl || '';
      tab.url = restore || NEW_TAB;
      tab.reader = false;
      tab.readerDoc = '';
      tab.modSource = '';
      tab.modSourceUrl = '';
    }
  });

  let nextId = tabs.reduce((max, tab) => Math.max(max, Number(tab.id) || 0), 0) + 1;
  let loadingCount = 0;
  let currentModal = null;

  function activeTab() {
    return tabs.find((tab) => tab.id === activeId) || tabs[0];
  }

  function persistTabs() {
    writeStore(KEYS.tabs, tabs.map((tab) => ({
      id: tab.id, url: tab.url, title: tab.title,
      back: tab.back.slice(-40), forward: tab.forward.slice(0, 40),
      reader: !!tab.reader, readerUrl: tab.readerUrl || '',
      modSource: tab.modSource || '', modSourceUrl: tab.modSourceUrl || ''
    })));
    writeStore('openweb.activeTab', activeId);
    writeStore(KEYS.settings, settings);
  }

  function setStatus(text) {
    if (el.status) el.status.textContent = text;
  }

  function escapeHtml(text) {
    return String(text == null ? '' : text).replace(/[&<>"']/g, (ch) => ({
      '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;'
    })[ch]);
  }

  function isNewTab(url) {
    return !url || url === 'about:blank' || url === NEW_TAB;
  }

  function isReader(url) {
    return url === READER_PAGE;
  }

  function isModTools(url) {
    return url === MODTOOLS_PAGE;
  }

  function isInternalPage(url) {
    return isNewTab(url) || isReader(url) || isModTools(url);
  }

  function looksLikeUrl(input) {
    if (!input || /\s/.test(input)) return false;
    if (/^[a-z][a-z0-9+.-]*:\/\//i.test(input)) return true;
    if (/^localhost(:\d+)?(\/|$)/i.test(input)) return true;
    if (/^\d{1,3}(\.\d{1,3}){3}(:\d+)?(\/|$)/.test(input)) return true;
    return input.includes('.') && !input.startsWith('.');
  }

  function normalizeInput(input) {
    const value = String(input || '').trim();
    if (!value) return NEW_TAB;
    if (looksLikeUrl(value)) {
      return /^[a-z][a-z0-9+.-]*:\/\//i.test(value) ? value : 'https://' + value;
    }
    const engine = SEARCH_ENGINES[settings.engine] || SEARCH_ENGINES.google;
    return engine.url + encodeURIComponent(value);
  }

  function displayUrl(url) {
    if (isInternalPage(url)) return '';
    return url;
  }

  function shownUrl(tab) {
    if (tab.reader && tab.readerUrl) return tab.readerUrl;
    if (isModTools(tab.url) && tab.modSourceUrl) return tab.modSourceUrl;
    return tab.url || NEW_TAB;
  }

  function secureLevel(url) {
    if (/^https:\/\//i.test(url)) return 'secure';
    if (/^http:\/\//i.test(url)) return 'warning';
    return 'danger';
  }

  function hostOf(url) {
    try {
      return new URL(url).hostname || '';
    } catch (err) {
      const match = /^[a-z][a-z0-9+.-]*:\/\/([^/?#]+)/i.exec(url || '');
      return match ? match[1] : '';
    }
  }

  function recordHistory(url, title) {
    if (!settings.rememberHistory || isNewTab(url)) return;
    history = history.filter((entry) => entry.url !== url);
    history.unshift({ url, title: title || hostOf(url), at: Date.now() });
    history = history.slice(0, 500);
    writeStore(KEYS.history, history);
  }

  function updateChrome() {
    const tab = activeTab();
    const url = tab.url || NEW_TAB;
    const visible = shownUrl(tab);
    el.address.value = displayUrl(visible);
    el.go.title = 'Go to ' + (displayUrl(visible) || 'address');

    const level = secureLevel(visible);
    el.lock.textContent = level === 'secure' ? '\u{1F512}' :
                          level === 'warning' ? '\u{26A0}' : '\u2139';
    el.security.className = 'security-badge ' + level;
    el.security.textContent = isReader(url) ? 'OpenWeb Reader' :
      isModTools(url) ? 'ModTools preview' :
      isNewTab(url) ? '' :
      (level === 'secure' ? 'Secure connection' :
       level === 'warning' ? 'Unencrypted connection' : 'Local content');

    el.back.disabled = !tab.back || tab.back.length === 0;
    el.forward.disabled = !tab.forward || tab.forward.length === 0;
    if (el.reader) el.reader.disabled = isNewTab(url) || isModTools(url);

    document.title = tab.title && tab.title !== 'New Tab'
      ? tab.title + ' - OpenWeb' : 'OpenWeb Browser';
    renderTabs();
  }

  function renderTabs() {
    if (!el.tabRow) return;
    el.tabRow.textContent = '';
    tabs.forEach((tab) => {
      const node = document.createElement('div');
      node.className = 'tab' + (tab.id === activeId ? ' active' : '');
      node.setAttribute('role', 'tab');
      node.setAttribute('aria-selected', tab.id === activeId ? 'true' : 'false');
      node.dataset.id = String(tab.id);
      node.title = tab.url || NEW_TAB;

      const title = document.createElement('span');
      title.className = 'tab-title';
      title.textContent = tab.title || 'New Tab';
      node.appendChild(title);

      const close = document.createElement('button');
      close.className = 'tab-close';
      close.type = 'button';
      close.title = 'Close tab';
      close.setAttribute('aria-label', 'Close tab');
      close.textContent = '\u00D7';
      close.addEventListener('click', (event) => {
        event.stopPropagation();
        closeTab(tab.id);
      });
      node.appendChild(close);

      node.addEventListener('click', () => activateTab(tab.id));
      node.addEventListener('auxclick', (event) => {
        if (event.button === 1) closeTab(tab.id);
      });
      el.tabRow.appendChild(node);
    });
  }

  function showNewTab(show) {
    if (!el.newTabPage) return;
    el.newTabPage.classList.toggle('hidden', !show);
    el.view.style.visibility = show ? 'hidden' : 'visible';
  }

  function renderShortcuts() {
    if (!el.shortcuts) return;
    el.shortcuts.textContent = '';
    bookmarks.items.filter((item) => item.folder === 'Bookmarks bar').slice(0, 8)
      .forEach((item) => {
        const link = document.createElement('a');
        link.className = 'shortcut';
        link.href = item.url;
        link.title = item.url;
        const icon = document.createElement('div');
        icon.className = 'shortcut-icon';
        icon.textContent = (item.name || '?').slice(0, 2).toUpperCase();
        const name = document.createElement('span');
        name.className = 'shortcut-name';
        name.textContent = item.name;
        link.appendChild(icon);
        link.appendChild(name);
        link.addEventListener('click', (event) => {
          event.preventDefault();
          navigate(item.url);
        });
        el.shortcuts.appendChild(link);
      });
  }

  function beginLoading() {
    loadingCount++;
    setStatus('Loading...');
  }

  function endLoading() {
    loadingCount = Math.max(0, loadingCount - 1);
    if (loadingCount === 0) setStatus('Ready');
  }

  function setFrameSandbox(internal) {
    if (!el.view) return;
    /* Reader/ModTools previews are untrusted HTML: never give them the
     * parent's origin. Live pages keep same-origin so ModTools can edit them. */
    el.view.setAttribute('sandbox', internal
      ? 'allow-scripts allow-forms allow-modals allow-popups'
      : 'allow-scripts allow-same-origin allow-forms allow-popups allow-modals');
  }

  function renderSpecialTab(tab) {
    setFrameSandbox(true);
    el.view.removeAttribute('src');
    if (isReader(tab.url) && tab.readerDoc) {
      el.view.srcdoc = tab.readerDoc;
    } else if (isModTools(tab.url) && tab.modSource) {
      el.view.srcdoc = MT() ? MT().preview(tab.modSource, MT().resolve(tab.modSourceUrl || '')) : tab.modSource;
    } else {
      el.view.removeAttribute('srcdoc');
    }
  }

  function navigate(input, fromHistory) {
    const url = normalizeInput(input);
    if (!url) return;
    const tab = activeTab();
    const internal = isInternalPage(url);

    if (!fromHistory && !internal) {
      if (tab.url && tab.url !== url) {
        tab.back.push(tab.url);
        tab.forward.length = 0;
      }
      recordHistory(url);
    }
    if (!internal) {
      tab.reader = false;
      tab.readerUrl = '';
      tab.readerDoc = '';
      tab.modSource = '';
      tab.modSourceUrl = '';
    }

    tab.url = url;
    tab.title = isNewTab(url) ? 'New Tab' :
      isReader(url) ? 'Reader: ' + (hostOf(tab.readerUrl || url) || 'page') :
      isModTools(url) ? 'ModTools preview' :
      hostOf(url) || url;
    persistTabs();
    updateChrome();
    renderShortcuts();
    showNewTab(isNewTab(url));
    if (!isNewTab(url)) el.view.removeAttribute('srcdoc');

    if (isNewTab(url)) {
      el.view.removeAttribute('src');
      setStatus('Ready');
      return;
    }
    if (isReader(url) || isModTools(url)) {
      renderSpecialTab(tab);
      setStatus(isReader(url) ? 'OpenWeb Reader' : 'ModTools isolated preview');
      return;
    }

    setFrameSandbox(false);
    beginLoading();
    try {
      el.view.src = url;
    } catch (err) {
      endLoading();
      setStatus('Could not open ' + url);
    }
  }

  function newTab(url) {
    if (tabs.length >= 24) {
      setStatus('Maximum number of tabs reached');
      return;
    }
    closeModal();
    const tab = { id: nextId++, url: NEW_TAB, title: 'New Tab', back: [], forward: [] };
    tabs.push(tab);
    activeId = tab.id;
    persistTabs();
    updateChrome();
    renderShortcuts();
    showNewTab(true);
    el.view.removeAttribute('src');
    if (url) navigate(url);
    else setTimeout(() => el.address.focus(), 0);
  }

  function closeTab(id) {
    if (tabs.length === 1) {
      tabs[0] = { id: nextId++, url: NEW_TAB, title: 'New Tab', back: [], forward: [] };
      activeId = tabs[0].id;
    } else {
      const index = tabs.findIndex((tab) => tab.id === id);
      if (index < 0) return;
      tabs.splice(index, 1);
      if (id === activeId) {
        activeId = tabs[Math.min(index, tabs.length - 1)].id;
      }
    }
    loadingCount = 0;
    persistTabs();
    updateChrome();
    const tab = activeTab();
    showNewTab(isNewTab(tab.url));
    if (isReader(tab.url) || isModTools(tab.url)) {
      renderSpecialTab(tab);
    } else if (!isNewTab(tab.url)) {
      setFrameSandbox(false);
      el.view.removeAttribute('srcdoc');
      el.view.src = tab.url;
    } else {
      el.view.removeAttribute('src');
    }
  }

  function activateTab(id) {
    if (id === activeId || !tabs.some((tab) => tab.id === id)) return;
    activeId = id;
    persistTabs();
    updateChrome();
    const tab = activeTab();
    showNewTab(isNewTab(tab.url));
    if (isReader(tab.url) || isModTools(tab.url)) {
      renderSpecialTab(tab);
    } else if (!isNewTab(tab.url) && el.view.getAttribute('src') !== tab.url) {
      setFrameSandbox(false);
      el.view.removeAttribute('srcdoc');
      el.view.src = tab.url;
    }
    setStatus('Tab ' + (tabs.indexOf(tab) + 1) + ' of ' + tabs.length);
  }

  function goBack() {
    const tab = activeTab();
    if (!tab.back || !tab.back.length) {
      setStatus('No earlier page in history');
      return;
    }
    const url = tab.back.pop();
    tab.forward.push(tab.url);
    navigate(url, true);
    setStatus('Back to ' + url);
  }

  function goForward() {
    const tab = activeTab();
    if (!tab.forward || !tab.forward.length) {
      setStatus('No later page in history');
      return;
    }
    const url = tab.forward.pop();
    tab.back.push(tab.url);
    navigate(url, true);
    setStatus('Forward to ' + url);
  }

  function reload() {
    const tab = activeTab();
    if (isReader(tab.url) || isModTools(tab.url)) {
      renderSpecialTab(tab);
      setStatus(isReader(tab.url) ? 'OpenWeb Reader' : 'ModTools isolated preview');
      return;
    }
    if (isNewTab(tab.url)) {
      showNewTab(true);
      return;
    }
    setFrameSandbox(false);
    beginLoading();
    el.view.removeAttribute('srcdoc');
    el.view.src = tab.url;
  }

  function stop() {
    loadingCount = 0;
    try {
      if (el.view.contentWindow) el.view.contentWindow.stop();
    } catch (err) {
      /* Cross-origin documents do not expose stop(); the flag is enough. */
    }
    setStatus('Loading stopped');
  }

  function goHome() {
    navigate(settings.home);
  }

  /* ── Modals ── */

  function openModal(node) {
    if (!node) return;
    closeModal();
    node.classList.remove('hidden');
    currentModal = node;
    const focusable = node.querySelector('input, button, select');
    if (focusable) setTimeout(() => focusable.focus(), 0);
  }

  function closeModal() {
    if (currentModal) currentModal.classList.add('hidden');
    currentModal = null;
  }

  document.querySelectorAll('.modal-close').forEach((button) => {
    button.addEventListener('click', closeModal);
  });
  document.querySelectorAll('.modal').forEach((modal) => {
    modal.addEventListener('click', (event) => {
      if (event.target === modal) closeModal();
    });
  });

  /* ── Bookmarks ── */

  function renderBookmarks() {
    if (!el.bookmarksTree) return;
    const query = (el.bookmarkSearch ? el.bookmarkSearch.value : '').toLowerCase();
    el.bookmarksTree.textContent = '';
    let shown = 0;

    bookmarks.folders.forEach((folder) => {
      const items = bookmarks.items.filter((item) =>
        item.folder === folder &&
        (!query || (item.name + ' ' + item.url).toLowerCase().includes(query)));
      if (!items.length) return;
      shown += items.length;

      const wrapper = document.createElement('div');
      wrapper.className = 'bookmark-folder';
      const header = document.createElement('div');
      header.className = 'folder-header';
      header.innerHTML = '<span class="folder-toggle">\u25BE</span>' +
        '<span class="folder-icon">\u{1F4C1}</span>' +
        '<span class="folder-name">' + escapeHtml(folder) + '</span>';
      const content = document.createElement('div');
      content.className = 'folder-content';
      header.addEventListener('click', () => {
        const open = content.style.display !== 'none';
        content.style.display = open ? 'none' : '';
        header.setAttribute('aria-expanded', open ? 'false' : 'true');
      });
      header.setAttribute('aria-expanded', 'true');
      wrapper.appendChild(header);
      wrapper.appendChild(content);

      items.forEach((item) => {
        const row = document.createElement('div');
        row.className = 'bookmark-item';
        row.innerHTML = '<span class="bookmark-favicon">' +
          escapeHtml((item.name || '?').slice(0, 1).toUpperCase()) + '</span>' +
          '<span class="bookmark-title">' + escapeHtml(item.name) + '</span>' +
          '<span class="bookmark-url">' + escapeHtml(item.url) + '</span>';
        row.addEventListener('click', () => {
          navigate(item.url);
          closeModal();
        });
        content.appendChild(row);
      });
      el.bookmarksTree.appendChild(wrapper);
    });

    if (!shown) {
      const empty = document.createElement('div');
      empty.className = 'empty-state';
      empty.innerHTML = '<h3>No bookmarks found</h3><p>Add a bookmark or clear the search.</p>';
      el.bookmarksTree.appendChild(empty);
    }

    if (el.bmFolder) {
      el.bmFolder.textContent = '';
      bookmarks.folders.forEach((folder) => {
        const option = document.createElement('option');
        option.value = folder;
        option.textContent = folder;
        el.bmFolder.appendChild(option);
      });
    }
  }

  function addBookmark(name, url, folder) {
    const clean = normalizeInput(url);
    bookmarks.items.push({
      name: name || hostOf(clean) || clean,
      url: clean,
      folder: folder || bookmarks.folders[0]
    });
    writeStore(KEYS.bookmarks, bookmarks);
    renderBookmarks();
    renderShortcuts();
    setStatus('Bookmark added');
  }

  function exportBookmarks() {
    const blob = new Blob([JSON.stringify(bookmarks, null, 2)],
      { type: 'application/json' });
    const link = document.createElement('a');
    link.href = URL.createObjectURL(blob);
    link.download = 'openweb-bookmarks.json';
    link.click();
    setTimeout(() => URL.revokeObjectURL(link.href), 1000);
  }

  function importBookmarks(text) {
    const parsed = JSON.parse(text);
    const items = Array.isArray(parsed) ? parsed : parsed.items;
    if (!Array.isArray(items)) throw new Error('No bookmark items');
    items.forEach((item) => {
      if (item && item.url) {
        bookmarks.items.push({
          name: item.name || hostOf(item.url),
          url: item.url,
          folder: item.folder || bookmarks.folders[0]
        });
      }
    });
    writeStore(KEYS.bookmarks, bookmarks);
    renderBookmarks();
    renderShortcuts();
    setStatus('Imported ' + items.length + ' bookmarks');
  }

  if (el.bookmarksButton) {
    el.bookmarksButton.addEventListener('click', () => {
      renderBookmarks();
      openModal(el.bookmarksModal);
    });
  }
  if (el.bookmarkSearch) el.bookmarkSearch.addEventListener('input', renderBookmarks);
  if (el.addBookmark) {
    el.addBookmark.addEventListener('click', () => {
      if (el.bmName) el.bmName.value = activeTab().title || '';
      if (el.bmUrl) el.bmUrl.value = displayUrl(activeTab().url);
      renderBookmarks();
      openModal(el.addBookmarkModal);
    });
  }
  if (el.addBookmarkForm) {
    el.addBookmarkForm.addEventListener('submit', (event) => {
      event.preventDefault();
      addBookmark(el.bmName.value, el.bmUrl.value, el.bmFolder.value);
      closeModal();
    });
  }
  if (el.exportBookmarks) el.exportBookmarks.addEventListener('click', exportBookmarks);
  if (el.importBookmarks) {
    el.importBookmarks.addEventListener('click', () => {
      const picker = document.createElement('input');
      picker.type = 'file';
      picker.accept = 'application/json,.json';
      picker.addEventListener('change', () => {
        const file = picker.files && picker.files[0];
        if (!file) return;
        const reader = new FileReader();
        reader.onload = () => {
          try {
            importBookmarks(String(reader.result));
          } catch (err) {
            setStatus('Could not import bookmarks: ' + err.message);
          }
        };
        reader.readAsText(file);
      });
      picker.click();
    });
  }

  /* ── History ── */

  function renderHistory() {
    if (!el.historyList) return;
    const query = (el.historySearch ? el.historySearch.value : '').toLowerCase();
    el.historyList.textContent = '';
    const rows = history.filter((entry) =>
      !query || (entry.title + ' ' + entry.url).toLowerCase().includes(query));
    let lastDay = '';

    if (!rows.length) {
      const empty = document.createElement('div');
      empty.className = 'empty-state';
      empty.innerHTML = '<h3>No history yet</h3><p>Pages you visit will appear here.</p>';
      el.historyList.appendChild(empty);
      return;
    }

    rows.forEach((entry) => {
      const day = new Date(entry.at || Date.now()).toLocaleDateString();
      if (day !== lastDay) {
        lastDay = day;
        const header = document.createElement('div');
        header.className = 'history-day';
        const label = document.createElement('div');
        label.className = 'history-day-header';
        label.textContent = day;
        header.appendChild(label);
        el.historyList.appendChild(header);
      }
      const item = document.createElement('div');
      item.className = 'history-item';
      item.innerHTML = '<span class="history-favicon">' +
        escapeHtml((entry.title || '?').slice(0, 1).toUpperCase()) + '</span>' +
        '<span class="history-info"><span class="history-title">' +
        escapeHtml(entry.title) + '</span><span class="history-url-time">' +
        escapeHtml(entry.url) + '</span></span>';
      item.addEventListener('click', () => {
        navigate(entry.url);
        closeModal();
      });
      el.historyList.appendChild(item);
    });
  }

  if (el.historyButton) {
    el.historyButton.addEventListener('click', () => {
      renderHistory();
      openModal(el.historyModal);
    });
  }
  if (el.historySearch) el.historySearch.addEventListener('input', renderHistory);
  if (el.clearHistory) {
    el.clearHistory.addEventListener('click', () => {
      history = [];
      writeStore(KEYS.history, history);
      renderHistory();
      setStatus('History cleared');
    });
  }

  /* ── Downloads ── */

  function renderDownloads() {
    if (!el.downloadsList) return;
    el.downloadsList.textContent = '';
    if (!downloads.length) {
      const empty = document.createElement('div');
      empty.className = 'empty-state';
      empty.innerHTML = '<h3>No downloads yet</h3><p>Downloads started from pages appear here.</p>';
      el.downloadsList.appendChild(empty);
      return;
    }
    downloads.forEach((item, index) => {
      const row = document.createElement('div');
      row.className = 'download-item ' + (item.error ? 'error' : 'complete');
      row.innerHTML = '<span class="download-icon">\u2193</span>' +
        '<span class="download-info"><span class="download-name">' +
        escapeHtml(item.name) + '</span><span class="download-speed">' +
        escapeHtml(item.url) + '</span></span>' +
        '<span class="download-actions"><button class="download-btn" type="button">' +
        'Open</button><button class="download-btn" type="button">Remove</button></span>';
      const buttons = row.querySelectorAll('button');
      buttons[0].addEventListener('click', () => {
        if (item.url) navigate(item.url);
      });
      buttons[1].addEventListener('click', () => {
        downloads.splice(index, 1);
        writeStore(KEYS.downloads, downloads);
        renderDownloads();
      });
      el.downloadsList.appendChild(row);
    });
  }

  if (el.downloadsButton) {
    el.downloadsButton.addEventListener('click', () => {
      renderDownloads();
      openModal(el.downloadsModal);
    });
  }

  window.addEventListener('message', (event) => {
    const data = event.data;
    if (!data || data.type !== 'openweb-download' || !data.url) return;
    downloads.unshift({ name: data.name || 'download', url: data.url, at: Date.now() });
    downloads = downloads.slice(0, 100);
    writeStore(KEYS.downloads, downloads);
    setStatus('Download started: ' + (data.name || 'file'));
    renderDownloads();
  });

  /* ── Settings ── */

  const SETTINGS_PAGES = [
    {
      id: 'general', label: 'General', rows: [
        { key: 'engine', label: 'Search engine', type: 'select',
          options: Object.keys(SEARCH_ENGINES).map((k) => [k, SEARCH_ENGINES[k].label]) },
        { key: 'home', label: 'Home page', type: 'text' }
      ]
    },
    {
      id: 'privacy', label: 'Privacy', rows: [
        { key: 'rememberHistory', label: 'Remember browsing history', type: 'toggle' }
      ]
    },
    {
      id: 'appearance', label: 'Appearance', rows: [
        { key: 'zoom', label: 'Page zoom', type: 'range', min: 50, max: 200, step: 10 }
      ]
    },
    { id: 'about', label: 'About', rows: [] }
  ];

  function applySettings() {
    if (el.engine) el.engine.value = settings.engine;
    if (el.zoom) {
      const zoom = Math.max(50, Math.min(200, Number(settings.zoom) || 100));
      el.zoom.textContent = zoom + '%';
      el.view.style.zoom = zoom + '%';
    }
    writeStore(KEYS.settings, settings);
  }

  function renderSettingsPage(pageId) {
    if (!el.settingsPanels) return;
    const page = SETTINGS_PAGES.find((p) => p.id === pageId) || SETTINGS_PAGES[0];
    el.settingsPanels.textContent = '';
    const panel = document.createElement('div');
    panel.className = 'settings-panel active';

    if (page.id === 'about') {
      panel.innerHTML = '<div class="settings-section"><h3>OpenWeb</h3>' +
        '<p>Version 2.1 - your web, shaped by you.</p>' +
        '<p>OpenWeb is built for CodeOS, not reskinned from another browser. ' +
        'Reader mode renders a clean, readable page; ModTools edits CSS, ' +
        'DOM rules and JavaScript or previews a pasted source. Tabs, history, ' +
        'bookmarks and overrides stay local to this device.</p></div>';
    }

    page.rows.forEach((row) => {
      const wrapper = document.createElement('div');
      wrapper.className = 'settings-section';
      const item = document.createElement('div');
      item.className = 'setting-item';
      const info = document.createElement('div');
      info.className = 'setting-info';
      info.innerHTML = '<div class="setting-label">' + escapeHtml(row.label) + '</div>';
      item.appendChild(info);

      if (row.type === 'toggle') {
        const label = document.createElement('label');
        label.className = 'toggle';
        const input = document.createElement('input');
        input.type = 'checkbox';
        input.checked = !!settings[row.key];
        input.addEventListener('change', () => {
          settings[row.key] = input.checked;
          applySettings();
        });
        const slider = document.createElement('span');
        slider.className = 'toggle-slider';
        label.appendChild(input);
        label.appendChild(slider);
        item.appendChild(label);
      } else if (row.type === 'select') {
        const select = document.createElement('select');
        row.options.forEach(([value, label]) => {
          const option = document.createElement('option');
          option.value = value;
          option.textContent = label;
          option.selected = settings[row.key] === value;
          select.appendChild(option);
        });
        select.addEventListener('change', () => {
          settings[row.key] = select.value;
          applySettings();
        });
        item.appendChild(select);
      } else if (row.type === 'text') {
        const input = document.createElement('input');
        input.type = 'text';
        input.value = settings[row.key] || '';
        input.addEventListener('change', () => {
          settings[row.key] = normalizeInput(input.value);
          input.value = settings[row.key];
          applySettings();
        });
        item.appendChild(input);
      } else if (row.type === 'range') {
        const input = document.createElement('input');
        input.type = 'range';
        input.min = String(row.min);
        input.max = String(row.max);
        input.step = String(row.step);
        input.value = String(settings[row.key] || 100);
        input.addEventListener('input', () => {
          settings[row.key] = Number(input.value);
          applySettings();
        });
        item.appendChild(input);
      }

      wrapper.appendChild(item);
      panel.appendChild(wrapper);
    });
    el.settingsPanels.appendChild(panel);

    if (el.settingsNav) {
      el.settingsNav.querySelectorAll('.settings-nav-item').forEach((node) => {
        node.classList.toggle('active', node.dataset.page === page.id);
      });
    }
  }

  if (el.settingsButton) {
    el.settingsButton.addEventListener('click', () => {
      if (el.settingsNav) {
        el.settingsNav.textContent = '';
        SETTINGS_PAGES.forEach((page) => {
          const item = document.createElement('button');
          item.type = 'button';
          item.className = 'settings-nav-item';
          item.dataset.page = page.id;
          item.textContent = page.label;
          item.addEventListener('click', () => renderSettingsPage(page.id));
          el.settingsNav.appendChild(item);
        });
      }
      renderSettingsPage('general');
      openModal(el.settingsModal);
    });
  }

  /* ── ModTools and OpenWeb Reader ── */

  function modBaseUrl() {
    const tab = activeTab();
    if (isReader(tab.url)) return tab.readerUrl || '';
    if (isModTools(tab.url)) return tab.modSourceUrl || '';
    return tab.url || '';
  }

  function modSetStatus(text) {
    if (el.modtoolsStatus) el.modtoolsStatus.textContent = text;
    setStatus('ModTools: ' + text);
  }

  function modRulesFromUi() {
    try {
      return MT() ? MT().parseRules(el.modtoolsRules.value) : [];
    } catch (err) {
      modSetStatus('Invalid DOM rules: ' + err.message);
      return null;
    }
  }

  function modOverrideFromUi() {
    const rules = modRulesFromUi();
    if (!rules) return null;
    return {
      enabled: true,
      css: el.modtoolsCss.value,
      js: el.modtoolsJs.value,
      rules: rules,
      source: el.modtoolsSource.value
    };
  }

  function renderModRules(rules) {
    if (!el.modtoolsRuleList || !MT()) return;
    el.modtoolsRuleList.textContent = '';
    if (!rules.length) {
      const empty = document.createElement('div');
      empty.className = 'modtools-empty';
      empty.textContent = 'No DOM rules yet. Add one below or paste a JSON array.';
      el.modtoolsRuleList.appendChild(empty);
      return;
    }
    rules.forEach((rule, index) => {
      const row = document.createElement('div');
      row.className = 'modtools-rule';
      const label = document.createElement('span');
      label.className = 'modtools-rule-label';
      label.textContent = MT().ruleSummary(rule) +
        (rule.value ? ' \u2192 ' + rule.value : '');
      const remove = document.createElement('button');
      remove.type = 'button';
      remove.className = 'modtools-rule-remove';
      remove.title = 'Remove rule';
      remove.textContent = '\u00D7';
      remove.addEventListener('click', () => {
        const current = modRulesFromUi() || [];
        current.splice(index, 1);
        el.modtoolsRules.value = JSON.stringify(current, null, 2);
        renderModRules(current);
      });
      row.appendChild(label);
      row.appendChild(remove);
      el.modtoolsRuleList.appendChild(row);
    });
  }

  function loadModToolsUi() {
    if (!MT()) {
      modSetStatus('ModTools failed to load');
      return;
    }
    const tab = activeTab();
    const base = modBaseUrl();
    el.modtoolsUrl.value = base ||
      (isNewTab(tab.url) ? 'New tab - use Source preview' : tab.url || 'No page');

    const state = MT().load();
    const urlKey = MT().keyFor(base, 'url');
    const hostKey = MT().keyFor(base, 'host');
    let scope = 'url';
    if (state.overrides[urlKey]) scope = 'url';
    else if (state.overrides[hostKey]) scope = 'host';
    else if (state.overrides['*']) scope = 'all';
    el.modtoolsScope.value = scope;

    const resolved = MT().resolve(base);
    el.modtoolsCss.value = resolved.css;
    el.modtoolsJs.value = resolved.js;
    el.modtoolsRules.value = JSON.stringify(resolved.rules, null, 2);
    el.modtoolsSource.value = isModTools(tab.url) ? tab.modSource : resolved.source;
    renderModRules(resolved.rules);
    modSetStatus(resolved.keys.length
      ? 'Active: ' + resolved.keys.map((key) => MT().labelFor(key)).join(', ')
      : 'No overrides yet');
  }

  function applyModToolsToFrame(override) {
    if (!MT()) return { ok: false, reason: 'unavailable', errors: [] };
    const result = override
      ? MT().applyOverride(el.view, modBaseUrl(), override)
      : MT().apply(el.view, modBaseUrl());
    if (!result.ok) {
      modSetStatus('Cross-origin page - use Source preview');
    } else if (result.errors.length) {
      modSetStatus('Applied with ' + result.errors.length + ' error(s): ' + result.errors[0]);
    } else {
      modSetStatus(result.applied ? 'Overrides applied to this page' : 'No overrides on this page');
    }
    return result;
  }

  function openModTools() {
    if (!MT()) {
      setStatus('ModTools failed to load');
      return;
    }
    loadModToolsUi();
    openModal(el.modtoolsModal);
  }

  function saveModTools() {
    if (!MT()) return;
    const override = modOverrideFromUi();
    if (!override) return;
    const base = modBaseUrl();
    if (!base) {
      modSetStatus('Navigate to a page first');
      return;
    }
    const scope = el.modtoolsScope.value;
    if (MT().writeOverride(base, scope, override)) {
      modSetStatus('Saved for ' + MT().labelFor(MT().keyFor(base, scope)));
      applyModToolsToFrame(override);
    } else {
      modSetStatus('Could not save - local storage is unavailable');
    }
  }

  function resetModTools() {
    if (!MT()) return;
    const base = modBaseUrl();
    if (!base) return;
    const scope = el.modtoolsScope.value;
    const key = MT().keyFor(base, scope);
    const existed = MT().deleteOverride(base, scope);
    MT().resetInDocument(el.view);
    const tab = activeTab();
    if (isReader(tab.url) || isModTools(tab.url)) {
      renderSpecialTab(tab);
    } else if (!isNewTab(tab.url)) {
      el.view.removeAttribute('srcdoc');
      el.view.src = base;
    }
    loadModToolsUi();
    modSetStatus(existed
      ? 'Removed ' + MT().labelFor(key) + '; page reloaded'
      : 'No saved override at this scope');
  }

  function openSourcePreview() {
    if (!MT()) return;
    const override = modOverrideFromUi();
    if (!override) return;
    const tab = activeTab();
    tab.modSource = el.modtoolsSource.value;
    tab.modSourceUrl = modBaseUrl() || tab.url || NEW_TAB;
    closeModal();
    navigate(MODTOOLS_PAGE);
    setStatus('ModTools isolated preview');
  }

  function toggleReaderMode() {
    if (!MT()) {
      setStatus('Reader mode failed to load');
      return;
    }
    const tab = activeTab();
    if (isReader(tab.url)) {
      navigate(tab.readerUrl || NEW_TAB, true);
      return;
    }
    if (isNewTab(tab.url) || isModTools(tab.url)) {
      setStatus('Reader mode needs a web page');
      return;
    }
    const url = tab.url;
    setStatus('Building OpenWeb Reader...');
    fetch(url).then((response) => {
      if (!response.ok) throw new Error('HTTP ' + response.status);
      return response.text();
    }).then((html) => {
      if (activeTab() !== tab || tab.url !== url) return;
      tab.reader = true;
      tab.readerUrl = url;
      tab.readerDoc = MT().reader(html, url);
      tab.url = READER_PAGE;
      tab.title = 'Reader: ' + (hostOf(url) || 'page');
      persistTabs();
      updateChrome();
      renderSpecialTab(tab);
      setStatus('OpenWeb Reader - F9 to return');
    }).catch((err) => {
      setStatus('Reader unavailable: ' + err.message);
    });
  }

  if (el.modtoolsButton) el.modtoolsButton.addEventListener('click', openModTools);
  if (el.modtoolsTabs) {
    el.modtoolsTabs.querySelectorAll('.modtools-tab').forEach((button) => {
      button.addEventListener('click', () => {
        const panel = button.dataset.panel;
        el.modtoolsTabs.querySelectorAll('.modtools-tab').forEach((tab) => {
          tab.classList.toggle('active', tab === button);
        });
        el.modtoolsModal.querySelectorAll('.modtools-panel').forEach((node) => {
          node.classList.toggle('hidden', node.dataset.panel !== panel);
        });
      });
    });
  }
  if (el.modtoolsAddRule && MT()) {
    el.modtoolsAddRule.addEventListener('click', () => {
      const selector = el.modtoolsSelector.value.trim();
      if (!selector) {
        modSetStatus('Enter a selector first');
        return;
      }
      const rules = modRulesFromUi() || [];
      rules.push({
        selector: selector,
        action: el.modtoolsAction.value,
        value: el.modtoolsValue.value
      });
      el.modtoolsRules.value = JSON.stringify(rules, null, 2);
      el.modtoolsSelector.value = '';
      el.modtoolsValue.value = '';
      renderModRules(rules);
      modSetStatus('Rule added - press Apply & save to keep it');
    });
  }
  if (el.modtoolsPreview) el.modtoolsPreview.addEventListener('click', () => {
    const override = modOverrideFromUi();
    if (!override) return;
    const result = applyModToolsToFrame(override);
    if (result.ok) modSetStatus('Preview applied (not saved)');
  });
  if (el.modtoolsApply) el.modtoolsApply.addEventListener('click', saveModTools);
  if (el.modtoolsReset) el.modtoolsReset.addEventListener('click', resetModTools);
  if (el.modtoolsPreviewSource) {
    el.modtoolsPreviewSource.addEventListener('click', openSourcePreview);
  }
  if (el.modtoolsPick && MT()) {
    el.modtoolsPick.addEventListener('click', () => {
      const result = MT().inspect(el.view, (info) => {
        el.modtoolsPicked.textContent = info.selector;
        el.modtoolsSelector.value = info.selector;
        modSetStatus('Picked ' + info.tag + (info.id ? '#' + info.id : '') +
          ' - choose an action, then add the rule');
      });
      if (!result.ok) modSetStatus('Cannot inspect this cross-origin page');
      else modSetStatus('Click an element in the page...');
    });
  }

  /* ── Toolbar, keyboard and frame ── */

  if (el.form) {
    el.form.addEventListener('submit', (event) => {
      event.preventDefault();
      navigate(el.address.value);
      el.address.blur();
    });
  }
  if (el.engine) {
    el.engine.value = settings.engine;
    el.engine.addEventListener('change', () => {
      settings.engine = el.engine.value;
      applySettings();
      setStatus('Search engine: ' + SEARCH_ENGINES[settings.engine].label);
    });
  }
  if (el.back) el.back.addEventListener('click', goBack);
  if (el.forward) el.forward.addEventListener('click', goForward);
  if (el.reload) el.reload.addEventListener('click', reload);
  if (el.home) el.home.addEventListener('click', goHome);
  if (el.newTab) el.newTab.addEventListener('click', () => newTab());
  if (el.ntpSearch) {
    el.ntpSearch.addEventListener('keydown', (event) => {
      if (event.key === 'Enter') {
        event.preventDefault();
        navigate(el.ntpSearch.value);
      }
    });
  }
  if (el.reader) el.reader.addEventListener('click', toggleReaderMode);

  if (el.view) {
    el.view.addEventListener('load', () => {
      endLoading();
      const tab = activeTab();
      try {
        const doc = el.view.contentDocument;
        if (doc && doc.title) tab.title = doc.title;
      } catch (err) {
        /* Foreign origins only expose the host-derived title. */
      }
      persistTabs();
      updateChrome();
      recordHistory(tab.url, tab.title);
      if (isNewTab(tab.url)) showNewTab(true);
      if (MT() && !isNewTab(tab.url) && MT().resolve(modBaseUrl()).keys.length) {
        const result = MT().apply(el.view, modBaseUrl());
        if (result.ok && result.errors.length) {
          setStatus('ModTools applied with ' + result.errors.length + ' error(s)');
        } else if (result.ok && result.applied) {
          setStatus('ModTools overrides applied');
        }
      }
    });
    el.view.addEventListener('error', () => {
      endLoading();
      setStatus('Could not load ' + activeTab().url);
    });
  }

  document.addEventListener('keydown', (event) => {
    const modifier = event.ctrlKey || event.metaKey;
    if (event.key === 'Escape') {
      if (currentModal) closeModal();
      else if (document.activeElement === el.address) el.address.blur();
      return;
    }
    if (event.key === 'F9') {
      event.preventDefault();
      toggleReaderMode();
      return;
    }
    if (!modifier) return;
    const key = event.key.toLowerCase();
    if (key === 't') { event.preventDefault(); newTab(); }
    else if (key === 'm') { event.preventDefault(); openModTools(); }
    else if (key === 'w') { event.preventDefault(); closeTab(activeId); }
    else if (key === 'l') {
      event.preventDefault();
      el.address.focus();
      el.address.select();
    }
    else if (key === 'r') { event.preventDefault(); reload(); }
    else if (key === 'h') { event.preventDefault(); renderHistory(); openModal(el.historyModal); }
    else if (key === 'b') { event.preventDefault(); renderBookmarks(); openModal(el.bookmarksModal); }
    else if (key === 'j') { event.preventDefault(); renderDownloads(); openModal(el.downloadsModal); }
    else if (key === '+' || key === '=') {
      event.preventDefault();
      settings.zoom = Math.min(200, Number(settings.zoom) + 10);
      applySettings();
    } else if (key === '-') {
      event.preventDefault();
      settings.zoom = Math.max(50, Number(settings.zoom) - 10);
      applySettings();
    }
  });

  /* Service worker registration lives here so index.html has one script tag. */
  if ('serviceWorker' in navigator) {
    window.addEventListener('load', () => {
      navigator.serviceWorker.register('/static/sw.js').catch((err) => {
        setStatus('Offline cache unavailable: ' + err.message);
      });
    });
  }

  applySettings();
  updateChrome();
  renderShortcuts();
  renderBookmarks();
  showNewTab(isNewTab(activeTab().url));
  if (!isNewTab(activeTab().url)) el.view.src = activeTab().url;
  setStatus('Ready');
})();

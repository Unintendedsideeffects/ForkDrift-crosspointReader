window.ConfiguratorSettingsDeviceUi = (function () {
  const SETTINGS_TABS = [
    { label: 'Display', categories: ['Display'] },
    { label: 'Reader', categories: ['Reader', 'Customise Status Bar'] },
    { label: 'Controls', categories: ['Controls'] },
    { label: 'System', categories: ['System', 'Time', 'Advanced'] },
  ];

  const CATEGORY_ORDER = [
    'Reader',
    'Display',
    'Customise Status Bar',
    'Controls',
    'System',
    'Time',
    'Advanced',
  ];

  const SETTINGS_TAB_TOPICS = [
    [
      { label: 'APPEARANCE', keys: ['uiTheme', 'recentBooksView', 'darkMode', 'fadingFix'] },
      { label: 'SLEEP SCREEN', keys: ['sleepScreen', 'sleepScreenSource', 'sleepScreenCoverMode', 'sleepScreenCoverFilter', 'sleepCycleMode', 'haikuClockLandscape', 'trmnlSleepEnabled', 'sleepPinnedPath'] },
      { label: 'DISPLAY', keys: ['refreshFrequency'] },
    ],
    [
      { label: 'TEXT', keys: ['fontFamily', 'userFontPath', 'fontSize', 'lineSpacing', 'textAntiAliasing', 'hyphenationEnabled', 'embeddedStyle'] },
      { label: 'LAYOUT', keys: ['orientation', 'paragraphAlignment', 'screenMargin', 'extraParagraphSpacing', 'forceParagraphIndents'] },
      { label: 'READING AIDS', keys: ['focusReadingEnabled', 'guideReadingEnabled', 'imageRendering'] },
      { label: 'STATUS BAR', keys: ['globalStatusBarPosition', 'hideBatteryPercentage'] },
    ],
    [],
    [
      { label: 'GENERAL', keys: ['sleepTimeoutMinutes', 'showHiddenFiles', 'todoOpenDirectToToday', 'moveFinishedToReadFolder'] },
      { label: 'TIME', keys: ['timeMode', 'timeZoneOffset'] },
      { label: 'ADVANCED', keys: [], headerOnly: true },
      { label: 'FILE SERVER', keys: ['usbMscPromptOnConnect', 'backgroundServerMode'] },
      { label: 'ANKICONNECT', keys: ['ankiConnectUrl', 'ankiConnectDeck'] },
      { label: null, keys: ['deviceName', 'developerMode'] },
    ],
  ];

  function isSettingVisible(setting, values, active) {
    if (setting.hidden) return false;
    if (setting.visibleWhen && values[setting.visibleWhen.key] !== setting.visibleWhen.eq) return false;
    return true;
  }

  function isSettingEnabled(setting, active) {
    return !setting.featureKey || !!active[setting.featureKey];
  }

  function getAllowedOptions(setting, active) {
    return (setting.options || []).filter(option => !option.featureKey || active[option.featureKey]);
  }

  function formatSettingValue(setting, value) {
    if (setting.type === 'toggle') return value ? 'On' : 'Off';
    if (setting.type === 'enum') {
      const match = (setting.options || []).find(option => option.value === value);
      return match ? match.label : String(value);
    }
    if (setting.type === 'value') return String(value);
    if (setting.type === 'string') return value || '';
    return String(value ?? '');
  }

  function makeSettingItem(setting, values, active, featureNames) {
    const enabled = isSettingEnabled(setting, active);
    return {
      type: 'setting',
      key: setting.key,
      label: setting.label,
      value: formatSettingValue(setting, values[setting.key]),
      settingType: setting.type,
      disabled: !enabled,
      requiredFeature: !enabled ? (featureNames[setting.featureKey] ?? setting.featureKey) : undefined,
    };
  }

  function buildItemsForTab(schemaSettings, tabIndex, values, active, featureNames = {}) {
    const tab = SETTINGS_TABS[tabIndex];
    if (!tab) return [];

    const byKey = new Map();
    schemaSettings.forEach(setting => {
      if (!tab.categories.includes(setting.category)) return;
      if (!isSettingVisible(setting, values, active)) return;
      byKey.set(setting.key, setting);
    });

    const items = [];
    const used = new Set();
    const topics = SETTINGS_TAB_TOPICS[tabIndex] || [];

    const appendTopic = (topic) => {
      const topicItems = [];
      (topic.keys || []).forEach(key => {
        const setting = byKey.get(key);
        if (!setting) return;
        topicItems.push(makeSettingItem(setting, values, active, featureNames));
        used.add(key);
      });
      if (!topicItems.length && !topic.headerOnly) return;
      if (topic.label) items.push({ type: 'header', label: topic.label });
      topicItems.forEach(item => items.push(item));
    };

    if (topics.length) {
      topics.forEach(appendTopic);
      schemaSettings.forEach(setting => {
        if (used.has(setting.key)) return;
        if (!tab.categories.includes(setting.category)) return;
        if (tabIndex === 1 && setting.category === 'Reader') return;
        if (!isSettingVisible(setting, values, active)) return;
        if (setting.category === 'Customise Status Bar' && tabIndex === 1) {
          if (!items.some(item => item.type === 'header' && item.label === 'CUSTOMISE STATUS BAR')) {
            items.push({ type: 'header', label: 'CUSTOMISE STATUS BAR' });
          }
        }
        items.push(makeSettingItem(setting, values, active, featureNames));
      });
      return items;
    }

    schemaSettings.forEach(setting => {
      if (!tab.categories.includes(setting.category)) return;
      if (!isSettingVisible(setting, values, active)) return;
      items.push(makeSettingItem(setting, values, active, featureNames));
    });
    return items;
  }

  function buildAllItems(schemaSettings, values, active, featureNames = {}) {
    const byCategory = new Map();
    schemaSettings.forEach(setting => {
      if (!isSettingVisible(setting, values, active)) return;
      const category = setting.category || 'Settings';
      if (!byCategory.has(category)) byCategory.set(category, []);
      const enabled = isSettingEnabled(setting, active);
      byCategory.get(category).push({
        type: 'setting',
        key: setting.key,
        label: setting.label,
        value: formatSettingValue(setting, values[setting.key]),
        settingType: setting.type,
        disabled: !enabled,
        requiredFeature: !enabled ? (featureNames[setting.featureKey] ?? setting.featureKey) : undefined,
      });
    });

    const items = [];
    const orderedCategories = [
      ...CATEGORY_ORDER.filter(category => byCategory.has(category)),
      ...Array.from(byCategory.keys()).filter(category => !CATEGORY_ORDER.includes(category)).sort(),
    ];

    orderedCategories.forEach(category => {
      const categoryItems = byCategory.get(category);
      if (!categoryItems?.length) return;
      items.push({ type: 'header', label: category.toUpperCase() });
      categoryItems.forEach(item => items.push(item));
    });

    return items;
  }

  function buildPreviewModel(schemaSettings, values, active, nav, options = {}) {
    const useAllCategories = options.allCategories !== false;
    const featureNames = options.featureNames ?? {};
    const tabIndex = Math.max(0, Math.min(SETTINGS_TABS.length - 1, nav?.tabIndex ?? 0));
    const items = useAllCategories
      ? buildAllItems(schemaSettings, values, active, featureNames)
      : buildItemsForTab(schemaSettings, tabIndex, values, active, featureNames);
    const selectableCount = items.length;
    let selectedIndex = nav?.selectedIndex ?? 1;
    if (selectedIndex < 1) selectedIndex = 1;
    if (selectedIndex > selectableCount) selectedIndex = Math.max(1, selectableCount);

    const nextTabLabel = SETTINGS_TABS[(tabIndex + 1) % SETTINGS_TABS.length].label;
    const confirmLabel = !useAllCategories && selectedIndex === 0 ? nextTabLabel : 'Toggle';

    return {
      tabs: SETTINGS_TABS.map((tab, index) => ({ label: tab.label, selected: index === tabIndex })),
      items,
      nav: { tabIndex, selectedIndex, selectableCount, allCategories: useAllCategories },
      hints: {
        back: 'Back',
        confirm: confirmLabel,
        up: 'Up',
        down: 'Down',
      },
      version: '1.3.0-dev',
    };
  }

  function clampNumeric(value, min, max, step) {
    let next = value + step;
    if (next > max) next = min;
    if (next < min) next = max;
    return next;
  }

  function cycleSetting(setting, currentValue, active, direction = 1) {
    if (setting.type === 'toggle') {
      return currentValue ? 0 : 1;
    }
    if (setting.type === 'enum') {
      const options = getAllowedOptions(setting, active);
      if (!options.length) return currentValue;
      const currentIndex = options.findIndex(option => option.value === currentValue);
      const startIndex = currentIndex === -1 ? 0 : currentIndex;
      const nextIndex = (startIndex + direction + options.length) % options.length;
      return options[nextIndex].value;
    }
    if (setting.type === 'value') {
      const min = setting.min ?? 0;
      const max = setting.max ?? min;
      const step = setting.step ?? 1;
      return clampNumeric(Number(currentValue) || min, min, max, step * direction);
    }
    return currentValue;
  }

  function moveSelection(nav, items, direction) {
    const maxIndex = items.length;
    let selectedIndex = nav.selectedIndex ?? 1;
    if (selectedIndex < 1) selectedIndex = 1;
    for (let attempt = 0; attempt <= maxIndex + 1; attempt += 1) {
      selectedIndex += direction;
      if (selectedIndex < 1) selectedIndex = maxIndex;
      if (selectedIndex > maxIndex) selectedIndex = 1;
      const item = items[selectedIndex - 1];
      if (item && item.type !== 'header' && !item.disabled) break;
    }
    return { ...nav, selectedIndex };
  }

  function escapeHtml(value) {
    return String(value)
      .replaceAll('&', '&amp;')
      .replaceAll('<', '&lt;')
      .replaceAll('>', '&gt;')
      .replaceAll('"', '&quot;')
      .replaceAll("'", '&#039;');
  }

  function syncScrollRail(root) {
    const list = root.querySelector('.device-settings-list');
    const rail = root.querySelector('.device-settings-scroll-rail');
    const thumb = root.querySelector('.device-settings-scroll-thumb');
    if (!list || !rail || !thumb) return;

    const scrollHeight = list.scrollHeight;
    const clientHeight = list.clientHeight;
    const needsScroll = scrollHeight > clientHeight + 1;
    rail.classList.toggle('is-static', !needsScroll);
    if (!needsScroll) return;

    const trackTop = 8;
    const trackBottom = 8;
    const trackHeight = Math.max(0, clientHeight - trackTop - trackBottom);
    const thumbHeight = Math.max(24, Math.round((clientHeight / scrollHeight) * trackHeight));
    const maxScroll = scrollHeight - clientHeight;
    const scrollRatio = maxScroll > 0 ? list.scrollTop / maxScroll : 0;
    const thumbTop = trackTop + Math.round((trackHeight - thumbHeight) * scrollRatio);

    thumb.style.height = thumbHeight + 'px';
    thumb.style.top = thumbTop + 'px';
  }

  function bindScrollRail(root) {
    const list = root.querySelector('.device-settings-list');
    if (!list) return;
    const update = () => syncScrollRail(root);
    list.addEventListener('scroll', update, { passive: true });
    if (typeof ResizeObserver !== 'undefined') {
      const observer = new ResizeObserver(update);
      observer.observe(list);
    }
    window.requestAnimationFrame(update);
  }

  function scrollSelectedIntoView(root) {
    const selected = root.querySelector('.device-settings-row.selected');
    if (!selected) return;
    selected.scrollIntoView({ block: 'nearest' });
  }

  function renderMarkup(model) {
    const selectedIndex = model.nav?.selectedIndex ?? 1;
    const showTabs = model.nav?.allCategories === false;

    const tabsHtml = showTabs
      ? (model.tabs || []).map((tab, index) => {
          const classes = ['device-settings-tab'];
          if (tab.selected) classes.push('selected');
          if (selectedIndex === 0 && tab.selected) classes.push('selected-row');
          return '<button type="button" class="' + classes.join(' ') + '" data-settings-tab="' + index + '">' +
            escapeHtml(tab.label) + '</button>';
        }).join('')
      : '';

    const rowsHtml = (model.items || []).map((item, index) => {
      const rowIndex = index + 1;
      const selected = selectedIndex === rowIndex;
      if (item.type === 'header') {
        return '<div class="device-settings-row header' + (selected ? ' selected' : '') + '" data-settings-index="' +
          rowIndex + '"><div class="device-settings-row-label">' + escapeHtml(item.label) + '</div></div>';
      }
      if (item.disabled) {
        const tooltip = item.requiredFeature ? 'Requires: ' + item.requiredFeature : 'Requires unavailable module';
        return '<div class="device-settings-row disabled" data-settings-index="' + rowIndex + '">' +
          '<span class="device-settings-row-label">' + escapeHtml(item.label) + '</span>' +
          '<span class="device-settings-row-value device-settings-row-locked" title="' + escapeHtml(tooltip) + '">' +
            '<svg width="8" height="10" viewBox="0 0 8 10" fill="currentColor" aria-hidden="true">' +
              '<rect x="1" y="4.5" width="6" height="5.5" rx="1"/>' +
              '<path d="M2.2 4.5V3A1.8 1.8 0 015.8 3v1.5" fill="none" stroke="currentColor" stroke-width="1.2"/>' +
            '</svg>' +
          '</span>' +
        '</div>';
      }
      return '<button type="button" class="device-settings-row' + (selected ? ' selected' : '') +
        '" data-settings-index="' + rowIndex + '" data-settings-key="' + escapeHtml(item.key) + '">' +
        '<span class="device-settings-row-label">' + escapeHtml(item.label) + '</span>' +
        '<span class="device-settings-row-value">' + escapeHtml(item.value || '') + '</span>' +
      '</button>';
    }).join('');

    const hints = model.hints || {};
    return (
      '<div class="device-settings-header">' +
        '<div class="device-settings-title">Settings</div>' +
        '<div class="device-settings-version">' + escapeHtml(model.version || '') + '</div>' +
      '</div>' +
      (showTabs
        ? '<div class="device-settings-tabs' + (selectedIndex === 0 ? ' selected-row' : '') + '">' + tabsHtml + '</div>'
        : '') +
      '<div class="device-settings-body">' +
        '<div class="device-settings-list">' + rowsHtml + '</div>' +
        '<div class="device-settings-scroll-rail" aria-hidden="true">' +
          '<div class="device-settings-scroll-up"></div>' +
          '<div class="device-settings-scroll-track"></div>' +
          '<div class="device-settings-scroll-thumb"></div>' +
          '<div class="device-settings-scroll-down"></div>' +
        '</div>' +
      '</div>' +
      '<div class="device-settings-hints">' +
        '<span class="device-settings-hint">' + escapeHtml(hints.back || 'Back') + '</span>' +
        '<span class="device-settings-hint">' + escapeHtml(hints.confirm || 'Toggle') + '</span>' +
        '<span class="device-settings-hint">' + escapeHtml(hints.up || 'Up') + '</span>' +
        '<span class="device-settings-hint">' + escapeHtml(hints.down || 'Down') + '</span>' +
      '</div>'
    );
  }

  function bindEditor(root, handlers) {
    if (!root) return;
    root.querySelectorAll('[data-settings-tab]').forEach(button => {
      button.addEventListener('click', () => {
        handlers.onSelectTab(parseInt(button.dataset.settingsTab, 10));
      });
    });
    root.querySelectorAll('[data-settings-index]').forEach(button => {
      button.addEventListener('click', () => {
        handlers.onSelectIndex(parseInt(button.dataset.settingsIndex, 10));
      });
      button.addEventListener('dblclick', () => {
        if (button.dataset.settingsKey) handlers.onConfirm();
      });
    });
    scrollSelectedIntoView(root);
    bindScrollRail(root);
  }

  return {
    SETTINGS_TABS,
    SETTINGS_TAB_TOPICS,
    CATEGORY_ORDER,
    isSettingVisible,
    getAllowedOptions,
    formatSettingValue,
    buildItemsForTab,
    buildAllItems,
    buildPreviewModel,
    cycleSetting,
    moveSelection,
    renderMarkup,
    bindEditor,
    syncScrollRail,
  };
})();

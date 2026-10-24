const tabPanels = {overview:'overview-panel', settings:'settings', storage:'storage-panel', logs:'logs-panel'};
function selectTab(name, focus = false) {
  if (rebooting) return;
  for (const [key, panel] of Object.entries(tabPanels)) {
    const selected = key === name, tab = $('tab-' + key);
    $(panel).hidden = !selected;
    tab.setAttribute('aria-selected', String(selected));
    tab.tabIndex = selected ? 0 : -1;
  }
  if (focus) $('tab-' + name).focus();
  if (name === 'logs') return refreshLogs();
}
const tabNames = Object.keys(tabPanels);
for (const [index, name] of tabNames.entries()) {
  const tab = $('tab-' + name);
  tab.addEventListener('click', () => selectTab(name));
  tab.addEventListener('keydown', event => {
    const next = {ArrowRight:(index + 1) % tabNames.length, ArrowLeft:(index + tabNames.length - 1) % tabNames.length,
      Home:0, End:tabNames.length - 1}[event.key];
    if (next === undefined) return;
    event.preventDefault();
    return selectTab(tabNames[next], true);
  });
}

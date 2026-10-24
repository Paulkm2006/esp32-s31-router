let logCursor = 0, logEntries = [], logsRefreshing = null;
async function refreshLogs() {
  const version = authVersion;
  if (logsRefreshing === version || rebooting || $('dashboard').hidden || $('logs-panel').hidden) return;
  logsRefreshing = version;
  try {
    let more;
    do {
      const data = await request('/api/logs?after=' + logCursor);
      if (version !== authVersion || $('logs-panel').hidden) return;
      const output = $('logs-content');
      const follow = !logEntries.length || output.scrollTop + output.clientHeight >= output.scrollHeight - 24;
      if (data.reset) logEntries = [];
      logEntries = logEntries.filter(entry => entry.id >= data.first).concat(data.entries);
      logCursor = data.next; more = data.more;
      output.textContent = logEntries.map(entry => entry.text).join('').replace(/\x1b\[[0-9;]*m/g, '') || '暂无应用日志';
      if (follow) output.scrollTop = output.scrollHeight;
      message('logs-error', '');
    } while (more && !$('logs-panel').hidden);
  } catch (error) {
    if (version !== authVersion || $('logs-panel').hidden) return;
    if (error.status === 401) loginView(); else message('logs-error', error.message);
  } finally { if (logsRefreshing === version) logsRefreshing = null; }
}

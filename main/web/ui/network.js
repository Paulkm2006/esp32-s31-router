function duration(seconds) {
  if (seconds === null || !Number.isFinite(seconds) || seconds < 0) return '暂不可用';
  const n = Math.floor(seconds), days = Math.floor(n / 86400), hours = Math.floor(n / 3600) % 24, minutes = Math.floor(n / 60) % 60;
  return (days ? days + ' 天 ' : '') + (hours ? hours + ' 小时 ' : '') + (minutes ? minutes + ' 分 ' : '') + n % 60 + ' 秒';
}
function render(data) {
  const {wan, ap, clients, config} = data;
  const values = {'wan-ip':wan.ip, 'wan-mask':wan.netmask, 'wan-gateway':wan.gateway, 'wan-dns':wan.dns, 'wan-mac':wan.mac, 'ap-ssid':config.ssid, 'ap-ip':ap.ip, 'ap-mask':ap.netmask, 'ap-dns':ap.dns};
  for (const [id, value] of Object.entries(values)) $(id).textContent = value || '暂不可用';
  $('wan-state').textContent = wan.ready ? '已获取 IP' : wan.link ? '网线已连接 · 等待 IP' : '网线未连接';
  $('wan-state').className = 'badge' + (wan.ready ? ' good' : '');
  $('wan-speed').textContent = wan.link && wan.speed_mbps ? wan.speed_mbps + ' Mbps · ' + (wan.full_duplex ? '全双工' : '半双工') : '—';
  $('client-count').textContent = clients.length + ' 台';
  $('clients').replaceChildren();
  for (const client of clients) {
    const row = document.createElement('tr');
    for (const value of [client.ip || '暂未分配', client.mac || '暂不可用', duration(client.connected_seconds)]) { const cell = document.createElement('td'); cell.textContent = value; row.append(cell); }
    $('clients').append(row);
  }
  if (!clients.length) { const row = document.createElement('tr'), cell = document.createElement('td'); cell.colSpan = 3; cell.className = 'empty'; cell.textContent = '暂无设备连接'; row.append(cell); $('clients').append(row); }
  if (!configLoaded) { for (const key of ['ssid', 'gateway', 'netmask', 'dns']) $(key).value = config[key] || ''; configLoaded = true; }
  renderStorage(data.storage, ap.ip);
}

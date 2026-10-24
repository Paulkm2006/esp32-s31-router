const configKeys = ['ssid', 'password', 'gateway', 'netmask', 'dns', 'admin_password'];
let configLoaded = false;
$('config-form').addEventListener('submit', async event => {
  event.preventDefault(); message('config-error', '');
  const body = Object.fromEntries(configKeys.map(key => [key, $(key).value]));
  for (const key of ['ssid', 'password', 'admin_password']) {
    const size = new TextEncoder().encode(body[key]).length, max = key === 'ssid' ? 32 : 63;
    if (size > max || (key !== 'ssid' && size > 0 && size < 8)) { message('config-error', key === 'ssid' ? '网络名称不能超过 32 个 UTF-8 字节。' : '密码必须为 8–63 个 UTF-8 字节。'); return; }
  }
  $('save').disabled = true;
  try {
    const data = await request('/api/config', body);
    rebooting = true; authVersion++; clearInterval(timer); $('settings').hidden = true; $('logout').hidden = true;
    $('storage-panel').hidden = true; $('smb-password').value = ''; $('storage-confirm').value = '';
    $('logs-panel').open = false; $('logs-panel').hidden = true;
    $('refresh-state').textContent = '等待路由器重启，请重新连接 Wi-Fi';
    $('new-ssid').textContent = body.ssid;
    const url = 'http://' + data.gateway + '/'; $('new-url').textContent = url; $('new-url').setAttribute('href', url);
    $('reconnect').hidden = false; $('password').value = ''; $('admin_password').value = '';
    $('reconnect').scrollIntoView({behavior:'smooth', block:'start'});
  } catch (error) { if (error.status === 401) { loginView(); message('login-error', '登录已过期，请重新登录。'); } else message('config-error', error.message); }
  finally { $('save').disabled = false; }
});

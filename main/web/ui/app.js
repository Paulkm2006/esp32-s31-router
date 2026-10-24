async function refresh() {
  if (refreshing === authVersion || rebooting) return;
  const version = authVersion; refreshing = version;
  try {
    const data = await request('/api/status');
    if (version !== authVersion) return;
    storagePending = false;
    render(data); $('login-panel').hidden = true; $('dashboard').hidden = false; $('logout').hidden = false;
    $('refresh-state').textContent = '每 5 秒自动更新';
    if (!timer) timer = setInterval(refresh, 5000);
    refreshLogs();
  } catch (error) {
    if (version !== authVersion) return;
    if (error.status === 401) loginView();
    else if (!$('dashboard').hidden) $('refresh-state').textContent = '更新失败，正在重试…';
    else message('login-error', '无法连接路由器，请检查网络后重试。');
  } finally { if (refreshing === version) refreshing = null; }
}
$('login-form').addEventListener('submit', async event => {
  event.preventDefault(); $('login-submit').disabled = true; message('login-error', '');
  try { await request('/api/login', {password:$('login-password').value}); authVersion++; $('login-password').value = ''; await refresh(); }
  catch (error) { message('login-error', error.message); }
  finally { $('login-submit').disabled = false; }
});
$('logout').addEventListener('click', async () => {
  $('logout').disabled = true;
  try { await request('/api/logout', {}); loginView(); }
  catch (error) { if (error.status === 401) loginView(); else toast(error.message); }
  finally { $('logout').disabled = false; }
});
refresh();

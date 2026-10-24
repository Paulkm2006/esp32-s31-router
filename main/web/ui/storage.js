function bytes(value) {
  if (!Number.isFinite(value) || value < 0) return '—';
  let n = value, unit = 0; const units = ['B', 'KiB', 'MiB', 'GiB', 'TiB'];
  while (n >= 1024 && unit < units.length - 1) { n /= 1024; unit++; }
  return n.toFixed(unit ? 1 : 0) + ' ' + units[unit];
}
function renderStorage(storage, gateway) {
  if (storageState && storage && storageState.media !== storage.media) $('storage-confirm').value = '';
  storageState = storage ? {...storage} : {ready:false, connected:false, mounted:false, busy:false};
  const s = storageState;
  const blocked = !s.ready || s.busy || storageSubmitting || storagePending;
  $('storage-state').textContent = s.busy || storagePending ? '正在操作' : !s.ready ? '存储未就绪' : s.mounted ? 'FAT32 已挂载' : s.connected ? '磁盘未挂载' : '未连接磁盘';
  $('storage-state').className = 'badge' + (s.mounted ? ' good' : '');
  $('storage-capacity').textContent = s.connected ? bytes(s.capacity) : '—';
  $('storage-usage').textContent = s.mounted && s.total > 0 ? bytes(s.total - s.free) + ' / ' + bytes(s.free) : '—';
  $('storage-address').textContent = gateway && s.share ? '\\\\' + gateway + '\\' + s.share : '—';
  $('smb-state').textContent = s.smb_running ? 'SMB3 · 账号认证与签名' : s.enabled ? '已启用 · 服务未运行' : '已关闭';
  $('storage-psram').textContent = bytes(s.psram_free) + ' / ' + bytes(s.psram_total);
  $('storage-mount').disabled = blocked || !s.connected || s.mounted;
  $('storage-unmount').disabled = blocked || !s.mounted;
  $('storage-format').disabled = blocked || !s.connected;
  $('smb-save').disabled = blocked;
  $('storage-progress').textContent = s.busy || storagePending ? '后台操作中，请勿拔盘或断电。' : '';
  if (!storageSubmitting) message('storage-error', s.error || '');
  if (!storageLoaded && s.ready) {
    $('smb-enabled').checked = s.enabled; $('smb-share').value = s.share || 'usb';
    $('smb-username').value = s.username || ''; storageLoaded = true;
  }
}
async function storageAction(body) {
  if (storageSubmitting || storagePending || !storageState || storageState.busy) return;
  const version = authVersion;
  storageSubmitting = true; renderStorage(storageState, $('ap-ip').textContent); message('storage-error', '');
  try {
    await request('/api/storage', body);
    if (version !== authVersion) return;
    $('smb-password').value = ''; $('storage-confirm').value = '';
    storagePending = true;
    toast('请求已接受，正在执行存储操作。');
    // The first status after acceptance is authoritative even if the operation already finished.
    await refresh();
  } catch (error) {
    if (version !== authVersion) return;
    if (error.status === 401) loginView(); else { storageState = {...storageState, error:error.message}; message('storage-error', error.message); }
  } finally {
    storageSubmitting = false;
    if (version === authVersion && storageState) renderStorage(storageState, $('ap-ip').textContent);
  }
}
$('storage-mount').addEventListener('click', () => storageAction({action:'mount', media:storageState.media}));
$('storage-unmount').addEventListener('click', () => storageAction({action:'unmount', media:storageState.media}));
$('storage-format-form').addEventListener('submit', event => {
  event.preventDefault();
  if ($('storage-confirm').value !== 'FORMAT') { message('storage-error', '请输入 FORMAT 确认清空当前 USB 磁盘。'); return; }
  return storageAction({action:'format', media:storageState.media, confirm:'FORMAT'});
});
$('smb-form').addEventListener('submit', event => {
  event.preventDefault();
  const enabled = $('smb-enabled').checked, password = $('smb-password').value;
  const size = new TextEncoder().encode(password).length;
  if (size > 63 || (size > 0 && size < 8) || (enabled && (!/^[A-Za-z0-9_.-]{1,32}$/.test($('smb-username').value) || (!password && !storageState.password_set)))) {
    message('storage-error', '启用 SMB 需设置有效用户名和 8–63 字节密码。'); return;
  }
  return storageAction({action:'configure', enabled, share:$('smb-share').value, username:$('smb-username').value, password});
});

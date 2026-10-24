'use strict';
const $ = id => document.getElementById(id);
let timer, toastTimer, refreshing = null, rebooting = false, authVersion = 0;
function message(id, text) { $(id).textContent = text; $(id).hidden = !text; }
function toast(text) { message('toast', text); clearTimeout(toastTimer); toastTimer = setTimeout(() => message('toast', ''), 5000); }
async function request(path, body) {
  const options = {credentials:'same-origin', cache:'no-store'};
  if (body !== undefined) Object.assign(options, {method:'POST', headers:{'Content-Type':'application/json'}, body:JSON.stringify(body)});
  const response = await fetch(path, options);
  const data = await response.json().catch(() => ({}));
  if (!response.ok) { const error = new Error(data.error || '请求失败，请稍后重试。'); error.status = response.status; throw error; }
  return data;
}
function loginView() {
  authVersion++; clearInterval(timer); timer = undefined; configLoaded = false;
  storageLoaded = false; storageState = null; storagePending = false;
  $('login-panel').hidden = false; $('dashboard').hidden = true; $('logout').hidden = true;
  $('login-password').value = ''; $('password').value = ''; $('admin_password').value = '';
  $('smb-password').value = ''; $('storage-confirm').value = '';
  logCursor = 0; logEntries = []; $('logs-content').textContent = ''; $('logs-panel').open = false;
  message('logs-error', '');
}

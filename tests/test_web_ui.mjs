import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import vm from 'node:vm';

const html = readFileSync(new URL('../main/router_web.html', import.meta.url), 'utf8');
class Element {
  value = ''; textContent = ''; hidden = false; disabled = false; children = []; listeners = {}; attributes = {};
  addEventListener(name, handler) { this.listeners[name] = handler; }
  append(child) { this.children.push(child); }
  replaceChildren() { this.children = []; }
  setAttribute(name, value) { this.attributes[name] = value; }
  scrollIntoView() {}
  set innerHTML(_) { assert.fail('Use textContent for values from the router.'); }
}
const elements = Object.fromEntries([...html.matchAll(/<[^>]+\bid="([^"]+)"[^>]*>/g)].map(([tag, id]) => {
  const element = new Element(); element.hidden = /\bhidden\b/.test(tag); return [id, element];
}));
const $ = id => elements[id];
const requests = [], intervals = new Map();
let handleRequest = () => ({status:401, data:{error:'请登录。'}}), nextTimer = 0;
const context = vm.createContext({
  document:{getElementById:$, createElement:() => new Element()}, TextEncoder,
  setInterval:callback => { const id = ++nextTimer; intervals.set(id, callback); return id; },
  clearInterval:id => intervals.delete(id), setTimeout:() => ++nextTimer, clearTimeout:() => {},
  fetch:async (path, options) => {
    requests.push({path, options});
    const response = await handleRequest(path, options);
    return {ok:response.status >= 200 && response.status < 300, status:response.status, json:async () => response.data};
  },
});
vm.runInContext(html.match(/<script>([\s\S]*?)<\/script>/)[1], context);
await new Promise(setImmediate);
assert.equal($('login-panel').hidden, false);
assert.equal($('dashboard').hidden, true);
assert.equal(intervals.size, 0);
assert.equal(vm.runInContext('duration(null)', context), '暂不可用');
assert.equal(vm.runInContext('duration(0)', context), '0 秒');
assert.equal(vm.runInContext('duration(90061)', context), '1 天 1 小时 1 分 1 秒');

const status = {
  wan:{link:true, ready:true, ip:'10.0.0.2', netmask:'255.255.255.0', gateway:'10.0.0.1', dns:'1.1.1.1', mac:'AA:BB:CC:DD:EE:FF', speed_mbps:100, full_duplex:true},
  ap:{ip:'192.168.4.1', netmask:'255.255.255.0', dns:'1.1.1.1'},
  clients:[{ip:'', mac:'11:22:33:44:55:66', connected_seconds:61}],
  config:{ssid:'<img src=x onerror=alert(1)>', gateway:'192.168.5.1', netmask:'255.255.255.0', dns:''},
  storage:{ready:true, connected:true, mounted:true, busy:false, media:7, capacity:1073741824,
    total:1000000000, free:400000000, share:'usb', enabled:false, username:'router', password_set:false,
    smb_running:false, psram_total:16777216, psram_free:12000000, error:''},
};
handleRequest = path => ({status:200, data:path === '/api/status' ? status : {}});
const submit = id => $(id).listeners.submit({preventDefault() {}});
$('login-password').value = 'secret-password';
await submit('login-form');
assert.equal($('dashboard').hidden, false);
assert.equal($('login-password').value, '');
assert.equal(intervals.size, 1);
const login = requests.find(request => request.path === '/api/login');
assert.equal(login.options.credentials, 'same-origin');
assert.equal(login.options.headers['Content-Type'], 'application/json');
assert.deepEqual(JSON.parse(login.options.body), {password:'secret-password'});
assert.equal($('ap-ssid').textContent, status.config.ssid);
assert.equal($('ap-ip').textContent, '192.168.4.1');
assert.equal($('gateway').value, '192.168.5.1');
assert.equal($('clients').children[0].children[0].textContent, '暂未分配');
assert.equal($('clients').children[0].children[2].textContent, '1 分 1 秒');
assert.equal($('storage-address').textContent, '\\\\192.168.4.1\\usb');
assert.equal($('storage-mount').disabled, true);
assert.equal($('storage-unmount').disabled, false);
assert.equal($('smb-password').value, '');
$('ssid').value = 'unsaved edit';
await vm.runInContext('refresh()', context);
assert.equal($('ssid').value, 'unsaved edit', 'Polling must preserve unsaved settings.');

$('password').value = 'must be cleared';
$('admin_password').value = 'must be cleared';
handleRequest = () => ({status:401, data:{error:'请登录。'}});
await vm.runInContext('refresh()', context);
assert.equal($('dashboard').hidden, true);
assert.equal($('password').value, '');
assert.equal($('admin_password').value, '');
assert.equal(intervals.size, 0);

handleRequest = path => ({status:200, data:path === '/api/status' ? status : {}});
await submit('login-form');
await $('logout').listeners.click();
assert.equal(requests.at(-1).path, '/api/logout');
assert.deepEqual(JSON.parse(requests.at(-1).options.body), {});
assert.equal($('login-panel').hidden, false);
assert.equal(intervals.size, 0);
await submit('login-form');
$('ssid').value = '网络名称太长'.repeat(6);
const beforeInvalid = requests.length;
await submit('config-form');
assert.equal(requests.length, beforeInvalid);
assert.match($('config-error').textContent, /32/);
$('ssid').value = '新网络'; $('password').value = '1234567';
await submit('config-form');
assert.equal(requests.length, beforeInvalid);
assert.match($('config-error').textContent, /8–63/);
$('password').value = '密密码'; $('admin_password').value = '';
handleRequest = () => ({status:400, data:{error:'网关地址无效。'}});
await submit('config-form');
assert.equal($('config-error').textContent, '网关地址无效。');
assert.equal($('settings').hidden, false);
assert.equal($('save').disabled, false);

handleRequest = path => ({status:200, data:path === '/api/status' ? status : {accepted:true}});
const storageBefore = requests.length;
await submit('storage-format-form');
assert.equal(requests.length, storageBefore, 'Formatting needs an explicit confirmation.');
assert.match($('storage-error').textContent, /FORMAT/);
$('storage-confirm').value = 'FORMAT';
await submit('storage-format-form');
let storagePost = requests.filter(r => r.path === '/api/storage').at(-1);
assert.deepEqual(JSON.parse(storagePost.options.body), {action:'format', media:7, confirm:'FORMAT'});
assert.equal($('storage-confirm').value, '');
$('storage-confirm').value = 'FORMAT'; status.storage.media = 8;
await vm.runInContext('refresh()', context);
assert.equal($('storage-confirm').value, '', 'Replacing the disk invalidates format confirmation.');
$('smb-enabled').checked = true; $('smb-password').value = 'short';
const beforeBadSmb = requests.length;
await submit('smb-form');
assert.equal(requests.length, beforeBadSmb);
$('smb-password').value = '独立共享密码';
await submit('smb-form');
storagePost = requests.filter(r => r.path === '/api/storage').at(-1);
assert.deepEqual(JSON.parse(storagePost.options.body), {action:'configure', enabled:true, share:'usb', username:'router', password:'独立共享密码'});
assert.equal($('smb-password').value, '');
$('smb-username').value = 'unsaved-user';
await vm.runInContext('refresh()', context);
assert.equal($('smb-username').value, 'unsaved-user');
status.storage.busy = true;
await vm.runInContext('refresh()', context);
assert.equal($('storage-format').disabled, true);
assert.equal($('smb-save').disabled, true);
status.storage.busy = false;
await vm.runInContext('refresh()', context);
handleRequest = path => path === '/api/storage' ? {status:409, data:{error:'磁盘已经改变。'}} : {status:200, data:status};
await $('storage-unmount').listeners.click();
assert.equal($('storage-error').textContent, '磁盘已经改变。');
assert.equal($('storage-unmount').disabled, false);

let resolveStatus;
handleRequest = path => path === '/api/status' ? new Promise(resolve => { resolveStatus = resolve; }) : {status:200, data:{rebooting:true, gateway:'192.168.5.1'}};
const staleRefresh = vm.runInContext('refresh()', context);
await submit('config-form');
assert.deepEqual(JSON.parse(requests.at(-1).options.body), {ssid:'新网络', password:'密密码', gateway:'192.168.5.1', netmask:'255.255.255.0', dns:'', admin_password:''});
assert.equal($('reconnect').hidden, false);
assert.equal($('new-ssid').textContent, '新网络');
assert.equal($('new-url').attributes.href, 'http://192.168.5.1/');
assert.equal($('settings').hidden, true);
assert.equal($('storage-panel').hidden, true);
assert.equal($('password').value, '');
assert.equal(intervals.size, 0);
resolveStatus({status:200, data:status}); await staleRefresh;
assert.equal($('logout').hidden, true, 'An old refresh must not restore controls after reboot starts.');
assert.equal(intervals.size, 0);
console.log('Web UI checks passed: login, safe rendering, polling, validation and reconnect flow.');

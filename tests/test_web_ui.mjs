import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import vm from 'node:vm';

const uiRoot = new URL('../main/web/ui/', import.meta.url);
const html = readFileSync(new URL('router_web.html', uiRoot), 'utf8').replace(
  /@UI_(\w+)@/g, (_, name) => readFileSync(new URL(name.replace(/_(css|js|html)$/, '.$1'), uiRoot), 'utf8'),
);
assert.doesNotMatch(html, /@UI_/);
class Element {
  value = ''; textContent = ''; hidden = false; disabled = false; children = []; listeners = {}; attributes = {};
  scrollTop = 0; clientHeight = 300; scrollHeight = 600;
  addEventListener(name, handler) { this.listeners[name] = handler; }
  append(child) { this.children.push(child); }
  replaceChildren() { this.children = []; }
  setAttribute(name, value) { this.attributes[name] = value; }
  scrollIntoView() {}
  focus() { this.focused = true; }
  set innerHTML(_) { assert.fail('Use textContent for values from the router.'); }
}
const elements = Object.fromEntries([...html.matchAll(/<[^>]+\bid="([^"]+)"[^>]*>/g)].map(([tag, id]) => {
  const element = new Element(); element.hidden = /\bhidden\b/.test(tag);
  for (const [, name, value] of tag.matchAll(/([\w-]+)="([^"]*)"/g)) element.attributes[name] = value;
  element.tabIndex = Number(element.attributes.tabindex || 0); return [id, element];
}));
const $ = id => elements[id];
assert.equal([...html.matchAll(/\bid="([^"]+)"/g)].length, Object.keys(elements).length, 'IDs must be unique across feature views.');
const panels = {overview:'overview-panel', settings:'settings', storage:'storage-panel', logs:'logs-panel'};
function assertTab(selected) {
  for (const [name, panel] of Object.entries(panels)) {
    assert.equal($(panel).hidden, name !== selected);
    assert.equal($('tab-' + name).attributes['aria-selected'], String(name === selected));
    assert.equal($('tab-' + name).attributes['aria-controls'], panel);
    assert.equal($(panel).attributes['aria-labelledby'], 'tab-' + name);
    assert.equal($('tab-' + name).tabIndex, name === selected ? 0 : -1);
  }
}
const clickTab = name => $('tab-' + name).listeners.click();
const pressTab = (name, key) => $('tab-' + name).listeners.keydown({key, preventDefault() {}});
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
assert.equal(requests.some(request => request.path.startsWith('/api/logs')), false);
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
assertTab('overview');
await clickTab('settings'); assertTab('settings');
$('ssid').value = 'tab draft';
await clickTab('storage'); assertTab('storage');
$('smb-username').value = 'tab-user';
await pressTab('storage', 'ArrowLeft'); assertTab('settings');
assert.equal($('tab-settings').focused, true);
assert.equal($('ssid').value, 'tab draft');
await clickTab('storage');
assert.equal($('smb-username').value, 'tab-user');
await pressTab('storage', 'Home'); assertTab('overview');
$('ssid').value = status.config.ssid; $('smb-username').value = 'router';
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
assert.equal(requests.some(request => request.path.startsWith('/api/logs')), false, 'Hidden logs must not poll.');
const logPages = [
  {first:0, next:2, reset:false, more:true, entries:[{id:0, text:'\x1b[31mfirst\x1b[0m\n'}, {id:1, text:'<img src=x onerror=alert(1)>\n'}]},
  {first:0, next:3, reset:false, more:false, entries:[{id:2, text:'third\n'}]},
];
handleRequest = path => path.startsWith('/api/logs') ? {status:200, data:logPages.shift()} : {status:200, data:status};
await pressTab('overview', 'End'); assertTab('logs');
assert.equal($('tab-logs').focused, true);
assert.equal(requests.at(-1).path, '/api/logs?after=2');
assert.equal($('logs-content').textContent, 'first\n<img src=x onerror=alert(1)>\nthird\n');
assert.equal($('logs-content').scrollTop, 600);
$('logs-content').scrollTop = 0;
handleRequest = () => ({status:200, data:{first:2, next:4, reset:false, more:false, entries:[{id:3, text:'fourth\n'}]}});
await vm.runInContext('refreshLogs()', context);
assert.equal($('logs-content').textContent, 'third\nfourth\n', 'Expired records must leave the browser too.');
assert.equal($('logs-content').scrollTop, 0, 'Polling must preserve a user reading older logs.');
handleRequest = () => ({status:200, data:{first:10, next:11, reset:true, more:false, entries:[{id:10, text:'after overflow\n'}]}});
await vm.runInContext('refreshLogs()', context);
assert.equal($('logs-content').textContent, 'after overflow\n');
handleRequest = () => ({status:503, data:{error:'日志缓存不可用或内存不足'}});
await vm.runInContext('refreshLogs()', context);
assert.equal($('logs-error').hidden, false);
assert.equal($('logs-content').textContent, 'after overflow\n');
let resolveHiddenLog;
handleRequest = () => new Promise(resolve => { resolveHiddenLog = resolve; });
const hiddenLogRequest = vm.runInContext('refreshLogs()', context);
await pressTab('logs', 'ArrowRight'); assertTab('overview');
resolveHiddenLog({status:200, data:{first:10, next:12, reset:false, more:true, entries:[{id:11, text:'hidden response'}]}});
const beforeHiddenResponse = requests.length;
await hiddenLogRequest;
assert.equal(requests.length, beforeHiddenResponse, 'Leaving logs stops pagination.');
assert.equal($('logs-content').textContent, 'after overflow\n');
await vm.runInContext('refreshLogs()', context);
assert.equal(requests.length, beforeHiddenResponse, 'Hidden logs do not poll.');
handleRequest = () => ({status:503, data:{error:'日志缓存不可用或内存不足'}});
await clickTab('logs');
let resolveLogs;
handleRequest = () => new Promise(resolve => { resolveLogs = resolve; });
const staleLogs = vm.runInContext('refreshLogs()', context);
await vm.runInContext('refreshLogs()', context);
handleRequest = () => ({status:200, data:{}});
await $('logout').listeners.click();
assert.equal($('logs-content').textContent, '');
resolveLogs({status:200, data:{first:10, next:12, reset:false, more:false, entries:[{id:11, text:'private old response'}]}});
await staleLogs;
assert.equal($('logs-content').textContent, '', 'An old log request must not restore logs after logout.');
handleRequest = path => ({status:200, data:path === '/api/status' ? status : {}});
await submit('login-form');
await clickTab('logs');
handleRequest = () => ({status:401, data:{error:'请登录。'}});
await vm.runInContext('refreshLogs()', context);
assert.equal($('dashboard').hidden, true);
assert.equal(intervals.size, 0);
handleRequest = path => ({status:200, data:path === '/api/status' ? status : {}});
await submit('login-form');
assertTab('overview'); await clickTab('settings');
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
await clickTab('settings');
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
await clickTab('storage');
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
await clickTab('settings');
await submit('config-form');
assert.deepEqual(JSON.parse(requests.at(-1).options.body), {ssid:'新网络', password:'密密码', gateway:'192.168.5.1', netmask:'255.255.255.0', dns:'', admin_password:''});
assert.equal($('reconnect').hidden, false);
assert.equal($('new-ssid').textContent, '新网络');
assert.equal($('new-url').attributes.href, 'http://192.168.5.1/');
assert.equal($('settings').hidden, true);
assert.equal($('storage-panel').hidden, true);
assert.equal($('logs-panel').hidden, true);
assert.equal($('tabs').hidden, true);
await clickTab('storage');
assert.equal($('storage-panel').hidden, true, 'Reboot must keep controls hidden.');
assert.equal($('password').value, '');
assert.equal(intervals.size, 0);
resolveStatus({status:200, data:status}); await staleRefresh;
assert.equal($('logout').hidden, true, 'An old refresh must not restore controls after reboot starts.');
assert.equal(intervals.size, 0);
console.log('Web UI checks passed: feature tabs, keyboard navigation, drafts, login, logs, storage, validation and reconnect flow.');

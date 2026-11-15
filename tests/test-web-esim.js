// Exercise the real eSIM page with synthetic card state; no card or network access.
const assert = require('assert');
const model = require('../web/factory/u60-web-model.js');

function element(tag) {
  const e = {tag, kids: [], handlers: {}, attrs: {}, _text: ''};
  e[0] = e;
  e.append = (...children) => { e.kids.push(...children); return e; };
  e.appendTo = parent => { parent.append(e); return e; };
  e.empty = () => { e.kids = []; return e; };
  e.addClass = () => e;
  e.hide = () => e;
  e.attr = (key, value) => {
    if (typeof key === 'object') Object.assign(e.attrs, key);
    else e.attrs[key] = value;
    return e;
  };
  e.prop = (key, value) => {
    if (typeof key === 'object') Object.assign(e, key);
    else e[key] = value;
    return e;
  };
  e.text = value => { e._text = String(value); return e; };
  e.on = (name, fn) => { e.handlers[name] = fn; return e; };
  return e;
}

const $ = arg => typeof arg === 'string' ? element(arg.slice(1, -1)) : arg;
let page;
global.define = (deps, factory) => { page = factory($, model); };
require('../web/factory/u60-web-esim.js');
delete global.define;
global.document = {createTextNode: text => ({_text: text})};
const walk = e => [e, ...(e.kids || []).flatMap(walk)];
const opened = [];
const api = {open: item => opened.push(item)};
const eid = '89049032' + '0'.repeat(24);
const base = {
  installed: true, job: {state: 'done'},
  view: {card: 'euicc', eid, profiles: [
    {iccid: '0'.repeat(19) + '1', name: 'Synthetic Travel', enabled: false},
  ], notifications: [{seq: 0, operation: 'delete', iccid: '0'.repeat(20), address: 'smdp.example'}]},
};
function render(change) {
  const data = JSON.parse(JSON.stringify(base));
  change(data);
  const host = element('div');
  page.render(host, data, api);
  return walk(host);
}
const writes = nodes => nodes.filter(n => n.tag === 'button' && n._text !== '读取卡片');

// A cached eUICC after interruption must not enable any mutation. Only the
// fresh read remains available, matching the native screen's recovery path.
for (const state of ['running', 'interrupted']) {
  const nodes = render(data => { data.job.state = state; });
  assert(writes(nodes).length > 0);
  assert(writes(nodes).every(n => n.disabled), state);
  const scan = nodes.find(n => n.tag === 'button' && /读取卡片|正在操作/.test(n._text));
  assert.strictEqual(scan.disabled, state === 'running');
}
for (const invalid of ['', '1'.repeat(31), '1'.repeat(31) + 'x']) {
  assert(writes(render(data => { data.view.eid = invalid; })).every(n => n.disabled));
}

// A completed reread restores the original EID-bound commands; notification
// processing is still an explicit choice and never becomes another delete.
let nodes = render(() => {});
let send = nodes.find(n => n.tag === 'button' && n._text === '发送通知');
assert.strictEqual(send.disabled, false);
send.handlers.click();
assert.deepStrictEqual(opened.pop().args, {
  confirmed: true, operation: 'process', seq: [0], route: 'auto', expected_eid: eid,
});
nodes.find(n => n.tag === 'button' && n._text === '删除').handlers.click();
const deletion = opened.pop();
assert.strictEqual(deletion.action, 'esim.delete');
assert.strictEqual(deletion.args.expected_eid, eid);
assert.strictEqual(deletion.fields[0].key, 'confirm_name');
assert.strictEqual(opened.length, 0);

// An unrelated numeric network error must retain its actual diagnosis.
assert.strictEqual(page.message('Cannot reach server (5)'), 'Cannot reach server (5)');
assert(page.message('the card is busy (catBusy)').includes('卡片正在忙'));
assert.strictEqual(page.message('unrecognized card error'), 'unrecognized card error');
console.log('web eSIM: passed; interrupted/invalid identity blocks writes, reread restores bound actions, errors stay distinct');

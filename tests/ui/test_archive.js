'use strict';
const assert = require('assert');
const fs = require('fs');
const path = require('path');
if (!String.prototype.format) {
	String.prototype.format = function() {
		let index = 0, values = arguments;
		return this.replace(/%s/g, () => String(values[index++]));
	};
}

function E(tag, attributes, children) {
	if (Array.isArray(tag)) return { tag: 'fragment', attributes: {}, children: attributes || tag };
	const node = { tag, attributes: attributes || {}, children: children == null ? [] :
		(Array.isArray(children) ? children : [ children ]), disabled: false };
	node.replaceChildren = function(...items) { this.children = items; };
	return node;
}
function find(node, predicate, output = []) {
	if (node == null || typeof node !== 'object') return output;
	if (Array.isArray(node)) { node.forEach(item => find(item, predicate, output)); return output; }
	if (predicate(node)) output.push(node);
	find(node.children, predicate, output);
	return output;
}
function text(node) {
	if (node == null) return '';
	if (typeof node !== 'object') return String(node);
	return (node.children || []).map(text).join(' ');
}

let calls = [];
let failRun = false;
const state = { available: true, month: '2026-09', export_status: 'success',
	upload_status: 'failed', updated_at: '2026-10-01T01:02:03Z', error: 'upload_failed_1' };
const api = {
	archiveStatus: () => Promise.resolve(state),
	archiveRun: () => { calls.push('run'); return Promise.resolve(failRun ? { error: '归档执行失败' } : { success: true }); },
	archiveRetry: () => { calls.push('retry'); return Promise.resolve({ success: true }); }
};
const source = fs.readFileSync(path.join(__dirname,
	'../../luci-app-trafficmon/htdocs/luci-static/resources/view/trafficmon/archive.js'), 'utf8');
const page = new Function('view', 'api', 'E', '_', source)(
	{ extend: object => object }, api, E, value => value);

const root = page.render(state);
assert(text(root).includes('归档管理'));
assert(text(root).includes('2026-09'));
assert(text(root).includes('导出状态'));
assert(text(root).includes('上传状态'));
assert(text(root).includes('更新时间'));
assert(text(root).includes('错误信息'));
const failedLoad = page.render({ error: '无法读取归档状态' });
assert(text(failedLoad).includes('无法读取归档状态'));
assert.strictEqual(find(failedLoad, node => node.attributes &&
	node.attributes.class === 'alert-message error').length, 1);

const run = find(root, node => node.tag === 'button' && text(node).includes('立即执行归档'))[0];
const retry = find(root, node => node.tag === 'button' && text(node).includes('重新上传'))[0];
assert(run && retry);

(async function() {
	assert.strictEqual(await page.load(), state);
	await retry.attributes.click();
	assert.deepStrictEqual(calls, [ 'retry' ]);
	assert(text(root).includes('重新上传成功'));
	failRun = true;
	await run.attributes.click();
	assert.deepStrictEqual(calls, [ 'retry', 'run' ]);
	assert(text(root).includes('归档执行失败'));
	console.log('PASS LuCI archive state display, fixed actions and friendly failure');
})().catch(error => { console.error(error); process.exit(1); });

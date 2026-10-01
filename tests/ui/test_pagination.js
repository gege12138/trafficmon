'use strict';
const assert = require('assert');
const fs = require('fs');
const path = require('path');

function E(tag, attributes, children) {
	const node = { tag, attributes: attributes || {}, children: children == null ? [] :
		(Array.isArray(children) ? children : [ children ]), value: '', listeners: {} };
	node.appendChild = function(child) { this.children.push(child); };
	node.replaceChildren = function(...items) { this.children = items; };
	node.addEventListener = function(event, callback) { this.listeners[event] = callback; };
	return node;
}

function find(node, predicate, result = []) {
	if (node == null || typeof node !== 'object') return result;
	if (Array.isArray(node)) {
		node.forEach(child => find(child, predicate, result));
		return result;
	}
	if (predicate(node)) result.push(node);
	find(node.children, predicate, result);
	return result;
}

const calls = [];
const api = {
	devices: (...args) => {
		calls.push(args);
		return Promise.resolve({ total_count: 500, devices: [{ device_id: 'ip4:10.0.0.1',
			addresses: [ '10.0.0.1' ], total_bytes: 1, download_bytes: 1,
			upload_bytes: 0, connections: 1 }] });
	}
};
const ui = { error: () => null, deviceTable: (data, period, options) => E('table', {}, [
	E('button', { click: () => options.onSort('connections', 'desc') }, 'Connections')
]) };
const source = fs.readFileSync(path.join(__dirname,
	'../../luci-app-trafficmon/htdocs/luci-static/resources/view/trafficmon/devices.js'), 'utf8');
const view = new Function('view', 'api', 'ui', 'E', '_', source)(
	{ extend: object => object }, api, ui, E, text => text);

(async () => {
	const root = view.render({ periods: [{ current: true }, { start: '2026-08-01' }] });
	await Promise.resolve();
	assert.deepStrictEqual(calls[0], [ 'current', 'total', 'desc', 50, 0, '' ]);
	let next = find(root, node => node.tag === 'button' && node.children.includes('Next'))[0];
	assert(next && !next.attributes.disabled);
	next.attributes.click();
	await Promise.resolve();
	assert.deepStrictEqual(calls[1], [ 'current', 'total', 'desc', 50, 50, '' ]);
	const search = find(root, node => node.tag === 'input' && node.attributes.type === 'search')[0];
	search.value = '  test-host  ';
	find(root, node => node.tag === 'button' && node.children.includes('Refresh'))[0].attributes.click();
	await Promise.resolve();
	assert.deepStrictEqual(calls[2], [ 'current', 'total', 'desc', 50, 0, 'test-host' ]);
	const selects = find(root, node => node.tag === 'select');
	selects[0].value = '2026-08-01';
	selects[0].listeners.change();
	await Promise.resolve();
	assert.deepStrictEqual(calls[3], [ '2026-08-01', 'total', 'desc', 50, 0, 'test-host' ]);
	find(root, node => node.tag === 'button' && node.children.includes('Connections'))[0].attributes.click();
	await Promise.resolve();
	assert.deepStrictEqual(calls[4], [ '2026-08-01', 'connections', 'desc', 50, 0, 'test-host' ]);
	assert.strictEqual(selects.length, 1);
	console.log('PASS Devices uses simple toolbar and server-side header sorting/search/pagination');
})().catch(error => { console.error(error); process.exitCode = 1; });

'use strict';
const assert = require('assert');
const fs = require('fs');
const path = require('path');

function E(tag, attributes, children) {
	return { tag, attributes: attributes || {}, children: children || [],
		appendChild: function(child) { this.children.push(child); } };
}
const L = { url: function(route) { return '/' + route; } };
const _ = function(text) { return text; };
const baseclass = { extend: function(object) { return object; } };
const source = fs.readFileSync(path.join(__dirname,
	'../../luci-app-trafficmon/htdocs/luci-static/resources/trafficmon/ui.js'), 'utf8');
const ui = new Function('E', 'L', '_', 'baseclass', 'api', source)(E, L, _, baseclass, {});

function walk(node, elements, text) {
	if (node == null) return;
	if (typeof node === 'string') { text.push(node); return; }
	if (Array.isArray(node)) { node.forEach(function(child) { walk(child, elements, text); }); return; }
	elements.push(node.tag);
	walk(node.children, elements, text);
}

function find(node, predicate, result = []) {
	if (node == null || typeof node !== 'object') return result;
	if (Array.isArray(node)) { node.forEach(child => find(child, predicate, result)); return result; }
	if (predicate(node)) result.push(node);
	find(node.children, predicate, result);
	return result;
}

for (const hostname of [ '<script>alert(1)</script>', '<img src=x onerror=alert(1)>',
	'" & <b>host</b>', 'x'.repeat(63) ]) {
	const tree = ui.deviceTable({ devices: [{ device_id: 'ip4:10.0.0.9', mac: null,
		hostname, addresses: [ '10.0.0.9' ], download_bytes: 1,
		upload_bytes: 2, total_bytes: 3, connections: 1 }] }, 'current');
	const elements = [], text = [];
	walk(tree, elements, text);
	assert(!elements.includes('script') && !elements.includes('img') && !elements.includes('b'));
	assert(text.includes(hostname));
}
assert(!source.includes('innerHTML'));

const readable = ui.deviceTable({ devices: [
	{ device_id: '02:00:00:00:00:01', mac: '02:00:00:00:00:01', hostname: 'laptop',
		addresses: [ '10.0.0.2', 'fd12::2', 'fd12::3', 'fd12::4' ], download_bytes: 1024,
		upload_bytes: 2048, total_bytes: 3072, connections: 4 },
	{ device_id: 'ip4:10.0.0.9', mac: null, hostname: null, addresses: [ '10.0.0.9' ],
		download_bytes: 1, upload_bytes: 2, total_bytes: 3, connections: 1 }
] }, 'current', { order: 'total', direction: 'desc', onSort: () => {} });
const headers = find(readable, node => node.tag === 'th');
assert.strictEqual(headers.length, 5);
const cells = find(readable, node => node.tag === 'td');
assert.strictEqual(cells.length, 10);
const elements = [], text = [];
walk(readable, elements, text);
const rendered = text.join(' ');
assert(rendered.includes('laptop') && rendered.includes('02:00:00:00:00:01'));
assert(rendered.includes('10.0.0.2') && rendered.includes('IPv6 ×3'));
assert(!rendered.includes('fd12::2'));
assert(rendered.includes('Unknown device') && rendered.includes('10.0.0.9'));
assert(!rendered.includes('null'));
assert(cells.slice(1, 5).every(node => node.attributes.style.includes('text-align:right')));

const menu = JSON.parse(fs.readFileSync(path.join(__dirname,
	'../../luci-app-trafficmon/root/usr/share/luci/menu.d/luci-app-trafficmon.json'), 'utf8'));
assert.strictEqual(menu['admin/status/trafficmon/detail'].title, undefined);
assert.strictEqual(menu['admin/status/trafficmon/detail'].firstchild_ineligible, true);
console.log('PASS LuCI device hierarchy, hidden detail route, null and hostname XSS regressions');

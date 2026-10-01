'use strict';
const assert = require('assert');
const fs = require('fs');
const path = require('path');

function E(tag, attributes, children) {
	const node = { tag, attributes: attributes || {}, children: children == null ? [] :
		(Array.isArray(children) ? children : [ children ]), listeners: {}, value: '' };
	node.appendChild = function(child) { this.children.push(child); };
	node.replaceChildren = function(...items) { this.children = items; };
	node.addEventListener = function(name, callback) { this.listeners[name] = callback; };
	return node;
}

function find(node, predicate, output = []) {
	if (node == null || typeof node !== 'object') return output;
	if (Array.isArray(node)) {
		node.forEach(item => find(item, predicate, output));
		return output;
	}
	if (predicate(node)) output.push(node);
	find(node.children, predicate, output);
	return output;
}

const _ = text => text;
const view = { extend: object => object };
const ui = { error: () => null, cell: value => E('td', {}, String(value)),
	bytes: value => String(value) + ' B', diagnostics: () => E('div') };
function load(name, form) {
	const source = fs.readFileSync(path.join(__dirname,
		'../../luci-app-trafficmon/htdocs/luci-static/resources/view/trafficmon/' + name + '.js'), 'utf8');
	return new Function('view', 'api', 'ui', 'form', 'E', '_', source)(
		view, {}, ui, form, E, _);
}

const detail = load('detail');
const malicious = '<script>alert(1)</script>&"';
const address = [ '10.123.0.1', 'fd12:3456:1::1', 'fd12:3456:1::2' ];
const main = { period: '2026-09-01', devices: [{ hostname: malicious,
	device_id: '02:00:00:00:00:01', mac: '02:00:00:00:00:01', addresses: address,
	download_bytes: 20, upload_bytes: 10, total_bytes: 30, connections: 3 }] };
const breakdown = { devices: [
	{ protocol: 'HTTPS', total_bytes: 20, download_bytes: 15, upload_bytes: 5, connections: 2 },
	{ protocol: 'DNS', total_bytes: 10, download_bytes: 5, upload_bytes: 5, connections: 1 }
] };
const root = detail.render([ main, breakdown, '2026-09-01', main.devices[0].device_id ]);
assert(find(root, node => node.tag === 'details').length === 0);
assert(find(root, node => node.tag === 'li').length === 3);
assert(find(root, node => node.tag === 'script').length === 0);
assert(find(root, node => node.tag === 'td').some(node => node.children.includes(malicious)));
const area = find(root, node => node.tag === 'div' && node.attributes.style === 'overflow-x:auto')[0];
function labels() {
	return area.children[0].children.filter(row => row.tag === 'tr').slice(1)
		.map(row => row.children[0].children[0]);
}
assert.deepStrictEqual(labels(), [ 'HTTPS', 'DNS' ]);
find(area, node => node.tag === 'button' && node.children[0].startsWith('Protocol'))[0].attributes.click();
assert.deepStrictEqual(labels(), [ 'HTTPS', 'DNS' ]);
find(area, node => node.tag === 'button' && node.children[0].startsWith('Protocol'))[0].attributes.click();
assert.deepStrictEqual(labels(), [ 'DNS', 'HTTPS' ]);
find(area, node => node.tag === 'button' && node.children[0].startsWith('Download'))[0].attributes.click();
assert.deepStrictEqual(labels(), [ 'HTTPS', 'DNS' ]);
console.log('PASS Device Detail header sorting, full address display and hostname text');

const options = {};
class Map {
	section() { return { option: function(type, name) { return options[name] = {}; } }; }
	render() { return Promise.resolve(E('form')); }
}
const form = { Map, NamedSection: {}, Flag: {}, Value: {}, DynamicList: {} };
const settings = load('settings', form);
settings.render({}).then(() => {
	for (const [name, bounds] of Object.entries({ refresh_interval: 'range(1,86400)',
		netlink_buffer_size: 'range(1,67108864)', database_generations: 'range(1,4096)',
		database_limit: 'range(1,1000000)', commit_interval: 'range(1,604800)' }))
		assert.strictEqual(options[name].datatype, bounds);
	assert.strictEqual(options.accounting_period.validate('main', '2026-09-01/7'), true);
	assert.strictEqual(options.accounting_period.validate('main', '-31'), true);
	assert.notStrictEqual(options.accounting_period.validate('main', '32'), true);
	assert.notStrictEqual(options.accounting_period.validate('main', '0'), true);
	assert.notStrictEqual(options.accounting_period.validate('main', '2026-02-30/7'), true);
	assert.strictEqual(options.accounting_period.validate('main', '2028-02-29/7'), true);
	assert.notStrictEqual(options.accounting_period.validate('main', '1; reboot'), true);
	assert.notStrictEqual(options.accounting_period.validate('main', ''), true);
	assert.strictEqual(options.local_network.datatype, 'or(uciname,cidr4,cidr6)');
	console.log('PASS r3 Settings range, period and local-network validation declarations');
}).catch(error => { console.error(error); process.exitCode = 1; });

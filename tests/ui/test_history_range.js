'use strict';
const assert = require('assert');
const fs = require('fs');
const path = require('path');

const source = fs.readFileSync(path.join(__dirname,
	'../../luci-app-trafficmon/htdocs/luci-static/resources/trafficmon/range.js'), 'utf8');
const range = new Function('baseclass', source)({ extend: object => object });
const periods = [
	{ start: '2026-10-01', end: '2026-11-01', current: true },
	{ start: '2026-09-01', end: '2026-10-01', current: false }
];

let result = range.selectPeriods(periods, '2026-09-01', '2026-09-30');
assert.strictEqual(result.error, undefined);
assert.deepStrictEqual(result.periods.map(item => item.query), [ '2026-09-01' ]);

result = range.selectPeriods(periods, '2026-09-01', '2026-10-31');
assert.strictEqual(result.error, undefined);
assert.deepStrictEqual(result.periods.map(item => item.query), [ '2026-09-01', 'current' ]);

result = range.selectPeriods(periods, '2026-09-10', '2026-09-20');
assert.strictEqual(result.error, 'partial_period');
assert.strictEqual(result.period.start, '2026-09-01');
assert.strictEqual(result.period.end, '2026-10-01');

assert.strictEqual(range.selectPeriods(periods, '2026-02-30', '2026-03-01').error,
	'invalid_date');
assert.strictEqual(range.selectPeriods(periods, '2026-10-02', '2026-10-01').error,
	'date_order');
assert.strictEqual(range.selectPeriods(periods, '../../etc/passwd', '2026-10-01').error,
	'invalid_date');
assert.strictEqual(range.selectPeriods([ periods[1] ], '2026-09-01', '2026-10-31').error,
	'not_covered');

const devices = range.mergeDevices([
	[ { device_id: '02:00:00:00:00:01', hostname: 'laptop', mac: '02:00:00:00:00:01',
		download_bytes: 100, download_packets: 2, upload_bytes: 30, upload_packets: 1,
		connections: 1 } ],
	[ { device_id: '02:00:00:00:00:01', hostname: 'laptop', mac: '02:00:00:00:00:01',
		download_bytes: 200, download_packets: 4, upload_bytes: 70, upload_packets: 3,
		connections: 2 },
	  { device_id: '02:00:00:00:00:02', mac: '02:00:00:00:00:02',
		download_bytes: 1, upload_bytes: 2, connections: 1 } ]
]);
assert.strictEqual(devices.length, 2);
assert.deepStrictEqual({ download: devices[0].download_bytes, upload: devices[0].upload_bytes,
	packets: devices[0].download_packets + devices[0].upload_packets,
	connections: devices[0].connections, total: devices[0].total_bytes },
	{ download: 300, upload: 100, packets: 10, connections: 3, total: 400 });

const ips = range.mergeIps([
	[ { family: 4, ip: '192.0.2.1', download_bytes: 10, upload_bytes: 5, connections: 1 } ],
	[ { family: 4, ip: '192.0.2.1', download_bytes: 20, upload_bytes: 7, connections: 2 },
	  { family: 6, ip: '2001:db8::1', download_bytes: 2, upload_bytes: 3, connections: 1 } ]
]);
assert.strictEqual(ips.length, 2);
assert.strictEqual(ips[0].ip, '192.0.2.1');
assert.deepStrictEqual({ download: ips[0].download_bytes, upload: ips[0].upload_bytes,
	connections: ips[0].connections, total: ips[0].total_bytes },
	{ download: 30, upload: 12, connections: 3, total: 42 });

function E(tag, attributes, children) {
	const node = { tag, attributes: attributes || {}, children: children == null ? [] :
		(Array.isArray(children) ? children : [ children ]), value: '', listeners: {} };
	node.appendChild = function(child) { this.children.push(child); };
	node.replaceChildren = function(...items) { this.children = items; };
	node.addEventListener = function(event, callback) { this.listeners[event] = callback; };
	return node;
}
function find(node, predicate, output = []) {
	if (node == null || typeof node !== 'object') return output;
	if (Array.isArray(node)) { node.forEach(item => find(item, predicate, output)); return output; }
	if (predicate(node)) output.push(node);
	find(node.children, predicate, output);
	return output;
}
const historySource = fs.readFileSync(path.join(__dirname,
	'../../luci-app-trafficmon/htdocs/luci-static/resources/view/trafficmon/history.js'), 'utf8');
const history = new Function('view', 'api', 'range', 'ui', 'E', '_', historySource)(
	{ extend: object => object },
	{ devices: () => Promise.resolve({ total_count: 0, devices: [] }),
	  ips: () => Promise.resolve({ records: [] }), exportData: () => Promise.resolve({ data: '' }) },
	range,
	{ error: () => null, deviceTable: () => E('p'), cell: value => E('td', {}, value),
	  bytes: value => String(value) + ' B' }, E, text => text);
const root = history.render({ periods });
const dateInputs = find(root, node => node.tag === 'input' && node.attributes.type === 'date');
assert.strictEqual(dateInputs.length, 2);
dateInputs[0].value = '2026-09-10';
dateInputs[1].value = '2026-09-20';
find(root, node => node.tag === 'button' && node.children.includes('Query'))[0].attributes.click();
const errors = find(root, node => node.attributes && node.attributes.class === 'alert-message error');
assert.strictEqual(errors.length, 1);
assert(String(errors[0].children[0]).includes('partial-period'));

console.log('PASS History exact-period coverage, date validation, aggregation and page controls');

'use strict';
'require view';
'require trafficmon.api as api';
'require trafficmon.ui as ui';

return view.extend({
	load: function() {
		var params = new URLSearchParams(window.location.search), id = params.get('id') || '',
			period = params.get('period') || 'current';
		if (!/^(?:[0-9a-f]{2}:){5}[0-9a-f]{2}$|^ip[46]:[0-9a-fA-F:.]+$/.test(id) ||
			!(/^(current|[0-9]{4}-[0-9]{2}-[0-9]{2})$/.test(period)))
			return Promise.resolve([ { error: _('Invalid device or period.') }, {}, period, id ]);
		return Promise.all([ api.device(period, id), api.detail(period, id) ]).then(function(data) {
			return [ data[0], data[1], period, id ];
		});
	},
	render: function(data) {
		var main = data[0], breakdown = data[1], period = data[2], id = data[3],
			device = (main.devices || [])[0];
		if (!device) {
			var missing = [ E('h2', _('Device Detail')) ], problem = ui.error(main);
			missing.push(problem || E('p', _('Device not found in this period.')));
			return E('div', {}, missing);
		}
		var v4 = [], v6 = [];
		(device.addresses || []).forEach(function(address) { (address.indexOf(':') >= 0 ? v6 : v4).push(address); });
		var summary = E('table', { 'class': 'table' });
		[ [ _('Hostname'), device.hostname || _('Unknown device') ], [ _('MAC'), device.mac || _('Unknown') ],
		  [ _('Device ID'), device.device_id ], [ _('Period'), main.period ],
		  [ _('Download'), ui.bytes(device.download_bytes) ], [ _('Upload'), ui.bytes(device.upload_bytes) ],
		  [ _('Total'), ui.bytes(device.total_bytes) ], [ _('Connections'), device.connections ]
		].forEach(function(pair) { summary.appendChild(E('tr', { 'class': 'tr' },
			[ ui.cell(pair[0]), ui.cell(pair[1]) ])); });
		function addressList(addresses) {
			return E('ul', {}, addresses.length ?
				addresses.map(function(address) { return E('li', {}, String(address)); }) :
				[ E('li', {}, _('None')) ]);
		}
		var protocols = (breakdown.devices || []).slice(), tableArea = E('div', { 'style': 'overflow-x:auto' }),
			sortKey = 'total_bytes', sortDirection = 'desc';
		function protocolHeader(label, key, numeric) {
			var active = sortKey === key, arrow = active ? (sortDirection === 'asc' ? ' ▲' : ' ▼') : '';
			return E('th', { 'class': 'th', 'style': numeric ? 'text-align:right;white-space:nowrap' : '',
				'aria-sort': active ? (sortDirection === 'asc' ? 'ascending' : 'descending') : 'none' },
				E('button', { 'class': 'btn cbi-button-link',
					'style': 'padding:0;border:0;background:none;font:inherit', 'click': function() {
						sortDirection = active && sortDirection === 'desc' ? 'asc' : 'desc';
						sortKey = key; drawProtocols();
					} }, _(label) + arrow));
		}
		function drawProtocols() {
			protocols.sort(function(a, b) {
				var comparison = sortKey === 'protocol' ?
					String(a.protocol || '').localeCompare(String(b.protocol || '')) :
					Number(a[sortKey] || 0) - Number(b[sortKey] || 0);
				return sortDirection === 'asc' ? comparison : -comparison;
			});
			var table = E('table', { 'class': 'table' }, E('tr', { 'class': 'tr table-titles' },
				[ protocolHeader('Protocol', 'protocol', false),
					protocolHeader('Download', 'download_bytes', true),
					protocolHeader('Upload', 'upload_bytes', true),
					protocolHeader('Total', 'total_bytes', true),
					protocolHeader('Connections', 'connections', true) ]));
			protocols.forEach(function(row) { table.appendChild(E('tr', { 'class': 'tr' }, [
				ui.cell(row.protocol || ('#' + row.protocol_id)),
				E('td', { 'class': 'td', 'style': 'text-align:right;white-space:nowrap' }, ui.bytes(row.download_bytes)),
				E('td', { 'class': 'td', 'style': 'text-align:right;white-space:nowrap' }, ui.bytes(row.upload_bytes)),
				E('td', { 'class': 'td', 'style': 'text-align:right;white-space:nowrap' }, ui.bytes(row.total_bytes)),
				E('td', { 'class': 'td', 'style': 'text-align:right;white-space:nowrap' }, String(row.connections || 0))
			])); });
			tableArea.replaceChildren(table);
		}
		drawProtocols();
		var content = [ E('h2', device.hostname || device.mac || _('Unknown device')) ],
			breakdownError = ui.error(breakdown);
		if (breakdownError) content.push(breakdownError);
		content.push(summary,
			period !== 'current' ? E('p', _('Hostname is current metadata and may differ from the historical name.')) : E('span'),
			E('h3', _('IPv4 addresses')), addressList(v4),
			E('h3', _('IPv6 addresses') + ' (' + v6.length + ')'), addressList(v6),
			device.addresses_truncated ? E('p', _('Address list was truncated by the backend.')) : E('span'),
			E('h3', _('Protocol breakdown')), tableArea);
		return E('div', {}, content);
	},
	handleSave: null, handleSaveApply: null, handleReset: null
});

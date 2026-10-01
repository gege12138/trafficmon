'use strict';
'require baseclass';
'require trafficmon.api as api';

function bytes(value) {
	var number = Number(value || 0), units = [ 'B', 'KiB', 'MiB', 'GiB', 'TiB', 'PiB' ], unit = 0;
	while (number >= 1024 && unit < units.length - 1) { number /= 1024; unit++; }
	return unit ? number.toFixed(2) + ' ' + units[unit] : String(number) + ' B';
}

function error(data) {
	return data && data.error ? E('div', { 'class': 'alert-message warning' }, String(data.error)) : null;
}

function cell(value) { return E('td', { 'class': 'td' }, value == null ? _('N/A') : String(value)); }

function addressSummary(addresses) {
	var v4 = [], v6 = [];
	(addresses || []).forEach(function(a) { (a.indexOf(':') >= 0 ? v6 : v4).push(a); });
	return { v4: v4, v6: v6 };
}

function numericCell(value) {
	return E('td', { 'class': 'td', 'style': 'text-align:right;white-space:nowrap' },
		value == null ? '—' : String(value));
}

function sortHeader(label, key, options) {
	var active = options && options.order === key,
		direction = active ? options.direction : 'desc',
		arrow = active ? (direction === 'asc' ? ' ▲' : ' ▼') : '';
	if (!options || !options.onSort)
		return E('th', { 'class': 'th', 'style': 'text-align:right;white-space:nowrap' }, _(label));
	return E('th', { 'class': 'th', 'style': 'text-align:right;white-space:nowrap',
		'aria-sort': active ? (direction === 'asc' ? 'ascending' : 'descending') : 'none' },
		E('button', { 'class': 'btn cbi-button-link', 'style': 'padding:0;border:0;background:none;font:inherit',
			'click': function() {
				if (options && options.onSort)
					options.onSort(key, active && direction === 'desc' ? 'asc' : 'desc');
			} }, _(label) + arrow));
}

function deviceTable(data, period, options) {
	if (data.error) return error(data);
	var rows = data.devices || [];
	if (!rows.length) return E('p', _('No devices found for this period.'));
	var table = E('table', { 'class': 'table' }, [
		E('tr', { 'class': 'tr table-titles' }, [
			E('th', { 'class': 'th' }, _('Device')),
			sortHeader('Download', 'download', options), sortHeader('Upload', 'upload', options),
			sortHeader('Total', 'total', options), sortHeader('Connections', 'connections', options)
		])
	]);
	rows.forEach(function(row) {
		var addresses = addressSummary(row.addresses),
			known = !!row.mac,
			label = row.hostname || row.mac || _('Unknown device'),
			link = L.url('admin/status/trafficmon/detail') + '?period=' + encodeURIComponent(period) +
				'&id=' + encodeURIComponent(row.device_id), metadata = [];
		if (row.hostname && row.mac) metadata.push(String(row.mac));
		else if (known && row.hostname !== row.mac) metadata.push(String(row.mac));
		if (addresses.v4.length) metadata.push(String(addresses.v4[0]) +
			(addresses.v4.length > 1 ? ' +' + (addresses.v4.length - 1) : ''));
		if (!known && !addresses.v4.length && addresses.v6.length) metadata.push(String(addresses.v6[0]));
		if (addresses.v6.length) metadata.push(_('IPv6') + ' ×' + addresses.v6.length);
		if (!known && !addresses.v4.length && !addresses.v6.length && row.device_id)
			metadata.push(String(row.device_id).replace(/^ip[46]:/, ''));
		table.appendChild(E('tr', { 'class': 'tr' }, [
			E('td', { 'class': 'td' }, [
				E('a', { 'href': link }, String(label)),
				metadata.length ? E('div', { 'style': 'font-size:90%;opacity:.75;margin-top:.2em' },
					metadata.join(' · ')) : E('span')
			]),
			numericCell(bytes(row.download_bytes)), numericCell(bytes(row.upload_bytes)),
			numericCell(bytes(row.total_bytes)), numericCell(row.connections)
		]));
	});
	return E('div', { 'style': 'overflow-x:auto' }, table);
}

function collectorState(value) {
	if (value === 'running') return _('Running');
	if (value === 'stopped') return _('Stopped');
	if (value === 'disabled') return _('Disabled');
	return value == null || value === '' ? _('Unknown') : String(value);
}

function diagnostics(status) {
	if (status.error) return error(status);
	var fields = [
		[ _('Daemon version'), status.daemon_version ],
		[ _('Collector state'), status.collector_state ],
		[ _('Current period'), status.current_period && status.current_period.start ],
		[ _('Record count'), status.record_count ],
		[ _('Database capacity'), status.capacity ],
		[ _('Dropped new keys'), status.dropped_new_keys ],
		[ _('Netlink receive buffer'), status.netlink_receive_buffer ],
		[ _('Netlink messages'), status.netlink_messages ],
		[ _('Netlink ENOBUFS'), status.netlink_enobufs ],
		[ _('Parse errors'), status.netlink_parse_errors ],
		[ _('NEW events'), status.conntrack_new_events ],
		[ _('DESTROY events'), status.conntrack_destroy_events ],
		[ _('CTRZERO dumps'), status.conntrack_dump_count ],
		[ _('CTRZERO dump errors'), status.conntrack_dump_errors ],
		[ _('Last successful dump'), status.last_successful_dump ],
		[ _('Neighbor cache entries'), status.neighbor_cache_entries ],
		[ _('Hostname cache entries'), status.hostname_cache_entries ]
	];
	var table = E('table', { 'class': 'table' });
	fields.forEach(function(pair) { table.appendChild(E('tr', { 'class': 'tr' }, [ cell(pair[0]), cell(pair[1]) ])); });
	return E('div', {}, [
		(status.collector_state !== 'running' || status.netlink_enobufs || status.netlink_parse_errors ||
		 status.conntrack_dump_errors) ? E('div', { 'class': 'alert-message warning' },
			_('Traffic accounting may be incomplete; check collector diagnostics.')) : E('span'),
		table
	]);
}

function exportButtons(period) {
	var container = E('span');
	[ 'json', 'csv' ].forEach(function(format) {
		container.appendChild(E('button', { 'class': 'btn', 'click': function() {
			api.exportData(period, format).then(function(reply) {
				if (reply.error) { alert(reply.error); return; }
				var blob = new Blob([ reply.data ], { type: format === 'csv' ? 'text/csv' : 'application/json' });
				var href = URL.createObjectURL(blob), anchor = document.createElement('a');
				anchor.href = href; anchor.download = 'trafficmon-' + period + '.' + format;
				anchor.click(); setTimeout(function() { URL.revokeObjectURL(href); }, 1000);
			});
		} }, _('Export ' + format.toUpperCase())));
	});
	return container;
}

return baseclass.extend({ bytes: bytes, error: error, cell: cell, deviceTable: deviceTable,
	collectorState: collectorState, diagnostics: diagnostics, exportButtons: exportButtons });

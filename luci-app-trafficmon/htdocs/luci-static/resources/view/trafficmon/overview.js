'use strict';
'require view';
'require trafficmon.api as api';
'require trafficmon.ui as ui';

return view.extend({
	load: function() {
		return Promise.all([ api.status(), api.summary('current'),
			api.devices('current', 'total', 'desc', 50, 0, '') ]);
	},
	render: function(data) {
		var status = data[0], summary = data[1], devices = data[2];
		var totals = { download_bytes: 0, upload_bytes: 0 };
		(summary.records || []).forEach(function(row) {
			totals.download_bytes += Number(row.download_bytes || 0);
			totals.upload_bytes += Number(row.upload_bytes || 0);
		});
		var fields = [
			[ _('Download'), ui.bytes(totals.download_bytes) ],
			[ _('Upload'), ui.bytes(totals.upload_bytes) ],
			[ _('Total traffic'), ui.bytes(totals.download_bytes + totals.upload_bytes) ],
			[ _('Devices'), devices.total_count == null ? '—' : devices.total_count ],
			[ _('Collector status'), ui.collectorState(status.collector_state) ]
		];
		var metrics = E('div', { 'style': 'display:grid;grid-template-columns:repeat(auto-fit,minmax(10em,1fr));gap:.75em;margin:1em 0' });
		fields.forEach(function(pair) { metrics.appendChild(E('div', {
			'class': 'cbi-section', 'style': 'margin:0;padding:1em'
		}, [ E('div', { 'style': 'font-size:90%;opacity:.75' }, pair[0]),
			E('div', { 'style': 'font-size:1.5em;font-weight:600;margin-top:.2em' }, String(pair[1])) ])); });
		var content = [ E('h2', _('Traffic Monitor Overview')) ];
		[ status, summary, devices ].forEach(function(item) {
			var problem = ui.error(item); if (problem) content.push(problem);
		});
		content.push(
			E('p', {}, [ _('Accounting period') + ': ', E('strong', {}, summary.period || _('N/A')) ]),
			(status.collector_state !== 'running' || status.netlink_enobufs ||
			 status.netlink_parse_errors || status.conntrack_dump_errors) ?
				E('div', { 'class': 'alert-message warning' },
					_('Traffic accounting may be incomplete because collector events were dropped or the collector is degraded.')) : E('span'),
			metrics, E('h3', _('Top devices')), ui.deviceTable(devices, 'current', {
				order: 'total', direction: 'desc'
			}),
			E('p', {}, E('a', { 'href': L.url('admin/status/trafficmon/devices') }, _('View all devices')))
		);
		return E('div', {}, content);
	},
	handleSave: null, handleSaveApply: null, handleReset: null
});

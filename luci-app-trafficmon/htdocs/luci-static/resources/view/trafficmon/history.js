'use strict';
'require view';
'require trafficmon.api as api';
'require trafficmon.ui as ui';

return view.extend({
	load: function() { return api.periods(); },
	render: function(periods) {
		var period = 'current', order = 'total', direction = 'desc', offset = 0, limit = 50, output = E('div'),
			selector = E('select', { 'class': 'cbi-input-select' });
		selector.appendChild(E('option', { 'value': 'current' }, _('Current')));
		(periods.periods || []).forEach(function(item) {
			if (!item.current) selector.appendChild(E('option', { 'value': item.start }, String(item.start)));
		});
		function refresh() {
			output.replaceChildren(E('p', _('Loading…')));
			api.devices(period, order, direction, limit, offset, '').then(function(data) {
				var total = Number(data.total_count || 0);
				output.replaceChildren(ui.deviceTable(data, period, {
					order: order, direction: direction, onSort: function(nextOrder, nextDirection) {
						order = nextOrder; direction = nextDirection; offset = 0; refresh();
					}
				}), E('div', {}, [
					E('button', { 'class': 'btn', 'disabled': !offset, 'click': function() {
						offset -= limit; refresh(); } }, _('Previous')),
					E('span', {}, ' ' + (total ? offset + 1 : 0) + '–' + Math.min(offset + limit, total) + ' / ' + total + ' '),
					E('button', { 'class': 'btn', 'disabled': offset + limit >= total, 'click': function() {
						offset += limit; refresh(); } }, _('Next'))
				]));
			});
		}
		selector.addEventListener('change', function() { period = selector.value; offset = 0; refresh(); });
		var buttons = E('span');
		[ 'json', 'csv' ].forEach(function(format) { buttons.appendChild(E('button', {
			'class': 'btn', 'click': function() {
				api.exportData(period, format).then(function(reply) {
					if (reply.error) { alert(reply.error); return; }
					var blob = new Blob([ reply.data ], { type: format === 'csv' ? 'text/csv' : 'application/json' }),
						href = URL.createObjectURL(blob), anchor = document.createElement('a');
					anchor.href = href; anchor.download = 'trafficmon-' + period + '.' + format;
					anchor.click(); setTimeout(function() { URL.revokeObjectURL(href); }, 1000);
				});
			} }, _('Export ' + format.toUpperCase()))); });
		refresh();
		var content = [ E('h2', _('History')) ], periodsError = ui.error(periods);
		if (periodsError) content.push(periodsError);
		content.push(
			E('p', {}, [ selector, ' ', buttons ]),
			E('p', _('Exports contain at most the first 10,000 backend query records.')),
			E('p', _('Historical hostnames, when shown, are current metadata rather than a historical snapshot.')),
			output);
		return E('div', {}, content);
	},
	handleSave: null, handleSaveApply: null, handleReset: null
});

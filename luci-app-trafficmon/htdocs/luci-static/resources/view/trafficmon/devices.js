'use strict';
'require view';
'require trafficmon.api as api';
'require trafficmon.ui as ui';

return view.extend({
	load: function() { return api.periods(); },
	render: function(periods) {
		var self = this, period = 'current', order = 'total', direction = 'desc', offset = 0,
			search = '', limit = 50, output = E('div'), selector = E('select', { 'class': 'cbi-input-select' });
		selector.appendChild(E('option', { 'value': 'current' }, _('Current')));
		(periods.periods || []).forEach(function(item) {
			if (!item.current) selector.appendChild(E('option', { 'value': item.start }, String(item.start)));
		});
		var searchInput = E('input', { 'class': 'cbi-input-text', 'type': 'search',
			'placeholder': _('Search hostname, MAC or address') });
		function sort(nextOrder, nextDirection) {
			order = nextOrder; direction = nextDirection; offset = 0; refresh();
		}
		function refresh() {
			search = searchInput.value.trim();
			output.replaceChildren(E('p', _('Loading…')));
			api.devices(period, order, direction, limit, offset, search).then(function(data) {
				var total = Number(data.total_count || 0), controls = E('div', {}, [
					E('button', { 'class': 'btn', 'disabled': offset === 0, 'click': function() {
						offset = Math.max(0, offset - limit); refresh(); } }, _('Previous')),
					E('span', {}, ' ' + (total ? offset + 1 : 0) + '–' + Math.min(offset + limit, total) + ' / ' + total + ' '),
					E('button', { 'class': 'btn', 'disabled': offset + limit >= total, 'click': function() {
						offset += limit; refresh(); } }, _('Next'))
				]);
				output.replaceChildren(ui.deviceTable(data, period, {
					order: order, direction: direction, onSort: sort
				}), controls);
			});
		}
		selector.addEventListener('change', function() { period = selector.value; offset = 0; refresh(); });
		searchInput.addEventListener('keydown', function(event) {
			if (event.key === 'Enter') { event.preventDefault(); offset = 0; refresh(); }
		});
		refresh();
		var content = [ E('h2', _('Devices')) ], periodsError = ui.error(periods);
		if (periodsError) content.push(periodsError);
		content.push(
			E('div', { 'class': 'cbi-section', 'style': 'display:flex;gap:.5em;align-items:center;flex-wrap:wrap' }, [
				selector, searchInput,
				E('button', { 'class': 'btn cbi-button-action', 'click': function() {
					offset = 0; refresh(); } }, _('Refresh'))
			]),
			output);
		return E('div', {}, content);
	},
	handleSave: null, handleSaveApply: null, handleReset: null
});

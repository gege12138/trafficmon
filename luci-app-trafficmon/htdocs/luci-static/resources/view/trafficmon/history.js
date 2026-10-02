'use strict';
'require view';
'require trafficmon.api as api';
'require trafficmon.range as range';
'require trafficmon.ui as ui';

var RANGE_PAGE_SIZE = 50;
var BACKEND_PAGE_SIZE = 200;

function collect(periods, loader) {
	var groups = [];
	function next(index) {
		if (index >= periods.length)
			return Promise.resolve(groups);
		return loader(periods[index].query).then(function(rows) {
			groups.push(rows);
			return next(index + 1);
		});
	}
	return next(0);
}

function fetchDevices(period) {
	var rows = [];
	function page(offset) {
		return api.devices(period, 'total', 'desc', BACKEND_PAGE_SIZE, offset, '').then(function(reply) {
			var batch, total;
			if (reply.error)
				throw new Error(reply.error);
			batch = reply.devices || [];
			total = Number(reply.total_count || 0);
			rows = rows.concat(batch);
			if (rows.length < total) {
				if (!batch.length || rows.length > 1000000)
					throw new Error(_('The device result is too large or incomplete.'));
				return page(rows.length);
			}
			return rows;
		});
	}
	return page(0);
}

function fetchIps(period) {
	var rows = [];
	function page(offset) {
		return api.ips(period, BACKEND_PAGE_SIZE, offset).then(function(reply) {
			var batch;
			if (reply.error)
				throw new Error(reply.error);
			batch = reply.records || [];
			rows = rows.concat(batch);
			if (batch.length === BACKEND_PAGE_SIZE) {
				if (rows.length >= 1000000)
					throw new Error(_('The IP result exceeds the supported query size.'));
				return page(rows.length);
			}
			return rows;
		});
	}
	return page(0);
}

function coverageError(result) {
	if (result.error === 'invalid_date')
		return _('Enter real dates in YYYY-MM-DD format.');
	if (result.error === 'date_order')
		return _('The start date must not be later than the end date.');
	if (result.error === 'partial_period')
		return _('The database period is ') + result.period.start + _(' to ') + result.period.end +
			_('; a partial-period query cannot be answered exactly.');
	return _('The selected range is not completely covered by available database periods.') +
		(result.date ? ' (' + result.date + ')' : '');
}

function numericCell(value) {
	return E('td', { 'class': 'td', 'style': 'text-align:right;white-space:nowrap' }, String(value));
}

function resultTable(rows, dimension) {
	var body = [];
	if (dimension === 'device') {
		rows.forEach(function(row) {
			body.push(E('tr', {}, [
				ui.cell(row.hostname || row.mac || _('Unknown device')),
				ui.cell(row.mac || _('Unknown')),
				numericCell(ui.bytes(row.download_bytes)),
				numericCell(ui.bytes(row.upload_bytes)),
				numericCell(ui.bytes(row.total_bytes))
			]));
		});
		return E('table', { 'class': 'table cbi-section-table' }, [
			E('thead', {}, E('tr', {}, [ E('th', {}, _('Device name')), E('th', {}, _('MAC')),
				E('th', {}, _('Download')), E('th', {}, _('Upload')), E('th', {}, _('Total')) ])),
			E('tbody', {}, body)
		]);
	}
	rows.forEach(function(row) {
		body.push(E('tr', {}, [
			ui.cell(row.ip),
			numericCell(ui.bytes(row.download_bytes)),
			numericCell(ui.bytes(row.upload_bytes)),
			numericCell(ui.bytes(row.total_bytes))
		]));
	});
	return E('table', { 'class': 'table cbi-section-table' }, [
		E('thead', {}, E('tr', {}, [ E('th', {}, _('IP')), E('th', {}, _('Download')),
			E('th', {}, _('Upload')), E('th', {}, _('Total')) ])),
		E('tbody', {}, body)
	]);
}

function renderRangeResult(output, rows, dimension, selection) {
	var offset = 0;
	function refresh() {
		var page = rows.slice(offset, offset + RANGE_PAGE_SIZE), controls;
		if (!rows.length) {
			output.replaceChildren(E('p', _('No traffic records were found for this range.')));
			return;
		}
		controls = E('div', {}, [
			E('button', { 'class': 'btn', 'disabled': offset === 0, 'click': function() {
				offset = Math.max(0, offset - RANGE_PAGE_SIZE); refresh();
			} }, _('Previous')),
			E('span', {}, ' ' + (offset + 1) + '–' + Math.min(offset + RANGE_PAGE_SIZE, rows.length) +
				' / ' + rows.length + ' '),
			E('button', { 'class': 'btn', 'disabled': offset + RANGE_PAGE_SIZE >= rows.length,
				'click': function() { offset += RANGE_PAGE_SIZE; refresh(); } }, _('Next'))
		]);
		output.replaceChildren(
			E('p', {}, _('Aggregated complete periods') + ': ' + selection.periods.length +
				' (' + selection.start + ' – ' + selection.end + ')'),
			selection.periods.some(function(item) { return item.current; }) ?
				E('div', { 'class': 'alert-message warning' },
					_('The current period is included and its counters are still accumulating.')) : E('span'),
			E('div', { 'style': 'overflow-x:auto' }, resultTable(page, dimension)), controls);
	}
	refresh();
}

return view.extend({
	load: function() { return api.periods(); },
	render: function(periods) {
		var periodItems = periods.periods || [], newest = periodItems[0] || {},
			startInput = E('input', { 'class': 'cbi-input-text', 'type': 'date',
				'id': 'trafficmon-start-date' }),
			endInput = E('input', { 'class': 'cbi-input-text', 'type': 'date',
				'id': 'trafficmon-end-date' }),
			dimension = E('select', { 'class': 'cbi-input-select' }, [
				E('option', { 'value': 'device' }, _('Device')),
				E('option', { 'value': 'ip' }, _('IP'))
			]),
			rangeOutput = E('div'), queryButton, period = 'current', order = 'total',
			direction = 'desc', offset = 0, limit = 50, output = E('div'),
			selector = E('select', { 'class': 'cbi-input-select' });

		if (range.parseDate(newest.start) && range.inclusiveEnd(newest.end)) {
			startInput.value = newest.start;
			endInput.value = range.inclusiveEnd(newest.end);
		}
		queryButton = E('button', { 'class': 'btn cbi-button-action', 'click': function() {
			var selection = range.selectPeriods(periodItems, startInput.value, endInput.value), loader;
			if (selection.error) {
				rangeOutput.replaceChildren(E('div', { 'class': 'alert-message error' },
					coverageError(selection)));
				return;
			}
			rangeOutput.replaceChildren(E('p', _('Loading…')));
			loader = dimension.value === 'ip' ? fetchIps : fetchDevices;
			collect(selection.periods, loader).then(function(groups) {
				var rows = dimension.value === 'ip' ? range.mergeIps(groups) : range.mergeDevices(groups);
				renderRangeResult(rangeOutput, rows, dimension.value, selection);
			}).catch(function(error) {
				rangeOutput.replaceChildren(E('div', { 'class': 'alert-message error' },
					error.message || _('Range query failed.')));
			});
		} }, _('Query'));

		selector.appendChild(E('option', { 'value': 'current' }, _('Current')));
		periodItems.forEach(function(item) {
			if (!item.current)
				selector.appendChild(E('option', { 'value': item.start }, String(item.start)));
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
					E('span', {}, ' ' + (total ? offset + 1 : 0) + '–' + Math.min(offset + limit, total) +
						' / ' + total + ' '),
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
			E('div', { 'class': 'cbi-section' }, [
				E('h3', _('Date range query')),
				E('p', _('Only ranges exactly covered by complete database periods can be queried.')),
				E('div', { 'style': 'display:flex;gap:.5em;align-items:end;flex-wrap:wrap' }, [
					E('label', { 'for': 'trafficmon-start-date' }, [ _('Start date'), E('br'), startInput ]),
					E('label', { 'for': 'trafficmon-end-date' }, [ _('End date'), E('br'), endInput ]),
					E('label', {}, [ _('Statistics'), E('br'), dimension ]), queryButton
				]), rangeOutput
			]),
			E('div', { 'class': 'cbi-section' }, [
				E('h3', _('Single period query and export')),
				E('p', {}, [ selector, ' ', buttons ]),
				E('p', _('Exports contain at most the first 10,000 backend query records.')),
				E('p', _('Historical hostnames, when shown, are current metadata rather than a historical snapshot.')),
				output
			]));
		return E('div', {}, content);
	},
	handleSave: null, handleSaveApply: null, handleReset: null
});

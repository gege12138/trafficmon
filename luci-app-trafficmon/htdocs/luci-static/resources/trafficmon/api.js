'use strict';
'require baseclass';
'require fs';

const helper = '/usr/libexec/trafficmon-luci';

function invoke(args, raw) {
	return fs.exec(helper, args).then(function(result) {
		if (result.code !== 0)
			return { error: (result.stderr || _('trafficmon is unavailable or the request was rejected.')).trim() };
		if (raw)
			return { data: result.stdout || '' };
		try {
			return JSON.parse(result.stdout || '{}');
		}
		catch (e) {
			return { error: _('trafficmon returned an invalid response.') };
		}
	}).catch(function() {
		return { error: _('Unable to contact the trafficmon helper.') };
	});
}

return baseclass.extend({
	status: function() { return invoke([ 'status' ]); },
	periods: function() { return invoke([ 'periods' ]); },
	summary: function(period) { return invoke([ 'summary', period ]); },
	devices: function(period, order, direction, limit, offset, search) {
		return invoke([ 'devices', period, order, direction, String(limit), String(offset), search || '' ]);
	},
	device: function(period, id) { return invoke([ 'device', period, id ]); },
	detail: function(period, id) { return invoke([ 'detail', period, id ]); },
	exportData: function(period, format) {
		return fs.exec_direct(helper,
			[ format === 'csv' ? 'export-csv' : 'export-json', period ], 'text').then(function(data) {
			return { data: data };
		}).catch(function() {
			return { error: _('Export failed or exceeded the backend query limit.') };
		});
	}
});

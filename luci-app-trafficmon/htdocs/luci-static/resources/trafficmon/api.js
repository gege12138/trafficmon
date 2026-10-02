'use strict';
'require baseclass';
'require fs';

const helper = '/usr/libexec/trafficmon-luci';
const archiveHelper = '/usr/libexec/trafficmon-luci-archive';

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

function archiveAction(action) {
	return fs.exec(archiveHelper, [ action ]).then(function(result) {
		if (result.code !== 0)
			return { error: (result.stderr || _('归档操作失败，请检查配置和系统日志。')).trim() };
		try {
			return JSON.parse(result.stdout || '{}');
		}
		catch (e) {
			return { error: _('归档命令返回了无效结果。') };
		}
	}).catch(function() {
		return { error: _('无法调用归档命令。') };
	});
}

return baseclass.extend({
	status: function() { return invoke([ 'status' ]); },
	archiveStatus: function() { return invoke([ 'archive-status' ]); },
	archiveRun: function() { return archiveAction('run'); },
	archiveRetry: function() { return archiveAction('retry'); },
	periods: function() { return invoke([ 'periods' ]); },
	summary: function(period) { return invoke([ 'summary', period ]); },
	devices: function(period, order, direction, limit, offset, search) {
		return invoke([ 'devices', period, order, direction, String(limit), String(offset), search || '' ]);
	},
	ips: function(period, limit, offset) {
		return invoke([ 'ips', period, String(limit), String(offset) ]);
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

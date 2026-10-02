'use strict';
'require baseclass';

var DAY_MS = 86400000;
var COUNTERS = [ 'connections', 'download_bytes', 'download_packets',
	'upload_bytes', 'upload_packets' ];

function pad(value) {
	return value < 10 ? '0' + value : String(value);
}

function parseDate(text) {
	var match = /^(\d{4})-(\d{2})-(\d{2})$/.exec(text || ''), date, year, month, day;
	if (!match)
		return null;
	year = Number(match[1]);
	month = Number(match[2]);
	day = Number(match[3]);
	if (year < 1 || month < 1 || month > 12 || day < 1 || day > 31)
		return null;
	date = new Date(0);
	date.setUTCHours(0, 0, 0, 0);
	date.setUTCFullYear(year, month - 1, day);
	if (date.getUTCFullYear() !== year || date.getUTCMonth() !== month - 1 ||
		date.getUTCDate() !== day)
		return null;
	return { text: text, day: Math.floor(date.getTime() / DAY_MS) };
}

function formatDay(day) {
	var date = new Date(day * DAY_MS), year = date.getUTCFullYear();
	if (!isFinite(date.getTime()) || year < 1 || year > 9999)
		return null;
	return ('0000' + year).slice(-4) + '-' + pad(date.getUTCMonth() + 1) + '-' +
		pad(date.getUTCDate());
}

function periodDescriptor(item) {
	var start = parseDate(item && item.start), end = parseDate(item && item.end);
	if (!start || !end || start.day >= end.day)
		return null;
	return {
		start: start.text,
		end: end.text,
		startDay: start.day,
		endDay: end.day,
		current: item.current === true,
		query: item.current === true ? 'current' : start.text
	};
}

function selectPeriods(items, startText, endText) {
	var start = parseDate(startText), end = parseDate(endText), endExclusive, periods = [],
		selected = [], cursor, exact, containing, index;
	if (!start || !end)
		return { error: 'invalid_date' };
	if (start.day > end.day)
		return { error: 'date_order' };
	endExclusive = end.day + 1;
	if (!formatDay(endExclusive))
		return { error: 'invalid_date' };
	(items || []).forEach(function(item) {
		var descriptor = periodDescriptor(item);
		if (descriptor)
			periods.push(descriptor);
	});
	periods.sort(function(left, right) {
		if (left.startDay !== right.startDay)
			return left.startDay - right.startDay;
		return left.current ? -1 : right.current ? 1 : 0;
	});
	cursor = start.day;
	while (cursor < endExclusive) {
		exact = null;
		containing = null;
		for (index = 0; index < periods.length; index++) {
			if (!exact && periods[index].startDay === cursor)
				exact = periods[index];
			if (!containing && periods[index].startDay < cursor && periods[index].endDay > cursor)
				containing = periods[index];
		}
		if (!exact) {
			return containing ? { error: 'partial_period', period: containing } :
				{ error: 'not_covered', date: formatDay(cursor) };
		}
		if (exact.endDay > endExclusive)
			return { error: 'partial_period', period: exact };
		selected.push(exact);
		cursor = exact.endDay;
	}
	return {
		periods: selected,
		start: start.text,
		end: end.text,
		endExclusive: formatDay(endExclusive)
	};
}

function addCounter(target, source, name) {
	var value = Number(source[name] || 0);
	if (isFinite(value) && value >= 0)
		target[name] += value;
}

function sortedRows(map) {
	var rows = [];
	Object.keys(map).forEach(function(key) {
		var row = map[key];
		row.total_bytes = row.download_bytes + row.upload_bytes;
		rows.push(row);
	});
	rows.sort(function(left, right) {
		return right.total_bytes - left.total_bytes || String(left._key).localeCompare(String(right._key));
	});
	return rows;
}

function mergeDevices(groups) {
	var result = Object.create(null);
	(groups || []).forEach(function(rows) {
		(rows || []).forEach(function(row) {
			var id = String(row.device_id || ''), key = 'device:' + id, target;
			if (!id)
				return;
			target = result[key];
			if (!target) {
				target = result[key] = { _key: key, device_id: id, hostname: null,
					mac: row.mac || null, connections: 0, download_bytes: 0,
					download_packets: 0, upload_bytes: 0, upload_packets: 0 };
			}
			if (row.hostname)
				target.hostname = row.hostname;
			if (row.mac)
				target.mac = row.mac;
			COUNTERS.forEach(function(name) { addCounter(target, row, name); });
		});
	});
	return sortedRows(result);
}

function mergeIps(groups) {
	var result = Object.create(null);
	(groups || []).forEach(function(rows) {
		(rows || []).forEach(function(row) {
			var family = Number(row.family), ip = String(row.ip || ''), key, target;
			if ((family !== 4 && family !== 6) || !ip)
				return;
			key = 'ip:' + family + ':' + ip;
			target = result[key];
			if (!target) {
				target = result[key] = { _key: key, family: family, ip: ip,
					connections: 0, download_bytes: 0, download_packets: 0,
					upload_bytes: 0, upload_packets: 0 };
			}
			COUNTERS.forEach(function(name) { addCounter(target, row, name); });
		});
	});
	return sortedRows(result);
}

function inclusiveEnd(periodEnd) {
	var end = parseDate(periodEnd);
	return end ? formatDay(end.day - 1) : null;
}

return baseclass.extend({
	parseDate: parseDate,
	selectPeriods: selectPeriods,
	mergeDevices: mergeDevices,
	mergeIps: mergeIps,
	inclusiveEnd: inclusiveEnd
});

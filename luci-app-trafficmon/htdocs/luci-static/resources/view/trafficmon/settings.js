'use strict';
'require view';
'require form';
'require trafficmon.api as api';
'require trafficmon.ui as ui';

return view.extend({
	load: function() { return api.status(); },
	render: function(status) {
		var m = new form.Map('trafficmon', _('Traffic Monitor Settings'),
			_('Saving and applying this configuration reloads trafficmon only; it does not restart network or firewall services.'));
		var s = m.section(form.NamedSection, 'main', 'trafficmon', _('Configuration'));
		var o = s.option(form.Flag, 'enabled', _('Enabled'));
		o.default = '1';
		function integer(name, label, minimum, maximum) {
			var value = s.option(form.Value, name, label);
			value.datatype = 'range(' + minimum + ',' + maximum + ')';
			value.rmempty = false;
		}
		integer('refresh_interval', _('Refresh interval (seconds)'), 1, 86400);
		integer('netlink_buffer_size', _('Netlink buffer size (bytes)'), 1, 67108864);
		integer('database_generations', _('Database generations'), 1, 4096);
		integer('database_limit', _('Database record limit'), 1, 1000000);
		integer('commit_interval', _('Commit interval (seconds)'), 1, 604800);
		o = s.option(form.Value, 'accounting_period', _('Accounting period rule'));
		o.rmempty = false;
		o.description = _('Monthly boundary (-31 to -1 or 1 to 31), or YYYY-MM-DD/days. The backend performs final validation.');
		o.validate = function(section, value) {
		var monthly = /^[+-]?[0-9]+$/.test(value) ? Number(value) : 0;
		if (monthly && monthly >= -31 && monthly <= 31) return true;
		var fixed = /^(\d{1,10})-(\d{1,2})-(\d{1,2})\/(\d{1,10})$/.exec(value || '');
		if (fixed) {
			var year = Number(fixed[1]), month = Number(fixed[2]), day = Number(fixed[3]),
				interval = Number(fixed[4]);
			var leap = year % 4 === 0 && (year % 100 !== 0 || year % 400 === 0);
			var days = [ 31, leap ? 29 : 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 ];
			if (year >= 1 && year <= 2147483647 && month >= 1 && month <= 12 &&
				day >= 1 && day <= days[month - 1] && interval >= 1 && interval <= 4294967295)
				return true;
		}
		return _('Enter a valid monthly day or date/interval rule.');
		};
		o = s.option(form.DynamicList, 'local_network', _('Local networks'));
		o.rmempty = false;
		o.datatype = 'or(uciname,cidr4,cidr6)';
		o.description = _('Logical network names (such as lan) or IPv4/IPv6 CIDRs. The trafficmon backend performs final validation.');
		s.option(form.Flag, 'debug', _('Debug logging'));
		return m.render().then(function(node) {
			return E('div', {}, [ node, E('h3', _('Status and diagnostics')), ui.diagnostics(status) ]);
		});
	}
});

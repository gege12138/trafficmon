'use strict';
'require view';
'require trafficmon.api as api';

function statusText(value) {
	return ({
		success: _('成功'),
		failed: _('失败'),
		pending: _('等待中'),
		not_attempted: _('未执行')
	})[value] || _('未知');
}

function errorText(value) {
	if (!value || value === 'none') return _('无');
	if (value === 'csv_publish_failed') return _('CSV 文件发布失败');
	var match = /^export_failed_([0-9]+)$/.exec(value);
	if (match) return _('CSV 导出失败（退出码 %s）').format(match[1]);
	match = /^upload_failed_([0-9]+)$/.exec(value);
	if (match) return _('SMB 上传失败（退出码 %s）').format(match[1]);
	return value;
}

function stateTable(state) {
	if (!state || !state.available) {
		if (state && state.error && state.error !== 'invalid_state')
			return E('div', { class: 'alert-message error' }, state.error);
		var message = state && state.error === 'invalid_state' ?
			_('最新归档状态文件损坏或格式无效。') :
			_('暂无归档状态。首次执行归档后将在此显示结果。');
		return E('div', { class: 'alert-message notice' }, message);
	}
	return E('table', { class: 'table' }, [
		E('tr', { class: 'tr table-titles' }, [ E('th', { class: 'th' }, _('项目')), E('th', { class: 'th' }, _('状态')) ]),
		E('tr', { class: 'tr' }, [ E('td', { class: 'td' }, _('最近归档月份')), E('td', { class: 'td' }, state.month) ]),
		E('tr', { class: 'tr' }, [ E('td', { class: 'td' }, _('导出状态')), E('td', { class: 'td' }, statusText(state.export_status)) ]),
		E('tr', { class: 'tr' }, [ E('td', { class: 'td' }, _('上传状态')), E('td', { class: 'td' }, statusText(state.upload_status)) ]),
		E('tr', { class: 'tr' }, [ E('td', { class: 'td' }, _('更新时间')), E('td', { class: 'td' }, state.updated_at === 'unknown' ? _('未知') : state.updated_at) ]),
		E('tr', { class: 'tr' }, [ E('td', { class: 'td' }, _('错误信息')), E('td', { class: 'td' }, errorText(state.error)) ])
	]);
}

return view.extend({
	load: function() { return api.archiveStatus(); },
	render: function(state) {
		var stateContainer = E('div', {}, stateTable(state));
		var resultContainer = E('div');
		var runButton, retryButton;

		function setBusy(busy) {
			runButton.disabled = busy;
			retryButton.disabled = busy;
		}
		function run(action, pendingText, successText) {
			setBusy(true);
			resultContainer.replaceChildren(E('div', { class: 'alert-message notice' }, pendingText));
			return action().then(function(result) {
				if (result.error) {
					resultContainer.replaceChildren(E('div', { class: 'alert-message error' }, result.error));
					return null;
				}
				resultContainer.replaceChildren(E('div', { class: 'alert-message success' }, successText));
				return api.archiveStatus().then(function(updated) {
					stateContainer.replaceChildren(stateTable(updated));
				});
			}).finally(function() { setBusy(false); });
		}

		runButton = E('button', {
			class: 'btn cbi-button cbi-button-action',
			click: function() { return run(api.archiveRun, _('正在执行归档…'), _('归档执行成功。')); }
		}, _('立即执行归档'));
		retryButton = E('button', {
			class: 'btn cbi-button cbi-button-action',
			click: function() { return run(api.archiveRetry, _('正在重新上传…'), _('重新上传成功。')); }
		}, _('重新上传'));

		return E([], [
			E('h2', {}, _('归档管理')),
			E('p', {}, _('查看最近一次月度归档状态，或手动执行归档和重新上传。')),
			stateContainer,
			E('div', { class: 'cbi-page-actions' }, [ runButton, ' ', retryButton ]),
			resultContainer
		]);
	},
	handleSaveApply: null,
	handleSave: null,
	handleReset: null
});

const I18N: Record<string, Record<string, string>> = {
  zh: {
    display: '显示器', all_displays: '全部显示器', curve_editor: '曲线编辑器', link: '联动',
    reset: '重置', presets: '预设', preset_name_ph: '预设名称', save_preset: '保存预设',
    apply_gamma: '应用伽马', reset_linear: '重置线性', gamma_preview: '伽马预览',
    upload_image: '上传图片', reset_image: '重置图片', original: '原始', gamma: '伽马',
    builtin_test: '内置测试图', ready: '就绪', loading: '加载中...',
    no_presets: '没有找到预设', point_info_hint: '点击控制点查看数值 | 双击曲线添加 | 右键控制点删除',
    enter_preset_name: '请输入预设名称', invalid_preset: '无效的预设', save_failed: '保存失败',
    loaded: '已加载', applied: '已应用到显示器', failed: '失败',
    reset_ok: '已重置为线性伽马', all: '全部', error: '错误',
    channel: '通道', point: '点',
    keybindings: '快捷键', no_keybindings: '没有快捷键',
    apply_preset: '应用预设', reset_display: '重置显示器', reset_all: '重置全部',
    press_combo: '按下组合键...', select_preset: '选择预设',
    update_preset: '更新预设',
    edit: '编辑', save: '保存', cancel: '取消', actions: '操作',
    action: '动作', details: '详情', hotkey: '快捷键',
    keybinding_admin_hint: '全屏游戏中快捷键无响应请尝试用管理员身份运行本程序',
  },
  en: {
    display: 'Display', all_displays: 'All Displays', curve_editor: 'Curve Editor', link: 'Link',
    reset: 'Reset', presets: 'Presets', preset_name_ph: 'preset_name', save_preset: 'Save as Preset',
    apply_gamma: 'Apply Gamma', reset_linear: 'Reset Linear', gamma_preview: 'Gamma Preview',
    upload_image: 'Upload Image', reset_image: 'Reset Image', original: 'Original', gamma: 'Gamma',
    builtin_test: 'Built-in test image', ready: 'Ready', loading: 'Loading...',
    no_presets: 'No presets found', point_info_hint: 'Click a control point to see values | Double-click canvas to add | Right-click point to delete',
    enter_preset_name: 'Enter a preset name', invalid_preset: 'Invalid preset', save_failed: 'Save failed',
    loaded: 'Loaded', applied: 'Applied to display', failed: 'Failed',
    reset_ok: 'Reset to linear gamma', all: 'ALL', error: 'Error',
    channel: 'Channel', point: 'Point',
    keybindings: 'Keybindings', no_keybindings: 'No keybindings',
    apply_preset: 'Apply Preset', reset_display: 'Reset Display', reset_all: 'Reset All',
    press_combo: 'press combo...', select_preset: 'select preset',
    update_preset: 'Update Preset',
    edit: 'Edit', save: 'Save', cancel: 'Cancel', actions: 'Actions',
    action: 'Action', details: 'Details', hotkey: 'Hotkey',
    keybinding_admin_hint: 'If hotkeys don\'t respond in fullscreen games, try running the program as administrator.',
  },
};

function getInitialLang(): 'zh' | 'en' {
  if (typeof navigator !== 'undefined' && (navigator.language || '').startsWith('zh')) {
    return 'zh';
  }
  return 'en';
}

const lang = $state({ current: getInitialLang() as 'zh' | 'en' });

export function t(key: string): string {
  return I18N[lang.current]?.[key] || key;
}

export function toggleLang() {
  lang.current = lang.current === 'zh' ? 'en' : 'zh';
}

export function isZh(): boolean {
  return lang.current === 'zh';
}

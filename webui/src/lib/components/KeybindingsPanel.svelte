<script lang="ts">
  import { t } from '$lib/i18n.svelte';
  import { store, saveKeybindingsToServer } from '$lib/state.svelte';

  let kbAction = $state('apply_preset');
  let kbPreset = $state('');
  let kbDisplay = $state(0);
  let kbKey = $state('');
  let capturing = $state(false);
  let editingIdx = $state<number | null>(null);

  let captureMods: string[] = [];

  function startCapture() {
    capturing = true;
    captureMods = [];
    kbKey = t('press_combo');
  }

  $effect(() => {
    if (!capturing) return;
    function handler(e: KeyboardEvent) {
      e.preventDefault();
      e.stopPropagation();
      if (e.key === 'Control' || e.key === 'Alt' || e.key === 'Shift') {
        const name = e.key.toLowerCase();
        if (!captureMods.includes(name)) captureMods.push(name);
        kbKey = captureMods.join('+') + '+...';
        return;
      }
      if (e.key === 'Meta' || e.key === 'Escape') return;
      if (!captureMods.includes(e.key.toLowerCase())) {
        captureMods.push(e.key.toLowerCase());
      }
      kbKey = captureMods.join('+');
      capturing = false;
    }
    window.addEventListener('keydown', handler);
    return () => window.removeEventListener('keydown', handler);
  });

  function resetForm() {
    kbAction = 'apply_preset';
    kbPreset = '';
    kbDisplay = 0;
    kbKey = '';
    editingIdx = null;
  }

  function submitKeybinding() {
    if (!kbKey || kbKey === t('press_combo')) return;
    if (kbAction === 'apply_preset' && !kbPreset) return;

    const entry: Record<string, unknown> = {
      action: kbAction,
      key: kbKey.split('+').map(s => s.trim()),
      display: [kbDisplay],
    };
    if (kbAction === 'apply_preset' && kbPreset) {
      entry.preset = kbPreset;
    }

    if (editingIdx !== null) {
      store.keybindings[editingIdx] = entry as any;
    } else {
      store.keybindings.push(entry as any);
    }

    resetForm();
    saveKeybindingsToServer();
  }

  function startEdit(idx: number) {
    const kb = store.keybindings[idx];
    editingIdx = idx;
    kbAction = kb.action;
    kbPreset = kb.preset || '';
    kbDisplay = kb.display?.[0] ?? 0;
    kbKey = kb.key.join('+');
  }

  function deleteKeybinding(idx: number) {
    if (editingIdx === idx) resetForm();
    store.keybindings.splice(idx, 1);
    saveKeybindingsToServer();
  }

  function actionLabel(action: string): string {
    const map: Record<string, string> = {
      apply_preset: t('apply_preset'),
      reset_display: t('reset_display'),
      reset_all: t('reset_all'),
    };
    return map[action] || action;
  }

  function kbDetails(kb: typeof store.keybindings[0]): string {
    if (kb.action === 'apply_preset' && kb.preset) return kb.preset;
    if (kb.display && kb.display[0] !== 0) return 'D' + kb.display.join(',');
    return '-';
  }
</script>

<h2>{t('keybindings')}</h2>

<div class="kb-table-wrap">
  {#if store.keybindings.length === 0}
    <div class="empty-msg">{t('no_keybindings')}</div>
  {:else}
    <table class="kb-table">
      <thead>
        <tr>
          <th>{t('action')}</th>
          <th>{t('details')}</th>
          <th>{t('hotkey')}</th>
          <th>{t('actions')}</th>
        </tr>
      </thead>
      <tbody>
        {#each store.keybindings as kb, i}
          <tr class:editing={editingIdx === i}>
            <td>{actionLabel(kb.action)}</td>
            <td>{kbDetails(kb)}</td>
            <td><span class="key-tag">{kb.key.join('+')}</span></td>
            <td class="actions-col">
              <button class="btn btn-sm btn-edit" onclick={() => startEdit(i)}>{t('edit')}</button>
              <!-- svelte-ignore a11y_click_events_have_key_events -->
              <!-- svelte-ignore a11y_no_static_element_interactions -->
              <span class="kb-del" onclick={() => deleteKeybinding(i)}>&times;</span>
            </td>
          </tr>
        {/each}
      </tbody>
    </table>
  {/if}
</div>

<div class="kb-edit">
  <select bind:value={kbAction}>
    <option value="apply_preset">{t('apply_preset')}</option>
    <option value="reset_display">{t('reset_display')}</option>
    <option value="reset_all">{t('reset_all')}</option>
  </select>
  {#if kbAction === 'apply_preset'}
    <select bind:value={kbPreset}>
      <option value="">-- {t('select_preset')} --</option>
      {#each store.presets as p}
        <option value={p}>{p}</option>
      {/each}
    </select>
  {/if}
  <select bind:value={kbDisplay}>
    <option value={0}>{t('all_displays')}</option>
    {#each store.displays as d, i}
      <option value={i + 1}>{d.name}</option>
    {/each}
  </select>
  <input type="text" value={kbKey} readonly
    placeholder="click then press keys"
    onclick={startCapture}
    class:capturing />
  <button class="btn btn-sm btn-save" onclick={submitKeybinding}>
    {editingIdx !== null ? t('save') : '+'}
  </button>
  {#if editingIdx !== null}
    <button class="btn btn-sm btn-reset" onclick={resetForm}>{t('cancel')}</button>
  {/if}
</div>

<p class="kb-admin-hint">{t('keybinding_admin_hint')}</p>

<style>
  .kb-table-wrap {
    max-height: 260px;
    overflow-y: auto;
    margin-top: 8px;
  }
  .empty-msg {
    font-size: 11px;
    color: #666;
  }
  .kb-table {
    width: 100%;
    border-collapse: collapse;
    font-size: 11px;
  }
  .kb-table th {
    text-align: left;
    padding: 5px 8px;
    color: #64748b;
    font-weight: 600;
    border-bottom: 1px solid #1e293b;
    position: sticky;
    top: 0;
    background: #181e26;
    z-index: 1;
  }
  .kb-table td {
    padding: 5px 8px;
    border-bottom: 1px solid #1e293b;
    vertical-align: middle;
  }
  .kb-table tr:hover td {
    background: #11171d;
  }
  .kb-table tr.editing td {
    background: #1a2838;
    outline: 1px solid #00d4ff;
    outline-offset: -1px;
  }
  .key-tag {
    background: #0c1014;
    padding: 1px 6px;
    border-radius: 3px;
    font-family: monospace;
    font-size: 10px;
  }
  .actions-col {
    white-space: nowrap;
    text-align: right;
  }
  .btn-edit {
    background: transparent;
    color: #64748b;
    border: 1px solid #334155;
    padding: 2px 8px;
    font-size: 10px;
    border-radius: 3px;
  }
  .btn-edit:hover {
    background: #00d4ff;
    color: #0c1014;
    border-color: #00d4ff;
  }
  .kb-del {
    margin-left: 4px;
    color: #ff5252;
    cursor: pointer;
    font-weight: bold;
    padding: 0 4px;
    font-size: 14px;
    vertical-align: middle;
  }
  .kb-del:hover { color: #ff7575; }
  .kb-edit {
    display: flex;
    gap: 4px;
    align-items: center;
    flex-wrap: wrap;
    font-size: 11px;
    margin-top: 8px;
  }
  .kb-edit select, .kb-edit input {
    font-size: 11px;
    padding: 3px 6px;
    min-width: auto;
    flex: 1;
  }
  .capturing {
    background: #00d4ff !important;
    color: #0c1014;
  }
  .kb-admin-hint {
    font-size: 10px;
    color: #64748b;
    margin: 6px 0 0 0;
  }
</style>

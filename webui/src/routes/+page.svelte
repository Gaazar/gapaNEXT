<script lang="ts">
  import { onMount } from 'svelte';
  import { t, toggleLang, isZh } from '$lib/i18n.svelte';
  import {
    store, setStatus,
    refreshDisplays, refreshPresets, refreshKeybindings,
    applyGammaAction, resetGammaAction, saveCurrentPreset, loadPresetFile,
  } from '$lib/state.svelte';
  import CurveEditor from '$lib/components/CurveEditor.svelte';
  import PreviewPanel from '$lib/components/PreviewPanel.svelte';
  import KeybindingsPanel from '$lib/components/KeybindingsPanel.svelte';

  onMount(() => {
    refreshDisplays();
    refreshPresets();
    refreshKeybindings();
  });

  async function handleApply() {
    const ok = await applyGammaAction();
    setStatus(
      ok ? t('applied') + ' ' + (store.selectedDisplay || t('all')) : t('failed'),
      !ok,
    );
  }

  async function handleReset() {
    const ok = await resetGammaAction();
    setStatus(ok ? t('reset_ok') : t('failed'), !ok);
  }

  async function handleSavePreset() {
    if (!store.presetName.trim()) {
      setStatus(t('enter_preset_name'), true);
      return;
    }
    const ok = await saveCurrentPreset();
    setStatus(ok ? t('loaded') : t('save_failed'), !ok);
  }

  async function handleLoadPreset(name: string) {
    const ok = await loadPresetFile(name);
    setStatus(ok ? t('loaded') + ': ' + name : t('invalid_preset'), true);
  }

  function isEditing() {
    return store.selectedPresetFile && store.selectedPresetFile === store.presetName;
  }

  const tabBtnActive = 'background:#00d4ff;color:#0c1014;border:none';
  const tabBtnInactive = 'background:transparent;color:#64748b;border:1px solid #334155';
</script>

<div class="panel">
  <div class="row" style="justify-content:space-between;align-items:center;margin-bottom:-4px">
    <span style="font-size:12px;font-weight:600;color:#00d4ff">gapaNEXT</span>
    <button class="btn btn-sm btn-save" style="font-size:10px;padding:2px 8px" onclick={toggleLang}>
      {isZh() ? 'EN' : '中文'}
    </button>
  </div>

  <div class="section">
    <h2>{t('display')}</h2>
    <select value={store.selectedDisplay} onchange={(e) => store.selectedDisplay = Number(e.currentTarget.value)}>
      <option value={0}>{t('all_displays')}</option>
      {#each store.displays as d, i}
        <option value={i + 1}>{d.name}</option>
      {/each}
    </select>
  </div>

  <CurveEditor />

  <div class="section">
    <h2>{t('presets')}</h2>
    <div class="row">
      <input type="text" value={store.presetName} oninput={(e) => store.presetName = e.currentTarget.value} placeholder={t('preset_name_ph')} />
      <button class="btn btn-sm btn-save" onclick={handleSavePreset}>
        {isEditing() ? t('update_preset') : t('save_preset')}
      </button>
    </div>
    <div class="file-list">
      {#if store.presets.length === 0}
        <div style="font-size:11px;color:#666">{t('no_presets')}</div>
      {:else}
        {#each store.presets as f}
          <div
            class="file-item"
            class:selected={f === store.selectedPresetFile}
            role="button"
            tabindex="0"
            onclick={() => handleLoadPreset(f)}
            onkeydown={(e) => { if (e.key === 'Enter') handleLoadPreset(f); }}
          >{f}</div>
        {/each}
      {/if}
    </div>
  </div>

  <div class="section">
    <div class="row">
      <button class="btn btn-apply" onclick={handleApply}>{t('apply_gamma')}</button>
      <button class="btn btn-reset" onclick={handleReset}>{t('reset_linear')}</button>
    </div>
    {#if store.statusMsg}
      <div class="status" class:ok={!store.statusErr} class:err={store.statusErr}>{store.statusMsg}</div>
    {:else}
      <div class="status">{t('ready')}</div>
    {/if}
  </div>
</div>

<div class="main">
  <div class="section" style="flex:1;display:flex;flex-direction:column;overflow:hidden">
    <div class="row" style="margin-bottom:8px">
      <button class="btn btn-sm"
        style={store.activeTab === 'preview' ? tabBtnActive : tabBtnInactive}
        onclick={() => store.activeTab = 'preview'}>{t('gamma_preview')}</button>
      <button class="btn btn-sm"
        style={store.activeTab === 'keybindings' ? tabBtnActive : tabBtnInactive}
        onclick={() => store.activeTab = 'keybindings'}>{t('keybindings')}</button>
    </div>
    {#if store.activeTab === 'preview'}
      <PreviewPanel />
    {:else}
      <KeybindingsPanel />
    {/if}
  </div>
</div>

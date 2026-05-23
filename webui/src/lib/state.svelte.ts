import { makeLinearPoints, pointsToRamp, MAX_VAL } from './gamma';
import type { ControlPoint, PointSelection, Keybinding, Display } from './types';
import * as api from './api';

export const store = $state({
  curPoints: [
    makeLinearPoints(),
    makeLinearPoints(),
    makeLinearPoints(),
  ] as ControlPoint[][],
  visibleCh: [true, true, true] as boolean[],
  linkedMode: true,
  selectedPoint: null as PointSelection | null,
  draggedPoint: null as PointSelection | null,
  selectedDisplay: 0,
  userImage: null as (HTMLImageElement & { _name?: string }) | null,
  splitPosition: 50,
  presets: [] as string[],
  displays: [] as Display[],
  keybindings: [] as Keybinding[],
  presetName: '',
  selectedPresetFile: '',
  activeTab: 'preview' as 'preview' | 'keybindings',
  statusMsg: '',
  statusErr: false,
});

let statusTimer: ReturnType<typeof setTimeout> | null = null;

export function setStatus(msg: string, isErr = false) {
  store.statusMsg = msg;
  store.statusErr = isErr;
  if (statusTimer) clearTimeout(statusTimer);
  if (!isErr) {
    statusTimer = setTimeout(() => {
      store.statusMsg = '';
      store.statusErr = false;
    }, 3000);
  }
}

export function loadIdentity() {
  const pts = makeLinearPoints();
  for (let ch = 0; ch < 3; ch++) {
    store.curPoints[ch] = pts.map(p => ({ ...p }));
  }
}

export function applyPresetPoints(
  pts: { r: [number, number][]; g: [number, number][]; b: [number, number][] }
) {
  const keys: Array<'r' | 'g' | 'b'> = ['r', 'g', 'b'];
  for (let ch = 0; ch < 3; ch++) {
    store.curPoints[ch] = pts[keys[ch]].map(([x, y]) => ({ x, y }));
  }
}

export function addPointAfterSelection() {
  if (!store.selectedPoint) return;
  const ch = store.selectedPoint.ch;
  let idx = store.selectedPoint.idx;
  if (idx >= store.curPoints[ch].length - 1) return;
  const p0 = store.curPoints[ch][idx];
  const p1 = store.curPoints[ch][idx + 1];
  if (p1.x - p0.x < 2) return;
  const newPt = { x: Math.round((p0.x + p1.x) / 2), y: Math.round((p0.y + p1.y) / 2) };
  store.curPoints[ch].splice(idx + 1, 0, newPt);
  if (store.linkedMode) {
    const dy = newPt.y - p0.y;
    for (let c = 0; c < 3; c++) {
      if (c === ch) continue;
      const q0 = store.curPoints[c][idx];
      const q1 = store.curPoints[c][idx + 1];
      const ny = Math.round(q0.y + (q1.y - q0.y) * dy / (p1.y - p0.y || 1));
      store.curPoints[c].splice(idx + 1, 0, { x: newPt.x, y: Math.max(0, Math.min(MAX_VAL, ny)) });
    }
  }
  store.selectedPoint = { ch, idx: idx + 1 };
}

export function deleteSelectedPoint(): boolean {
  if (!store.selectedPoint) return false;
  const ch = store.selectedPoint.ch;
  const idx = store.selectedPoint.idx;
  if (store.curPoints[ch].length <= 2) return false;
  if (idx === 0 || idx === store.curPoints[ch].length - 1) return false;
  store.curPoints[ch].splice(idx, 1);
  if (store.linkedMode) {
    for (let c = 0; c < 3; c++) {
      if (c === ch) continue;
      if (store.curPoints[c].length > 2) store.curPoints[c].splice(idx, 1);
    }
  }
  store.selectedPoint = null;
  return true;
}

export async function refreshPresets() {
  store.presets = await api.loadPresets();
}

export async function refreshDisplays() {
  store.displays = await api.loadDisplays();
}

export async function refreshKeybindings() {
  store.keybindings = await api.fetchKeybindings();
}

export async function applyGammaAction(): Promise<boolean> {
  const ramp = {
    red: pointsToRamp(store.curPoints[0]),
    green: pointsToRamp(store.curPoints[1]),
    blue: pointsToRamp(store.curPoints[2]),
  };
  const result = await api.applyGamma(store.selectedDisplay, ramp);
  return result.ok === true;
}

export async function resetGammaAction(): Promise<boolean> {
  const result = await api.resetGamma(store.selectedDisplay);
  return result.ok === true;
}

export async function saveCurrentPreset(): Promise<boolean> {
  let name = store.presetName.trim();
  if (!name) return false;
  if (!name.endsWith('.gmd')) name += '.gmd';
  const points: Record<string, [number, number][]> = {};
  ['r', 'g', 'b'].forEach((k, ch) => {
    points[k] = store.curPoints[ch].map(p => [p.x, p.y]);
  });
  const ok = await api.savePreset(name, points);
  if (ok) {
    store.presetName = '';
    await refreshPresets();
  }
  return ok;
}

export async function loadPresetFile(name: string): Promise<boolean> {
  const points = await api.loadPreset(name);
  if (!points) return false;
  applyPresetPoints(points);
  store.selectedPresetFile = name;
  store.presetName = name;
  return true;
}

export async function saveKeybindingsToServer() {
  await api.persistKeybindings(store.keybindings);
}

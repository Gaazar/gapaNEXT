import type {
  ApiDisplaysResponse, ApiFilesResponse, ApiPresetResponse,
  ApiStatusResponse, ApiKeybindingsResponse, PresetPoints, Keybinding, Display
} from './types';

async function api<T>(url: string, opts: RequestInit = {}): Promise<T> {
  const r = await fetch(url, opts);
  return r.json();
}

export async function loadDisplays(): Promise<Display[]> {
  try {
    const data = await api<ApiDisplaysResponse>('/api/displays');
    return data.displays || [];
  } catch (e) {
    console.error(e);
    return [];
  }
}

export async function loadPresets(): Promise<string[]> {
  try {
    const data = await api<ApiFilesResponse>('/api/files');
    return data.files || [];
  } catch (e) {
    console.error(e);
    return [];
  }
}

export async function loadPreset(name: string): Promise<PresetPoints | null> {
  try {
    const data = await api<ApiPresetResponse>('/api/presets/' + encodeURIComponent(name));
    return data.points || null;
  } catch (e) {
    console.error(e);
    return null;
  }
}

export async function savePreset(
  name: string,
  points: { r: [number, number][]; g: [number, number][]; b: [number, number][] }
): Promise<boolean> {
  try {
    const data = await api<ApiStatusResponse>('/api/presets/save', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ name, points }),
    });
    return data.ok === true;
  } catch (e) {
    console.error(e);
    return false;
  }
}

export async function applyGamma(
  displayIdx: number,
  ramp: { red: number[]; green: number[]; blue: number[] }
): Promise<ApiStatusResponse> {
  try {
    return await api<ApiStatusResponse>('/api/gamma/apply', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ display: displayIdx, ramp }),
    });
  } catch (e: any) {
    return { ok: false, error: e.message };
  }
}

export async function resetGamma(displayIdx: number): Promise<ApiStatusResponse> {
  try {
    return await api<ApiStatusResponse>('/api/gamma/reset', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ display: displayIdx }),
    });
  } catch (e: any) {
    return { ok: false, error: e.message };
  }
}

export async function fetchKeybindings(): Promise<Keybinding[]> {
  try {
    const data = await api<ApiKeybindingsResponse>('/api/keybindings');
    return data.keybindings || [];
  } catch (e) {
    console.error(e);
    return [];
  }
}

export async function persistKeybindings(keybindings: Keybinding[]): Promise<void> {
  try {
    await api('/api/keybindings', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ keybindings }),
    });
  } catch (e) {
    console.error(e);
  }
}

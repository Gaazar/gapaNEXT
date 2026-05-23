export interface ControlPoint {
  x: number;
  y: number;
}

export interface PointSelection {
  ch: number;
  idx: number;
}

export interface Keybinding {
  action: string;
  key: string[];
  preset?: string;
  display: number[];
}

export interface Display {
  name: string;
}

export interface PresetPoints {
  r: [number, number][];
  g: [number, number][];
  b: [number, number][];
}

export interface ApiDisplaysResponse {
  displays?: Display[];
}

export interface ApiFilesResponse {
  files?: string[];
}

export interface ApiPresetResponse {
  points?: PresetPoints;
}

export interface ApiStatusResponse {
  ok?: boolean;
  error?: string;
}

export interface ApiKeybindingsResponse {
  keybindings?: Keybinding[];
}

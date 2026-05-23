import type { ControlPoint } from './types';

export const N = 256;
export const MAX_VAL = 65535;

export function makeLinearPoints(): ControlPoint[] {
  return [
    { x: 0, y: 0 },
    { x: 255, y: MAX_VAL },
  ];
}

// Monotone Cubic Hermite spline (Fritsch-Carlson)
export function pointsToRamp(pts: ControlPoint[]): number[] {
  const ramp = new Array<number>(N);
  const n = pts.length;
  const xs = pts.map(p => p.x);
  const ys = pts.map(p => p.y);

  const m = new Array<number>(n - 1);
  for (let i = 0; i < n - 1; i++) {
    const dx = xs[i + 1] - xs[i];
    m[i] = dx > 0 ? (ys[i + 1] - ys[i]) / dx : 0;
  }

  const d = new Array<number>(n);
  d[0] = m[0];
  d[n - 1] = m[n - 2];
  for (let i = 1; i < n - 1; i++) {
    if (m[i - 1] * m[i] <= 0) {
      d[i] = 0;
    } else {
      const h0 = xs[i] - xs[i - 1];
      const h1 = xs[i + 1] - xs[i];
      const a = (1 + h1 / (h0 + h1)) / 3;
      d[i] = (m[i - 1] * m[i]) / (a * m[i] + (1 - a) * m[i - 1]);
    }
  }

  for (let i = 0; i < N; i++) {
    const x = i;
    if (x <= xs[0]) { ramp[i] = Math.round(ys[0]); continue; }
    if (x >= xs[n - 1]) { ramp[i] = Math.round(ys[n - 1]); continue; }

    let seg = 0;
    while (seg < n - 2 && x > xs[seg + 1]) seg++;

    const h = xs[seg + 1] - xs[seg];
    const t = (x - xs[seg]) / h;
    const t2 = t * t;
    const t3 = t2 * t;

    const y0 = ys[seg], y1 = ys[seg + 1];
    const m0 = d[seg] * h, m1 = d[seg + 1] * h;

    let v = Math.round(
      (2 * t3 - 3 * t2 + 1) * y0 +
      (t3 - 2 * t2 + t) * m0 +
      (-2 * t3 + 3 * t2) * y1 +
      (t3 - t2) * m1
    );
    v = Math.max(0, Math.min(MAX_VAL, v));
    ramp[i] = v;
  }

  for (let i = 1; i < N; i++) {
    if (ramp[i] < ramp[i - 1]) ramp[i] = ramp[i - 1];
  }
  return ramp;
}

export function buildTestImage(): HTMLCanvasElement {
  const c = document.createElement('canvas');
  const P = 512, Q = 256;
  c.width = P; c.height = Q;
  const ctx = c.getContext('2d')!;
  const barH = Q / 4;

  for (let x = 0; x < P; x++) {
    const t = x / (P - 1);
    ctx.fillStyle = `rgb(${(t * 255) | 0},${(t * 255) | 0},${(t * 255) | 0})`;
    ctx.fillRect(x, 0, 1, barH);
  }

  const imgData = ctx.getImageData(0, barH, P, Q - barH);
  const d = imgData.data;
  const rampH = Q - barH;
  for (let y = 0; y < rampH; y++) {
    for (let x = 0; x < P; x++) {
      const i = (y * P + x) * 4;
      const t = x / (P - 1);
      const seg = Math.floor(y / (rampH / 3));
      if (seg === 0) { d[i] = 255; d[i + 1] = (t * 255) | 0; d[i + 2] = (t * 255) | 0; }
      else if (seg === 1) { d[i] = (t * 255) | 0; d[i + 1] = 255; d[i + 2] = (t * 255) | 0; }
      else { d[i] = (t * 255) | 0; d[i + 1] = (t * 255) | 0; d[i + 2] = 255; }
      d[i + 3] = 255;
    }
  }
  ctx.putImageData(imgData, 0, barH);
  ctx.fillStyle = '#fff'; ctx.font = '12px monospace';
  ctx.fillText('Grayscale', 4, 12);
  ctx.fillText('Red-Yellow', 4, barH + 12);
  ctx.fillText('Green-Yellow', 4, barH + rampH / 3 + 12);
  ctx.fillText('Blue-Yellow', 4, barH + 2 * rampH / 3 + 12);
  return c;
}

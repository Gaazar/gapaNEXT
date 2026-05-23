<script lang="ts">
  import { t } from '$lib/i18n.svelte';
  import { pointsToRamp, MAX_VAL } from '$lib/gamma';
  import { store, loadIdentity, deleteSelectedPoint, addPointAfterSelection } from '$lib/state.svelte';
  import type { PointSelection } from '$lib/types';

  const W = 384, H = 256, MARGIN = 20;
  const PW = W - MARGIN * 2, PH = H - MARGIN * 2;
  const chColors = ['#ff4444', '#44ff44', '#4488ff'];
  const chLabels = ['R', 'G', 'B'];

  let canvasEl = $state<HTMLCanvasElement | null>(null);

  $effect(() => {
    if (store.linkedMode) {
      store.visibleCh[0] = store.visibleCh[1] = store.visibleCh[2] = true;
    }
  });

  function hitTest(mx: number, my: number): PointSelection | null {
    let best: PointSelection | null = null;
    let bestDist = 16;
    for (let ch = 0; ch < 3; ch++) {
      if (!store.visibleCh[ch]) continue;
      for (let i = 0; i < store.curPoints[ch].length; i++) {
        const pt = store.curPoints[ch][i];
        const px = MARGIN + PW * pt.x / 255;
        const py = MARGIN + PH - PH * pt.y / MAX_VAL;
        const dist = Math.hypot(mx - px, my - py);
        if (dist < bestDist) { bestDist = dist; best = { ch, idx: i }; }
      }
    }
    return best;
  }

  function canvasCoords(e: MouseEvent) {
    const rect = canvasEl!.getBoundingClientRect();
    const sx = W / rect.width;
    const sy = H / rect.height;
    return {
      mx: (e.clientX - rect.left) * sx,
      my: (e.clientY - rect.top) * sy,
    };
  }

  function onMouseDown(e: MouseEvent) {
    const { mx, my } = canvasCoords(e);
    const hit = hitTest(mx, my);
    if (hit) {
      store.draggedPoint = hit;
      store.selectedPoint = hit;
      e.preventDefault();
    } else {
      store.selectedPoint = null;
    }
  }

  function onMouseMove(e: MouseEvent) {
    if (!store.draggedPoint || !canvasEl) return;
    const { mx, my } = canvasCoords(e);
    let nx = Math.round((mx - MARGIN) / PW * 255);
    let ny = Math.round((MARGIN + PH - my) / PH * MAX_VAL);
    nx = Math.max(0, Math.min(255, nx));
    ny = Math.max(0, Math.min(MAX_VAL, ny));

    const ch = store.draggedPoint.ch;
    const idx = store.draggedPoint.idx;

    if (idx > 0 && nx <= store.curPoints[ch][idx - 1].x) nx = store.curPoints[ch][idx - 1].x + 1;
    if (idx < store.curPoints[ch].length - 1 && nx >= store.curPoints[ch][idx + 1].x) nx = store.curPoints[ch][idx + 1].x - 1;

    const dx = nx - store.curPoints[ch][idx].x;
    const dy = ny - store.curPoints[ch][idx].y;

    store.curPoints[ch][idx].x = nx;
    store.curPoints[ch][idx].y = ny;

    if (store.linkedMode) {
      for (let c = 0; c < 3; c++) {
        if (c === ch) continue;
        const pt = store.curPoints[c][idx];
        if (idx > 0 && pt.x + dx <= store.curPoints[c][idx - 1].x) continue;
        if (idx < store.curPoints[c].length - 1 && pt.x + dx >= store.curPoints[c][idx + 1].x) continue;
        pt.x += dx;
        pt.y = Math.max(0, Math.min(MAX_VAL, pt.y + dy));
      }
    }
  }

  function onMouseUp() { store.draggedPoint = null; }

  function onDblClick(e: MouseEvent) {
    const { mx, my } = canvasCoords(e);
    let nx = Math.round((mx - MARGIN) / PW * 255);
    let ny = Math.round((MARGIN + PH - my) / PH * MAX_VAL);
    nx = Math.max(1, Math.min(254, nx));
    ny = Math.max(0, Math.min(MAX_VAL, ny));

    const firstVis = store.visibleCh.findIndex(v => v);
    if (firstVis < 0) return;

    let ins = store.curPoints[firstVis].length - 1;
    for (let i = 0; i < store.curPoints[firstVis].length - 1; i++) {
      if (nx > store.curPoints[firstVis][i].x && nx < store.curPoints[firstVis][i + 1].x) { ins = i + 1; break; }
    }

    if (store.linkedMode) {
      for (let c = 0; c < 3; c++) {
        store.curPoints[c].splice(ins, 0, { x: nx, y: ny });
      }
    } else {
      for (let c = 0; c < 3; c++) {
        if (!store.visibleCh[c]) continue;
        const ramp = pointsToRamp(store.curPoints[c]);
        const iy = ramp[nx];
        store.curPoints[c].splice(ins, 0, { x: nx, y: iy });
      }
    }
    store.selectedPoint = { ch: firstVis, idx: ins };
  }

  function onContextMenu(e: MouseEvent) {
    e.preventDefault();
    const { mx, my } = canvasCoords(e);
    const hit = hitTest(mx, my);
    if (!hit) return;
    if (store.curPoints[hit.ch].length <= 2) return;
    if (hit.idx === 0 || hit.idx === store.curPoints[hit.ch].length - 1) return;
    store.selectedPoint = hit;
    deleteSelectedPoint();
  }

  // --- Drawing ---
  function draw(ctx: CanvasRenderingContext2D) {
    ctx.clearRect(0, 0, W, H);

    // Grid
    ctx.strokeStyle = '#1e293b';
    ctx.lineWidth = 1;
    for (let i = 0; i <= 4; i++) {
      const y = MARGIN + PH * i / 4;
      ctx.beginPath(); ctx.moveTo(MARGIN, y); ctx.lineTo(W - MARGIN, y); ctx.stroke();
      const x = MARGIN + PW * i / 4;
      ctx.beginPath(); ctx.moveTo(x, MARGIN); ctx.lineTo(x, H - MARGIN); ctx.stroke();
    }

    for (let ch = 0; ch < 3; ch++) {
      if (!store.visibleCh[ch]) continue;
      const ramp = pointsToRamp(store.curPoints[ch]);
      ctx.strokeStyle = chColors[ch];
      ctx.lineWidth = 2;
      ctx.beginPath();
      for (let i = 0; i < 256; i++) {
        const x = MARGIN + PW * i / 255;
        const y = MARGIN + PH - PH * (ramp[i] / MAX_VAL);
        if (i === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y);
      }
      ctx.stroke();

      for (let k = 0; k < store.curPoints[ch].length; k++) {
        const pt = store.curPoints[ch][k];
        const px = MARGIN + PW * pt.x / 255;
        const py = MARGIN + PH - PH * pt.y / MAX_VAL;
        const sel = store.selectedPoint && store.selectedPoint.ch === ch && store.selectedPoint.idx === k;
        ctx.fillStyle = sel ? '#fff' : chColors[ch];
        ctx.beginPath(); ctx.arc(px, py, sel ? 7 : 5, 0, Math.PI * 2); ctx.fill();
        ctx.strokeStyle = sel ? chColors[ch] : '#fff';
        ctx.lineWidth = sel ? 3 : 2;
        ctx.beginPath(); ctx.arc(px, py, sel ? 7 : 5, 0, Math.PI * 2); ctx.stroke();
        ctx.lineWidth = 2;
      }
    }
  }

  $effect(() => {
    const ctx = canvasEl?.getContext('2d');
    if (!ctx) return;
    void store.curPoints; void store.visibleCh; void store.selectedPoint;
    draw(ctx);
  });
</script>

<div class="section">
  <h2>{t('curve_editor')}</h2>
  <div class="toggle-bar">
    {#each [0, 1, 2] as ch}
      <label>
        <input type="checkbox" checked={store.visibleCh[ch]} disabled={store.linkedMode}
          onchange={(e) => store.visibleCh[ch] = e.currentTarget.checked} />
        <span style="color:{chColors[ch]}">{chLabels[ch]}</span>
      </label>
    {/each}
    <label style="margin-left:8px">
      <input type="checkbox" checked={store.linkedMode}
        onchange={(e) => store.linkedMode = e.currentTarget.checked} />
      <span style="color:#00d4ff">{t('link')}</span>
    </label>
  </div>
  <canvas
    bind:this={canvasEl}
    width={W} height={H}
    onmousedown={onMouseDown}
    onmousemove={onMouseMove}
    onmouseup={onMouseUp}
    onmouseleave={onMouseUp}
    ondblclick={onDblClick}
    oncontextmenu={onContextMenu}
    style="cursor:crosshair;background:#0c1014;width:100%"
  ></canvas>
  <div class="row" style="margin-top:4px">
    <button class="btn btn-sm btn-reset" onclick={loadIdentity}>{t('reset')}</button>
  </div>
  <div class="point-info">
    {#if store.draggedPoint || store.selectedPoint}
      {@const dp = store.draggedPoint ?? store.selectedPoint!}
      {@const pt = store.curPoints[dp.ch][dp.idx]}
      {t('channel')}: <span style="color:{chColors[dp.ch]}">{chLabels[dp.ch]}</span>
      {t('point')} {dp.idx} X={pt.x} Y={pt.y} ({(pt.y / MAX_VAL * 100).toFixed(1)}%)
    {:else}
      {t('point_info_hint')}
    {/if}
  </div>
</div>

<style>
  .toggle-bar {
    display: flex;
    gap: 8px;
    margin-bottom: 4px;
  }
  .toggle-bar label {
    font-size: 11px;
    cursor: pointer;
    display: flex;
    align-items: center;
    gap: 3px;
  }
  .toggle-bar input {
    accent-color: #00d4ff;
  }
  .point-info {
    font-size: 11px;
    font-family: monospace;
    color: #64748b;
    margin-top: 4px;
  }
</style>

<script lang="ts">
  import { onMount } from 'svelte';
  import { t } from '$lib/i18n.svelte';
  import { pointsToRamp, MAX_VAL, buildTestImage } from '$lib/gamma';
  import { store } from '$lib/state.svelte';

  let canvasEl = $state<HTMLCanvasElement | null>(null);
  let testImg = $state<HTMLCanvasElement | null>(null);
  let splitDragging = $state(false);
  let fileInput = $state<HTMLInputElement | null>(null);

  onMount(() => {
    testImg = buildTestImage();
  });

  function getSplitX(w: number): number {
    return Math.round(w * store.splitPosition / 100);
  }

  function renderPreview() {
    const canvas = canvasEl;
    if (!canvas) return;
    const wrap = canvas.parentElement;
    if (!wrap) return;
    const w = wrap.clientWidth;
    const h = wrap.clientHeight;
    if (w <= 0 || h <= 0) return;

    canvas.width = w;
    canvas.height = h;
    const ctx = canvas.getContext('2d')!;

    const rRamp = pointsToRamp(store.curPoints[0]);
    const gRamp = pointsToRamp(store.curPoints[1]);
    const bRamp = pointsToRamp(store.curPoints[2]);
    const lutR = new Uint8Array(256);
    const lutG = new Uint8Array(256);
    const lutB = new Uint8Array(256);
    for (let i = 0; i < 256; i++) {
      const idx = Math.round(i * 255 / 255);
      lutR[i] = Math.round(rRamp[idx] / MAX_VAL * 255);
      lutG[i] = Math.round(gRamp[idx] / MAX_VAL * 255);
      lutB[i] = Math.round(bRamp[idx] / MAX_VAL * 255);
    }

    function applyLUT(imgData: ImageData) {
      const d = imgData.data;
      for (let i = 0; i < d.length; i += 4) {
        d[i] = lutR[d[i]];
        d[i + 1] = lutG[d[i + 1]];
        d[i + 2] = lutB[d[i + 2]];
      }
    }

    const srcImg = store.userImage ?? testImg;
    if (!srcImg) return;

    const scale = Math.min(w / srcImg.width, h / srcImg.height);
    const sw = srcImg.width * scale;
    const sh = srcImg.height * scale;
    const sx = (w - sw) / 2;
    const sy = (h - sh) / 2;

    ctx.drawImage(srcImg, sx, sy, sw, sh);
    const origData = ctx.getImageData(0, 0, w, h);

    const off = document.createElement('canvas');
    off.width = w; off.height = h;
    const offCtx = off.getContext('2d')!;
    offCtx.drawImage(srcImg, sx, sy, sw, sh);
    const gammaData = offCtx.getImageData(0, 0, w, h);
    applyLUT(gammaData);

    const splitX = getSplitX(w);
    const outData = ctx.getImageData(0, 0, w, h);
    const o = origData.data, g = gammaData.data, out = outData.data;
    for (let i = 0; i < out.length; i += 4) {
      const px = (i / 4) % w;
      if (px < splitX) {
        out[i] = o[i]; out[i + 1] = o[i + 1]; out[i + 2] = o[i + 2];
      } else {
        out[i] = g[i]; out[i + 1] = g[i + 1]; out[i + 2] = g[i + 2];
      }
      out[i + 3] = 255;
    }
    ctx.putImageData(outData, 0, 0);

    if (store.splitPosition > 0 && store.splitPosition < 100) {
      ctx.strokeStyle = '#fff';
      ctx.lineWidth = 2;
      ctx.setLineDash([4, 4]);
      ctx.beginPath(); ctx.moveTo(splitX, 0); ctx.lineTo(splitX, h); ctx.stroke();
      ctx.setLineDash([]);
    }
  }

  $effect(() => {
    void store.curPoints; void store.userImage; void store.splitPosition;
    renderPreview();
  });

  function onWindowResize() {
    renderPreview();
  }

  function onSplitMouseDown(e: MouseEvent) {
    if (!canvasEl) return;
    const w = canvasEl.clientWidth;
    const splitX = getSplitX(w);
    const mx = e.clientX - canvasEl.getBoundingClientRect().left;
    if (Math.abs(mx - splitX) < 12) {
      splitDragging = true;
      e.preventDefault();
    }
  }

  function onSplitMouseMove(e: MouseEvent) {
    if (!canvasEl) return;
    const w = canvasEl.clientWidth;
    const splitXNew = getSplitX(w);
    const mx = e.clientX - canvasEl.getBoundingClientRect().left;
    if (splitDragging) {
      store.splitPosition = Math.round(mx / w * 100);
    } else {
      canvasEl.style.cursor = Math.abs(mx - splitXNew) < 12 ? 'ew-resize' : 'default';
    }
  }

  function onSplitMouseUp() { splitDragging = false; }

  function handleImageUpload(e: Event) {
    const target = e.target as HTMLInputElement;
    const file = target.files?.[0];
    if (!file) return;
    const img = new Image();
    img.onload = () => {
      (img as any)._name = file.name;
      store.userImage = img as HTMLImageElement & { _name?: string };
    };
    img.src = URL.createObjectURL(file);
  }

  function resetImage() {
    store.userImage = null;
    if (fileInput) fileInput.value = '';
  }
</script>

<svelte:window onresize={onWindowResize} onmouseup={onSplitMouseUp} />

<h2>{t('gamma_preview')}</h2>
<div class="row" style="margin-bottom:6px">
  <button class="btn btn-sm btn-save" onclick={() => fileInput?.click()}>{t('upload_image')}</button>
  <button class="btn btn-sm btn-reset" onclick={resetImage}>{t('reset_image')}</button>
  <input type="file" accept="image/*" style="display:none" bind:this={fileInput} onchange={handleImageUpload} />
  <span style="font-size:10px;color:#64748b">
    {store.userImage && (store.userImage as any)._name ? (store.userImage as any)._name : t('builtin_test')}
  </span>
</div>
<div class="row" style="margin-top:4px">
  <span style="font-size:10px;color:#64748b;min-width:40px">{t('original')}</span>
  <input type="range" min="0" max="100" value={store.splitPosition}
    oninput={(e) => store.splitPosition = Number(e.currentTarget.value)} style="flex:1" />
  <span style="font-size:10px;color:#64748b;min-width:40px;text-align:right">{t('gamma')}</span>
</div>
<div class="preview-wrap">
  <canvas bind:this={canvasEl}
    onmousedown={onSplitMouseDown}
    onmousemove={onSplitMouseMove}
  ></canvas>
</div>

<style>
  .preview-wrap {
    flex: 1;
    display: flex;
    align-items: center;
    justify-content: center;
    overflow: hidden;
    min-height: 0;
  }
  .preview-wrap canvas {
    width: 100%;
    height: 100%;
    object-fit: contain;
  }
</style>

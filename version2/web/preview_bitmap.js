const LOGICAL_W = 400;
const LOGICAL_H = 440;
const DEVICE_W = 240;
const DEVICE_H = 240;

function hexToRgb(hex) {
  const clean = hex.replace("#", "");
  return {
    r: parseInt(clean.slice(0, 2), 16),
    g: parseInt(clean.slice(2, 4), 16),
    b: parseInt(clean.slice(4, 6), 16),
  };
}

function rgbTo565({ r, g, b }) {
  return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
}

function roundRectPath(ctx, x, y, w, h, r) {
  ctx.beginPath();
  ctx.moveTo(x + r, y);
  ctx.lineTo(x + w - r, y);
  ctx.quadraticCurveTo(x + w, y, x + w, y + r);
  ctx.lineTo(x + w, y + h - r);
  ctx.quadraticCurveTo(x + w, y + h, x + w - r, y + h);
  ctx.lineTo(x + r, y + h);
  ctx.quadraticCurveTo(x, y + h, x, y + h - r);
  ctx.lineTo(x, y + r);
  ctx.quadraticCurveTo(x, y, x + r, y);
  ctx.closePath();
}

function drawQuote(ctx, vstate) {
  if (!vstate.quote) return;

  const quote = vstate.quote;
  ctx.save();
  ctx.font = 'bold 14px "SF Mono", Menlo, monospace';
  ctx.textAlign = "center";
  ctx.textBaseline = "middle";

  const textWidth = ctx.measureText(quote).width;
  const padX = 12;
  const padY = 7;
  const bubbleW = Math.min(LOGICAL_W * 0.8, textWidth + padX * 2);
  const bubbleH = 34;
  const bubbleX = (LOGICAL_W - bubbleW) / 2;
  const bubbleY = 58;

  ctx.fillStyle = vstate.bgColorSoft;
  ctx.strokeStyle = vstate.personColor;
  ctx.lineWidth = 1.5;
  roundRectPath(ctx, bubbleX, bubbleY, bubbleW, bubbleH, 4);
  ctx.fill();
  ctx.stroke();

  ctx.fillStyle = vstate.personColor;
  ctx.beginPath();
  ctx.moveTo(LOGICAL_W / 2 - 7, bubbleY + bubbleH);
  ctx.lineTo(LOGICAL_W / 2 + 7, bubbleY + bubbleH);
  ctx.lineTo(LOGICAL_W / 2, bubbleY + bubbleH + 12);
  ctx.closePath();
  ctx.fill();

  ctx.fillStyle = vstate.personColor;
  ctx.fillText(quote, LOGICAL_W / 2, bubbleY + bubbleH / 2 + 1);
  ctx.restore();
}

function drawAsciiSprite(ctx, vstate) {
  if (!vstate.asciiArt) return;

  const lines = vstate.asciiArt.split("\n");
  const stepX = 7.7;
  const rowPitch = 12.1;
  const originY = 72;

  ctx.save();
  ctx.fillStyle = vstate.personColor;
  ctx.font = 'bold 22px "Lucida Console", Monaco, "Courier New", monospace';
  ctx.textBaseline = "alphabetic";

  for (let row = 0; row < lines.length; row += 1) {
    const line = lines[row];
    const startX = (LOGICAL_W - line.length * stepX) / 2;
    const baselineY = originY + row * rowPitch + 18;

    for (let col = 0; col < line.length; col += 1) {
      const ch = line[col];
      if (ch === " ") continue;
      ctx.fillText(ch, startX + col * stepX, baselineY);
    }
  }

  ctx.restore();
}

function renderPreviewCanvas(vstate) {
  const canvas = document.createElement("canvas");
  canvas.width = DEVICE_W;
  canvas.height = DEVICE_H;
  const ctx = canvas.getContext("2d", { willReadFrequently: true });

  const scale = Math.min(DEVICE_W / LOGICAL_W, DEVICE_H / LOGICAL_H);
  const drawW = LOGICAL_W * scale;
  const drawH = LOGICAL_H * scale;
  const offsetX = (DEVICE_W - drawW) / 2;
  const offsetY = (DEVICE_H - drawH) / 2;

  ctx.fillStyle = vstate.bgColorSoft;
  ctx.fillRect(0, 0, DEVICE_W, DEVICE_H);

  ctx.save();
  ctx.translate(offsetX, offsetY);
  ctx.scale(scale, scale);

  ctx.fillStyle = vstate.bgColorSoft;
  ctx.strokeStyle = vstate.darkerColor;
  ctx.lineWidth = 1.5;
  roundRectPath(ctx, 0.75, 0.75, LOGICAL_W - 1.5, LOGICAL_H - 1.5, 8);
  ctx.fill();
  ctx.stroke();

  drawQuote(ctx, vstate);
  drawAsciiSprite(ctx, vstate);

  ctx.restore();
  return canvas;
}

function packBitmap(canvas, fgHex, bgHex) {
  const fg = hexToRgb(fgHex);
  const bg = hexToRgb(bgHex);
  const ctx = canvas.getContext("2d", { willReadFrequently: true });
  const { data } = ctx.getImageData(0, 0, canvas.width, canvas.height);
  const bytesPerRow = Math.ceil(canvas.width / 8);
  const bitmap = new Uint8Array(bytesPerRow * canvas.height);

  function distSq(px, ref) {
    const dr = px.r - ref.r;
    const dg = px.g - ref.g;
    const db = px.b - ref.b;
    return dr * dr + dg * dg + db * db;
  }

  for (let y = 0; y < canvas.height; y += 1) {
    for (let x = 0; x < canvas.width; x += 1) {
      const i = (y * canvas.width + x) * 4;
      const px = { r: data[i], g: data[i + 1], b: data[i + 2] };
      const useFg = distSq(px, fg) <= distSq(px, bg);
      if (useFg) {
        const byteIndex = y * bytesPerRow + (x >> 3);
        bitmap[byteIndex] |= (0x80 >> (x & 7));
      }
    }
  }

  return {
    width: canvas.width,
    height: canvas.height,
    fg565: rgbTo565(fg),
    bg565: rgbTo565(bg),
    bitmap,
  };
}

export function renderPreviewBitmap(vstate) {
  const canvas = renderPreviewCanvas(vstate);
  const packed = packBitmap(canvas, vstate.personColor, vstate.bgColorSoft);
  return {
    ...packed,
    canvas,
  };
}

const FONT_FAMILY = '"LXGW WenKai GB Screen"';
const SCREEN = { width: 800, height: 480 };
const BOX = { left: 32, top: 24, width: 736, height: 300 };
const STRIP_TOP = 336;
const SIZE_MAX = 128;
const SIZE_MIN = 32;
const SIZE_STEP = 4;
const LINE_HEIGHT = 1.5;
const DIGIT_GAP_EM = 0.25;
const HANGING_SHIFT_EM = 0.25;

const HAN = /\p{Script=Han}/u;
const DIGIT = /\p{Nd}/u;
const WORD = /[\p{L}\p{N}'’-]/u;
const HANGING = /[。，！？]\s*$/u;
const NO_LINE_START = /[。，、：；！？」』）】》〉〕〗’”,.;:!?)\]}%…]/u;
const NO_LINE_END = /[「『（【《〈〔〖‘“(\[{]/u;

function gapBetween(a, b) {
  return (DIGIT.test(a) && HAN.test(b)) || (HAN.test(a) && DIGIT.test(b));
}

function breakAllowed(a, b) {
  if (b === " ") return false;
  if (a === " ") return true;
  if (NO_LINE_START.test(b) || NO_LINE_END.test(a)) return false;
  const bothLatin = WORD.test(a) && WORD.test(b) && !HAN.test(a) && !HAN.test(b);
  return !bothLatin;
}

function runsOf(line) {
  const runs = [];
  let run = "";
  let previous = null;
  for (const character of line) {
    if (previous !== null && gapBetween(previous, character)) {
      runs.push(run);
      run = "";
    }
    run += character;
    previous = character;
  }
  runs.push(run);
  return runs;
}

function lineWidth(ctx, sizePx, line) {
  const runs = runsOf(line);
  const text = runs.reduce((sum, run) => sum + ctx.measureText(run).width, 0);
  return text + (runs.length - 1) * sizePx * DIGIT_GAP_EM;
}

function wrapParagraph(ctx, sizePx, paragraph) {
  const characters = [...paragraph];
  const lines = [];
  let start = 0;
  while (start < characters.length) {
    let end = start;
    let lastBreak = -1;
    while (end < characters.length) {
      const candidate = characters
        .slice(start, end + 1)
        .join("")
        .trimEnd();
      if (lineWidth(ctx, sizePx, candidate) > BOX.width) break;
      end++;
      if (end < characters.length && breakAllowed(characters[end - 1], characters[end])) {
        lastBreak = end;
      }
    }
    // a word wider than the box breaks inside, as Pango's word-char wrap does
    const lineEnd =
      end === characters.length ? end : lastBreak > start ? lastBreak : Math.max(end, start + 1);
    lines.push(characters.slice(start, lineEnd).join("").trimEnd());
    start = lineEnd;
    while (characters[start] === " ") start++;
  }
  return lines.length > 0 ? lines : [""];
}

function layoutAt(ctx, sizePx, text) {
  ctx.font = `${sizePx}px ${FONT_FAMILY}`;
  return text.split("\n").flatMap((paragraph) => wrapParagraph(ctx, sizePx, paragraph));
}

export function fitPhrase(ctx, text) {
  let lines = [];
  for (let sizePx = SIZE_MAX; sizePx >= SIZE_MIN; sizePx -= SIZE_STEP) {
    lines = layoutAt(ctx, sizePx, text);
    if (lines.length * sizePx * LINE_HEIGHT <= BOX.height) return { sizePx, lines, fits: true };
  }
  return { sizePx: SIZE_MIN, lines, fits: false };
}

function cutAtHalf(ctx, width, height) {
  const image = ctx.getImageData(0, 0, width, height);
  const pixels = image.data;
  for (let i = 0; i < pixels.length; i += 4) {
    const ink = pixels[i] < 128 ? 0 : 255;
    pixels[i] = pixels[i + 1] = pixels[i + 2] = ink;
    pixels[i + 3] = 255;
  }
  ctx.putImageData(image, 0, 0);
}

function drawLines(ctx, { sizePx, lines }) {
  ctx.font = `${sizePx}px ${FONT_FAMILY}`;
  ctx.fillStyle = "#000";
  ctx.textBaseline = "alphabetic";
  const lineHeight = sizePx * LINE_HEIGHT;
  const blockTop = BOX.top + (BOX.height - lines.length * lineHeight) / 2;
  lines.forEach((line, index) => {
    const metrics = ctx.measureText(line);
    const ascent = metrics.fontBoundingBoxAscent ?? sizePx * 0.88;
    const descent = metrics.fontBoundingBoxDescent ?? sizePx * 0.12;
    const lineTop = blockTop + index * lineHeight;
    const baseline = Math.round(lineTop + (lineHeight - ascent - descent) / 2 + ascent);
    let x = BOX.left + (BOX.width - lineWidth(ctx, sizePx, line)) / 2;
    // the ink of 。，！？ sits in the left half of its box and would pull the line off centre
    if (HANGING.test(line)) x += sizePx * HANGING_SHIFT_EM;
    for (const run of runsOf(line)) {
      ctx.fillText(run, Math.round(x), baseline);
      x += ctx.measureText(run).width + sizePx * DIGIT_GAP_EM;
    }
  });
}

export function drawPreview(canvas, text, screenPng) {
  const ctx = canvas.getContext("2d", { willReadFrequently: true });
  ctx.fillStyle = "#fff";
  ctx.fillRect(0, 0, SCREEN.width, SCREEN.height);
  const fit = fitPhrase(ctx, text);
  ctx.save();
  ctx.beginPath();
  ctx.rect(0, 0, SCREEN.width, STRIP_TOP);
  ctx.clip();
  drawLines(ctx, fit);
  ctx.restore();
  cutAtHalf(ctx, SCREEN.width, STRIP_TOP);
  const stripHeight = SCREEN.height - STRIP_TOP;
  if (screenPng) {
    ctx.drawImage(
      screenPng,
      0,
      STRIP_TOP,
      SCREEN.width,
      stripHeight,
      0,
      STRIP_TOP,
      SCREEN.width,
      stripHeight,
    );
  } else {
    ctx.fillStyle = "#000";
    ctx.fillRect(BOX.left, STRIP_TOP, BOX.width, 2);
  }
  return fit;
}

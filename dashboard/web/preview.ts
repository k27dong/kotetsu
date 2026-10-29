export const FONT_FAMILY = '"LXGW WenKai GB Screen"';
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
const PUNCTUATION = /[\p{P}\p{S}]/u;
const SPACE = /\s/u;
const HANGING = /[。，！？]\s*$/u;
const BREAK_PUNCTUATION = /[。，！？、；：]/u;
const NO_LINE_START = /[。，、：；！？」』）】》〉〕〗’”,.;:!?)\]}%…]/u;
const NO_LINE_END = /[「『（【《〈〔〖‘“(\[{]/u;

export type Fit = { sizePx: number; lines: string[]; fits: boolean };
export type Measurer = { font: string; measureText(text: string): { width: number } };
type Line = { text: string; start: number; paragraphStart: boolean };
type Cut = { after: number; spaces: number };

const gapBetween = (a: string, b: string): boolean =>
  (DIGIT.test(a) && HAN.test(b)) || (HAN.test(a) && DIGIT.test(b));

const breakBefore = (characters: string[], index: number): boolean => {
  const a = characters[index - 1];
  const b = characters[index];
  if (a === undefined || b === undefined) return true;
  if (b === " ") return false;
  if (a === " ") return true;
  if (NO_LINE_START.test(b) || NO_LINE_END.test(a)) return false;
  const bothLatin = WORD.test(a) && WORD.test(b) && !HAN.test(a) && !HAN.test(b);
  return !bothLatin;
};

const runsOf = (line: string): string[] => {
  const runs: string[] = [];
  let run = "";
  let previous: string | null = null;
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
};

const lineWidth = (ctx: Measurer, sizePx: number, line: string): number => {
  const runs = runsOf(line);
  const text = runs.reduce((sum, run) => sum + ctx.measureText(run).width, 0);
  return text + (runs.length - 1) * sizePx * DIGIT_GAP_EM;
};

const lineEndFrom = (
  ctx: Measurer,
  sizePx: number,
  characters: string[],
  start: number,
): number => {
  let end = start;
  let lastBreak = -1;
  while (end < characters.length) {
    const candidate = characters
      .slice(start, end + 1)
      .join("")
      .trimEnd();
    if (lineWidth(ctx, sizePx, candidate) > BOX.width) break;
    end++;
    if (end < characters.length && breakBefore(characters, end)) lastBreak = end;
  }
  if (end === characters.length) return end;
  if (lastBreak > start) return lastBreak;
  let wordEnd = Math.max(end, start + 1);
  while (wordEnd < characters.length && !breakBefore(characters, wordEnd)) wordEnd++;
  return wordEnd; // too wide for the box, which refuses this size
};

const layoutLines = (ctx: Measurer, sizePx: number, text: string): Line[] => {
  ctx.font = `${sizePx}px ${FONT_FAMILY}`;
  const lines: Line[] = [];
  let offset = 0;
  for (const paragraph of text.split("\n")) {
    const characters = [...paragraph];
    let start = 0;
    do {
      const end = lineEndFrom(ctx, sizePx, characters, start);
      lines.push({
        text: characters.slice(start, end).join("").trimEnd(),
        start: offset + start,
        paragraphStart: start === 0,
      });
      start = end;
      while (characters[start] === " ") start++;
    } while (start < characters.length);
    offset += characters.length + 1;
  }
  return lines;
};

const blockFits = (ctx: Measurer, sizePx: number, lines: Line[]): boolean => {
  if (lines.length * sizePx * LINE_HEIGHT > BOX.height) return false;
  return lines.every((line) => lineWidth(ctx, sizePx, line.text) <= BOX.width);
};

const punctuationCut = (ctx: Measurer, sizePx: number, line: string): Cut | null => {
  const characters = [...line];
  const width = lineWidth(ctx, sizePx, line);
  let cut: Cut | null = null;
  for (const [i, character] of characters.entries()) {
    if (!BREAK_PUNCTUATION.test(character)) continue;
    let resume = i + 1;
    while (SPACE.test(characters[resume] ?? "")) resume++;
    const next = characters[resume];
    if (next === undefined) return null;
    if (PUNCTUATION.test(next)) continue; // a closing quote stays with its punctuation
    const trailingX = lineWidth(ctx, sizePx, characters.slice(0, i + 1).join(""));
    if (trailingX * 4 >= width * 3) cut = { after: i + 1, spaces: resume - i - 1 };
  }
  return cut;
};

const preferPunctuationBreaks = (
  ctx: Measurer,
  sizePx: number,
  text: string,
  lines: Line[],
): Line[] => {
  let characters = [...text];
  let current = lines;
  const count = lines.length;
  for (let i = 0; i + 1 < count; i++) {
    const line = current[i];
    const following = current[i + 1];
    if (line === undefined || following === undefined || following.paragraphStart) continue;
    const cut = punctuationCut(ctx, sizePx, line.text);
    if (cut === null) continue;
    const moved = characters.toSpliced(line.start + cut.after, cut.spaces, "\n");
    const next = layoutLines(ctx, sizePx, moved.join(""));
    if (next.length !== count || !blockFits(ctx, sizePx, next)) continue;
    characters = moved;
    current = next;
  }
  return current;
};

export const fitPhrase = (ctx: Measurer, text: string): Fit => {
  let lines: Line[] = [];
  for (let sizePx = SIZE_MAX; sizePx >= SIZE_MIN; sizePx -= SIZE_STEP) {
    lines = layoutLines(ctx, sizePx, text);
    if (blockFits(ctx, sizePx, lines)) {
      const preferred = preferPunctuationBreaks(ctx, sizePx, text, lines);
      return { sizePx, lines: preferred.map((line) => line.text), fits: true };
    }
  }
  return { sizePx: SIZE_MIN, lines: lines.map((line) => line.text), fits: false };
};

const cutAtHalf = (ctx: CanvasRenderingContext2D, width: number, height: number): void => {
  const image = ctx.getImageData(0, 0, width, height);
  const pixels = image.data;
  for (let i = 0; i < pixels.length; i += 4) {
    const ink = (pixels[i] ?? 255) < 128 ? 0 : 255;
    pixels[i] = pixels[i + 1] = pixels[i + 2] = ink;
    pixels[i + 3] = 255;
  }
  ctx.putImageData(image, 0, 0);
};

const drawLines = (ctx: CanvasRenderingContext2D, { sizePx, lines }: Fit): void => {
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
};

export const drawPreview = (
  canvas: HTMLCanvasElement,
  text: string,
  screenPng: CanvasImageSource | null,
): Fit => {
  const ctx = canvas.getContext("2d", { willReadFrequently: true });
  if (ctx === null) throw new Error("The browser gave no 2D context for the preview");
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
  if (screenPng === null) {
    ctx.fillStyle = "#000";
    ctx.fillRect(BOX.left, STRIP_TOP, BOX.width, 2);
  } else {
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
  }
  return fit;
};

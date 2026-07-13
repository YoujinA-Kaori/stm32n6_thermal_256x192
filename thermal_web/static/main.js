const canvas = document.getElementById("thermalCanvas");
const ctx = canvas.getContext("2d");
const offscreen = document.createElement("canvas");
const offctx = offscreen.getContext("2d");

const statusChip = document.getElementById("statusChip");
const serialPortEl = document.getElementById("serialPort");
const baudRateEl = document.getElementById("baudRate");
const frameCounterEl = document.getElementById("frameCounter");
const resolutionEl = document.getElementById("resolution");
const centerValueEl = document.getElementById("centerValue");
const maxValueEl = document.getElementById("maxValue");
const minValueEl = document.getElementById("minValue");
const pauseBtn = document.getElementById("pauseBtn");
const shotBtn = document.getElementById("shotBtn");
const analyzeBtn = document.getElementById("analyzeBtn");
const syncGalleryBtn = document.getElementById("syncGalleryBtn");
const downloadSelectedBtn = document.getElementById("downloadSelectedBtn");
const galleryStatusEl = document.getElementById("galleryStatus");
const galleryListEl = document.getElementById("galleryList");
const galleryDownloadWrapEl = document.getElementById("galleryDownloadWrap");
const galleryDownloadLinkEl = document.getElementById("galleryDownloadLink");

const serialSelect = document.getElementById("serialSelect");
const refreshPortsBtn = document.getElementById("refreshPortsBtn");
const connectBtn = document.getElementById("connectBtn");
const paletteSelect = document.getElementById("paletteSelect");
const autoRangeCheck = document.getElementById("autoRangeCheck");
const lowRangeSlider = document.getElementById("lowRangeSlider");
const highRangeSlider = document.getElementById("highRangeSlider");
const lowRangeValue = document.getElementById("lowRangeValue");
const highRangeValue = document.getElementById("highRangeValue");
const legendStrip = document.getElementById("legendStrip");

const analysisOverlay = document.getElementById("analysisOverlay");
const analysisCanvas = document.getElementById("analysisCanvas");
const analysisCtx = analysisCanvas.getContext("2d");
const analysisResetBtn = document.getElementById("analysisResetBtn");
const analysisRecaptureBtn = document.getElementById("analysisRecaptureBtn");
const analysisCloseBtn = document.getElementById("analysisCloseBtn");
const analysisFrameEl = document.getElementById("analysisFrame");
const analysisResolutionEl = document.getElementById("analysisResolution");
const analysisSampleEl = document.getElementById("analysisSample");
const analysisMinEl = document.getElementById("analysisMin");
const analysisMaxEl = document.getElementById("analysisMax");
const analysisRangeEl = document.getElementById("analysisRange");
const analysisPaletteEl = document.getElementById("analysisPalette");
const analysisCaptureTimeEl = document.getElementById("analysisCaptureTime");
const analysisLegendStrip = document.getElementById("analysisLegendStrip");
const analysisLegendLow = document.getElementById("analysisLegendLow");
const analysisLegendHigh = document.getElementById("analysisLegendHigh");

const AUTO_PORT = "auto";
const STORAGE_KEYS = {
  palette: "thermal_web_palette",
  autoRange: "thermal_web_auto_range",
  lowC: "thermal_web_low_c",
  highC: "thermal_web_high_c",
};
const REQUEST_TIMEOUT_MS = 15000;
const GALLERY_DOWNLOAD_TIMEOUT_MS = 120000;

let paused = false;
let latestFrame = null;
let displayedFrame = null;
let socket = null;
let availablePorts = [];
let selectedGalleryFile = "";

const state = {
  palette: localStorage.getItem(STORAGE_KEYS.palette) || "thermal",
  autoRange: localStorage.getItem(STORAGE_KEYS.autoRange) !== "0",
  lowC: Number(localStorage.getItem(STORAGE_KEYS.lowC) || "20"),
  highC: Number(localStorage.getItem(STORAGE_KEYS.highC) || "50"),
};

const paletteTables = {
  thermal: buildPalette([
    [0.0, [0, 0, 0]],
    [0.16, [0, 24, 112]],
    [0.34, [0, 100, 255]],
    [0.5, [0, 210, 255]],
    [0.66, [80, 255, 120]],
    [0.82, [255, 225, 54]],
    [0.92, [255, 96, 0]],
    [1.0, [255, 255, 255]],
  ]),
  rainbow: buildPalette([
    [0.0, [0, 0, 120]],
    [0.2, [0, 96, 255]],
    [0.4, [0, 224, 255]],
    [0.58, [0, 255, 96]],
    [0.75, [255, 240, 0]],
    [0.88, [255, 128, 0]],
    [1.0, [255, 0, 0]],
  ]),
  iron: buildPalette([
    [0.0, [0, 0, 0]],
    [0.25, [64, 0, 64]],
    [0.5, [160, 32, 0]],
    [0.75, [255, 128, 0]],
    [0.9, [255, 220, 110]],
    [1.0, [255, 255, 255]],
  ]),
  gray: buildPalette([
    [0.0, [0, 0, 0]],
    [1.0, [255, 255, 255]],
  ]),
};

const PALETTE_LABELS = {
  thermal: "热力",
  rainbow: "彩虹",
  iron: "铁红",
  gray: "灰度",
};

const paletteCssTables = Object.fromEntries(
  Object.entries(paletteTables).map(([name, table]) => {
    const colors = [];
    for (let level = 0; level < 256; level++) {
      const index = level * 4;
      colors.push(`rgb(${table[index]}, ${table[index + 1]}, ${table[index + 2]})`);
    }
    return [name, colors];
  }),
);

const analysisView = {
  snapshot: null,
  yaw: -0.68,
  pitch: 0.72,
  zoom: 0.9,
  dragging: false,
  pointerId: null,
  lastX: 0,
  lastY: 0,
  renderPending: false,
  viewportWidth: 1,
  viewportHeight: 1,
  pixelRatio: 1,
};

function buildPalette(stops) {
  const table = new Uint8ClampedArray(256 * 4);
  for (let i = 0; i < 256; i++) {
    const t = i / 255;
    let left = stops[0];
    let right = stops[stops.length - 1];

    for (let index = 0; index < stops.length - 1; index++) {
      if (t >= stops[index][0] && t <= stops[index + 1][0]) {
        left = stops[index];
        right = stops[index + 1];
        break;
      }
    }

    const span = right[0] - left[0] || 1;
    const local = (t - left[0]) / span;
    const rgb = [
      Math.round(left[1][0] + (right[1][0] - left[1][0]) * local),
      Math.round(left[1][1] + (right[1][1] - left[1][1]) * local),
      Math.round(left[1][2] + (right[1][2] - left[1][2]) * local),
    ];

    table[i * 4 + 0] = rgb[0];
    table[i * 4 + 1] = rgb[1];
    table[i * 4 + 2] = rgb[2];
    table[i * 4 + 3] = 255;
  }
  return table;
}

function clamp(value, min, max) {
  return Math.max(min, Math.min(max, value));
}

function base64ToBytes(base64) {
  const binary = atob(base64);
  const bytes = new Uint8Array(binary.length);
  for (let i = 0; i < binary.length; i++) {
    bytes[i] = binary.charCodeAt(i);
  }
  return bytes;
}

function bytesToUint16LittleEndian(bytes) {
  const count = bytes.byteLength / 2;
  const values = new Uint16Array(count);
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  for (let i = 0; i < count; i++) {
    values[i] = view.getUint16(i * 2, true);
  }
  return values;
}

function temp14ToCelsius(temp14) {
  return temp14 / 16.0 - 273.15;
}

function celsiusToTemp14(tempC) {
  return Math.round((tempC + 273.15) * 16.0);
}

function formatTemp(tempC) {
  return `${tempC.toFixed(2)} °C`;
}

function resizeCanvas() {
  const shell = canvas.parentElement;
  const rect = shell.getBoundingClientRect();
  canvas.width = Math.max(1, Math.floor(rect.width));
  canvas.height = Math.max(1, Math.floor(rect.height));
}

function roundRect(context, x, y, width, height, radius) {
  const r = Math.min(radius, width / 2, height / 2);
  context.beginPath();
  context.moveTo(x + r, y);
  context.arcTo(x + width, y, x + width, y + height, r);
  context.arcTo(x + width, y + height, x, y + height, r);
  context.arcTo(x, y + height, x, y, r);
  context.arcTo(x, y, x + width, y, r);
  context.closePath();
}

function drawRoundedLabel(x, y, text, fillStyle, strokeStyle) {
  const padX = 8;
  ctx.save();
  ctx.font = "600 14px Microsoft YaHei, Segoe UI, sans-serif";
  const metrics = ctx.measureText(text);
  const boxWidth = metrics.width + padX * 2;
  const boxHeight = 24;
  const left = clamp(x, 0, Math.max(0, canvas.width - boxWidth));
  const top = clamp(y, 0, Math.max(0, canvas.height - boxHeight));

  ctx.fillStyle = fillStyle;
  ctx.strokeStyle = strokeStyle;
  ctx.lineWidth = 1;
  roundRect(ctx, left, top, boxWidth, boxHeight, 8);
  ctx.fill();
  ctx.stroke();
  ctx.fillStyle = "#ffffff";
  ctx.fillText(text, left + padX, top + 16);
  ctx.restore();
}

function drawCross(x, y) {
  ctx.save();
  ctx.strokeStyle = "rgba(255,255,255,0.9)";
  ctx.lineWidth = 1.5;
  ctx.beginPath();
  ctx.moveTo(x - 10, y);
  ctx.lineTo(x + 10, y);
  ctx.moveTo(x, y - 10);
  ctx.lineTo(x, y + 10);
  ctx.stroke();
  ctx.restore();
}

function drawMarker(x, y, label, color) {
  ctx.save();
  ctx.fillStyle = color;
  ctx.strokeStyle = "#111";
  ctx.lineWidth = 1.5;
  ctx.beginPath();
  ctx.arc(x, y, 4.5, 0, Math.PI * 2);
  ctx.fill();
  ctx.stroke();
  drawRoundedLabel(x + 8, y - 12, label, "rgba(0,0,0,0.72)", color);
  ctx.restore();
}

function getActiveRangeCelsius(frame) {
  if (state.autoRange) {
    const lowC = Math.floor(frame.min_temp_c);
    const highC = Math.max(lowC + 1, Math.ceil(frame.max_temp_c));
    return { lowC, highC };
  }

  let lowC = Number(lowRangeSlider.value);
  let highC = Number(highRangeSlider.value);
  if (highC <= lowC) {
    highC = lowC + 1;
  }
  return { lowC, highC };
}

function paletteGradient(paletteName) {
  const table = paletteTables[paletteName] || paletteTables.thermal;
  const stops = [];
  for (let level = 0; level <= 255; level += 32) {
    const index = level * 4;
    stops.push(`rgb(${table[index]}, ${table[index + 1]}, ${table[index + 2]}) ${(level / 255) * 100}%`);
  }
  const lastIndex = 255 * 4;
  stops.push(`rgb(${table[lastIndex]}, ${table[lastIndex + 1]}, ${table[lastIndex + 2]}) 100%`);
  return `linear-gradient(90deg, ${stops.join(", ")})`;
}

function updatePaletteLegends() {
  const gradient = paletteGradient(state.palette);
  legendStrip.style.background = gradient;
  analysisLegendStrip.style.background = gradient;
}

function buildSampleAxis(length, step) {
  const axis = [];
  for (let value = 0; value < length - 1; value += step) {
    axis.push(value);
  }
  if (axis.length === 0 || axis[axis.length - 1] !== length - 1) {
    axis.push(length - 1);
  }
  return axis;
}

function buildAnalysisMesh(frame, values) {
  const width = frame.frame_width;
  const height = frame.frame_height;
  const sampleStep = Math.max(1, Math.ceil(Math.max(width / 64, height / 48)));
  const xSamples = buildSampleAxis(width, sampleStep);
  const ySamples = buildSampleAxis(height, sampleStep);
  const xSpan = 2.2;
  const depthSpan = xSpan * (height / width);
  const baseHeight = -0.52;
  const topHeight = 0.92;
  const tempSpan = Math.max(1, frame.max_temp14 - frame.min_temp14);
  const vertices = [];

  for (const sourceY of ySamples) {
    for (const sourceX of xSamples) {
      const temp14 = values[sourceY * width + sourceX];
      const normalizedTemp = clamp((temp14 - frame.min_temp14) / tempSpan, 0, 1);
      vertices.push({
        x: ((sourceX / Math.max(1, width - 1)) - 0.5) * xSpan,
        depth: ((sourceY / Math.max(1, height - 1)) - 0.5) * depthSpan,
        height: baseHeight + normalizedTemp * (topHeight - baseHeight),
        temp14,
      });
    }
  }

  return {
    vertices,
    columns: xSamples.length,
    rows: ySamples.length,
    sampleStep,
    xSpan,
    depthSpan,
    baseHeight,
    topHeight,
  };
}

function resetAnalysisView() {
  analysisView.yaw = -0.68;
  analysisView.pitch = 0.72;
  analysisView.zoom = 0.9;
  scheduleAnalysisRender();
}

function resizeAnalysisCanvas() {
  const shell = analysisCanvas.parentElement;
  const rect = shell.getBoundingClientRect();
  const width = Math.max(1, Math.floor(rect.width));
  const height = Math.max(1, Math.floor(rect.height));
  const pixelRatio = Math.min(window.devicePixelRatio || 1, 2);
  const pixelWidth = Math.max(1, Math.floor(width * pixelRatio));
  const pixelHeight = Math.max(1, Math.floor(height * pixelRatio));

  analysisView.viewportWidth = width;
  analysisView.viewportHeight = height;
  analysisView.pixelRatio = pixelRatio;
  if (analysisCanvas.width !== pixelWidth || analysisCanvas.height !== pixelHeight) {
    analysisCanvas.width = pixelWidth;
    analysisCanvas.height = pixelHeight;
  }
  scheduleAnalysisRender();
}

function projectAnalysisPoint(point) {
  const cosYaw = Math.cos(analysisView.yaw);
  const sinYaw = Math.sin(analysisView.yaw);
  const cosPitch = Math.cos(analysisView.pitch);
  const sinPitch = Math.sin(analysisView.pitch);
  const yawX = point.x * cosYaw - point.depth * sinYaw;
  const yawDepth = point.x * sinYaw + point.depth * cosYaw;
  const pitchHeight = point.height * cosPitch - yawDepth * sinPitch;
  const pitchDepth = point.height * sinPitch + yawDepth * cosPitch;
  const cameraDistance = 4.2;
  const depth = Math.max(0.25, cameraDistance - pitchDepth);
  const focalLength = Math.min(analysisView.viewportWidth, analysisView.viewportHeight) * 1.5;
  const scale = (focalLength / depth) * analysisView.zoom;

  return {
    x: analysisView.viewportWidth * 0.5 + yawX * scale,
    y: analysisView.viewportHeight * 0.57 - pitchHeight * scale,
    depth,
  };
}

function drawWorldLine(points, strokeStyle, lineWidth = 1) {
  if (points.length < 2) {
    return;
  }
  analysisCtx.beginPath();
  const first = projectAnalysisPoint(points[0]);
  analysisCtx.moveTo(first.x, first.y);
  for (let index = 1; index < points.length; index++) {
    const point = projectAnalysisPoint(points[index]);
    analysisCtx.lineTo(point.x, point.y);
  }
  analysisCtx.strokeStyle = strokeStyle;
  analysisCtx.lineWidth = lineWidth;
  analysisCtx.stroke();
}

function drawAnalysisFloor(mesh) {
  const xMin = -mesh.xSpan / 2;
  const xMax = mesh.xSpan / 2;
  const depthMin = -mesh.depthSpan / 2;
  const depthMax = mesh.depthSpan / 2;
  const gridColor = "rgba(137, 190, 220, 0.12)";

  for (let index = 0; index <= 8; index++) {
    const t = index / 8;
    const x = xMin + (xMax - xMin) * t;
    const depth = depthMin + (depthMax - depthMin) * t;
    drawWorldLine([
      { x, depth: depthMin, height: mesh.baseHeight },
      { x, depth: depthMax, height: mesh.baseHeight },
    ], gridColor);
    drawWorldLine([
      { x: xMin, depth, height: mesh.baseHeight },
      { x: xMax, depth, height: mesh.baseHeight },
    ], gridColor);
  }
}

function drawAnalysisMeshLines(mesh, projected) {
  const rowStride = Math.max(1, Math.round(mesh.rows / 12));
  const columnStride = Math.max(1, Math.round(mesh.columns / 16));
  analysisCtx.strokeStyle = "rgba(255, 255, 255, 0.10)";
  analysisCtx.lineWidth = 0.65;

  for (let row = 0; row < mesh.rows; row += rowStride) {
    analysisCtx.beginPath();
    for (let column = 0; column < mesh.columns; column++) {
      const point = projected[row * mesh.columns + column];
      if (column === 0) {
        analysisCtx.moveTo(point.x, point.y);
      } else {
        analysisCtx.lineTo(point.x, point.y);
      }
    }
    analysisCtx.stroke();
  }

  for (let column = 0; column < mesh.columns; column += columnStride) {
    analysisCtx.beginPath();
    for (let row = 0; row < mesh.rows; row++) {
      const point = projected[row * mesh.columns + column];
      if (row === 0) {
        analysisCtx.moveTo(point.x, point.y);
      } else {
        analysisCtx.lineTo(point.x, point.y);
      }
    }
    analysisCtx.stroke();
  }
}

function drawAnalysisAxes(snapshot) {
  const mesh = snapshot.mesh;
  const origin = {
    x: -mesh.xSpan / 2,
    depth: mesh.depthSpan / 2,
    height: mesh.baseHeight,
  };
  const xEnd = { ...origin, x: mesh.xSpan / 2 };
  const yEnd = { ...origin, depth: -mesh.depthSpan / 2 };
  const zEnd = { ...origin, height: mesh.topHeight };

  drawWorldLine([origin, xEnd], "rgba(102, 217, 255, 0.9)", 1.7);
  drawWorldLine([origin, yEnd], "rgba(112, 255, 170, 0.8)", 1.7);
  drawWorldLine([origin, zEnd], "rgba(255, 190, 86, 0.95)", 2);

  const projectedOrigin = projectAnalysisPoint(origin);
  const projectedX = projectAnalysisPoint(xEnd);
  const projectedY = projectAnalysisPoint(yEnd);
  const projectedZ = projectAnalysisPoint(zEnd);
  analysisCtx.save();
  analysisCtx.font = "600 12px Microsoft YaHei, Segoe UI, sans-serif";
  analysisCtx.shadowColor = "rgba(0, 0, 0, 0.9)";
  analysisCtx.shadowBlur = 4;
  analysisCtx.fillStyle = "#8fe7ff";
  analysisCtx.fillText("X / 像素", projectedX.x + 6, projectedX.y + 4);
  analysisCtx.fillStyle = "#8fffc0";
  analysisCtx.fillText("Y / 像素", projectedY.x + 6, projectedY.y + 4);
  analysisCtx.fillStyle = "#ffd18b";
  analysisCtx.fillText(`Z / 温度 ${formatTemp(snapshot.frame.max_temp_c)}`, projectedZ.x + 7, projectedZ.y - 5);
  analysisCtx.fillText(formatTemp(snapshot.frame.min_temp_c), projectedOrigin.x + 7, projectedOrigin.y + 14);
  analysisCtx.restore();
}

function renderAnalysisSurface() {
  analysisView.renderPending = false;
  const snapshot = analysisView.snapshot;
  const width = analysisView.viewportWidth;
  const height = analysisView.viewportHeight;
  const pixelRatio = analysisView.pixelRatio;
  analysisCtx.setTransform(pixelRatio, 0, 0, pixelRatio, 0, 0);
  analysisCtx.clearRect(0, 0, width, height);

  if (!snapshot || width <= 1 || height <= 1) {
    return;
  }

  const mesh = snapshot.mesh;
  drawAnalysisFloor(mesh);
  const projected = mesh.vertices.map(projectAnalysisPoint);
  const lowTemp14 = celsiusToTemp14(snapshot.lowC);
  const highTemp14 = celsiusToTemp14(snapshot.highC);
  const rangeTemp14 = Math.max(1, highTemp14 - lowTemp14);
  const colors = paletteCssTables[snapshot.palette] || paletteCssTables.thermal;
  const triangles = [];

  const addTriangle = (a, b, c) => {
    const averageTemp14 = (
      mesh.vertices[a].temp14 + mesh.vertices[b].temp14 + mesh.vertices[c].temp14
    ) / 3;
    const level = clamp(Math.round(((averageTemp14 - lowTemp14) * 255) / rangeTemp14), 0, 255);
    triangles.push({
      a,
      b,
      c,
      depth: (projected[a].depth + projected[b].depth + projected[c].depth) / 3,
      level,
    });
  };

  for (let row = 0; row < mesh.rows - 1; row++) {
    for (let column = 0; column < mesh.columns - 1; column++) {
      const topLeft = row * mesh.columns + column;
      const topRight = topLeft + 1;
      const bottomLeft = topLeft + mesh.columns;
      const bottomRight = bottomLeft + 1;
      addTriangle(topLeft, bottomLeft, topRight);
      addTriangle(topRight, bottomLeft, bottomRight);
    }
  }

  triangles.sort((left, right) => right.depth - left.depth);
  analysisCtx.lineJoin = "round";
  for (const triangle of triangles) {
    const a = projected[triangle.a];
    const b = projected[triangle.b];
    const c = projected[triangle.c];
    const color = colors[triangle.level];
    analysisCtx.beginPath();
    analysisCtx.moveTo(a.x, a.y);
    analysisCtx.lineTo(b.x, b.y);
    analysisCtx.lineTo(c.x, c.y);
    analysisCtx.closePath();
    analysisCtx.fillStyle = color;
    analysisCtx.strokeStyle = color;
    analysisCtx.lineWidth = 0.7;
    analysisCtx.fill();
    analysisCtx.stroke();
  }

  drawAnalysisMeshLines(mesh, projected);
  drawAnalysisAxes(snapshot);
}

function scheduleAnalysisRender() {
  if (analysisView.renderPending) {
    return;
  }
  analysisView.renderPending = true;
  window.requestAnimationFrame(renderAnalysisSurface);
}

function updateAnalysisMeta() {
  const snapshot = analysisView.snapshot;
  if (!snapshot) {
    return;
  }
  const frame = snapshot.frame;
  analysisFrameEl.textContent = String(frame.frame_counter);
  analysisResolutionEl.textContent = `${frame.frame_width} x ${frame.frame_height}`;
  analysisSampleEl.textContent = `${snapshot.mesh.columns} x ${snapshot.mesh.rows} / 步进 ${snapshot.mesh.sampleStep}`;
  analysisMinEl.textContent = formatTemp(frame.min_temp_c);
  analysisMaxEl.textContent = formatTemp(frame.max_temp_c);
  analysisRangeEl.textContent = `${snapshot.lowC.toFixed(0)} ~ ${snapshot.highC.toFixed(0)} °C`;
  analysisPaletteEl.textContent = PALETTE_LABELS[snapshot.palette] || snapshot.palette;
  analysisCaptureTimeEl.textContent = snapshot.capturedAt.toLocaleTimeString("zh-CN", { hour12: false });
  analysisLegendLow.textContent = `${snapshot.lowC.toFixed(0)} °C`;
  analysisLegendHigh.textContent = `${snapshot.highC.toFixed(0)} °C`;
}

function syncAnalysisMapping() {
  const snapshot = analysisView.snapshot;
  if (!snapshot) {
    return;
  }
  const { lowC, highC } = getActiveRangeCelsius(snapshot.frame);
  snapshot.lowC = lowC;
  snapshot.highC = highC;
  snapshot.palette = state.palette;
  updatePaletteLegends();
  updateAnalysisMeta();
  scheduleAnalysisRender();
}

function captureAnalysisFrame() {
  const frame = displayedFrame || latestFrame;
  if (!frame) {
    return;
  }
  const values = bytesToUint16LittleEndian(base64ToBytes(frame.payload_b64));
  const expectedValues = frame.frame_width * frame.frame_height;
  if (values.length < expectedValues) {
    console.error(`温度帧长度不足：期望 ${expectedValues}，实际 ${values.length}`);
    return;
  }
  const frozenValues = values.slice(0, expectedValues);
  const frozenFrame = { ...frame };
  const { lowC, highC } = getActiveRangeCelsius(frozenFrame);
  analysisView.snapshot = {
    frame: frozenFrame,
    values: frozenValues,
    lowC,
    highC,
    palette: state.palette,
    capturedAt: new Date(),
    mesh: buildAnalysisMesh(frozenFrame, frozenValues),
  };
  analysisOverlay.classList.remove("hidden");
  document.body.classList.add("modal-open");
  updatePaletteLegends();
  updateAnalysisMeta();
  window.requestAnimationFrame(() => {
    resizeAnalysisCanvas();
    analysisCloseBtn.focus();
  });
}

function closeAnalysis() {
  analysisOverlay.classList.add("hidden");
  document.body.classList.remove("modal-open");
  analyzeBtn.focus();
}

function drawFrame(frame) {
  latestFrame = frame;
  displayedFrame = frame;
  const width = frame.frame_width;
  const height = frame.frame_height;
  const bytes = base64ToBytes(frame.payload_b64);
  const values = bytesToUint16LittleEndian(bytes);
  const palette = paletteTables[state.palette] || paletteTables.thermal;
  const { lowC, highC } = getActiveRangeCelsius(frame);
  const lowTemp14 = celsiusToTemp14(lowC);
  const highTemp14 = celsiusToTemp14(highC);
  const rangeTemp14 = Math.max(1, highTemp14 - lowTemp14);

  offscreen.width = width;
  offscreen.height = height;
  const imageData = offctx.createImageData(width, height);
  const data = imageData.data;

  for (let i = 0; i < values.length; i++) {
    let level = Math.round(((values[i] - lowTemp14) * 255) / rangeTemp14);
    level = clamp(level, 0, 255);
    const paletteIndex = level * 4;
    const dataIndex = i * 4;
    data[dataIndex + 0] = palette[paletteIndex + 0];
    data[dataIndex + 1] = palette[paletteIndex + 1];
    data[dataIndex + 2] = palette[paletteIndex + 2];
    data[dataIndex + 3] = 255;
  }

  offctx.putImageData(imageData, 0, 0);

  const sourceAspect = width / height;
  const canvasAspect = canvas.width / canvas.height;
  let renderWidth = canvas.width;
  let renderHeight = canvas.height;
  if (canvasAspect > sourceAspect) {
    renderWidth = canvas.height * sourceAspect;
  } else {
    renderHeight = canvas.width / sourceAspect;
  }
  const renderX = (canvas.width - renderWidth) * 0.5;
  const renderY = (canvas.height - renderHeight) * 0.5;

  ctx.fillStyle = "#000000";
  ctx.fillRect(0, 0, canvas.width, canvas.height);
  ctx.imageSmoothingEnabled = true;
  ctx.drawImage(offscreen, 0, 0, width, height, renderX, renderY, renderWidth, renderHeight);

  const scaleX = renderWidth / width;
  const scaleY = renderHeight / height;
  const centerX = renderX + (width / 2) * scaleX;
  const centerY = renderY + (height / 2) * scaleY;

  drawCross(centerX, centerY);
  drawMarker(renderX + (frame.max_x + 0.5) * scaleX, renderY + (frame.max_y + 0.5) * scaleY, "最高", "#ff7a7a");
  drawMarker(renderX + (frame.min_x + 0.5) * scaleX, renderY + (frame.min_y + 0.5) * scaleY, "最低", "#69dcff");

  frameCounterEl.textContent = String(frame.frame_counter);
  resolutionEl.textContent = `${width} x ${height}`;
  centerValueEl.textContent = `中心 ${formatTemp(frame.center_temp_c)}`;
  maxValueEl.textContent = `${formatTemp(frame.max_temp_c)} @ (${frame.max_x}, ${frame.max_y})`;
  minValueEl.textContent = `${formatTemp(frame.min_temp_c)} @ (${frame.min_x}, ${frame.min_y})`;

  if (state.autoRange) {
    const autoLowC = Math.floor(frame.min_temp_c);
    const autoHighC = Math.max(autoLowC + 1, Math.ceil(frame.max_temp_c));
    lowRangeSlider.value = String(clamp(autoLowC, -20, 600));
    highRangeSlider.value = String(clamp(autoHighC, -20, 600));
    lowRangeValue.textContent = `${autoLowC} °C`;
    highRangeValue.textContent = `${autoHighC} °C`;
  } else {
    lowRangeValue.textContent = `${lowRangeSlider.value} °C`;
    highRangeValue.textContent = `${highRangeSlider.value} °C`;
  }
}

function syncRangeControlsFromState() {
  paletteSelect.value = state.palette;
  autoRangeCheck.checked = state.autoRange;
  lowRangeSlider.disabled = state.autoRange;
  highRangeSlider.disabled = state.autoRange;
  lowRangeValue.textContent = `${Number(lowRangeSlider.value)} °C`;
  highRangeValue.textContent = `${Number(highRangeSlider.value)} °C`;
}

function persistDisplayState() {
  localStorage.setItem(STORAGE_KEYS.palette, state.palette);
  localStorage.setItem(STORAGE_KEYS.autoRange, state.autoRange ? "1" : "0");
  localStorage.setItem(STORAGE_KEYS.lowC, String(lowRangeSlider.value));
  localStorage.setItem(STORAGE_KEYS.highC, String(highRangeSlider.value));
}

function setStatus(status) {
  if (!status) {
    return;
  }
  statusChip.textContent = status.status || "待连接";
  serialPortEl.textContent = status.serial_port || "-";
  baudRateEl.textContent = status.baud_rate || "-";
}

function setGalleryStatus(text, isError = false) {
  galleryStatusEl.textContent = text;
  galleryStatusEl.classList.toggle("error", isError);
}

function setSelectedGalleryFile(fileName) {
  selectedGalleryFile = fileName || "";
  downloadSelectedBtn.disabled = selectedGalleryFile.length === 0;

  const listItems = galleryListEl.querySelectorAll("li[data-file-name]");
  for (const item of listItems) {
    item.classList.toggle("selected", item.dataset.fileName === selectedGalleryFile);
  }
}

function renderGalleryList(files) {
  galleryListEl.innerHTML = "";
  if (!files || files.length === 0) {
    setSelectedGalleryFile("");
    const item = document.createElement("li");
    item.textContent = "SD 中暂无截图";
    galleryListEl.appendChild(item);
    return;
  }

  for (const fileName of files) {
    const item = document.createElement("li");
    item.textContent = fileName;
    item.dataset.fileName = fileName;
    item.tabIndex = 0;
    item.addEventListener("click", () => {
      setSelectedGalleryFile(fileName);
      setGalleryStatus(`已选择 ${fileName}`);
    });
    item.addEventListener("keydown", (event) => {
      if (event.key === "Enter" || event.key === " ") {
        event.preventDefault();
        setSelectedGalleryFile(fileName);
        setGalleryStatus(`已选择 ${fileName}`);
      }
    });
    galleryListEl.appendChild(item);
  }

  setSelectedGalleryFile(files[files.length - 1] || "");
  if (selectedGalleryFile) {
    setGalleryStatus(`已选择 ${selectedGalleryFile}`);
  }
}

function setDownloadLink(downloadUrl, label) {
  if (!downloadUrl) {
    galleryDownloadWrapEl.classList.add("hidden");
    galleryDownloadLinkEl.href = "#";
    galleryDownloadLinkEl.textContent = "";
    return;
  }

  galleryDownloadLinkEl.href = downloadUrl;
  galleryDownloadLinkEl.textContent = label || "打开下载文件";
  galleryDownloadWrapEl.classList.remove("hidden");
}

function updatePortSelect(currentPort) {
  const selected = currentPort || AUTO_PORT;
  serialSelect.innerHTML = "";

  const autoOption = document.createElement("option");
  autoOption.value = AUTO_PORT;
  autoOption.textContent = "自动（首个可用）";
  serialSelect.appendChild(autoOption);

  for (const port of availablePorts) {
    const option = document.createElement("option");
    option.value = port.device;
    option.textContent = `${port.device} - ${port.description}`;
    serialSelect.appendChild(option);
  }

  const hasSelected = Array.from(serialSelect.options).some((option) => option.value === selected);
  serialSelect.value = hasSelected ? selected : AUTO_PORT;
}

async function loadPorts() {
  try {
    const response = await fetch("/api/ports");
    const data = await response.json();
    availablePorts = data.ports || [];
    updatePortSelect(data.current_port || AUTO_PORT);
  } catch (error) {
    console.error(error);
    availablePorts = [];
    updatePortSelect(AUTO_PORT);
  }
}

async function fetchJsonWithTimeout(url, options = {}, timeoutMs = REQUEST_TIMEOUT_MS) {
  const controller = new AbortController();
  const timerId = window.setTimeout(() => controller.abort(), timeoutMs);
  try {
    const response = await fetch(url, { ...options, signal: controller.signal });
    const data = await response.json();
    return { response, data };
  } finally {
    window.clearTimeout(timerId);
  }
}

async function connectSerial() {
  const serialPort = serialSelect.value || AUTO_PORT;
  connectBtn.disabled = true;
  connectBtn.textContent = "连接中...";
  try {
    const { response, data } = await fetchJsonWithTimeout("/api/connect", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ serial_port: serialPort }),
    });
    setStatus(data.status || null);
    availablePorts = data.ports || availablePorts;
    updatePortSelect((data.status && data.status.serial_port) || serialPort);
  } catch (error) {
    console.error(error);
    statusChip.textContent = "连接失败";
  } finally {
    connectBtn.disabled = false;
    connectBtn.textContent = "连接";
  }
}

async function syncGallery() {
  syncGalleryBtn.disabled = true;
  downloadSelectedBtn.disabled = true;
  setGalleryStatus("正在同步 SD 图库...");
  try {
    const { response, data } = await fetchJsonWithTimeout("/api/gallery/list", { method: "POST" });
    if (!response.ok) {
      throw new Error(data.error || "同步失败");
    }
    renderGalleryList(data.files || []);
    setGalleryStatus(`已同步 ${data.count || 0} 张截图`);
  } catch (error) {
    console.error(error);
    const message = error.name === "AbortError" ? "请求超时，请重试" : error.message;
    setGalleryStatus(`同步失败：${message}`, true);
  } finally {
    syncGalleryBtn.disabled = false;
    downloadSelectedBtn.disabled = selectedGalleryFile.length === 0;
  }
}

async function downloadSelectedSnapshot() {
  if (!selectedGalleryFile) {
    setGalleryStatus("请先从列表里选择一张截图", true);
    return;
  }

  const requestedFile = selectedGalleryFile;
  const startedAt = Date.now();
  let progressTimer = null;
  syncGalleryBtn.disabled = true;
  downloadSelectedBtn.disabled = true;
  downloadSelectedBtn.textContent = "下载中...";
  setGalleryStatus(`正在下载 ${requestedFile}，大尺寸截图需要更多时间...`);
  setDownloadLink("", "");
  progressTimer = window.setInterval(() => {
    const elapsedSeconds = Math.floor((Date.now() - startedAt) / 1000);
    setGalleryStatus(`正在下载 ${requestedFile}，已等待 ${elapsedSeconds} 秒...`);
  }, 1000);
  try {
    const { response, data } = await fetchJsonWithTimeout("/api/gallery/download", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ file_name: requestedFile }),
    }, GALLERY_DOWNLOAD_TIMEOUT_MS);
    window.clearInterval(progressTimer);
    progressTimer = null;
    if (!response.ok) {
      throw new Error(data.error || "下载失败");
    }

    const elapsedText = Number.isFinite(Number(data.elapsed_seconds))
      ? `，耗时 ${Number(data.elapsed_seconds).toFixed(1)} 秒`
      : "";
    setGalleryStatus(`下载完成：${data.saved_name}${elapsedText}`);
    setDownloadLink(data.download_url, `打开 ${data.saved_name}`);
  } catch (error) {
    console.error(error);
    const message = error.name === "AbortError"
      ? "下载超过 120 秒，请检查串口连接后重试"
      : error.message;
    setGalleryStatus(`下载失败：${message}`, true);
  } finally {
    if (progressTimer !== null) {
      window.clearInterval(progressTimer);
    }
    syncGalleryBtn.disabled = false;
    downloadSelectedBtn.disabled = selectedGalleryFile.length === 0;
    downloadSelectedBtn.textContent = "下载选中";
  }
}

function renderFrame(frame) {
  latestFrame = frame;
  analyzeBtn.disabled = false;
  if (paused) {
    return;
  }
  drawFrame(frame);
}

function rerenderLatestFrame() {
  const frame = paused ? displayedFrame : latestFrame;
  if (frame) {
    drawFrame(frame);
  }
}

function takeSnapshot() {
  const frame = displayedFrame || latestFrame;
  if (!frame) {
    return;
  }
  const link = document.createElement("a");
  link.download = `thermal_${frame.frame_counter.toString().padStart(8, "0")}.png`;
  link.href = canvas.toDataURL("image/png");
  link.click();
}

function connectWebSocket() {
  const scheme = location.protocol === "https:" ? "wss:" : "ws:";
  socket = new WebSocket(`${scheme}//${location.host}/ws`);

  socket.onopen = () => {
    statusChip.textContent = "已连接";
  };

  socket.onclose = () => {
    statusChip.textContent = "重连中...";
    setTimeout(connectWebSocket, 1000);
  };

  socket.onerror = () => {
    statusChip.textContent = "连接异常";
  };

  socket.onmessage = (event) => {
    const message = JSON.parse(event.data);
    if (message.type === "status") {
      setStatus(message.data);
      if (message.data && message.data.serial_port) {
        updatePortSelect(message.data.serial_port);
      }
      return;
    }

    if (message.type === "frame") {
      renderFrame(message.data);
    }
  };
}

function bindUi() {
  pauseBtn.addEventListener("click", () => {
    paused = !paused;
    pauseBtn.textContent = paused ? "继续" : "暂停";
    rerenderLatestFrame();
  });

  shotBtn.addEventListener("click", takeSnapshot);
  analyzeBtn.addEventListener("click", captureAnalysisFrame);
  analysisResetBtn.addEventListener("click", resetAnalysisView);
  analysisRecaptureBtn.addEventListener("click", captureAnalysisFrame);
  analysisCloseBtn.addEventListener("click", closeAnalysis);
  analysisOverlay.addEventListener("click", (event) => {
    if (event.target === analysisOverlay) {
      closeAnalysis();
    }
  });

  analysisCanvas.addEventListener("pointerdown", (event) => {
    if (event.button !== 0) {
      return;
    }
    analysisView.dragging = true;
    analysisView.pointerId = event.pointerId;
    analysisView.lastX = event.clientX;
    analysisView.lastY = event.clientY;
    analysisCanvas.setPointerCapture(event.pointerId);
    analysisCanvas.classList.add("dragging");
  });

  analysisCanvas.addEventListener("pointermove", (event) => {
    if (!analysisView.dragging || event.pointerId !== analysisView.pointerId) {
      return;
    }
    const deltaX = event.clientX - analysisView.lastX;
    const deltaY = event.clientY - analysisView.lastY;
    analysisView.lastX = event.clientX;
    analysisView.lastY = event.clientY;
    analysisView.yaw -= deltaX * 0.009;
    analysisView.pitch = clamp(analysisView.pitch + deltaY * 0.009, -0.05, 1.45);
    scheduleAnalysisRender();
  });

  const finishAnalysisDrag = (event) => {
    if (event.pointerId !== analysisView.pointerId) {
      return;
    }
    analysisView.dragging = false;
    analysisView.pointerId = null;
    analysisCanvas.classList.remove("dragging");
  };
  analysisCanvas.addEventListener("pointerup", finishAnalysisDrag);
  analysisCanvas.addEventListener("pointercancel", finishAnalysisDrag);
  analysisCanvas.addEventListener("wheel", (event) => {
    event.preventDefault();
    analysisView.zoom = clamp(analysisView.zoom * Math.exp(-event.deltaY * 0.001), 0.5, 2.2);
    scheduleAnalysisRender();
  }, { passive: false });
  analysisCanvas.addEventListener("dblclick", resetAnalysisView);
  document.addEventListener("keydown", (event) => {
    if (event.key === "Escape" && !analysisOverlay.classList.contains("hidden")) {
      closeAnalysis();
    }
  });

  refreshPortsBtn.addEventListener("click", loadPorts);
  connectBtn.addEventListener("click", connectSerial);
  syncGalleryBtn.addEventListener("click", syncGallery);
  downloadSelectedBtn.addEventListener("click", downloadSelectedSnapshot);

  paletteSelect.addEventListener("change", () => {
    state.palette = paletteSelect.value;
    persistDisplayState();
    rerenderLatestFrame();
    syncAnalysisMapping();
  });

  autoRangeCheck.addEventListener("change", () => {
    state.autoRange = autoRangeCheck.checked;
    lowRangeSlider.disabled = state.autoRange;
    highRangeSlider.disabled = state.autoRange;
    persistDisplayState();
    rerenderLatestFrame();
    syncAnalysisMapping();
  });

  lowRangeSlider.addEventListener("input", () => {
    if (Number(highRangeSlider.value) <= Number(lowRangeSlider.value)) {
      highRangeSlider.value = String(Number(lowRangeSlider.value) + 1);
    }
    state.lowC = Number(lowRangeSlider.value);
    state.highC = Number(highRangeSlider.value);
    lowRangeValue.textContent = `${lowRangeSlider.value} °C`;
    highRangeValue.textContent = `${highRangeSlider.value} °C`;
    persistDisplayState();
    rerenderLatestFrame();
    syncAnalysisMapping();
  });

  highRangeSlider.addEventListener("input", () => {
    if (Number(highRangeSlider.value) <= Number(lowRangeSlider.value)) {
      lowRangeSlider.value = String(Number(highRangeSlider.value) - 1);
    }
    state.lowC = Number(lowRangeSlider.value);
    state.highC = Number(highRangeSlider.value);
    lowRangeValue.textContent = `${lowRangeSlider.value} °C`;
    highRangeValue.textContent = `${highRangeSlider.value} °C`;
    persistDisplayState();
    rerenderLatestFrame();
    syncAnalysisMapping();
  });
}

window.addEventListener("resize", () => {
  resizeCanvas();
  rerenderLatestFrame();
  if (!analysisOverlay.classList.contains("hidden")) {
    resizeAnalysisCanvas();
  }
});

function init() {
  paletteSelect.value = state.palette;
  autoRangeCheck.checked = state.autoRange;
  lowRangeSlider.value = String(clamp(Math.round(state.lowC), -20, 600));
  highRangeSlider.value = String(clamp(Math.round(state.highC), -20, 600));
  if (Number(highRangeSlider.value) <= Number(lowRangeSlider.value)) {
    highRangeSlider.value = String(Number(lowRangeSlider.value) + 1);
  }
  syncRangeControlsFromState();
  persistDisplayState();
  updatePaletteLegends();
  bindUi();
  resizeCanvas();
  loadPorts();
  connectWebSocket();
  downloadSelectedBtn.disabled = true;
  renderGalleryList([]);
}

init();

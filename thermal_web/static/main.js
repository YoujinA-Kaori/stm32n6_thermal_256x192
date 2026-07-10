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

const AUTO_PORT = "auto";
const STORAGE_KEYS = {
  palette: "thermal_web_palette",
  autoRange: "thermal_web_auto_range",
  lowC: "thermal_web_low_c",
  highC: "thermal_web_high_c",
};
const REQUEST_TIMEOUT_MS = 15000;

let paused = false;
let latestFrame = null;
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
    return {
      lowC: Math.floor(frame.min_temp_c),
      highC: Math.ceil(frame.max_temp_c),
    };
  }

  let lowC = Number(lowRangeSlider.value);
  let highC = Number(highRangeSlider.value);
  if (highC <= lowC) {
    highC = lowC + 1;
  }
  return { lowC, highC };
}

function drawFrame(frame) {
  latestFrame = frame;
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

  ctx.clearRect(0, 0, canvas.width, canvas.height);
  ctx.imageSmoothingEnabled = true;
  ctx.drawImage(offscreen, 0, 0, width, height, 0, 0, canvas.width, canvas.height);

  const scaleX = canvas.width / width;
  const scaleY = canvas.height / height;
  const centerX = (width / 2) * scaleX;
  const centerY = (height / 2) * scaleY;

  drawCross(centerX, centerY);
  drawMarker(frame.max_x * scaleX, frame.max_y * scaleY, "最高", "#ff7a7a");
  drawMarker(frame.min_x * scaleX, frame.min_y * scaleY, "最低", "#69dcff");

  frameCounterEl.textContent = String(frame.frame_counter);
  resolutionEl.textContent = `${width} x ${height}`;
  centerValueEl.textContent = `中心 ${formatTemp(frame.center_temp_c)}`;
  maxValueEl.textContent = `${formatTemp(frame.max_temp_c)} @ (${frame.max_x}, ${frame.max_y})`;
  minValueEl.textContent = `${formatTemp(frame.min_temp_c)} @ (${frame.min_x}, ${frame.min_y})`;

  if (state.autoRange) {
    lowRangeSlider.value = String(Math.floor(frame.min_temp_c));
    highRangeSlider.value = String(Math.ceil(frame.max_temp_c));
    if (Number(highRangeSlider.value) <= Number(lowRangeSlider.value)) {
      highRangeSlider.value = String(Number(lowRangeSlider.value) + 1);
    }
  }

  lowRangeValue.textContent = `${lowRangeSlider.value} °C`;
  highRangeValue.textContent = `${highRangeSlider.value} °C`;
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

  syncGalleryBtn.disabled = true;
  downloadSelectedBtn.disabled = true;
  setGalleryStatus(`正在下载 ${selectedGalleryFile}...`);
  setDownloadLink("", "");
  try {
    const { response, data } = await fetchJsonWithTimeout("/api/gallery/download", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ file_name: selectedGalleryFile }),
    }, 20000);
    if (!response.ok) {
      throw new Error(data.error || "下载失败");
    }

    setGalleryStatus(`下载完成：${data.saved_name}`);
    setDownloadLink(data.download_url, `打开 ${data.saved_name}`);
    await new Promise((resolve) => window.setTimeout(resolve, 300));
    await syncGallery();
  } catch (error) {
    console.error(error);
    const message = error.name === "AbortError" ? "请求超时，请重试" : error.message;
    setGalleryStatus(`下载失败：${message}`, true);
  } finally {
    syncGalleryBtn.disabled = false;
    downloadSelectedBtn.disabled = selectedGalleryFile.length === 0;
  }
}

function renderFrame(frame) {
  if (paused) {
    latestFrame = frame;
    return;
  }
  drawFrame(frame);
}

function rerenderLatestFrame() {
  if (latestFrame) {
    drawFrame(latestFrame);
  }
}

function takeSnapshot() {
  if (!latestFrame) {
    return;
  }
  const link = document.createElement("a");
  link.download = `thermal_${latestFrame.frame_counter.toString().padStart(8, "0")}.png`;
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
  refreshPortsBtn.addEventListener("click", loadPorts);
  connectBtn.addEventListener("click", connectSerial);
  syncGalleryBtn.addEventListener("click", syncGallery);
  downloadSelectedBtn.addEventListener("click", downloadSelectedSnapshot);

  paletteSelect.addEventListener("change", () => {
    state.palette = paletteSelect.value;
    persistDisplayState();
    rerenderLatestFrame();
  });

  autoRangeCheck.addEventListener("change", () => {
    state.autoRange = autoRangeCheck.checked;
    lowRangeSlider.disabled = state.autoRange;
    highRangeSlider.disabled = state.autoRange;
    persistDisplayState();
    rerenderLatestFrame();
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
  });
}

window.addEventListener("resize", () => {
  resizeCanvas();
  rerenderLatestFrame();
});

function init() {
  paletteSelect.value = state.palette;
  autoRangeCheck.checked = state.autoRange;
  lowRangeSlider.value = String(clamp(Math.round(state.lowC), -20, 120));
  highRangeSlider.value = String(clamp(Math.round(state.highC), -20, 120));
  if (Number(highRangeSlider.value) <= Number(lowRangeSlider.value)) {
    highRangeSlider.value = String(Number(lowRangeSlider.value) + 1);
  }
  syncRangeControlsFromState();
  persistDisplayState();
  bindUi();
  resizeCanvas();
  loadPorts();
  connectWebSocket();
  downloadSelectedBtn.disabled = true;
  renderGalleryList([]);
}

init();

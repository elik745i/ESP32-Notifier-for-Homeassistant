const state = {
  status: null,
  settings: null,
  otaCheck: null,
};

const elements = {
  friendlyName: document.getElementById("friendlyName"),
  summaryText: document.getElementById("summaryText"),
  connectionState: document.getElementById("connectionState"),
  ipAddress: document.getElementById("ipAddress"),
  wifiRssi: document.getElementById("wifiRssi"),
  mqttStatus: document.getElementById("mqttStatus"),
  firmwareVersion: document.getElementById("firmwareVersion"),
  batteryVoltage: document.getElementById("batteryVoltage"),
  batteryRaw: document.getElementById("batteryRaw"),
  freeHeap: document.getElementById("freeHeap"),
  settingsSource: document.getElementById("settingsSource"),
  playbackState: document.getElementById("playbackState"),
  playbackType: document.getElementById("playbackType"),
  currentTitle: document.getElementById("currentTitle"),
  currentUrl: document.getElementById("currentUrl"),
  wifiPill: document.getElementById("wifiPill"),
  mqttPill: document.getElementById("mqttPill"),
  audioPill: document.getElementById("audioPill"),
  otaPill: document.getElementById("otaPill"),
  volumeSlider: document.getElementById("volumeSlider"),
  volumeValue: document.getElementById("volumeValue"),
  otaStatus: document.getElementById("otaStatus"),
  playForm: document.getElementById("playForm"),
  settingsForm: document.getElementById("settingsForm"),
};

function setPill(element, label, mode) {
  element.textContent = label;
  element.className = `stat-value ${mode}`;
}

function toast(message) {
  const template = document.getElementById("toastTemplate");
  const node = template.content.firstElementChild.cloneNode(true);
  node.textContent = message;
  document.body.appendChild(node);
  window.setTimeout(() => node.remove(), 2500);
}

async function request(path, options = {}) {
  const response = await fetch(path, {
    headers: { "Content-Type": "application/json" },
    ...options,
  });
  if (!response.ok) {
    const text = await response.text();
    throw new Error(text || `HTTP ${response.status}`);
  }
  const contentType = response.headers.get("content-type") || "";
  return contentType.includes("application/json") ? response.json() : response.text();
}

function fillForm(data) {
  const entries = Object.entries(data);
  for (const [section, sectionValue] of entries) {
    if (sectionValue === null || typeof sectionValue !== "object") {
      continue;
    }
    for (const [key, value] of Object.entries(sectionValue)) {
      const field = elements.settingsForm.elements.namedItem(`${section}.${key}`);
      if (!field) continue;
      if (field.type === "checkbox") {
        field.checked = Boolean(value);
      } else {
        field.value = value ?? "";
      }
    }
  }
}

function collectForm() {
  const payload = {};
  for (const field of elements.settingsForm.elements) {
    if (!field.name) continue;
    const [section, key] = field.name.split(".");
    payload[section] ||= {};
    payload[section][key] = field.type === "checkbox" ? field.checked : field.value;
  }
  payload.mqtt.port = Number(payload.mqtt.port || 1883);
  payload.device.savedVolumePercent = Number(payload.device.savedVolumePercent || 60);
  payload.battery.dividerRatio = Number(payload.battery.dividerRatio || 2);
  payload.battery.calibrationMultiplier = Number(payload.battery.calibrationMultiplier || 1);
  payload.battery.smoothingAlpha = Number(payload.battery.smoothingAlpha || 0.2);
  payload.battery.minVoltageClamp = Number(payload.battery.minVoltageClamp || 2.8);
  payload.battery.maxVoltageClamp = Number(payload.battery.maxVoltageClamp || 4.35);
  payload.battery.updateIntervalMs = Number(payload.battery.updateIntervalMs || 10000);
  payload.battery.sampleCount = Number(payload.battery.sampleCount || 8);
  payload.oled.i2cAddress = Number(payload.oled.i2cAddress || 60);
  payload.oled.width = Number(payload.oled.width || 128);
  payload.oled.height = Number(payload.oled.height || 64);
  payload.oled.sdaPin = Number(payload.oled.sdaPin || 21);
  payload.oled.sclPin = Number(payload.oled.sclPin || 19);
  payload.oled.resetPin = Number(payload.oled.resetPin || -1);
  payload.oled.dimTimeoutSeconds = Number(payload.oled.dimTimeoutSeconds || 0);
  return payload;
}

function renderStatus(status) {
  state.status = status;
  elements.friendlyName.textContent = status.device.friendlyName;
  if (elements.summaryText) {
    elements.summaryText.textContent = `${status.device.deviceName} on ${status.network.ip || "no IP"}`;
  }
  elements.connectionState.textContent = status.network.apMode && !status.network.wifiConnected
    ? `AP mode: ${status.network.apSsid}\nOpen http://192.168.4.1`
    : `${status.device.deviceName} on ${status.network.ip || "no IP"}`;
  elements.ipAddress.textContent = status.network.ip || "-";
  elements.wifiRssi.textContent = `${status.network.wifiRssi} dBm`;
  elements.mqttStatus.textContent = status.network.mqttConnected ? "Connected" : "Disconnected";
  elements.firmwareVersion.textContent = `${status.firmware.version} (${status.firmware.buildDate})`;
  elements.batteryVoltage.textContent = `${status.battery.voltage.toFixed(3)} V`;
  elements.batteryRaw.textContent = `${status.battery.rawAdc}`;
  elements.freeHeap.textContent = `${status.system.freeHeap} B`;
  elements.settingsSource.textContent = status.settings.usingSaved ? "Saved settings" : "Hardwired defaults";
  elements.playbackState.textContent = status.playback.state;
  elements.playbackType.textContent = status.playback.type;
  elements.currentTitle.textContent = status.playback.title || "Idle";
  elements.currentUrl.textContent = status.playback.url || "-";
  elements.volumeSlider.value = status.playback.volumePercent;
  elements.volumeValue.textContent = `${status.playback.volumePercent}%`;
  setPill(elements.wifiPill, status.network.wifiConnected ? "Wi-Fi Connected" : "Wi-Fi Down", status.network.wifiConnected ? "ok" : "bad");
  setPill(elements.mqttPill, status.network.mqttConnected ? "MQTT Connected" : "MQTT Offline", status.network.mqttConnected ? "ok" : "warn");
  setPill(elements.audioPill, status.playback.state, status.playback.state === "playing" ? "ok" : "warn");
  setPill(elements.otaPill, status.ota.lastResult || "OTA idle", status.ota.updateAvailable ? "warn" : "ok");
}

async function loadStatus() {
  renderStatus(await request("/api/status"));
}

async function loadSettings() {
  state.settings = await request("/api/settings");
  fillForm(state.settings);
}

async function saveSettings(event) {
  event.preventDefault();
  await request("/api/settings", {
    method: "POST",
    body: JSON.stringify(collectForm()),
  });
  toast("Settings saved");
  await Promise.all([loadStatus(), loadSettings()]);
}

async function submitPlay(event) {
  event.preventDefault();
  const payload = {
    url: document.getElementById("playUrl").value,
    label: document.getElementById("playLabel").value,
    type: document.getElementById("playType").value,
  };
  await request("/api/play", { method: "POST", body: JSON.stringify(payload) });
  toast("Playback started");
  await loadStatus();
}

async function setVolume(volumePercent) {
  await request("/api/volume", {
    method: "POST",
    body: JSON.stringify({ volumePercent }),
  });
  elements.volumeValue.textContent = `${volumePercent}%`;
}

async function stopPlayback() {
  await request("/api/stop", { method: "POST", body: JSON.stringify({}) });
  toast("Playback stopped");
  await loadStatus();
}

async function checkOta(apply) {
  const result = await request("/api/ota/check", {
    method: "POST",
    body: JSON.stringify({ apply }),
  });
  state.otaCheck = result;
  elements.otaStatus.textContent = JSON.stringify(result, null, 2);
  await loadStatus();
}

async function postSimple(path, message) {
  await request(path, { method: "POST", body: JSON.stringify({}) });
  toast(message);
}

document.getElementById("reloadButton").addEventListener("click", () => Promise.all([loadStatus(), loadSettings()]));
document.getElementById("stopButton").addEventListener("click", stopPlayback);
document.getElementById("checkOtaButton").addEventListener("click", () => checkOta(false));
document.getElementById("applyOtaButton").addEventListener("click", () => checkOta(true));
document.getElementById("rebootButton").addEventListener("click", () => postSimple("/api/reboot", "Reboot requested"));
document.getElementById("factoryResetButton").addEventListener("click", async () => {
  if (!window.confirm("Erase saved settings and reboot?")) return;
  await postSimple("/api/factory-reset", "Factory reset requested");
});

elements.playForm.addEventListener("submit", submitPlay);
elements.settingsForm.addEventListener("submit", saveSettings);
elements.volumeSlider.addEventListener("change", (event) => setVolume(Number(event.target.value)));
elements.volumeSlider.addEventListener("input", (event) => {
  elements.volumeValue.textContent = `${event.target.value}%`;
});

Promise.all([loadStatus(), loadSettings()]).catch((error) => {
  console.error(error);
  toast(`Load failed: ${error.message}`);
});

window.setInterval(() => {
  loadStatus().catch((error) => console.error(error));
}, 5000);

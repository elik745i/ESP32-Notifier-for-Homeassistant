const state = {
  status: null,
  settings: null,
  recentPlayback: loadRecentPlayback(),
};

const elements = {
  deviceTitle: document.getElementById("deviceTitle"),
  deviceNameValue: document.getElementById("deviceNameValue"),
  connectionState: document.getElementById("connectionState"),
  ipAddress: document.getElementById("ipAddress"),
  apInfo: document.getElementById("apInfo"),
  wifiRssi: document.getElementById("wifiRssi"),
  mqttStatus: document.getElementById("mqttStatus"),
  firmwareVersion: document.getElementById("firmwareVersion"),
  firmwareVersionCard: document.getElementById("firmwareVersionCard"),
  batteryVoltage: document.getElementById("batteryVoltage"),
  batteryRaw: document.getElementById("batteryRaw"),
  batteryHero: document.getElementById("batteryHero"),
  freeHeap: document.getElementById("freeHeap"),
  settingsSource: document.getElementById("settingsSource"),
  playbackState: document.getElementById("playbackState"),
  currentTitle: document.getElementById("currentTitle"),
  currentUrl: document.getElementById("currentUrl"),
  wifiPill: document.getElementById("wifiPill"),
  mqttPill: document.getElementById("mqttPill"),
  audioPill: document.getElementById("audioPill"),
  volumeSlider: document.getElementById("volumeSlider"),
  volumeValue: document.getElementById("volumeValue"),
  otaStatus: document.getElementById("otaStatus"),
  otaStatusLabel: document.getElementById("otaStatusLabel"),
  latestVersion: document.getElementById("latestVersion"),
  otaProgressFill: document.getElementById("otaProgressFill"),
  otaProgressLabel: document.getElementById("otaProgressLabel"),
  message: document.getElementById("message"),
  playForm: document.getElementById("playForm"),
  settingsForm: document.getElementById("settingsForm"),
  recentPlaybackList: document.getElementById("recentPlaybackList"),
  useStaticIpToggle: document.getElementById("useStaticIpToggle"),
};

function loadRecentPlayback() {
  try {
    return JSON.parse(window.localStorage.getItem("notifierRecentPlayback") || "[]");
  } catch {
    return [];
  }
}

function saveRecentPlayback() {
  window.localStorage.setItem("notifierRecentPlayback", JSON.stringify(state.recentPlayback.slice(0, 6)));
}

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

function setMessage(message, isError = false) {
  elements.message.textContent = message;
  elements.message.style.color = isError ? "#b42318" : "#333333";
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
  for (const [section, sectionValue] of Object.entries(data)) {
    if (sectionValue === null || typeof sectionValue !== "object") {
      continue;
    }
    for (const [key, value] of Object.entries(sectionValue)) {
      const field = elements.settingsForm.elements.namedItem(`${section}.${key}`);
      if (!field) {
        continue;
      }
      if (field.type === "checkbox") {
        field.checked = Boolean(value);
      } else {
        field.value = value ?? "";
      }
    }
  }
  updateConditionalVisibility();
}

function collectForm() {
  const payload = {};
  for (const field of elements.settingsForm.elements) {
    if (!field.name) {
      continue;
    }
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

function updateConditionalVisibility() {
  const showStatic = elements.useStaticIpToggle.checked;
  for (const node of document.querySelectorAll(".static-ip-group")) {
    node.style.display = showStatic ? "grid" : "none";
  }
}

function renderRecentPlayback() {
  if (!state.recentPlayback.length) {
    elements.recentPlaybackList.innerHTML = '<div class="firmware-item"><div class="firmware-meta"><div class="firmware-title">No recent playback</div><div class="firmware-subtitle">Played URLs will appear here for one-click reuse.</div></div></div>';
    return;
  }

  elements.recentPlaybackList.innerHTML = state.recentPlayback.map((item, index) => `
    <div class="firmware-item">
      <div class="firmware-meta">
        <div class="firmware-title">${escapeHtml(item.label || item.url)}</div>
        <div class="firmware-subtitle">${escapeHtml(item.type)} | ${escapeHtml(item.url)}</div>
      </div>
      <button type="button" class="secondary recent-play-button" data-index="${index}">Use</button>
    </div>
  `).join("");

  for (const button of document.querySelectorAll(".recent-play-button")) {
    button.addEventListener("click", () => {
      const item = state.recentPlayback[Number(button.dataset.index)];
      document.getElementById("playUrl").value = item.url;
      document.getElementById("playLabel").value = item.label || "";
      document.getElementById("playType").value = item.type || "stream";
      toast("Loaded recent playback entry");
    });
  }
}

function escapeHtml(value) {
  return String(value)
    .replaceAll("&", "&amp;")
    .replaceAll("<", "&lt;")
    .replaceAll(">", "&gt;")
    .replaceAll('"', "&quot;")
    .replaceAll("'", "&#39;");
}

function renderStatus(status) {
  state.status = status;
  const ota = status.otaManager || status.ota || {};
  const wifiConnected = Boolean(status.network.wifiConnected);
  const mqttConnected = Boolean(status.network.mqttConnected);
  const playbackActive = status.playback.state === "playing";

  elements.deviceTitle.textContent = status.device.friendlyName || "ESP32 Notifier";
  elements.deviceNameValue.textContent = status.device.deviceName || "-";
  elements.connectionState.textContent = status.network.apMode && !wifiConnected
    ? `AP mode: ${status.network.apSsid}\nOpen http://192.168.4.1`
    : `${status.device.deviceName} on ${status.network.ip || "no IP"}`;

  elements.ipAddress.textContent = status.network.ip || "-";
  elements.apInfo.textContent = status.network.apMode ? `${status.network.apSsid || "AP active"}` : "Disabled";
  elements.wifiRssi.textContent = wifiConnected ? `${status.network.wifiRssi} dBm` : "-";
  elements.mqttStatus.textContent = mqttConnected ? "Connected" : "Disconnected";
  elements.firmwareVersion.textContent = `${status.firmware.version} (${status.firmware.buildDate})`;
  elements.firmwareVersionCard.textContent = status.firmware.version;
  elements.batteryVoltage.textContent = `${Number(status.battery.voltage || 0).toFixed(3)} V`;
  elements.batteryRaw.textContent = `${status.battery.rawAdc ?? "-"}`;
  elements.batteryHero.textContent = `${Number(status.battery.voltage || 0).toFixed(2)} V`;
  elements.freeHeap.textContent = `${status.system.freeHeap} B`;
  elements.settingsSource.textContent = status.settings.usingSaved ? "Saved settings" : "Hardwired defaults";
  elements.playbackState.textContent = status.playback.state || "idle";
  elements.currentTitle.textContent = status.playback.title || "Idle";
  elements.currentUrl.value = status.playback.url || "";
  elements.volumeSlider.value = status.playback.volumePercent || 0;
  elements.volumeValue.textContent = `${status.playback.volumePercent || 0}%`;

  setPill(elements.wifiPill, wifiConnected ? "Wi-Fi Connected" : "Wi-Fi Down", wifiConnected ? "ok" : "bad");
  setPill(elements.mqttPill, mqttConnected ? "MQTT Connected" : "MQTT Offline", mqttConnected ? "ok" : "warn");
  setPill(elements.audioPill, status.playback.state || "idle", playbackActive ? "ok" : "warn");

  elements.otaStatusLabel.textContent = ota.message || ota.lastResult || "Idle";
  elements.latestVersion.textContent = ota.latestVersion || status.ota.latestVersion || "-";
  elements.otaStatus.textContent = JSON.stringify({ ota, snapshot: status.ota }, null, 2);
  elements.otaProgressFill.style.width = ota.busy ? "55%" : ota.updateAvailable ? "100%" : "0%";
  elements.otaProgressLabel.textContent = ota.busy ? "Working..." : ota.updateAvailable ? "Update available" : "No pending update";
}

function setupTabs() {
  const buttons = [...document.querySelectorAll(".tab-button")];
  const panels = [...document.querySelectorAll(".tab-panel")];
  for (const button of buttons) {
    button.addEventListener("click", () => {
      for (const other of buttons) {
        other.setAttribute("aria-selected", String(other === button));
      }
      for (const panel of panels) {
        panel.classList.toggle("active", panel.id === `tab-${button.dataset.tab}`);
      }
    });
  }
}

function setupPasswordToggles() {
  for (const button of document.querySelectorAll(".password-toggle")) {
    button.addEventListener("click", () => {
      const field = elements.settingsForm.elements.namedItem(button.dataset.targetName);
      if (!field) {
        return;
      }
      const reveal = field.type === "password";
      field.type = reveal ? "text" : "password";
      button.classList.toggle("revealed", reveal);
    });
  }
}

async function loadStatus() {
  renderStatus(await request("/api/status"));
}

async function loadSettings() {
  state.settings = await request("/api/settings");
  fillForm(state.settings);
}

async function saveSettings() {
  await request("/api/settings", {
    method: "POST",
    body: JSON.stringify(collectForm()),
  });
  setMessage("Settings saved");
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
  state.recentPlayback.unshift(payload);
  state.recentPlayback = state.recentPlayback.filter((item, index, array) => index === array.findIndex((entry) => entry.url === item.url && entry.type === item.type));
  saveRecentPlayback();
  renderRecentPlayback();
  setMessage("Playback started");
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
  setMessage("Playback stopped");
  toast("Playback stopped");
  await loadStatus();
}

async function checkOta(apply) {
  const result = await request("/api/ota/check", {
    method: "POST",
    body: JSON.stringify({ apply }),
  });
  elements.otaStatus.textContent = JSON.stringify(result, null, 2);
  setMessage(apply ? "OTA install requested" : "OTA check requested");
  await loadStatus();
}

async function postSimple(path, message) {
  await request(path, { method: "POST", body: JSON.stringify({}) });
  setMessage(message);
  toast(message);
}

async function copyCurrentUrl() {
  const value = elements.currentUrl.value;
  if (!value) {
    toast("No current URL to copy");
    return;
  }
  await navigator.clipboard.writeText(value);
  toast("Copied current URL");
}

document.getElementById("reloadButton").addEventListener("click", () => Promise.all([loadStatus(), loadSettings()]));
document.getElementById("saveSettingsButton").addEventListener("click", () => saveSettings().catch(handleError));
document.getElementById("stopButton").addEventListener("click", () => stopPlayback().catch(handleError));
document.getElementById("copyUrlButton").addEventListener("click", () => copyCurrentUrl().catch(handleError));
document.getElementById("checkOtaButton").addEventListener("click", () => checkOta(false).catch(handleError));
document.getElementById("applyOtaButton").addEventListener("click", () => checkOta(true).catch(handleError));
document.getElementById("rebootButton").addEventListener("click", () => postSimple("/api/reboot", "Reboot requested").catch(handleError));
document.getElementById("factoryResetButton").addEventListener("click", async () => {
  if (!window.confirm("Erase saved settings and reboot?")) {
    return;
  }
  await postSimple("/api/factory-reset", "Factory reset requested");
});

elements.playForm.addEventListener("submit", submitPlay);
elements.volumeSlider.addEventListener("change", (event) => setVolume(Number(event.target.value)).catch(handleError));
elements.volumeSlider.addEventListener("input", (event) => {
  elements.volumeValue.textContent = `${event.target.value}%`;
});
elements.useStaticIpToggle.addEventListener("change", updateConditionalVisibility);

function handleError(error) {
  console.error(error);
  setMessage(error.message, true);
  toast(`Error: ${error.message}`);
}

setupTabs();
setupPasswordToggles();
renderRecentPlayback();

Promise.all([loadStatus(), loadSettings()]).catch(handleError);

window.setInterval(() => {
  loadStatus().catch((error) => console.error(error));
}, 5000);

const state = {
  meta: null,
  status: null,
  canDebug: null,
  debug: null,
  stats: null,
  logs: null,
  params: [],
  selectedKey: null,
  recentChanges: [],
  activeTab: "status",
  activeLogSource: "tmux",
  connection: {
    online: true,
    lastError: "",
    lastOkAt: null,
  },
  filters: {
    search: "",
    scope: "all",
    type: "all",
    presentOnly: false,
    changedOnly: false,
  },
  monitoring: {
    canEnabled: false,
    debugEnabled: false,
  },
  ui: {
    statusDetailsExpanded: false,
  },
};

const CAN_MONITOR_STORAGE_KEY = "frogpilot.dashboard.canMonitorEnabled";
const DEBUG_MONITOR_STORAGE_KEY = "frogpilot.dashboard.debugMonitorEnabled";
const STATUS_DETAILS_STORAGE_KEY = "frogpilot.dashboard.statusDetailsExpanded";

let liveStatusTimer = null;
let canRefreshTimer = null;
let debugRefreshTimer = null;
let metaRefreshTimer = null;
let statsRefreshTimer = null;
let logsRefreshTimer = null;
let statusRefreshInFlight = false;
let canRefreshInFlight = false;
let debugRefreshInFlight = false;
let metaRefreshInFlight = false;
let statsRefreshInFlight = false;
let logsRefreshInFlight = false;

const statusCardCache = new Map();

const $ = (id) => document.getElementById(id);

function setText(target, value) {
  const element = typeof target === "string" ? $(target) : target;
  if (!element) return;
  const next = formatValue(value);
  if (element.textContent !== next) {
    element.textContent = next;
  }
}

function setExactText(target, value) {
  const element = typeof target === "string" ? $(target) : target;
  if (!element) return;
  const next = value ?? "";
  if (element.textContent !== next) {
    element.textContent = next;
  }
}

function setClass(target, className) {
  const element = typeof target === "string" ? $(target) : target;
  if (!element) return;
  if (element.className !== className) {
    element.className = className;
  }
}

function formatTimeLabel(ts) {
  if (!ts) return "-";
  const millis = Number(ts) * 1000;
  if (!Number.isFinite(millis) || millis <= 0) return "-";
  return new Date(millis).toLocaleTimeString("ko-KR", { hour12: false });
}

function formatAgeLabel(ageSec) {
  if (ageSec === null || ageSec === undefined || ageSec === "-" || Number.isNaN(Number(ageSec))) return "-";
  const age = Number(ageSec);
  if (!Number.isFinite(age)) return "-";
  if (age < 1) return "방금";
  return `${age.toFixed(1)}초 전`;
}

function formatDateTimeLabel(ts) {
  if (!ts) return "-";
  const millis = Number(ts) * 1000;
  if (!Number.isFinite(millis) || millis <= 0) return "-";
  return new Date(millis).toLocaleString("ko-KR", { hour12: false });
}

function boolLabel(value) {
  return value ? "ON" : "OFF";
}

function selectedText(id) {
  const element = $(id);
  if (!element) return "-";
  return element.options[element.selectedIndex]?.textContent || element.value || "-";
}

async function api(path, options = {}) {
  const controller = new AbortController();
  const timeout = window.setTimeout(() => controller.abort(), 3000);
  let response;
  try {
    response = await fetch(path, {
      headers: { "Content-Type": "application/json" },
      signal: controller.signal,
      ...options,
    });
  } catch (error) {
    state.connection.online = false;
    state.connection.lastError = error?.name === "AbortError" ? "요청 시간 초과" : (error?.message || "연결 실패");
    renderConnection();
    throw error;
  } finally {
    window.clearTimeout(timeout);
  }

  let payload = {};
  try {
    payload = await response.json();
  } catch (error) {
    payload = {};
  }

  if (!response.ok) {
    state.connection.online = false;
    state.connection.lastError = payload.error || `HTTP ${response.status}`;
    renderConnection();
    throw new Error(payload.error || `HTTP ${response.status}`);
  }

  state.connection.online = true;
  state.connection.lastError = "";
  state.connection.lastOkAt = Date.now();
  renderConnection();
  return payload;
}

function addLog(title, detail) {
  state.recentChanges.unshift({
    time: new Date().toLocaleTimeString("ko-KR", { hour12: false }),
    title,
    detail,
  });
  state.recentChanges = state.recentChanges.slice(0, 12);
}

function replaceParam(nextParam) {
  const index = state.params.findIndex((param) => param.key === nextParam.key);
  if (index >= 0) state.params[index] = nextParam;
}

function setParamLocalValue(key, patch) {
  const index = state.params.findIndex((param) => param.key === key);
  if (index >= 0) {
    state.params[index] = { ...state.params[index], ...patch };
  }
}

function filteredParams() {
  return state.params.filter((param) => {
    const query = state.filters.search.trim().toLowerCase();
    if (query) {
      const haystack = `${param.key} ${param.flags} ${param.type} ${param.defaultValue ?? ""}`.toLowerCase();
      if (!haystack.includes(query)) return false;
    }
    if (state.filters.scope !== "all" && param.scope !== state.filters.scope) return false;
    if (state.filters.type !== "all" && param.type !== state.filters.type) return false;
    if (state.filters.presentOnly && !param.present) return false;
    if (state.filters.changedOnly && !(param.present && !param.isDefault)) return false;
    return true;
  });
}

function counts() {
  return {
    present: state.params.filter((param) => param.present).length,
    changed: state.params.filter((param) => param.present && !param.isDefault).length,
    frogpilot: state.params.filter((param) => param.scope === "frogpilot").length,
    core: state.params.filter((param) => param.scope === "core").length,
  };
}

function typeCount(type) {
  return state.meta?.typeCounts?.[type] || 0;
}

function barWidth(type) {
  const total = state.meta?.totalKeys || 1;
  return `${Math.max(8, Math.min(100, (typeCount(type) / total) * 100))}%`;
}

function formatValue(value) {
  if (typeof value === "boolean") return value ? "ON" : "OFF";
  if (value === null || value === undefined || value === "") return "-";
  return String(value);
}

function loadMonitoringPreferences() {
  try {
    state.monitoring.canEnabled = localStorage.getItem(CAN_MONITOR_STORAGE_KEY) === "1";
    state.monitoring.debugEnabled = localStorage.getItem(DEBUG_MONITOR_STORAGE_KEY) === "1";
  } catch (error) {
    state.monitoring.canEnabled = false;
    state.monitoring.debugEnabled = false;
  }
}

function persistMonitoringPreferences() {
  try {
    localStorage.setItem(CAN_MONITOR_STORAGE_KEY, state.monitoring.canEnabled ? "1" : "0");
    localStorage.setItem(DEBUG_MONITOR_STORAGE_KEY, state.monitoring.debugEnabled ? "1" : "0");
  } catch (error) {
    // Ignore storage failures and keep runtime state only.
  }
}

function loadUiPreferences() {
  try {
    state.ui.statusDetailsExpanded = localStorage.getItem(STATUS_DETAILS_STORAGE_KEY) === "1";
  } catch (error) {
    state.ui.statusDetailsExpanded = false;
  }
}

function persistUiPreferences() {
  try {
    localStorage.setItem(STATUS_DETAILS_STORAGE_KEY, state.ui.statusDetailsExpanded ? "1" : "0");
  } catch (error) {
    // Ignore storage failures and keep runtime state only.
  }
}

function setActiveTab(nextTab) {
  state.activeTab = nextTab;
  if (window.location.hash !== `#${nextTab}`) {
    window.history.replaceState(null, "", `#${nextTab}`);
  }
  document.querySelectorAll(".tab-button").forEach((button) => {
    button.classList.toggle("active", button.dataset.tab === nextTab);
  });
  document.querySelectorAll(".tab-panel").forEach((panel) => {
    panel.classList.toggle("active", panel.dataset.panel === nextTab);
  });

  if (nextTab === "status") {
    refreshLiveStatusView().catch(() => {});
    if (state.monitoring.canEnabled && state.ui.statusDetailsExpanded) {
      refreshCanDebugView().catch(() => {});
    } else {
      renderCanSignalTools();
    }
  } else if (nextTab === "debug") {
    refreshDebugView().catch(() => {});
  } else if (nextTab === "stats") {
    refreshStatsOnly().then(renderStats).catch(() => {});
  } else if (nextTab === "query") {
    refreshLogsOnly().then(renderLogs).catch(() => {});
  } else if (nextTab === "settings") {
    refreshParamsOnly().then(() => {
      if (!state.selectedKey && state.params.length) {
        state.selectedKey = state.params[0].key;
      }
      renderParamsSummary();
      renderParams();
    }).catch(() => {});
  }
}

function renderConnection() {
  const card = $("connectionCard");
  const dot = $("connectionDot");
  const label = $("connectionLabel");
  const detail = $("deviceLabel");
  if (!card || !dot || !label || !detail) return;

  if (state.connection.online) {
    card.classList.remove("offline");
    dot.className = "dot live";
    label.textContent = "연결됨";
    if (detail) detail.textContent = state.meta ? `${state.meta.modeLabel} · ${state.meta.branch}` : "연결됨";
    card.title = detail ? detail.textContent : "연결됨";
    return;
  }

  card.classList.add("offline");
  dot.className = "dot offline";
  label.textContent = "끊김";
  if (detail) detail.textContent = state.connection.lastError || "응답 없음";
  card.title = detail ? detail.textContent : (state.connection.lastError || "응답 없음");
}

function renderHero() {
  renderConnection();
  if (!state.meta) return;

  $("connectionLabel").textContent = state.connection.online ? "연결됨" : "끊김";
  if ($("deviceLabel")) {
    $("deviceLabel").textContent = `${state.meta.modeLabel} · ${state.meta.branch} · prefix ${state.meta.prefix}`;
    $("connectionCard").title = $("deviceLabel").textContent;
  }
  $("deviceStateTitle").textContent = state.meta.liveMessaging ? "실시간 메시지 + Params 연결됨" : "실제 Params 연결됨";
  $("deviceStateDescription").textContent = state.meta.liveMessaging
    ? `${state.meta.paramsRoot} 와 openpilot 실시간 메시지를 함께 읽는 기기 웹 콘솔입니다.`
    : `${state.meta.paramsRoot} 를 직접 읽고 쓰는 웹 콘솔입니다.`;
  $("startedChip").textContent = "LIVE";
  $("networkChip").textContent = state.meta.branch.toUpperCase();
  $("thermalChip").textContent = state.meta.liveMessaging ? "MESSAGING" : `${state.meta.presentKeys} SET`;

  $("opModeValue").textContent = state.meta.branch;
  $("alertValue").textContent = state.meta.commit;
  $("vehicleValue").textContent = `${state.meta.modifiedCount} files`;
  $("speedValue").textContent = state.meta.totalKeys;
  $("accValue").textContent = state.meta.presentKeys;
  $("fakeValue").textContent = state.meta.frogpilotKeys;

  if (state.status?.apn) {
    $("apnConnectionValue").textContent = state.status.apn.statusLabel;
    $("apnBridgeValue").textContent = state.status.apn.bridgeEnabled ? "RUNNING" : "IDLE";
    $("apnPacketValue").textContent = state.status.apn.lastPacketKind !== "-" ?
      `${state.status.apn.lastPacketKind} · ${formatAgeLabel(state.status.apn.lastPacketAgeSec)}` :
      (state.status.apn.message || "-");
  }
}

function renderStatusHero() {
  if (!state.status) return;
  const runtime = state.status.runtime;
  const updatedLabel = runtime.updatedAt
    ? `마지막 갱신 ${new Date(runtime.updatedAt * 1000).toLocaleTimeString("ko-KR", { hour12: false })}`
    : "";

  setExactText("statusVehicleTitle", runtime.vehicleDisplayName || "-");
  setExactText("statusVehicleSubtitle", [runtime.subtitle, updatedLabel].filter(Boolean).join(" · "));
  setText("statusVoltageValue", runtime.carVoltage);
  setText("statusSpeedValue", runtime.vehicleSpeed);
  setText("statusAccValue", runtime.accSpeed);
  setText("statusOpenpilotValue", runtime.openpilotState);

  const enabledChip = $("statusOpenpilotChip");
  setExactText(enabledChip, runtime.enabledToggle ? "ENABLED" : "DISABLED");
  setClass(enabledChip, `pill ${runtime.enabledToggle ? "success" : "neutral"}`);

  const onroadChip = $("statusOnroadChip");
  setExactText(onroadChip, runtime.isOnroad ? "ONROAD" : "OFFROAD");
  setClass(onroadChip, `pill ${runtime.isOnroad ? "success" : "neutral"}`);

  const engagedChip = $("statusEngagedChip");
  setExactText(engagedChip, runtime.isEngaged ? "ENGAGED" : "DISENGAGED");
  setClass(engagedChip, `pill ${runtime.isEngaged ? "success" : "neutral"}`);

  const apnChip = $("statusApnChip");
  if (apnChip && state.status?.apn) {
    setExactText(apnChip, state.status.apn.connected ? "APN CONNECTED" : state.status.apn.useApn ? "APN WAITING" : "APN OFF");
    setClass(apnChip, `pill ${state.status.apn.connected ? "success" : state.status.apn.useApn ? "warn" : "neutral"}`);
  }
}

function renderStatusDetailsToggle() {
  const wrap = $("statusDetailsWrap");
  const button = $("toggleStatusDetailsButton");
  const caption = $("statusDetailsCaption");
  if (!wrap || !button || !caption) return;

  wrap.classList.toggle("is-collapsed", !state.ui.statusDetailsExpanded);
  button.textContent = state.ui.statusDetailsExpanded ? "접기" : "더보기";
  button.classList.toggle("is-active", state.ui.statusDetailsExpanded);
  caption.textContent = state.ui.statusDetailsExpanded
    ? "하단 상세 상태를 실시간 표시 중"
    : "상단 핵심 정보만 표시 중";
}

function renderStatusCard(gridId, entries) {
  const grid = $(gridId);
  const labels = entries.map(([label]) => label);
  const cached = statusCardCache.get(gridId);
  const needsRebuild = !cached ||
    cached.labels.length !== labels.length ||
    cached.labels.some((label, index) => label !== labels[index]);

  if (needsRebuild) {
    grid.innerHTML = "";
    const nodes = {};
    entries.forEach(([label, value]) => {
      const card = document.createElement("div");
      card.className = "info-card";
      const labelNode = document.createElement("span");
      labelNode.textContent = label;
      const valueNode = document.createElement("strong");
      valueNode.textContent = formatValue(value);
      card.appendChild(labelNode);
      card.appendChild(valueNode);
      grid.appendChild(card);
      nodes[label] = valueNode;
    });
    statusCardCache.set(gridId, { labels, nodes });
    return;
  }

  entries.forEach(([label, value]) => {
    const valueNode = cached.nodes[label];
    if (valueNode) {
      const next = formatValue(value);
      if (valueNode.textContent !== next) {
        valueNode.textContent = next;
      }
    }
  });
}

function renderStatusPanels() {
  renderStatusDetailsToggle();
  if (!state.status || !state.ui.statusDetailsExpanded) return;

  renderStatusCard("deviceInfoGrid", [
    ["Started", state.status.device.started],
    ["Ignition", state.status.device.ignition],
    ["Device Type", state.status.device.deviceType],
    ["Network", state.status.device.networkType],
    ["Thermal", state.status.device.thermalStatus],
    ["Free Space", state.status.device.freeSpacePercent],
    ["Memory Usage", state.status.device.memoryUsagePercent],
    ["Brightness", state.status.device.screenBrightnessPercent],
    ["Fan Target", state.status.device.fanSpeedPercentDesired],
    ["Max CPU Temp", state.status.device.maxCpuTempC],
    ["Max GPU Temp", state.status.device.maxGpuTempC],
    ["Memory Temp", state.status.device.memoryTempC],
    ["Controls Allowed", state.status.device.controlsAllowed],
    ["Safety Param", state.status.device.safetyParam],
    ["Dongle ID", state.status.device.dongleId],
    ["Hardware Serial", state.status.device.hardwareSerial],
    ["Boot Count", state.status.device.bootCount],
    ["Language", state.status.device.language],
    ["Metric", state.status.device.isMetric],
    ["ADB", state.status.device.adbEnabled],
    ["SSH", state.status.device.sshEnabled],
    ["Record Front", state.status.device.recordFront],
    ["Record Audio", state.status.device.recordAudio],
    ["Disable Updates", state.status.device.disableUpdates],
  ]);

  renderStatusCard("openpilotInfoGrid", [
    ["State", state.status.openpilot.state],
    ["Enabled", state.status.openpilot.enabled],
    ["Active", state.status.openpilot.active],
    ["Engageable", state.status.openpilot.engageable],
    ["Enabled Toggle", state.status.openpilot.enabledToggle],
    ["Experimental Mode", state.status.openpilot.experimentalMode],
    ["Is Onroad", state.status.openpilot.isOnroad],
    ["Is Offroad", state.status.openpilot.isOffroad],
    ["Is Engaged", state.status.openpilot.isEngaged],
    ["LDW", state.status.openpilot.isLdwEnabled],
    ["Disengage On Accelerator", state.status.openpilot.disengageOnAccelerator],
    ["Long Personality", state.status.openpilot.longitudinalPersonality],
    ["Always On DM", state.status.openpilot.alwaysOnDM],
    ["Always On Lateral", state.status.openpilot.alwaysOnLateral],
    ["Fake-Long", state.status.openpilot.fakeLong],
    ["Fake-Long Test UI", state.status.openpilot.fakeLongTestUI],
    ["Driving Model Name", state.status.openpilot.drivingModelName],
    ["Alert 1", state.status.openpilot.alertText1],
    ["Alert 2", state.status.openpilot.alertText2],
  ]);

  renderStatusCard("vehicleInfoGrid", [
    ["Car Make", state.status.vehicle.carMake],
    ["Car Model", state.status.vehicle.carModel],
    ["Car Model Name", state.status.vehicle.carModelName],
    ["Car Voltage", state.status.vehicle.carVoltage],
    ["CAN Valid", state.status.vehicle.canValid],
    ["CAN Timeout", state.status.vehicle.canTimeout],
    ["ACC Faulted", state.status.vehicle.accFaulted],
    ["Gear", state.status.vehicle.gear],
    ["Standstill", state.status.vehicle.standstill],
    ["Vehicle Speed", state.status.vehicle.vehicleSpeed],
    ["Cluster Speed", state.status.vehicle.vehicleSpeedCluster],
    ["ACC Speed", state.status.vehicle.accSpeed],
    ["ACC Available", state.status.vehicle.accAvailable],
    ["ACC Enabled", state.status.vehicle.accEnabled],
    ["Gas Pressed", state.status.vehicle.gasPressed],
    ["Brake Pressed", state.status.vehicle.brakePressed],
    ["Steering Pressed", state.status.vehicle.steeringPressed],
    ["Left Blinker", state.status.vehicle.leftBlinker],
    ["Right Blinker", state.status.vehicle.rightBlinker],
    ["Force Fingerprint", state.status.vehicle.forceFingerprint],
    ["Disable OP Long", state.status.vehicle.disableOpenpilotLongitudinal],
    ["Distance Button Control", state.status.vehicle.distanceButtonControl],
    ["Cluster Offset", state.status.vehicle.clusterOffset],
    ["Driving Model", state.status.vehicle.drivingModel],
    ["Driving Model Version", state.status.vehicle.drivingModelVersion],
  ]);

  renderStatusCard("apnInfoGrid", [
    ["Use APN", state.status.apn.useApn],
    ["연결 상태", state.status.apn.statusLabel],
    ["브리지 실행", state.status.apn.bridgeEnabled],
    ["Route Active", state.status.apn.routeActive],
    ["임시 라벨", state.status.apn.currentLabel],
    ["브리지 메세지", state.status.apn.message || "-"],
    ["기기 IP", state.status.apn.deviceIp],
    ["수신 포트", state.status.apn.listenPort],
    ["HTTP 포트", state.status.apn.httpPort],
    ["HTTP 경로", state.status.apn.httpPath],
    ["브로드캐스트 대상", (state.status.apn.broadcastTargets || []).join(", ") || "-"],
    ["최근 패킷", state.status.apn.lastPacketKind],
    ["수신 주소", state.status.apn.lastPacketFrom],
    ["최근 수신", formatAgeLabel(state.status.apn.lastPacketAgeSec)],
    ["도로명", state.status.apn.roadName],
    ["제한속도", state.status.apn.roadLimitKph === "-" ? "-" : `${state.status.apn.roadLimitKph} km/h`],
    ["도착까지 남은 총 거리", state.status.apn.remainingDistanceLabel],
    ["도착까지 남은 총 시간", state.status.apn.remainingTimeLabel],
    ["다음 안내까지 거리", state.status.apn.nextTurnDistanceLabel],
    ["다음 안내 종류", state.status.apn.nextTurnLabel],
    ["SDI 타입", state.status.apn.sdiType],
    ["SDI 구간", state.status.apn.sdiSection],
    ["SDI Plus 타입", state.status.apn.sdiPlusType],
    ["SDI Block 타입", state.status.apn.sdiBlockType],
    ["Block Section", state.status.apn.sdiBlockSection],
    ["SDI 거리", state.status.apn.sdiDistanceM === "-" ? "-" : `${state.status.apn.sdiDistanceM} m`],
  ]);

  setText("apnDebugSubtitle", state.status.apn.lastPacketAt ? `마지막 수신 ${formatTimeLabel(state.status.apn.lastPacketAt)}` : "브리지 대기 중");
  setExactText("apnDebugViewer", state.status.apn.debugJson || "{}");
  renderApnLabelTools();
  renderCanSignalTools();
}

function renderApnLabelTools() {
  const apn = state.status?.apn;
  if (!apn) return;

  const input = $("apnLabelInput");
  const saveButton = $("saveApnLabelButton");
  const deleteButton = $("deleteApnLabelButton");
  const list = $("apnLabelList");
  const currentSignature = apn.currentSignature || "";
  const currentLabel = apn.currentLabel && apn.currentLabel !== "-" ? apn.currentLabel : "";

  setExactText("apnLabelSubtitle", currentSignature ? apn.currentSignatureSummary || currentSignature : "현재 SDI 시그니처 없음");
  setExactText("apnSignatureValue", currentSignature || "-");
  setExactText("apnCurrentLabelValue", currentLabel || "-");

  if (input && document.activeElement !== input) {
    input.value = currentLabel;
  }
  if (input) {
    input.disabled = !currentSignature;
  }
  if (saveButton) {
    saveButton.disabled = !currentSignature;
  }
  if (deleteButton) {
    deleteButton.disabled = !currentSignature || !currentLabel;
  }
  if (!list) return;

  list.innerHTML = "";
  const labels = Array.isArray(apn.savedLabels) ? apn.savedLabels : [];
  if (!labels.length) {
    const empty = document.createElement("div");
    empty.className = "apn-label-empty";
    empty.textContent = "저장된 SDI 임시 라벨이 없습니다.";
    list.appendChild(empty);
    return;
  }

  labels.forEach((entry) => {
    const item = document.createElement("div");
    item.className = "apn-label-item";

    const head = document.createElement("div");
    head.className = "apn-label-item-head";

    const titleWrap = document.createElement("div");
    titleWrap.className = "apn-label-title-wrap";

    const title = document.createElement("strong");
    title.textContent = entry.label || "-";
    titleWrap.appendChild(title);

    if (entry.signature === currentSignature) {
      const badge = document.createElement("span");
      badge.className = "param-badge param-badge-live";
      badge.textContent = "현재";
      titleWrap.appendChild(badge);
    }

    const updated = document.createElement("span");
    updated.className = "panel-caption";
    updated.textContent = formatDateTimeLabel(entry.updatedAt);

    head.appendChild(titleWrap);
    head.appendChild(updated);

    const signature = document.createElement("code");
    signature.className = "apn-label-signature";
    signature.textContent = entry.signature || "-";

    const summary = document.createElement("div");
    summary.className = "apn-label-summary";
    summary.textContent = entry.summary || "-";

    const removeButton = document.createElement("button");
    removeButton.className = "mini-button mini-button-muted";
    removeButton.textContent = "삭제";
    removeButton.addEventListener("click", async () => {
      await deleteApnLabel(entry.signature);
    });

    item.appendChild(head);
    item.appendChild(signature);
    item.appendChild(summary);
    item.appendChild(removeButton);
    list.appendChild(item);
  });
}

async function saveCurrentApnLabel() {
  const apn = state.status?.apn;
  const input = $("apnLabelInput");
  const saveButton = $("saveApnLabelButton");
  if (!apn?.currentSignature || !input || !saveButton) return;

  const label = input.value.trim();
  if (!label) {
    addLog("라벨 저장 실패", "임시 라벨 문구를 입력해 주세요.");
    renderRecentChanges();
    return;
  }

  saveButton.disabled = true;
  try {
    await api("/api/apn-labels", {
      method: "POST",
      body: JSON.stringify({
        signature: apn.currentSignature,
        label,
        fields: apn.currentSignatureFields || {},
      }),
    });
    await refreshStatusOnly();
    addLog("SDI 라벨 저장", `${apn.currentSignature} → ${label}`);
    renderStatusHero();
    renderStatusPanels();
    renderRecentChanges();
  } catch (error) {
    addLog("라벨 저장 실패", error.message);
    renderRecentChanges();
  } finally {
    saveButton.disabled = false;
  }
}

async function deleteApnLabel(signature = state.status?.apn?.currentSignature) {
  const targetSignature = String(signature || "").trim();
  if (!targetSignature) return;

  try {
    await api(`/api/apn-labels/${encodeURIComponent(targetSignature)}`, { method: "DELETE" });
    await refreshStatusOnly();
    addLog("SDI 라벨 삭제", targetSignature);
    renderStatusHero();
    renderStatusPanels();
    renderRecentChanges();
  } catch (error) {
    addLog("라벨 삭제 실패", error.message);
    renderRecentChanges();
  }
}

function renderCanSignalTools() {
  renderCanMonitorControls();

  if (!state.monitoring.canEnabled) {
    const list = $("canSignalList");
    if (!list) return;
    setExactText("canDebugSubtitle", "모니터링 꺼짐 · 필요할 때만 시작");
    list.innerHTML = "";
    const empty = document.createElement("div");
    empty.className = "apn-label-empty";
    empty.textContent = "부하를 줄이기 위해 CAN/차량 상태 모니터링은 기본적으로 꺼져 있습니다. 필요할 때만 '모니터링 시작'을 눌러 주세요.";
    list.appendChild(empty);
    return;
  }

  const canDebug = state.canDebug || state.status?.canDebug;
  const list = $("canSignalList");
  if (!list) return;

  setExactText("canDebugSubtitle", canDebug?.available
    ? `차량 상태 + CAN 후보 · 마지막 갱신 ${formatTimeLabel(canDebug.updatedAt)}`
    : (canDebug?.error || "CAN 후보 신호 대기 중"));

  list.innerHTML = "";
  if (!canDebug?.available) {
    const empty = document.createElement("div");
    empty.className = "apn-label-empty";
    empty.textContent = canDebug?.error || "CAN 후보 신호를 아직 읽지 못했습니다.";
    list.appendChild(empty);
    return;
  }

  const entries = Array.isArray(canDebug.entries) ? canDebug.entries : [];
  if (!entries.length) {
    const empty = document.createElement("div");
    empty.className = "apn-label-empty";
    empty.textContent = "현재 후보 CAN 신호가 없습니다.";
    list.appendChild(empty);
    return;
  }

  entries.forEach((entry) => {
    const card = document.createElement("div");
    card.className = `can-signal-card ${entry.live ? "is-live" : "is-stale"}`;

    const header = document.createElement("div");
    header.className = "can-signal-head";
    const kindText = entry.kind === "raw" ? "RAW CAN" : entry.kind === "status" ? `${entry.message} · ${entry.signal}` : `${entry.message} · ${entry.signal}`;
    header.innerHTML = `<div><strong>${entry.title}</strong><span>${kindText}</span></div>`;

    const chips = document.createElement("div");
    chips.className = "param-badges";
    if (entry.kind === "status") {
      chips.appendChild(createBadge("vehicle", "param-badge-live"));
    } else {
      chips.appendChild(createBadge(`src ${entry.src}`));
      chips.appendChild(createBadge(`addr ${entry.address}`));
    }
    if (entry.kind === "raw") {
      chips.appendChild(createBadge("raw", "param-badge-warn"));
    } else if (entry.kind === "status") {
      chips.appendChild(createBadge("status"));
    } else {
      chips.appendChild(createBadge("decoded", "param-badge-live"));
    }
    chips.appendChild(createBadge(entry.live ? "LIVE" : "RECENT", entry.live ? "param-badge-live" : "param-badge-recent"));
    header.appendChild(chips);

    const valueRow = document.createElement("div");
    valueRow.className = "can-signal-value";
    valueRow.innerHTML = `<span>현재 값</span><code>${formatValue(entry.value)}</code>`;

    const lastSeenRow = document.createElement("div");
    lastSeenRow.className = "can-signal-last-seen";
    lastSeenRow.innerHTML = `<span>마지막 관측</span><strong>${entry.live ? "지금" : formatAgeLabel(entry.lastSeenAgeSec)}</strong>`;

    const labelRow = document.createElement("div");
    labelRow.className = "can-signal-current-label";
    labelRow.innerHTML = `<span>라벨</span><strong>${entry.label && entry.label !== "-" ? entry.label : "-"}</strong>`;

    const inputRow = document.createElement("div");
    inputRow.className = "can-signal-form";
    const input = document.createElement("input");
    input.className = "param-input";
    input.type = "text";
    input.placeholder = "예: 버튼 누를 때만 변함, set speed 후보";
    input.value = entry.label && entry.label !== "-" ? entry.label : "";

    const saveButton = document.createElement("button");
    saveButton.className = "mini-button";
    saveButton.textContent = "저장";
    saveButton.addEventListener("click", async () => {
      await saveCanLabel(entry.id, input.value, entry);
    });

    const deleteButton = document.createElement("button");
    deleteButton.className = "mini-button mini-button-muted";
    deleteButton.textContent = "삭제";
    deleteButton.disabled = !(entry.label && entry.label !== "-");
    deleteButton.addEventListener("click", async () => {
      await deleteCanLabel(entry.id);
    });

    input.addEventListener("keydown", async (event) => {
      if (event.key === "Enter") {
        event.preventDefault();
        await saveCanLabel(entry.id, input.value, entry);
      }
    });

    inputRow.appendChild(input);
    inputRow.appendChild(saveButton);
    inputRow.appendChild(deleteButton);

    card.appendChild(header);
    card.appendChild(valueRow);
    card.appendChild(lastSeenRow);
    card.appendChild(labelRow);
    card.appendChild(inputRow);
    list.appendChild(card);
  });
}

function renderCanMonitorControls() {
  const button = $("toggleCanMonitorButton");
  if (!button) return;

  button.textContent = state.monitoring.canEnabled ? "모니터링 중지" : "모니터링 시작";
  button.classList.toggle("is-active", state.monitoring.canEnabled);
}

function debugBusLabel(value) {
  return {
    camera: "카메라",
  }[String(value || "").trim().toLowerCase()] || formatValue(value);
}

function debugModeLabel(value) {
  return {
    tap: "눌렀다 떼기",
    press: "누르기만",
    release: "떼기만",
  }[String(value || "").trim().toLowerCase()] || formatValue(value);
}

function describeDebugCommand(command) {
  if (!command || typeof command !== "object") return "전송 대기 중";
  const button = String(command.button || "-").toUpperCase();
  const repeats = Number(command.repeats || 0);
  const holdFrames = Number(command.holdFrames || 0);
  const parts = [
    button,
    debugBusLabel(command.bus),
    debugModeLabel(command.mode),
  ];
  if (repeats > 0) {
    parts.push(`${repeats}회`);
  }
  if (holdFrames > 0) {
    parts.push(`${holdFrames}프레임 유지`);
  }
  return parts.join(" · ");
}

function renderDebugCommandConfig() {
  setExactText(
    "debugCommandSubtitle",
    `${selectedText("debugBusMode")} · ${selectedText("debugActionMode")} · ${selectedText("debugRepeatCount")} · ${selectedText("debugHoldFrames")}`,
  );
}

function renderDebugMonitorControls() {
  const toggleButton = $("toggleDebugMonitorButton");
  if (toggleButton) {
    toggleButton.textContent = state.monitoring.debugEnabled ? "실시간 보기 끄기" : "실시간 보기 켜기";
    toggleButton.classList.toggle("is-active", state.monitoring.debugEnabled);
  }

  setExactText("debugPollingStatus", state.monitoring.debugEnabled ? "실시간 보기 켜짐" : "수동 갱신 모드");
}

function renderDebug() {
  const debug = state.debug;
  renderDebugMonitorControls();
  if (!debug) {
    setExactText("debugTitle", "버튼 실험 콘솔");
    setExactText("debugSubtitle", "웹에서 fake-long 버튼 조합을 직접 전송합니다.");
    setExactText("debugSafetyHint", "디버그 상태를 불러오는 중...");
    setClass("debugSafetyHint", "debug-safety-hint is-neutral");
    setExactText("debugSendStatus", "디버그 대기 중");
    renderDebugCommandConfig();
    renderStatusCard("debugInfoGrid", []);
    setExactText("debugPendingViewer", "{}");
    setExactText("debugFakeLongViewer", "{}");
    return;
  }

  const runtime = debug.runtime || {};
  const safetyHint = debug.safetyHint || {};
  const fakeLongDebug = debug.fakeLongDebug || {};
  const pendingTest = debug.pendingTest || {};
  const updatedLabel = debug.updatedAt ? `마지막 갱신 ${formatTimeLabel(debug.updatedAt)}` : "";
  const activeDebugBits = [
    runtime.fakeLong ? "Fake-Long ON" : "Fake-Long OFF",
    runtime.fakeLongTestUI ? "Test UI ON" : "Test UI OFF",
    runtime.apnFakeLong ? "APN ON" : "APN OFF",
  ];

  setExactText("debugTitle", "버튼 실험 콘솔");
  setExactText("debugSubtitle", [activeDebugBits.join(" · "), updatedLabel].filter(Boolean).join(" · "));
  setExactText("debugSafetyHint", safetyHint.message || "웹 명령이 carcontroller까지 들어가는지 확인 중입니다.");
  setClass("debugSafetyHint", `debug-safety-hint is-${safetyHint.level || "neutral"}`);

  const onroadChip = $("debugOnroadChip");
  setExactText(onroadChip, runtime.onroad ? "ONROAD" : "OFFROAD");
  setClass(onroadChip, `pill ${runtime.onroad ? "success" : "neutral"}`);

  const cruiseChip = $("debugCruiseChip");
  const cruiseLabel = runtime.accEnabled ? "ACC ENGAGED" : runtime.accAvailable ? "ACC READY" : "CRUISE OFF";
  setExactText(cruiseChip, cruiseLabel);
  setClass(cruiseChip, `pill ${runtime.accEnabled ? "success" : runtime.accAvailable ? "warn" : "neutral"}`);

  const safetyChip = $("debugSafetyChip");
  setExactText(safetyChip, `SAFETY ${formatValue(runtime.safetyParam)}`);
  setClass(safetyChip, `pill ${runtime.controlsAllowed ? "success" : "neutral"}`);

  setText("debugVehicleSpeedValue", runtime.vehicleSpeed);
  setText("debugAccSpeedValue", runtime.accSpeed);
  setText("debugLastButtonValue", fakeLongDebug.last || "-");
  setText("debugFakeLongFlagValue", boolLabel(runtime.fakeLong));
  setText("debugFakeLongTestUIValue", boolLabel(runtime.fakeLongTestUI));
  setText("debugApnFakeLongValue", boolLabel(runtime.apnFakeLong));

  setExactText(
    "debugSendStatus",
    pendingTest && Object.keys(pendingTest).length
      ? `최근 전송 · ${describeDebugCommand(pendingTest)}`
      : "전송 대기 중",
  );

  renderStatusCard("debugInfoGrid", [
    ["Onroad", runtime.onroad],
    ["Enabled", runtime.enabled],
    ["Engaged", runtime.engaged],
    ["Ignition", runtime.ignition],
    ["Controls Allowed", runtime.controlsAllowed],
    ["Safety Param", runtime.safetyParam],
    ["ACC Available", runtime.accAvailable],
    ["ACC Enabled", runtime.accEnabled],
    ["Armed", fakeLongDebug.armed],
    ["Paused", fakeLongDebug.paused],
    ["Current Set", fakeLongDebug.set],
    ["Target", fakeLongDebug.target],
    ["Commanded", fakeLongDebug.commanded],
    ["User Set", fakeLongDebug.userSet],
    ["Raw Set", fakeLongDebug.rawSet],
    ["Raw Target", fakeLongDebug.rawTarget],
    ["Test Bus", fakeLongDebug.testBus || pendingTest.bus || "-"],
    ["Test Mode", fakeLongDebug.testMode || pendingTest.mode || "-"],
    ["APN Active", fakeLongDebug.apnControlActive],
    ["APN Recovery", fakeLongDebug.apnRecoveryActive],
  ]);

  setExactText(
    "debugPendingSubtitle",
    pendingTest.sentAtMs ? `마지막 요청 ${formatTimeLabel(pendingTest.sentAtMs / 1000)}` : "마지막 요청 payload",
  );
  setExactText("debugPendingViewer", debug.pendingTestRaw || "{}");
  setExactText(
    "debugViewerSubtitle",
    fakeLongDebug.last ? `최근 버튼 ${String(fakeLongDebug.last).toUpperCase()}` : "memory param",
  );
  setExactText("debugFakeLongViewer", debug.fakeLongDebugRaw || "{}");
  renderDebugCommandConfig();
}

async function setCanMonitoringEnabled(enabled) {
  const next = Boolean(enabled);
  state.monitoring.canEnabled = next;
  persistMonitoringPreferences();

  if (!next) {
    state.canDebug = null;
    renderCanSignalTools();
    addLog("CAN 모니터링 중지", "상태 탭의 실시간 CAN/차량 상태 polling을 멈췄습니다.");
    renderRecentChanges();
    return;
  }

  renderCanSignalTools();
  try {
    await refreshCanDebugView();
    addLog("CAN 모니터링 시작", "실시간 CAN/차량 상태 후보 신호를 다시 읽기 시작했습니다.");
  } catch (error) {
    addLog("CAN 모니터링 시작 실패", error.message);
  }
  renderRecentChanges();
}

async function saveCanLabel(id, labelValue, entry) {
  const label = String(labelValue || "").trim();
  if (!id || !label) {
    addLog("CAN 라벨 저장 실패", "신호와 라벨 문구를 확인해 주세요.");
    renderRecentChanges();
    return;
  }

  try {
    await api("/api/can-labels", {
      method: "POST",
      body: JSON.stringify({
        id,
        label,
        meta: {
          title: entry.title,
          src: entry.src,
          address: entry.address,
          message: entry.message,
          signal: entry.signal,
        },
      }),
    });
    await refreshCanDebugOnly();
    addLog("CAN 라벨 저장", `${entry.title} → ${label}`);
    renderCanSignalTools();
    renderRecentChanges();
  } catch (error) {
    addLog("CAN 라벨 저장 실패", error.message);
    renderRecentChanges();
  }
}

async function deleteCanLabel(id) {
  if (!id) return;
  try {
    await api(`/api/can-labels/${encodeURIComponent(id)}`, { method: "DELETE" });
    await refreshCanDebugOnly();
    addLog("CAN 라벨 삭제", id);
    renderCanSignalTools();
    renderRecentChanges();
  } catch (error) {
    addLog("CAN 라벨 삭제 실패", error.message);
    renderRecentChanges();
  }
}

async function refreshDebugOnly() {
  state.debug = await api("/api/debug");
}

async function refreshDebugView() {
  if (debugRefreshInFlight) return;
  debugRefreshInFlight = true;
  try {
    await refreshDebugOnly();
    renderDebug();
  } catch (error) {
    addLog("디버그 갱신 실패", error.message);
    renderRecentChanges();
  } finally {
    debugRefreshInFlight = false;
  }
}

async function setDebugMonitoringEnabled(enabled) {
  const next = Boolean(enabled);
  state.monitoring.debugEnabled = next;
  persistMonitoringPreferences();
  renderDebugMonitorControls();

  if (!next) {
    addLog("디버그 실시간 보기 중지", "디버그 탭 자동 polling을 멈췄습니다.");
    renderRecentChanges();
    return;
  }

  try {
    await refreshDebugView();
    addLog("디버그 실시간 보기 시작", "디버그 탭 자동 polling을 다시 시작했습니다.");
  } catch (error) {
    addLog("디버그 실시간 보기 실패", error.message);
  }
  renderRecentChanges();
}

function currentDebugCommandPayload(button) {
  return {
    button,
    bus: "camera",
    mode: $("debugActionMode")?.value || "tap",
    repeats: Number($("debugRepeatCount")?.value || 1),
    holdFrames: Number($("debugHoldFrames")?.value || 0),
    note: `web-debug:${button}`,
  };
}

async function sendDebugButton(button) {
  const payload = currentDebugCommandPayload(button);
  const status = $("debugSendStatus");
  setExactText(status, `전송 중 · ${describeDebugCommand(payload)}`);
  try {
    const response = await api("/api/debug/fake-long-test", {
      method: "POST",
      body: JSON.stringify(payload),
    });
    state.debug = response.debug || state.debug;
    renderDebug();
    addLog("디버그 버튼 전송", describeDebugCommand(payload));
    renderRecentChanges();
  } catch (error) {
    setExactText(status, `전송 실패 · ${error.message}`);
    addLog("디버그 버튼 실패", `${String(button).toUpperCase()} · ${error.message}`);
    renderRecentChanges();
  }
}

function applyDebugPreset(button) {
  const { presetMode, presetRepeats, presetHold } = button.dataset;
  if ($("debugBusMode")) $("debugBusMode").value = "camera";
  if ($("debugActionMode") && presetMode) $("debugActionMode").value = presetMode;
  if ($("debugRepeatCount") && presetRepeats) $("debugRepeatCount").value = presetRepeats;
  if ($("debugHoldFrames") && presetHold) $("debugHoldFrames").value = presetHold;
  renderDebugCommandConfig();
}

function renderStats() {
  if (!state.stats) return;

  const available = Boolean(state.stats.available);
  const updatedLabel = state.stats.updatedAt
    ? new Date(state.stats.updatedAt * 1000).toLocaleTimeString("ko-KR", { hour12: false })
    : "-";

  $("statsTitle").textContent = available ? "FrogPilot 통계" : "통계 없음";
  $("statsSubtitle").textContent = available
    ? `누적 통계 ${state.stats.trackedTime} 기준 · 마지막 갱신 ${updatedLabel}`
    : "아직 수집된 FrogPilot 통계가 없습니다.";

  const availabilityChip = $("statsAvailabilityChip");
  availabilityChip.textContent = available ? "LIVE STATS" : "NO DATA";
  availabilityChip.className = `pill ${available ? "success" : "neutral"}`;

  const updatedChip = $("statsUpdatedChip");
  updatedChip.textContent = updatedLabel;
  updatedChip.className = "pill neutral";

  $("statsSummaryDrives").textContent = state.stats.summary?.drives || "-";
  $("statsSummaryDistance").textContent = state.stats.summary?.distance || "-";
  $("statsSummaryTime").textContent = state.stats.summary?.time || "-";
  $("statsSummaryCaption").textContent = available
    ? "핵심 지표와 카테고리별 통계를 한눈에 볼 수 있도록 재구성한 대시보드"
    : "기기에서 통계 수집 전";

  const pulse = state.stats.pulse || {};
  $("statsPulsePrimaryLabel").textContent = pulse.primaryLabel || "총 활성화";
  $("statsPulsePrimaryValue").textContent = pulse.primaryValue || "-";
  $("statsPulseSecondaryLabel").textContent = pulse.secondaryLabel || "긴급 제동 경고";
  $("statsPulseSecondaryValue").textContent = pulse.secondaryValue || "-";
  $("statsPulseNoteLabel").textContent = pulse.noteLabel || "Overview";
  $("statsHeroFocus").textContent = pulse.noteText || (available
    ? `${(state.stats.sections || []).length}개 섹션 통계를 확인할 수 있습니다.`
    : "아직 누적 통계가 없습니다.");

  const summaryGrid = $("statsSummaryGrid");
  summaryGrid.innerHTML = "";
  const summaryCards = state.stats.summary?.cards || [];
  const groupedSummaryCards = [];
  for (let i = 0; i < summaryCards.length; i += 2) {
    groupedSummaryCards.push(summaryCards.slice(i, i + 2));
  }

  groupedSummaryCards.forEach((group) => {
    const tones = group.map((item) => item.tone).filter(Boolean);
    const primaryTone = tones[0];
    const card = document.createElement("div");
    card.className = `stats-summary-card stats-summary-card-double ${primaryTone ? `stats-item-${primaryTone}` : ""}`.trim();
    card.innerHTML = group.map((item, index) => {
      const toneLabel = item.tone === "warning" ? "Alert" : item.tone === "accent" ? "Highlight" : item.tone === "success" ? "Live" : "Info";
      return `
        <div class="stats-summary-stat ${index > 0 ? "is-secondary" : ""}">
          <div class="stats-summary-card-head">
            <span>${item.label}</span>
            <em class="stats-tone-pill">${toneLabel}</em>
          </div>
          <strong>${formatValue(item.value)}</strong>
        </div>
      `;
    }).join("");
    summaryGrid.appendChild(card);
  });

  const sectionGrid = $("statsSectionGrid");
  sectionGrid.innerHTML = "";

  (state.stats.sections || []).forEach((section) => {
    const card = document.createElement("section");
    card.className = `stats-section-card stats-section-${section.id}`;

    const header = document.createElement("div");
    header.className = "stats-section-header";
    header.innerHTML = `<div><p class="panel-kicker">${section.id.toUpperCase()}</p><h3>${section.title}</h3></div><span class="panel-caption">${(section.items || []).length}개 항목</span>`;

    const items = Array.isArray(section.items) ? section.items : [];
    const [spotlightItem, ...restItems] = items;

    let spotlight = null;
    if (spotlightItem) {
      spotlight = document.createElement("div");
      spotlight.className = `stats-spotlight ${spotlightItem.tone ? `stats-item-${spotlightItem.tone}` : ""}`.trim();
      spotlight.innerHTML = `
        <span>${spotlightItem.label}</span>
        <strong>${formatValue(spotlightItem.value)}</strong>
      `;
    }

    const grid = document.createElement("div");
    grid.className = `stats-item-grid ${restItems.length <= 2 ? "is-compact" : ""}`.trim();

    restItems.forEach((item) => {
      const row = document.createElement("div");
      row.className = `stats-item ${item.tone ? `stats-item-${item.tone}` : ""}`.trim();
      row.innerHTML = `<span>${item.label}</span><strong>${formatValue(item.value)}</strong>`;
      grid.appendChild(row);
    });

    card.appendChild(header);
    if (spotlight) {
      card.appendChild(spotlight);
    }
    card.appendChild(grid);
    sectionGrid.appendChild(card);
  });
}

function renderLogs() {
  if (!state.logs) return;

  $("logSourceValue").textContent = state.logs.source;
  $("logStatusValue").textContent = state.logs.status;
  $("logUpdatedValue").textContent = state.logs.updatedAt
    ? new Date(state.logs.updatedAt * 1000).toLocaleTimeString("ko-KR", { hour12: false })
    : "-";
  $("logLinesValue").textContent = state.logs.lineCount ?? 0;
  $("logViewerTitle").textContent = state.logs.title;
  $("logViewerSubtitle").textContent = state.logs.subtitle;
  $("logViewer").textContent = state.logs.content;

  const wrap = $("logSourceButtons");
  wrap.innerHTML = "";
  (state.logs.sources || []).forEach((source) => {
    const button = document.createElement("button");
    button.className = `log-source-button ${state.activeLogSource === source.id ? "active" : ""}`;
    button.textContent = source.title;
    button.addEventListener("click", async () => {
      state.activeLogSource = source.id;
      await refreshLogsOnly();
      renderLogs();
    });
    wrap.appendChild(button);
  });
}

function renderParamsSummary() {
  const caption = $("paramSummaryCaption");
  if (caption && state.meta) {
    caption.textContent = `전체 ${state.meta.totalKeys}개 · 현재 값 ${state.meta.presentKeys}개 · FrogPilot ${state.meta.frogpilotKeys}개`;
  }
}

function createBadge(text, extraClass = "") {
  const badge = document.createElement("span");
  badge.className = `param-badge ${extraClass}`.trim();
  badge.textContent = text;
  return badge;
}

function createResetButton(param) {
  const button = document.createElement("button");
  button.className = "mini-button mini-button-muted";
  button.textContent = "기본값";
  button.addEventListener("click", async (event) => {
    event.stopPropagation();
    button.disabled = true;
    try {
      await api(`/api/params/${encodeURIComponent(param.key)}`, { method: "DELETE" });
      await refreshParamsOnly();
      addLog("기본값 복구", `${param.key} 값을 기본 상태로 되돌렸습니다.`);
      await refreshStatusOnly();
      renderAll();
    } catch (error) {
      await refreshParamsOnly();
      addLog("복구 실패", `${param.key} · ${error.message}`);
      renderAll();
    } finally {
      button.disabled = false;
    }
  });
  return button;
}

function createEditor(param) {
  const wrapper = document.createElement("div");
  wrapper.className = "param-editor";

  if (param.inputKind === "toggle") {
    const toggle = document.createElement("button");
    toggle.className = "toggle";
    toggle.dataset.on = String(Boolean(param.boolValue));
    toggle.addEventListener("click", async (event) => {
      event.stopPropagation();
      const nextValue = !param.boolValue;
      toggle.disabled = true;
      toggle.dataset.on = String(nextValue);
      setParamLocalValue(param.key, {
        present: true,
        boolValue: nextValue,
        valueText: nextValue ? "1" : "0",
      });
      renderParams();
      try {
        await api(`/api/params/${encodeURIComponent(param.key)}`, {
          method: "POST",
          body: JSON.stringify({ value: nextValue }),
        });
        await refreshParamsOnly();
        addLog("토글 변경", `${param.key} → ${nextValue ? "ON" : "OFF"}`);
        await refreshStatusOnly();
        renderAll();
      } catch (error) {
        await refreshParamsOnly();
        addLog("저장 실패", `${param.key} · ${error.message}`);
        renderAll();
      } finally {
        toggle.disabled = false;
      }
    });
    wrapper.appendChild(toggle);
    wrapper.appendChild(createResetButton(param));
    return wrapper;
  }

  const input = param.inputKind === "textarea" ? document.createElement("textarea") : document.createElement("input");
  input.className = param.inputKind === "textarea" ? "param-textarea" : "param-input";
  if (param.inputKind !== "textarea") input.type = "text";
  input.value = param.valueText ?? "";
  input.addEventListener("click", (event) => event.stopPropagation());

  const saveButton = document.createElement("button");
  saveButton.className = "mini-button";
  saveButton.textContent = "저장";
  saveButton.addEventListener("click", async (event) => {
    event.stopPropagation();
    saveButton.disabled = true;
    try {
      await api(`/api/params/${encodeURIComponent(param.key)}`, {
        method: "POST",
        body: JSON.stringify({
          value: input.value,
          mode: param.editorMode,
        }),
      });
      await refreshParamsOnly();
      addLog("저장 완료", `${param.key} 값을 저장했습니다.`);
      await refreshStatusOnly();
      renderAll();
    } catch (error) {
      await refreshParamsOnly();
      addLog("저장 실패", `${param.key} · ${error.message}`);
      renderAll();
    } finally {
      saveButton.disabled = false;
    }
  });

  wrapper.appendChild(input);
  wrapper.appendChild(saveButton);
  wrapper.appendChild(createResetButton(param));
  return wrapper;
}

function renderParams() {
  const list = $("paramList");
  list.innerHTML = "";

  const visible = filteredParams();
  $("paramSummaryCaption").textContent = `전체 ${state.meta?.totalKeys ?? 0}개 · 현재 필터 ${visible.length}개`;

  visible.forEach((param) => {
    const item = document.createElement("div");
    item.className = `param-item ${state.selectedKey === param.key ? "selected" : ""}`;
    item.addEventListener("click", () => {
      state.selectedKey = param.key;
      renderParams();
    });

    const meta = document.createElement("div");
    meta.className = "param-meta";

    const head = document.createElement("div");
    head.className = "param-item-head";
    head.innerHTML = `
      <div>
        <h4>${param.key}</h4>
        <p>${param.flags}</p>
      </div>
    `;

    const badges = document.createElement("div");
    badges.className = "param-badges";
    badges.appendChild(createBadge(param.scope));
    badges.appendChild(createBadge(param.type));
    badges.appendChild(createBadge(param.present ? "stored" : "default", param.present ? "param-badge-live" : ""));
    if (param.editorMode === "hex") badges.appendChild(createBadge("hex", "param-badge-warn"));
    head.appendChild(badges);

    const detail = document.createElement("div");
    detail.className = "param-detail-line";
    detail.innerHTML = `<span>기본값</span><strong>${param.defaultValue ?? "(없음)"}</strong>`;

    meta.appendChild(head);
    meta.appendChild(detail);
    item.appendChild(meta);
    item.appendChild(createEditor(param));
    list.appendChild(item);
  });
}

function renderRecentChanges() {
  const feed = $("eventFeed");
  if (!feed) return;
  feed.innerHTML = "";
  const rows = state.recentChanges.length
    ? state.recentChanges
    : [{ time: "-", title: "변경 없음", detail: "아직 웹에서 저장한 설정이 없습니다." }];

  rows.forEach((event) => {
    const item = document.createElement("li");
    item.innerHTML = `<time>${event.time}</time><strong>${event.title}</strong><span>${event.detail}</span>`;
    feed.appendChild(item);
  });
}

async function refreshStatusOnly() {
  const detail = state.ui.statusDetailsExpanded ? "full" : "lite";
  state.status = await api(`/api/status?detail=${detail}`);
}

async function refreshCanDebugOnly() {
  if (!state.monitoring.canEnabled) {
    state.canDebug = null;
    return;
  }
  state.canDebug = await api("/api/can-debug");
}

async function refreshMetaOnly() {
  state.meta = await api("/api/meta");
}

async function refreshParamsOnly() {
  const paramsPayload = await api("/api/params");
  state.params = paramsPayload.params;
}

async function refreshLogsOnly() {
  state.logs = await api(`/api/logs?source=${encodeURIComponent(state.activeLogSource)}`);
}

async function refreshStatsOnly() {
  state.stats = await api("/api/stats");
}

async function refreshLiveStatusView() {
  if (statusRefreshInFlight) return;
  statusRefreshInFlight = true;
  try {
    await refreshStatusOnly();
    renderStatusHero();
    renderStatusPanels();
  } catch (error) {
    addLog("상태 갱신 실패", error.message);
    renderRecentChanges();
  } finally {
    statusRefreshInFlight = false;
  }
}

async function refreshCanDebugView() {
  if (canRefreshInFlight) return;
  canRefreshInFlight = true;
  try {
    await refreshCanDebugOnly();
    renderCanSignalTools();
  } catch (error) {
    addLog("CAN 디버그 갱신 실패", error.message);
    renderRecentChanges();
  } finally {
    canRefreshInFlight = false;
  }
}

async function refreshMetaView() {
  if (metaRefreshInFlight) return;
  metaRefreshInFlight = true;
  try {
    await refreshMetaOnly();
    renderHero();
    renderParamsSummary();
  } catch (error) {
    addLog("메타 갱신 실패", error.message);
    renderRecentChanges();
  } finally {
    metaRefreshInFlight = false;
  }
}

async function loadAll() {
  const [meta] = await Promise.all([
    api("/api/meta"),
    refreshStatusOnly(),
  ]);
  state.meta = meta;
  renderAll();
}

function renderAll() {
  renderHero();
  renderStatusHero();
  renderStatusPanels();
  renderDebug();
  renderStats();
  renderLogs();
  renderParamsSummary();
  renderParams();
  renderRecentChanges();
}

function bindFilters() {
  $("paramSearch").addEventListener("input", (event) => {
    state.filters.search = event.target.value;
    renderParams();
  });
  $("scopeFilter").addEventListener("change", (event) => {
    state.filters.scope = event.target.value;
    renderParams();
  });
  $("typeFilter").addEventListener("change", (event) => {
    state.filters.type = event.target.value;
    renderParams();
  });
  $("presentOnly").addEventListener("change", (event) => {
    state.filters.presentOnly = event.target.checked;
    renderParams();
  });
  $("changedOnly").addEventListener("change", (event) => {
    state.filters.changedOnly = event.target.checked;
    renderParams();
  });
}

function bindActions() {
  $("refreshButton").addEventListener("click", async () => {
    await Promise.all([refreshMetaOnly(), refreshStatusOnly()]);
    if (state.activeTab === "status" && state.monitoring.canEnabled && state.ui.statusDetailsExpanded) {
      await refreshCanDebugView();
    } else if (state.activeTab === "debug") {
      await refreshDebugOnly();
    } else if (state.activeTab === "stats") {
      await refreshStatsOnly();
    } else if (state.activeTab === "query") {
      await refreshLogsOnly();
    } else if (state.activeTab === "settings") {
      await refreshParamsOnly();
      if (!state.selectedKey && state.params.length) {
        state.selectedKey = state.params[0].key;
      }
    }
    addLog("새로고침", "상태와 params를 다시 읽었습니다.");
    renderAll();
  });

  $("toggleStatusDetailsButton").addEventListener("click", async () => {
    state.ui.statusDetailsExpanded = !state.ui.statusDetailsExpanded;
    persistUiPreferences();
    renderStatusPanels();
    if (state.activeTab !== "status") return;
    await refreshLiveStatusView();
    if (state.monitoring.canEnabled && state.ui.statusDetailsExpanded) {
      await refreshCanDebugView();
    } else {
      renderCanSignalTools();
    }
  });

  $("saveApnLabelButton").addEventListener("click", async () => {
    await saveCurrentApnLabel();
  });

  $("deleteApnLabelButton").addEventListener("click", async () => {
    await deleteApnLabel();
  });

  $("toggleCanMonitorButton").addEventListener("click", async () => {
    await setCanMonitoringEnabled(!state.monitoring.canEnabled);
  });

  $("refreshDebugButton").addEventListener("click", async () => {
    await refreshDebugView();
  });

  $("toggleDebugMonitorButton").addEventListener("click", async () => {
    await setDebugMonitoringEnabled(!state.monitoring.debugEnabled);
  });

  ["debugBusMode", "debugActionMode", "debugRepeatCount", "debugHoldFrames"].forEach((id) => {
    $(id).addEventListener("change", () => {
      renderDebugCommandConfig();
    });
  });

  document.querySelectorAll(".debug-preset-button").forEach((button) => {
    button.addEventListener("click", () => {
      applyDebugPreset(button);
    });
  });

  $("debugButtonMain").addEventListener("click", async () => {
    await sendDebugButton("main");
  });
  $("debugButtonCancel").addEventListener("click", async () => {
    await sendDebugButton("cancel");
  });
  $("debugButtonRes").addEventListener("click", async () => {
    await sendDebugButton("res");
  });
  $("debugButtonSet").addEventListener("click", async () => {
    await sendDebugButton("set");
  });
  $("debugButtonUnpress").addEventListener("click", async () => {
    await sendDebugButton("unpress");
  });

  $("apnLabelInput").addEventListener("keydown", async (event) => {
    if (event.key === "Enter") {
      event.preventDefault();
      await saveCurrentApnLabel();
    }
  });

  $("refreshLogsButton").addEventListener("click", async () => {
    await refreshLogsOnly();
    addLog("로그 새로고침", `${state.activeLogSource} 로그를 다시 읽었습니다.`);
    renderLogs();
  });

  $("showAllButton").addEventListener("click", () => {
    state.filters.presentOnly = false;
    state.filters.changedOnly = false;
    $("presentOnly").checked = false;
    $("changedOnly").checked = false;
    renderParams();
    setActiveTab("settings");
  });

  $("showPresentButton").addEventListener("click", () => {
    state.filters.presentOnly = true;
    state.filters.changedOnly = false;
    $("presentOnly").checked = true;
    $("changedOnly").checked = false;
    renderParams();
    setActiveTab("settings");
  });

  $("showChangedButton").addEventListener("click", () => {
    state.filters.presentOnly = false;
    state.filters.changedOnly = true;
    $("presentOnly").checked = false;
    $("changedOnly").checked = true;
    renderParams();
    setActiveTab("settings");
  });

  $("resetFiltersButton").addEventListener("click", () => {
    state.filters = {
      search: "",
      scope: "all",
      type: "all",
      presentOnly: false,
      changedOnly: false,
    };
    $("paramSearch").value = "";
    $("scopeFilter").value = "all";
    $("typeFilter").value = "all";
    $("presentOnly").checked = false;
    $("changedOnly").checked = false;
    renderParams();
  });

  document.querySelectorAll(".tab-button").forEach((button) => {
    button.addEventListener("click", () => setActiveTab(button.dataset.tab));
  });
}

function startAutoRefresh() {
  if (!liveStatusTimer) {
    liveStatusTimer = window.setInterval(() => {
      if (state.activeTab === "status") {
        refreshLiveStatusView();
      }
    }, 5000);
  }

  if (!canRefreshTimer) {
    canRefreshTimer = window.setInterval(() => {
      if (state.activeTab === "status" && state.monitoring.canEnabled && state.ui.statusDetailsExpanded) {
        refreshCanDebugView();
      }
    }, 5000);
  }

  if (!debugRefreshTimer) {
    debugRefreshTimer = window.setInterval(() => {
      if (state.activeTab === "debug" && state.monitoring.debugEnabled) {
        refreshDebugView();
      }
    }, 5000);
  }

  if (!metaRefreshTimer) {
    metaRefreshTimer = window.setInterval(() => {
      refreshMetaView();
    }, 30000);
  }

  if (!statsRefreshTimer) {
    statsRefreshTimer = window.setInterval(() => {
      if (state.activeTab === "stats" && !statsRefreshInFlight) {
        statsRefreshInFlight = true;
        refreshStatsOnly().then(renderStats).catch((error) => {
          addLog("통계 갱신 실패", error.message);
          renderRecentChanges();
        }).finally(() => {
          statsRefreshInFlight = false;
        });
      }
    }, 30000);
  }

  if (!logsRefreshTimer) {
    logsRefreshTimer = window.setInterval(() => {
      if (state.activeTab === "query" && !logsRefreshInFlight) {
        logsRefreshInFlight = true;
        refreshLogsOnly().then(renderLogs).catch((error) => {
          addLog("로그 갱신 실패", error.message);
          renderRecentChanges();
        }).finally(() => {
          logsRefreshInFlight = false;
        });
      }
    }, 10000);
  }

  document.addEventListener("visibilitychange", () => {
    if (!document.hidden) {
      if (state.activeTab === "status") {
        refreshLiveStatusView();
        if (state.monitoring.canEnabled && state.ui.statusDetailsExpanded) {
          refreshCanDebugView();
        }
      }
      if (state.activeTab === "debug") {
        refreshDebugView();
      }
      refreshMetaView();
      if (state.activeTab === "stats") {
        refreshStatsOnly().then(renderStats).catch(() => {});
      }
      if (state.activeTab === "query") {
        refreshLogsOnly().then(renderLogs).catch(() => {});
      }
    }
  });
}

loadMonitoringPreferences();
loadUiPreferences();
bindFilters();
bindActions();
const initialTab = ["status", "settings", "stats", "query"].includes(window.location.hash.slice(1))
  || window.location.hash.slice(1) === "debug"
  ? window.location.hash.slice(1)
  : "status";
state.activeTab = initialTab;
loadAll()
  .then(() => {
    setActiveTab(state.activeTab);
    if (state.monitoring.canEnabled) {
      return refreshCanDebugView().catch(() => {});
    }
    renderCanSignalTools();
    return null;
  })
  .catch((error) => {
    addLog("초기화 실패", error.message);
    renderRecentChanges();
  });
startAutoRefresh();

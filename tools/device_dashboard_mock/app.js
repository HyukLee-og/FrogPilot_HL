const state = {
  meta: null,
  status: null,
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
};

let liveStatusTimer = null;
let metaRefreshTimer = null;
let statsRefreshTimer = null;
let logsRefreshTimer = null;

const $ = (id) => document.getElementById(id);

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

function setActiveTab(nextTab) {
  state.activeTab = nextTab;
  document.querySelectorAll(".tab-button").forEach((button) => {
    button.classList.toggle("active", button.dataset.tab === nextTab);
  });
  document.querySelectorAll(".tab-panel").forEach((panel) => {
    panel.classList.toggle("active", panel.dataset.panel === nextTab);
  });

  if (nextTab === "stats") {
    refreshStatsOnly().then(renderStats).catch(() => {});
  } else if (nextTab === "query") {
    refreshLogsOnly().then(renderLogs).catch(() => {});
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
    label.textContent = state.meta?.modeLabel || "Device Console Connected";
    detail.textContent = state.meta ? `${state.meta.branch} · prefix ${state.meta.prefix}` : "연결됨";
    return;
  }

  card.classList.add("offline");
  dot.className = "dot offline";
  label.textContent = "기기 연결 끊김";
  detail.textContent = state.connection.lastError || "응답 없음";
}

function renderHero() {
  renderConnection();
  if (!state.meta) return;

  $("connectionLabel").textContent = state.meta.modeLabel;
  $("deviceLabel").textContent = `${state.meta.branch} · prefix ${state.meta.prefix}`;
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
}

function renderStatusHero() {
  if (!state.status) return;
  const runtime = state.status.runtime;
  const updatedLabel = runtime.updatedAt
    ? `마지막 갱신 ${new Date(runtime.updatedAt * 1000).toLocaleTimeString("ko-KR", { hour12: false })}`
    : "";

  $("statusVehicleTitle").textContent = runtime.vehicleDisplayName;
  $("statusVehicleSubtitle").textContent = [runtime.subtitle, updatedLabel].filter(Boolean).join(" · ");
  $("statusVoltageValue").textContent = formatValue(runtime.carVoltage);
  $("statusSpeedValue").textContent = formatValue(runtime.vehicleSpeed);
  $("statusAccValue").textContent = formatValue(runtime.accSpeed);
  $("statusOpenpilotValue").textContent = formatValue(runtime.openpilotState);

  const enabledChip = $("statusOpenpilotChip");
  enabledChip.textContent = runtime.enabledToggle ? "ENABLED" : "DISABLED";
  enabledChip.className = `pill ${runtime.enabledToggle ? "success" : "neutral"}`;

  const onroadChip = $("statusOnroadChip");
  onroadChip.textContent = runtime.isOnroad ? "ONROAD" : "OFFROAD";
  onroadChip.className = `pill ${runtime.isOnroad ? "success" : "neutral"}`;

  const engagedChip = $("statusEngagedChip");
  engagedChip.textContent = runtime.isEngaged ? "ENGAGED" : "DISENGAGED";
  engagedChip.className = `pill ${runtime.isEngaged ? "success" : "neutral"}`;
}

function renderStatusCard(gridId, entries) {
  const grid = $(gridId);
  grid.innerHTML = "";
  entries.forEach(([label, value]) => {
    const card = document.createElement("div");
    card.className = "info-card";
    card.innerHTML = `<span>${label}</span><strong>${formatValue(value)}</strong>`;
    grid.appendChild(card);
  });
}

function renderStatusPanels() {
  if (!state.status) return;

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
    ? "FrogPilotStats를 카테고리별로 정리한 누적 통계"
    : "기기에서 통계 수집 전";

  const summaryGrid = $("statsSummaryGrid");
  summaryGrid.innerHTML = "";
  (state.stats.summary?.cards || []).forEach((item) => {
    const card = document.createElement("div");
    card.className = `stats-summary-card ${item.tone ? `stats-item-${item.tone}` : ""}`.trim();
    card.innerHTML = `<span>${item.label}</span><strong>${formatValue(item.value)}</strong>`;
    summaryGrid.appendChild(card);
  });

  const sectionGrid = $("statsSectionGrid");
  sectionGrid.innerHTML = "";

  (state.stats.sections || []).forEach((section) => {
    const card = document.createElement("section");
    card.className = "stats-section-card";

    const header = document.createElement("div");
    header.className = "stats-section-header";
    header.innerHTML = `<div><p class="panel-kicker">STATS</p><h3>${section.title}</h3></div><span class="panel-caption">${(section.items || []).length}개 항목</span>`;

    const grid = document.createElement("div");
    grid.className = "stats-item-grid";

    (section.items || []).forEach((item) => {
      const row = document.createElement("div");
      row.className = `stats-item ${item.tone ? `stats-item-${item.tone}` : ""}`.trim();
      row.innerHTML = `<span>${item.label}</span><strong>${formatValue(item.value)}</strong>`;
      grid.appendChild(row);
    });

    card.appendChild(header);
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
  state.status = await api("/api/status");
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
  try {
    await refreshStatusOnly();
    renderStatusHero();
    renderStatusPanels();
  } catch (error) {
    addLog("상태 갱신 실패", error.message);
    renderRecentChanges();
  }
}

async function refreshMetaView() {
  try {
    await refreshMetaOnly();
    renderHero();
    renderParamsSummary();
  } catch (error) {
    addLog("메타 갱신 실패", error.message);
    renderRecentChanges();
  }
}

async function loadAll() {
  const [meta, status, stats, logs, paramsPayload] = await Promise.all([
    api("/api/meta"),
    api("/api/status"),
    api("/api/stats"),
    api(`/api/logs?source=${encodeURIComponent(state.activeLogSource)}`),
    api("/api/params"),
  ]);
  state.meta = meta;
  state.status = status;
  state.stats = stats;
  state.logs = logs;
  state.params = paramsPayload.params;
  if (!state.selectedKey && state.params.length) {
    state.selectedKey = state.params[0].key;
  }
  renderAll();
}

function renderAll() {
  renderHero();
  renderStatusHero();
  renderStatusPanels();
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
    await loadAll();
    addLog("새로고침", "상태와 params를 다시 읽었습니다.");
    renderAll();
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
      refreshLiveStatusView();
    }, 1000);
  }

  if (!metaRefreshTimer) {
    metaRefreshTimer = window.setInterval(() => {
      refreshMetaView();
    }, 5000);
  }

  if (!statsRefreshTimer) {
    statsRefreshTimer = window.setInterval(() => {
      if (state.activeTab === "stats") {
        refreshStatsOnly().then(renderStats).catch((error) => {
          addLog("통계 갱신 실패", error.message);
          renderRecentChanges();
        });
      }
    }, 10000);
  }

  if (!logsRefreshTimer) {
    logsRefreshTimer = window.setInterval(() => {
      if (state.activeTab === "query") {
        refreshLogsOnly().then(renderLogs).catch((error) => {
          addLog("로그 갱신 실패", error.message);
          renderRecentChanges();
        });
      }
    }, 2000);
  }

  document.addEventListener("visibilitychange", () => {
    if (!document.hidden) {
      refreshLiveStatusView();
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

bindFilters();
bindActions();
loadAll().catch((error) => {
  addLog("초기화 실패", error.message);
  renderRecentChanges();
});
startAutoRefresh();

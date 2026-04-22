const STORAGE_KEY = 'ab-demo-client-config';
const DEFAULTS = {
  apiKey: '',
  externalUserId: '',
  deviceId: `demo-device-${Math.random().toString(16).slice(2)}`,
  flagKey: 'demo_button_text',
  featureKey: 'demo_button_click',
};

const elements = {
  apiKey: document.querySelector('#api-key'),
  externalUserId: document.querySelector('#external-user-id'),
  deviceId: document.querySelector('#device-id'),
  flagKey: document.querySelector('#flag-key'),
  featureKey: document.querySelector('#feature-key'),
  testId: document.querySelector('#test-id'),
  configForm: document.querySelector('#config-form'),
  reloadConfig: document.querySelector('#reload-config'),
  clearTestId: document.querySelector('#clear-test-id'),
  demoButton: document.querySelector('#demo-button'),
  localClicks: document.querySelector('#local-clicks'),
  lastReqId: document.querySelector('#last-req-id'),
  assignmentJson: document.querySelector('#assignment-json'),
  eventJson: document.querySelector('#event-json'),
  statusBadge: document.querySelector('#status-badge'),
  assignmentBadge: document.querySelector('#assignment-badge'),
};

let state = {
  config: loadConfig(),
  localClicks: 0,
  assignment: null,
  lastEvent: null,
  currentReqId: '',
};

function loadConfig() {
  const raw = window.localStorage.getItem(STORAGE_KEY);
  if (!raw) {
    return { ...DEFAULTS };
  }

  try {
    return { ...DEFAULTS, ...JSON.parse(raw) };
  } catch (error) {
    return { ...DEFAULTS };
  }
}

function saveConfig() {
  window.localStorage.setItem(STORAGE_KEY, JSON.stringify(state.config));
}

function getTestIdFromUrl() {
  return new URL(window.location.href).searchParams.get('test-id') ?? '';
}

function setTestIdInUrl(testId) {
  const url = new URL(window.location.href);
  if (testId) {
    url.searchParams.set('test-id', testId);
  } else {
    url.searchParams.delete('test-id');
  }
  window.history.replaceState({}, '', url);
}

function syncFormFromState() {
  elements.apiKey.value = state.config.apiKey;
  elements.externalUserId.value = state.config.externalUserId;
  elements.deviceId.value = state.config.deviceId;
  elements.flagKey.value = state.config.flagKey;
  elements.featureKey.value = state.config.featureKey;
  elements.testId.value = getTestIdFromUrl();
}

function setBadge(element, text, variant) {
  element.textContent = text;
  element.className = `badge ${variant}`;
}

function setStatus(text, ok = true) {
  setBadge(elements.statusBadge, text, ok ? 'badge-ok' : 'badge-error');
}

function setAssignmentState(assignment) {
  if (!assignment) {
    setBadge(elements.assignmentBadge, 'not assigned', 'badge-waiting');
    return;
  }
  setBadge(
    elements.assignmentBadge,
    `${assignment.variant_name} (#${assignment.variant_id})`,
    'badge-ok'
  );
}

function render() {
  elements.localClicks.textContent = String(state.localClicks);
  elements.lastReqId.textContent = state.currentReqId || 'n/a';
  elements.assignmentJson.textContent = JSON.stringify(state.assignment ?? {}, null, 2);
  elements.eventJson.textContent = JSON.stringify(state.lastEvent ?? {}, null, 2);

  const flagValue =
    state.assignment?.flag_values?.[state.config.flagKey] ??
    state.assignment?.flags?.find((flag) => flag.flag_key === state.config.flagKey)?.value ??
    'Click me';
  elements.demoButton.textContent = flagValue || 'Click me';

  setAssignmentState(state.assignment?.assignments?.[0]);
}

function nextReqId() {
  return `demo-${Date.now()}-${Math.random().toString(16).slice(2)}`;
}

async function abRequest(path, payload) {
  const response = await fetch(`/ab-api${path}`, {
    method: 'POST',
    headers: {
      'Content-Type': 'application/json',
      'X-API-Key': state.config.apiKey,
    },
    body: JSON.stringify(payload),
  });

  const text = await response.text();
  const data = text ? JSON.parse(text) : {};
  if (!response.ok) {
    throw new Error(data?.message || `${path} failed`);
  }
  return data;
}

async function loadAssignment() {
  if (!state.config.apiKey) {
    setStatus('Paste API key first', false);
    return;
  }

  const reqId = nextReqId();
  state.currentReqId = reqId;

  const payload = {
    req_id: reqId,
    external_user_id: state.config.externalUserId,
    device_id: state.config.deviceId,
    ip_address: '127.0.0.1',
  };
  const forcedTestId = getTestIdFromUrl();
  if (forcedTestId) {
    payload.test_id = forcedTestId;
  }

  try {
    const data = await abRequest('/v1/runtime/flags', payload);
    state.assignment = data;
    setStatus(
      data.assignments_count > 0
        ? `Assignment loaded (config v${data.config_version || 0})`
        : 'No active assignment returned',
      data.assignments_count > 0
    );
  } catch (error) {
    setStatus(error.message, false);
  }

  render();
}

async function sendClickEvent() {
  if (!state.assignment) {
    await loadAssignment();
    if (!state.assignment) {
      return;
    }
  }

  try {
    const data = await abRequest('/v1/runtime/events', {
      feature_key: state.config.featureKey,
      req_id: state.currentReqId,
      external_user_id: state.config.externalUserId,
      device_id: state.config.deviceId,
      ip_address: '127.0.0.1',
      occurred_at: new Date().toISOString(),
      value: 1,
      properties: {
        source: 'demo-client',
        variant_id: state.assignment?.assignments?.[0]?.variant_id ?? null,
        variant_key: state.assignment?.assignments?.[0]?.variant_key ?? null,
        test_id: getTestIdFromUrl() || null,
      },
    });
    state.localClicks += 1;
    state.lastEvent = data;
    setStatus('Click event sent', true);
  } catch (error) {
    setStatus(error.message, false);
  }

  render();
}

elements.configForm.addEventListener('submit', async (event) => {
  event.preventDefault();
  state.config = {
    apiKey: elements.apiKey.value.trim(),
    externalUserId: elements.externalUserId.value.trim(),
    deviceId: elements.deviceId.value.trim(),
    flagKey: elements.flagKey.value.trim() || DEFAULTS.flagKey,
    featureKey: elements.featureKey.value.trim() || DEFAULTS.featureKey,
  };
  saveConfig();
  setTestIdInUrl(elements.testId.value.trim());
  await loadAssignment();
});

elements.reloadConfig.addEventListener('click', loadAssignment);
elements.clearTestId.addEventListener('click', async () => {
  elements.testId.value = '';
  setTestIdInUrl('');
  await loadAssignment();
});
elements.demoButton.addEventListener('click', sendClickEvent);

if (!state.config.externalUserId) {
  state.config.externalUserId = `demo-user-${Math.random().toString(16).slice(2)}`;
  saveConfig();
}

syncFormFromState();
render();

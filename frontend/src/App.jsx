import { useEffect, useMemo, useState } from 'react';

const SESSION_TOKEN_KEY = 'ab-experiments-session-token';
const SESSION_USER_KEY = 'ab-experiments-session-user';

const initialRegisterForm = {
  service_name: '',
  service_description: '',
  admin_email: '',
  admin_full_name: '',
  password: '',
};

const initialLoginForm = {
  email: '',
  password: '',
  client_service_id: '',
};

const initialCreateUserForm = {
  email: '',
  full_name: '',
  password: '',
  role_code: 'DEVELOPER',
};

const initialCreateApiKeyForm = {
  name: '',
};

const initialCreateFlagForm = {
  key: '',
  name: '',
  description: '',
  value_type: 'BOOL',
  default_value: '',
};

const initialCreateFeatureForm = {
  key: '',
  name: '',
  description: '',
};

const initialCreateMetricForm = {
  code: '',
  name: '',
  description: '',
  metric_type: 'COUNT',
  feature_key: '',
};

const roleOptions = ['ADMIN', 'ANALYST', 'DEVELOPER'];
const flagTypeOptions = ['BOOL', 'STRING', 'INT', 'JSON'];
const metricTypeOptions = ['COUNT', 'SUM', 'MEAN'];
const experimentActionLabels = {
  send_to_queue: 'Send to queue',
  add_to_config: 'Mark as added in config',
  start: 'Mark as running',
  complete: 'Mark as completed',
  request_decision: 'Move to pending decision',
  close: 'Close experiment',
};

function createLocalId() {
  return `${Date.now()}-${Math.random().toString(16).slice(2)}`;
}

function createExperimentFlagDraft(overrides = {}) {
  return {
    client_id: createLocalId(),
    flag_id: '',
    variant_value: '',
    ...overrides,
  };
}

function createExperimentVariantDraft(overrides = {}) {
  return {
    client_id: createLocalId(),
    key: '',
    name: '',
    description: '',
    flags: [createExperimentFlagDraft()],
    ...overrides,
  };
}

function createDefaultExperimentForm() {
  return {
    name: '',
    description: '',
    layer_name: '',
    layer_description: '',
    duration_days: '14',
    variant_traffic_percent: '10',
    variants: [
      createExperimentVariantDraft({
        key: 'control',
        name: 'Control',
        flags: [createExperimentFlagDraft()],
      }),
      createExperimentVariantDraft({
        key: 'test',
        name: 'Test',
        flags: [createExperimentFlagDraft()],
      }),
    ],
  };
}

function readStoredJson(key) {
  const value = window.localStorage.getItem(key);
  if (!value) {
    return null;
  }

  try {
    return JSON.parse(value);
  } catch (error) {
    window.localStorage.removeItem(key);
    return null;
  }
}

function readStoredUser() {
  return readStoredJson(SESSION_USER_KEY);
}

async function readJson(response) {
  const text = await response.text();
  if (!text) {
    return null;
  }

  try {
    return JSON.parse(text);
  } catch (error) {
    return { message: text };
  }
}

function getRoleCodes(user) {
  if (!user?.role_codes_csv) {
    return [];
  }

  return user.role_codes_csv
    .split(',')
    .map((role) => role.trim())
    .filter(Boolean);
}

function maskSecret(value) {
  if (!value) {
    return '';
  }

  if (value.length <= 14) {
    return '•'.repeat(value.length);
  }

  return `${value.slice(0, 8)}${'•'.repeat(Math.max(8, value.length - 16))}${value.slice(-8)}`;
}

function getRoleBadgeClass(roleCode) {
  switch (roleCode) {
    case 'ADMIN':
      return 'access-pill access-pill-role access-pill-role-admin';
    case 'DEVELOPER':
      return 'access-pill access-pill-role access-pill-role-developer';
    case 'ANALYST':
      return 'access-pill access-pill-role access-pill-role-analyst';
    default:
      return 'access-pill access-pill-role';
  }
}

function getExperimentActionLabel(action) {
  return experimentActionLabels[action] ?? action;
}

function getMetricCalculationStateBadgeClass(isDirty) {
  return isDirty ? 'badge badge-dirty' : 'badge badge-clean';
}

function getMetricCalculationReasonLabel(reason) {
  switch (reason) {
    case 'metric_binding_changed':
      return 'Metric binding changed';
    case 'assignment_created':
      return 'New assignment detected';
    case 'event_ingested':
      return 'New event ingested';
    default:
      return reason || 'n/a';
  }
}

function App() {
  const [backendStatus, setBackendStatus] = useState('checking');
  const [clientServicesCount, setClientServicesCount] = useState(null);
  const [sessionToken, setSessionToken] = useState(
    () => window.localStorage.getItem(SESSION_TOKEN_KEY) ?? ''
  );
  const [currentUser, setCurrentUser] = useState(() => readStoredUser());

  const [activeTab, setActiveTab] = useState('auth');
  const [tokenVisible, setTokenVisible] = useState(false);
  const [toast, setToast] = useState(null);

  const [registerForm, setRegisterForm] = useState(initialRegisterForm);
  const [loginForm, setLoginForm] = useState(initialLoginForm);
  const [createUserForm, setCreateUserForm] = useState(initialCreateUserForm);
  const [createApiKeyForm, setCreateApiKeyForm] = useState(initialCreateApiKeyForm);
  const [createFeatureForm, setCreateFeatureForm] = useState(initialCreateFeatureForm);
  const [createMetricForm, setCreateMetricForm] = useState(initialCreateMetricForm);
  const [createFlagForm, setCreateFlagForm] = useState(initialCreateFlagForm);
  const [createExperimentForm, setCreateExperimentForm] = useState(() =>
    createDefaultExperimentForm()
  );

  const [users, setUsers] = useState([]);
  const [apiKeys, setApiKeys] = useState([]);
  const [flagsCatalog, setFlagsCatalog] = useState([]);
  const [featuresCatalog, setFeaturesCatalog] = useState([]);
  const [metricsCatalog, setMetricsCatalog] = useState([]);
  const [experimentMetrics, setExperimentMetrics] = useState([]);
  const [metricResults, setMetricResults] = useState([]);
  const [metricAutomationEnabled, setMetricAutomationEnabled] = useState(false);
  const [selectedMetricIdsByExperiment, setSelectedMetricIdsByExperiment] = useState({});
  const [metricBindingOptionsByExperiment, setMetricBindingOptionsByExperiment] = useState({});
  const [experiments, setExperiments] = useState([]);
  const [configVersions, setConfigVersions] = useState([]);
  const [configAutomationEnabled, setConfigAutomationEnabled] = useState(false);
  const [loginServiceOptions, setLoginServiceOptions] = useState([]);
  const [latestCreatedApiKey, setLatestCreatedApiKey] = useState(null);

  const [registerLoading, setRegisterLoading] = useState(false);
  const [loginLoading, setLoginLoading] = useState(false);
  const [usersLoading, setUsersLoading] = useState(false);
  const [apiKeysLoading, setApiKeysLoading] = useState(false);
  const [flagsLoading, setFlagsLoading] = useState(false);
  const [featuresLoading, setFeaturesLoading] = useState(false);
  const [metricsLoading, setMetricsLoading] = useState(false);
  const [experimentMetricsLoading, setExperimentMetricsLoading] = useState(false);
  const [metricResultsLoading, setMetricResultsLoading] = useState(false);
  const [experimentsLoading, setExperimentsLoading] = useState(false);
  const [configVersionsLoading, setConfigVersionsLoading] = useState(false);

  const isAuthorized = Boolean(sessionToken && currentUser);
  const roleCodes = useMemo(() => getRoleCodes(currentUser), [currentUser]);
  const isAdmin = roleCodes.includes('ADMIN');
  const canViewApiKeys = isAdmin || roleCodes.includes('DEVELOPER');

  const authHeader = useMemo(() => {
    if (!sessionToken) {
      return {};
    }

    return {
      Authorization: `Bearer ${sessionToken}`,
    };
  }, [sessionToken]);

  const visibleTabs = useMemo(() => {
    const tabs = [{ id: 'auth', label: 'Auth' }];
    if (isAuthorized) {
      tabs.push({ id: 'experiments', label: 'Experiments' });
      tabs.push({ id: 'users', label: 'Users' });
    }
    if (canViewApiKeys) {
      tabs.push({ id: 'api-keys', label: 'API Keys' });
    }
    return tabs;
  }, [isAuthorized, canViewApiKeys]);

  const variantTrafficPercent = useMemo(() => {
    const percent = Number(createExperimentForm.variant_traffic_percent);
    return Number.isFinite(percent) ? percent : 0;
  }, [createExperimentForm.variant_traffic_percent]);

  const experimentTrafficTotal = useMemo(
    () => createExperimentForm.variants.length * variantTrafficPercent,
    [createExperimentForm.variants.length, variantTrafficPercent]
  );

  const experimentMetricsByExperiment = useMemo(() => {
    const grouped = {};
    experimentMetrics.forEach((item) => {
      const key = String(item.experiment_id);
      grouped[key] = grouped[key] ?? [];
      grouped[key].push(item);
    });
    return grouped;
  }, [experimentMetrics]);

  const metricResultsByExperiment = useMemo(() => {
    const grouped = {};
    metricResults.forEach((item) => {
      const key = String(item.experiment_id);
      grouped[key] = grouped[key] ?? [];
      grouped[key].push(item);
    });
    return grouped;
  }, [metricResults]);

  const pushToast = (message, type = 'info') => {
    setToast({ message, type });
  };

  const persistSession = (nextToken, nextUser) => {
    setSessionToken(nextToken);
    setCurrentUser(nextUser);
    setLatestCreatedApiKey(null);
    window.localStorage.setItem(SESSION_TOKEN_KEY, nextToken);
    window.localStorage.setItem(SESSION_USER_KEY, JSON.stringify(nextUser));
  };

  const clearSession = () => {
    setSessionToken('');
    setCurrentUser(null);
    setUsers([]);
    setApiKeys([]);
    setFlagsCatalog([]);
    setFeaturesCatalog([]);
    setMetricsCatalog([]);
    setExperimentMetrics([]);
    setMetricResults([]);
    setMetricAutomationEnabled(false);
    setSelectedMetricIdsByExperiment({});
    setMetricBindingOptionsByExperiment({});
    setExperiments([]);
    setConfigVersions([]);
    setConfigAutomationEnabled(false);
    setLoginServiceOptions([]);
    setLatestCreatedApiKey(null);
    setLoginForm(initialLoginForm);
    setCreateFeatureForm(initialCreateFeatureForm);
    setCreateMetricForm(initialCreateMetricForm);
    setCreateFlagForm(initialCreateFlagForm);
    setCreateExperimentForm(createDefaultExperimentForm());
    setTokenVisible(false);
    setActiveTab('auth');
    window.localStorage.removeItem(SESSION_TOKEN_KEY);
    window.localStorage.removeItem(SESSION_USER_KEY);
    pushToast('Session cleared.', 'info');
  };

  const copyToClipboard = async (value, label) => {
    try {
      await navigator.clipboard.writeText(value);
      pushToast(`${label} copied.`, 'success');
    } catch (error) {
      pushToast(`Failed to copy ${label.toLowerCase()}.`, 'error');
    }
  };

  const loadStatus = async () => {
    try {
      const healthResponse = await fetch('/api/health');
      if (!healthResponse.ok) {
        throw new Error('Backend returned an error');
      }
      setBackendStatus('ok');
    } catch (error) {
      setBackendStatus('unreachable');
      setClientServicesCount(null);
      return;
    }

    try {
      const clientServicesResponse = await fetch('/api/v1/client-services');
      if (!clientServicesResponse.ok) {
        throw new Error('Client services endpoint returned an error');
      }

      const clientServicesPayload = await clientServicesResponse.json();
      setClientServicesCount(clientServicesPayload.count ?? 0);
    } catch (error) {
      setClientServicesCount(null);
    }
  };

  const loadUsers = async () => {
    if (!isAuthorized) {
      setUsers([]);
      return;
    }

    setUsersLoading(true);
    try {
      const response = await fetch('/api/v1/platform-users', {
        headers: authHeader,
      });
      const payload = await readJson(response);
      if (!response.ok) {
        throw new Error(payload?.message ?? 'Failed to load platform users');
      }

      setUsers(payload?.items ?? []);
    } catch (error) {
      pushToast(error.message, 'error');
    } finally {
      setUsersLoading(false);
    }
  };

  const handleRevokeApiKey = async (apiKeyId) => {
    setApiKeysLoading(true);

    try {
      const response = await fetch(`/api/v1/api-keys?api_key_id=${apiKeyId}`, {
        method: 'DELETE',
        headers: authHeader,
      });
      const payload = await readJson(response);
      if (!response.ok) {
        throw new Error(payload?.message ?? 'Failed to revoke API key');
      }

      if (latestCreatedApiKey?.id === apiKeyId) {
        setLatestCreatedApiKey(null);
      }
      await loadApiKeys();
      pushToast('API key revoked.', 'success');
    } catch (error) {
      pushToast(error.message, 'error');
    } finally {
      setApiKeysLoading(false);
    }
  };

  const loadApiKeys = async () => {
    if (!isAuthorized || !canViewApiKeys) {
      setApiKeys([]);
      return;
    }

    setApiKeysLoading(true);
    try {
      const response = await fetch('/api/v1/api-keys', {
        headers: authHeader,
      });
      const payload = await readJson(response);
      if (!response.ok) {
        throw new Error(payload?.message ?? 'Failed to load API keys');
      }

      setApiKeys(payload?.items ?? []);
    } catch (error) {
      pushToast(error.message, 'error');
    } finally {
      setApiKeysLoading(false);
    }
  };

  const loadFlags = async () => {
    if (!isAuthorized) {
      setFlagsCatalog([]);
      return;
    }

    setFlagsLoading(true);
    try {
      const response = await fetch('/api/v1/flags', {
        headers: authHeader,
      });
      const payload = await readJson(response);
      if (!response.ok) {
        throw new Error(payload?.message ?? 'Failed to load flags');
      }

      setFlagsCatalog(payload?.items ?? []);
    } catch (error) {
      pushToast(error.message, 'error');
    } finally {
      setFlagsLoading(false);
    }
  };

  const loadFeatures = async () => {
    if (!isAuthorized) {
      setFeaturesCatalog([]);
      return;
    }

    setFeaturesLoading(true);
    try {
      const response = await fetch('/api/v1/features', {
        headers: authHeader,
      });
      const payload = await readJson(response);
      if (!response.ok) {
        throw new Error(payload?.message ?? 'Failed to load features');
      }

      setFeaturesCatalog(payload?.items ?? []);
    } catch (error) {
      pushToast(error.message, 'error');
    } finally {
      setFeaturesLoading(false);
    }
  };

  const loadMetrics = async () => {
    if (!isAuthorized) {
      setMetricsCatalog([]);
      return;
    }

    setMetricsLoading(true);
    try {
      const response = await fetch('/api/v1/metrics', {
        headers: authHeader,
      });
      const payload = await readJson(response);
      if (!response.ok) {
        throw new Error(payload?.message ?? 'Failed to load metrics');
      }

      setMetricsCatalog(payload?.items ?? []);
    } catch (error) {
      pushToast(error.message, 'error');
    } finally {
      setMetricsLoading(false);
    }
  };

  const loadExperimentMetrics = async () => {
    if (!isAuthorized) {
      setExperimentMetrics([]);
      return;
    }

    setExperimentMetricsLoading(true);
    try {
      const response = await fetch('/api/v1/experiment-metrics', {
        headers: authHeader,
      });
      const payload = await readJson(response);
      if (!response.ok) {
        throw new Error(payload?.message ?? 'Failed to load experiment metrics');
      }

      setExperimentMetrics(payload?.items ?? []);
    } catch (error) {
      pushToast(error.message, 'error');
    } finally {
      setExperimentMetricsLoading(false);
    }
  };

  const loadMetricResults = async () => {
    if (!isAuthorized) {
      setMetricResults([]);
      return;
    }

    setMetricResultsLoading(true);
    try {
      const response = await fetch('/api/v1/metric-results', {
        headers: authHeader,
      });
      const payload = await readJson(response);
      if (!response.ok) {
        throw new Error(payload?.message ?? 'Failed to load metric results');
      }

      setMetricResults(payload?.items ?? []);
      setMetricAutomationEnabled(Boolean(payload?.auto_calculation_enabled));
    } catch (error) {
      pushToast(error.message, 'error');
    } finally {
      setMetricResultsLoading(false);
    }
  };

  const loadExperiments = async () => {
    if (!isAuthorized) {
      setExperiments([]);
      return;
    }

    setExperimentsLoading(true);
    try {
      const response = await fetch('/api/v1/experiments', {
        headers: authHeader,
      });
      const payload = await readJson(response);
      if (!response.ok) {
        throw new Error(payload?.message ?? 'Failed to load experiments');
      }

      setExperiments(payload?.items ?? []);
    } catch (error) {
      pushToast(error.message, 'error');
    } finally {
      setExperimentsLoading(false);
    }
  };

  const loadConfigVersions = async () => {
    if (!isAuthorized) {
      setConfigVersions([]);
      return;
    }

    setConfigVersionsLoading(true);
    try {
      const response = await fetch('/api/v1/config-versions', {
        headers: authHeader,
      });
      const payload = await readJson(response);
      if (!response.ok) {
        throw new Error(payload?.message ?? 'Failed to load config versions');
      }

      setConfigVersions(payload?.items ?? []);
      setConfigAutomationEnabled(Boolean(payload?.auto_build_enabled));
    } catch (error) {
      pushToast(error.message, 'error');
    } finally {
      setConfigVersionsLoading(false);
    }
  };

  useEffect(() => {
    loadStatus();
  }, []);

  useEffect(() => {
    if (!toast) {
      return undefined;
    }

    const timeoutId = window.setTimeout(() => setToast(null), 2600);
    return () => window.clearTimeout(timeoutId);
  }, [toast]);

  useEffect(() => {
    if (!visibleTabs.some((tab) => tab.id === activeTab)) {
      setActiveTab('auth');
    }
  }, [activeTab, visibleTabs]);

  useEffect(() => {
    window.localStorage.removeItem('ab-experiments-plain-api-keys');
  }, []);

  useEffect(() => {
    loadUsers();
    loadApiKeys();
    loadFeatures();
    loadMetrics();
    loadExperimentMetrics();
    loadMetricResults();
    loadFlags();
    loadExperiments();
    loadConfigVersions();
  }, [isAuthorized, canViewApiKeys, sessionToken]);

  useEffect(() => {
    if (!isAuthorized || activeTab !== 'experiments') {
      return undefined;
    }

    const intervalId = window.setInterval(() => {
      loadMetricResults();
    }, 7000);

    return () => {
      window.clearInterval(intervalId);
    };
  }, [isAuthorized, activeTab, sessionToken]);

  const handleRegisterSubmit = async (event) => {
    event.preventDefault();
    setRegisterLoading(true);

    try {
      const response = await fetch('/api/v1/auth/register-service', {
        method: 'POST',
        headers: {
          'Content-Type': 'application/json',
        },
        body: JSON.stringify(registerForm),
      });
      const payload = await readJson(response);
      if (!response.ok) {
        throw new Error(payload?.message ?? 'Failed to register service');
      }

      persistSession(payload.session_token, payload.user);
      setRegisterForm(initialRegisterForm);
      setLoginForm({
        email: payload.user.email,
        password: '',
        client_service_id: '',
      });
      setLoginServiceOptions([]);
      setActiveTab('users');
      pushToast(`Service "${payload.user.client_service_name}" created.`, 'success');
    } catch (error) {
      pushToast(error.message, 'error');
    } finally {
      setRegisterLoading(false);
      loadStatus();
    }
  };

  const handleLoginInputChange = (field, value) => {
    setLoginForm((previous) => ({
      ...previous,
      [field]: value,
      ...(field === 'email' || field === 'password' ? { client_service_id: '' } : {}),
    }));

    if (field === 'email' || field === 'password') {
      setLoginServiceOptions([]);
    }
  };

  const handleLoginSubmit = async (event) => {
    event.preventDefault();
    setLoginLoading(true);

    try {
      const payloadToSend = {
        email: loginForm.email,
        password: loginForm.password,
      };
      if (loginForm.client_service_id) {
        payloadToSend.client_service_id = Number(loginForm.client_service_id);
      }

      const response = await fetch('/api/v1/auth/login', {
        method: 'POST',
        headers: {
          'Content-Type': 'application/json',
        },
        body: JSON.stringify(payloadToSend),
      });
      const payload = await readJson(response);
      if (!response.ok) {
        throw new Error(payload?.message ?? 'Failed to log in');
      }

      if (payload?.requires_service_selection) {
        setLoginServiceOptions(payload.service_options ?? []);
        pushToast('Choose the service you want to enter.', 'info');
        return;
      }

      persistSession(payload.session_token, payload.user);
      setLoginServiceOptions([]);
      setLoginForm(initialLoginForm);
      setTokenVisible(false);
      setActiveTab('users');
      pushToast(`Logged in to "${payload.user.client_service_name}".`, 'success');
    } catch (error) {
      pushToast(error.message, 'error');
    } finally {
      setLoginLoading(false);
    }
  };

  const handleCreateUserSubmit = async (event) => {
    event.preventDefault();
    setUsersLoading(true);

    try {
      const response = await fetch('/api/v1/platform-users', {
        method: 'POST',
        headers: {
          'Content-Type': 'application/json',
          ...authHeader,
        },
        body: JSON.stringify(createUserForm),
      });
      const payload = await readJson(response);
      if (!response.ok) {
        throw new Error(payload?.message ?? 'Failed to create platform user');
      }

      setCreateUserForm(initialCreateUserForm);
      await loadUsers();
      setActiveTab('users');
      pushToast('Platform user created.', 'success');
    } catch (error) {
      pushToast(error.message, 'error');
    } finally {
      setUsersLoading(false);
    }
  };

  const handleCreateApiKeySubmit = async (event) => {
    event.preventDefault();
    setApiKeysLoading(true);

    try {
      const response = await fetch('/api/v1/api-keys', {
        method: 'POST',
        headers: {
          'Content-Type': 'application/json',
          ...authHeader,
        },
        body: JSON.stringify(createApiKeyForm),
      });
      const payload = await readJson(response);
      if (!response.ok) {
        throw new Error(payload?.message ?? 'Failed to create API key');
      }

      setCreateApiKeyForm(initialCreateApiKeyForm);
      if (payload?.plain_api_key) {
        setLatestCreatedApiKey({
          id: payload.api_key.id,
          name: payload.api_key.name,
          created_at: payload.api_key.created_at,
          plain_api_key: payload.plain_api_key,
        });
      }

      await loadApiKeys();
      setActiveTab('api-keys');
      pushToast('API key created. Copy it now, it will not be shown again after refresh.', 'success');
    } catch (error) {
      pushToast(error.message, 'error');
    } finally {
      setApiKeysLoading(false);
    }
  };

  const updateExperimentField = (field, value) => {
    setCreateExperimentForm((previous) => ({
      ...previous,
      [field]: value,
    }));
  };

  const updateVariantField = (variantId, field, value) => {
    setCreateExperimentForm((previous) => ({
      ...previous,
      variants: previous.variants.map((variant) =>
        variant.client_id === variantId
          ? {
              ...variant,
              [field]: value,
            }
          : variant
      ),
    }));
  };

  const addVariant = () => {
    setCreateExperimentForm((previous) => ({
      ...previous,
      variants: [
        ...previous.variants,
        createExperimentVariantDraft({
          key: `variant-${previous.variants.length + 1}`,
          name: `Variant ${previous.variants.length + 1}`,
        }),
      ],
    }));
  };

  const removeVariant = (variantId) => {
    setCreateExperimentForm((previous) => ({
      ...previous,
      variants: previous.variants.filter((variant) => variant.client_id !== variantId),
    }));
  };

  const addVariantFlag = (variantId) => {
    setCreateExperimentForm((previous) => ({
      ...previous,
      variants: previous.variants.map((variant) =>
        variant.client_id === variantId
          ? {
              ...variant,
              flags: [...variant.flags, createExperimentFlagDraft()],
            }
          : variant
      ),
    }));
  };

  const updateVariantFlagField = (variantId, flagId, field, value) => {
    setCreateExperimentForm((previous) => ({
      ...previous,
      variants: previous.variants.map((variant) =>
        variant.client_id === variantId
          ? {
              ...variant,
              flags: variant.flags.map((flag) =>
                flag.client_id === flagId
                  ? {
                      ...flag,
                      [field]: value,
                    }
                  : flag
              ),
            }
          : variant
      ),
    }));
  };

  const removeVariantFlag = (variantId, flagId) => {
    setCreateExperimentForm((previous) => ({
      ...previous,
      variants: previous.variants.map((variant) =>
        variant.client_id === variantId
          ? {
              ...variant,
              flags: variant.flags.filter((flag) => flag.client_id !== flagId),
            }
          : variant
      ),
    }));
  };

  const validateExperimentForm = () => {
    if (!createExperimentForm.name.trim()) {
      throw new Error('Experiment name is required');
    }
    if (!createExperimentForm.layer_name.trim()) {
      throw new Error('Layer name is required');
    }
    const durationDays = Number(createExperimentForm.duration_days);
    if (!Number.isInteger(durationDays) || durationDays <= 0) {
      throw new Error('Duration in days must be a positive integer');
    }
    const perVariantPercent = Number(createExperimentForm.variant_traffic_percent);
    if (!Number.isFinite(perVariantPercent) || perVariantPercent <= 0) {
      throw new Error('Traffic percent per variant must be a positive number');
    }
    if (createExperimentForm.variants.length < 2) {
      throw new Error('Add at least two variants');
    }
    if (createExperimentForm.variants.length * perVariantPercent > 100.0001) {
      throw new Error('Total experiment traffic percent must not exceed 100');
    }

    const variantKeys = new Set();
    createExperimentForm.variants.forEach((variant, variantIndex) => {
      if (!variant.key.trim()) {
        throw new Error(`Variant ${variantIndex + 1} key is required`);
      }
      if (!variant.name.trim()) {
        throw new Error(`Variant ${variantIndex + 1} name is required`);
      }
      if (variantKeys.has(variant.key.trim())) {
        throw new Error(`Variant key "${variant.key}" is duplicated`);
      }
      variantKeys.add(variant.key.trim());

      if (variant.flags.length === 0) {
        throw new Error(`Variant ${variant.key} must contain at least one flag`);
      }

      const variantFlagIds = new Set();
      variant.flags.forEach((flag) => {
        const flagId = Number(flag.flag_id);
        if (!Number.isInteger(flagId) || flagId <= 0) {
          throw new Error(`Variant ${variant.key} has an unselected flag`);
        }
        if (!flag.variant_value.trim()) {
          throw new Error(`A flag in variant ${variant.key} must have variant value`);
        }
        if (variantFlagIds.has(flagId)) {
          throw new Error(`Variant ${variant.key} contains the same flag more than once`);
        }
        variantFlagIds.add(flagId);
      });
    });
  };

  const handleExperimentAction = async (experimentId, action) => {
    setExperimentsLoading(true);

    try {
      const response = await fetch('/api/v1/experiments', {
        method: 'PATCH',
        headers: {
          'Content-Type': 'application/json',
          ...authHeader,
        },
        body: JSON.stringify({
          experiment_id: experimentId,
          action,
        }),
      });
      const payload = await readJson(response);
      if (!response.ok) {
        throw new Error(payload?.message ?? 'Failed to change experiment status');
      }

      await loadExperiments();
      pushToast(`Experiment updated: ${getExperimentActionLabel(action)}.`, 'success');
    } catch (error) {
      pushToast(error.message, 'error');
    } finally {
      setExperimentsLoading(false);
    }
  };

  const handleBuildConfigVersion = async () => {
    setConfigVersionsLoading(true);

    try {
      const response = await fetch('/api/v1/config-versions', {
        method: 'POST',
        headers: {
          ...authHeader,
        },
      });
      const payload = await readJson(response);
      if (!response.ok) {
        throw new Error(payload?.message ?? 'Failed to build config version');
      }

      await loadConfigVersions();
      await loadExperiments();
      pushToast(
        `Config v${payload?.items?.[0]?.version ?? '?'} built. Added from queue: ${
          payload?.queued_added_count ?? 0
        }, removed completed: ${payload?.completed_removed_count ?? 0}.`,
        'success'
      );
    } catch (error) {
      pushToast(error.message, 'error');
    } finally {
      setConfigVersionsLoading(false);
    }
  };

  const handleCreateFlagSubmit = async (event) => {
    event.preventDefault();
    setFlagsLoading(true);

    try {
      const response = await fetch('/api/v1/flags', {
        method: 'POST',
        headers: {
          'Content-Type': 'application/json',
          ...authHeader,
        },
        body: JSON.stringify(createFlagForm),
      });
      const payload = await readJson(response);
      if (!response.ok) {
        throw new Error(payload?.message ?? 'Failed to create flag');
      }

      setCreateFlagForm(initialCreateFlagForm);
      await loadFlags();
      setActiveTab('experiments');
      pushToast(`Flag "${payload?.items?.[0]?.key ?? createFlagForm.key}" created.`, 'success');
    } catch (error) {
      pushToast(error.message, 'error');
    } finally {
      setFlagsLoading(false);
    }
  };

  const handleCreateFeatureSubmit = async (event) => {
    event.preventDefault();
    setFeaturesLoading(true);

    try {
      const response = await fetch('/api/v1/features', {
        method: 'POST',
        headers: {
          'Content-Type': 'application/json',
          ...authHeader,
        },
        body: JSON.stringify(createFeatureForm),
      });
      const payload = await readJson(response);
      if (!response.ok) {
        throw new Error(payload?.message ?? 'Failed to create feature');
      }

      setCreateFeatureForm(initialCreateFeatureForm);
      await loadFeatures();
      setActiveTab('experiments');
      pushToast(
        `Feature "${payload?.items?.[0]?.key ?? createFeatureForm.key}" created.`,
        'success'
      );
    } catch (error) {
      pushToast(error.message, 'error');
    } finally {
      setFeaturesLoading(false);
    }
  };

  const handleCreateMetricSubmit = async (event) => {
    event.preventDefault();
    setMetricsLoading(true);

    try {
      const response = await fetch('/api/v1/metrics', {
        method: 'POST',
        headers: {
          'Content-Type': 'application/json',
          ...authHeader,
        },
        body: JSON.stringify(createMetricForm),
      });
      const payload = await readJson(response);
      if (!response.ok) {
        throw new Error(payload?.message ?? 'Failed to create metric');
      }

      setCreateMetricForm(initialCreateMetricForm);
      await loadMetrics();
      setActiveTab('experiments');
      pushToast(
        `Metric "${payload?.items?.[0]?.code ?? createMetricForm.code}" created.`,
        'success'
      );
    } catch (error) {
      pushToast(error.message, 'error');
    } finally {
      setMetricsLoading(false);
    }
  };

  const handleAttachMetricToExperiment = async (experimentId) => {
    const selectedMetricId = Number(selectedMetricIdsByExperiment[experimentId] ?? 0);
    if (!Number.isInteger(selectedMetricId) || selectedMetricId <= 0) {
      pushToast('Select a metric first.', 'error');
      return;
    }

    const bindingOptions = metricBindingOptionsByExperiment[experimentId] ?? {
      is_primary: false,
      is_guardrail: false,
    };

    setExperimentMetricsLoading(true);
    try {
      const response = await fetch('/api/v1/experiment-metrics', {
        method: 'POST',
        headers: {
          'Content-Type': 'application/json',
          ...authHeader,
        },
        body: JSON.stringify({
          experiment_id: experimentId,
          metric_id: selectedMetricId,
          is_primary: Boolean(bindingOptions.is_primary),
          is_guardrail: Boolean(bindingOptions.is_guardrail),
        }),
      });
      const payload = await readJson(response);
      if (!response.ok) {
        throw new Error(payload?.message ?? 'Failed to attach metric');
      }

      setSelectedMetricIdsByExperiment((previous) => ({
        ...previous,
        [experimentId]: '',
      }));
      setMetricBindingOptionsByExperiment((previous) => ({
        ...previous,
        [experimentId]: {
          is_primary: false,
          is_guardrail: false,
        },
      }));
      await loadExperimentMetrics();
      pushToast('Metric attached to experiment.', 'success');
    } catch (error) {
      pushToast(error.message, 'error');
    } finally {
      setExperimentMetricsLoading(false);
    }
  };

  const handleCreateExperimentSubmit = async (event) => {
    event.preventDefault();
    setExperimentsLoading(true);

    try {
      validateExperimentForm();

      const payloadToSend = {
        name: createExperimentForm.name.trim(),
        description: createExperimentForm.description.trim(),
        layer_name: createExperimentForm.layer_name.trim(),
        layer_description: createExperimentForm.layer_description.trim(),
        duration_days: Number(createExperimentForm.duration_days),
        variant_traffic_percent: Number(createExperimentForm.variant_traffic_percent),
        variants: createExperimentForm.variants.map((variant) => ({
          key: variant.key.trim(),
          name: variant.name.trim(),
          description: variant.description.trim(),
          flags: variant.flags.map((flag) => ({
            flag_id: Number(flag.flag_id),
            variant_value: flag.variant_value.trim(),
          })),
        })),
      };

      const response = await fetch('/api/v1/experiments', {
        method: 'POST',
        headers: {
          'Content-Type': 'application/json',
          ...authHeader,
        },
        body: JSON.stringify(payloadToSend),
      });
      const payload = await readJson(response);
      if (!response.ok) {
        throw new Error(payload?.message ?? 'Failed to create experiment');
      }

      setCreateExperimentForm(createDefaultExperimentForm());
      await loadExperiments();
      await loadFlags();
      setActiveTab('experiments');
      pushToast(`Experiment "${payload?.items?.[0]?.name ?? payloadToSend.name}" created.`, 'success');
    } catch (error) {
      pushToast(error.message, 'error');
    } finally {
      setExperimentsLoading(false);
    }
  };

  return (
    <main className="app">
      {toast ? (
        <div className={`toast toast-${toast.type}`}>
          <span>{toast.message}</span>
          <button className="toast-close" type="button" onClick={() => setToast(null)}>
            Close
          </button>
        </div>
      ) : null}

      <section className="hero">
        <div>
          <p className="eyebrow">AB Experiments</p>
          <h1>Service onboarding, access, and experiment setup</h1>
          <p className="subtitle">
            Manage auth, platform users, API keys, and the first CRUD workflow for experiment
            layers, variants, and flags from one dashboard.
          </p>
          {isAuthorized ? (
            <div className="access-summary">
              <span className="access-pill">
                Service: {currentUser.client_service_name}
              </span>
              <span className="access-pill">
                User: {currentUser.email}
              </span>
              {roleCodes.map((roleCode) => (
                <span key={roleCode} className={getRoleBadgeClass(roleCode)}>
                  {roleCode}
                </span>
              ))}
            </div>
          ) : null}
        </div>
        <div className="hero-actions">
          <button className="ghost-button" type="button" onClick={loadStatus}>
            Refresh backend status
          </button>
          {isAuthorized ? (
            <button className="danger-button" type="button" onClick={clearSession}>
              Logout
            </button>
          ) : null}
        </div>
      </section>

      <section className="grid">
        <article className="panel panel-wide">
          <div className="panel-header">
            <h2>Environment</h2>
            <span className={`badge badge-${backendStatus}`}>{backendStatus}</span>
          </div>
          <div className="stats">
            <div className="stat">
              <span className="stat-label">Frontend</span>
              <strong>ready</strong>
            </div>
            <div className="stat">
              <span className="stat-label">Backend</span>
              <strong>{backendStatus}</strong>
            </div>
            <div className="stat">
              <span className="stat-label">Client services</span>
              <strong>{clientServicesCount === null ? 'unavailable' : clientServicesCount}</strong>
            </div>
          </div>
        </article>

        <article className="panel panel-wide">
          <div className="tabs">
            {visibleTabs.map((tab) => (
              <button
                key={tab.id}
                className={tab.id === activeTab ? 'tab-button is-active' : 'tab-button'}
                type="button"
                onClick={() => setActiveTab(tab.id)}
              >
                {tab.label}
              </button>
            ))}
          </div>

          {activeTab === 'auth' ? (
            <div className="tab-grid">
              <section className="subpanel">
                <div className="panel-header">
                  <h2>Register Service</h2>
                </div>
                <form className="form" onSubmit={handleRegisterSubmit}>
                  <label>
                    Service name
                    <input
                      required
                      value={registerForm.service_name}
                      onChange={(event) =>
                        setRegisterForm((previous) => ({
                          ...previous,
                          service_name: event.target.value,
                        }))
                      }
                    />
                  </label>
                  <label>
                    Service description
                    <textarea
                      rows="3"
                      value={registerForm.service_description}
                      onChange={(event) =>
                        setRegisterForm((previous) => ({
                          ...previous,
                          service_description: event.target.value,
                        }))
                      }
                    />
                  </label>
                  <label>
                    Admin email
                    <input
                      required
                      type="email"
                      value={registerForm.admin_email}
                      onChange={(event) =>
                        setRegisterForm((previous) => ({
                          ...previous,
                          admin_email: event.target.value,
                        }))
                      }
                    />
                  </label>
                  <label>
                    Admin full name
                    <input
                      value={registerForm.admin_full_name}
                      onChange={(event) =>
                        setRegisterForm((previous) => ({
                          ...previous,
                          admin_full_name: event.target.value,
                        }))
                      }
                    />
                  </label>
                  <label>
                    Password
                    <input
                      required
                      type="password"
                      value={registerForm.password}
                      onChange={(event) =>
                        setRegisterForm((previous) => ({
                          ...previous,
                          password: event.target.value,
                        }))
                      }
                    />
                  </label>
                  <button type="submit" disabled={registerLoading}>
                    {registerLoading ? 'Creating...' : 'Create service'}
                  </button>
                </form>
              </section>

              <section className="subpanel">
                <div className="panel-header">
                  <h2>Login</h2>
                </div>
                <form className="form" onSubmit={handleLoginSubmit}>
                  <label>
                    Email
                    <input
                      required
                      type="email"
                      value={loginForm.email}
                      onChange={(event) => handleLoginInputChange('email', event.target.value)}
                    />
                  </label>
                  <label>
                    Password
                    <input
                      required
                      type="password"
                      value={loginForm.password}
                      onChange={(event) => handleLoginInputChange('password', event.target.value)}
                    />
                  </label>
                  {loginServiceOptions.length > 0 ? (
                    <label>
                      Choose service
                      <select
                        required
                        value={loginForm.client_service_id}
                        onChange={(event) =>
                          handleLoginInputChange('client_service_id', event.target.value)
                        }
                      >
                        <option value="">Select service</option>
                        {loginServiceOptions.map((option) => (
                          <option key={option.client_service_id} value={option.client_service_id}>
                            {option.client_service_name} (ID: {option.client_service_id})
                          </option>
                        ))}
                      </select>
                    </label>
                  ) : null}
                  <button type="submit" disabled={loginLoading}>
                    {loginLoading
                      ? 'Signing in...'
                      : loginServiceOptions.length > 0
                        ? 'Enter selected service'
                        : 'Login'}
                  </button>
                </form>

                <div className="session-card session-card-spaced">
                  <div className="panel-header">
                    <h3>Current Session</h3>
                    {isAuthorized ? (
                      <button className="ghost-button" type="button" onClick={clearSession}>
                        Logout
                      </button>
                    ) : null}
                  </div>
                  {isAuthorized ? (
                    <>
                      <div className="inline-actions">
                        <p>
                          <strong>User:</strong> {currentUser.email}
                        </p>
                        <button
                          className="ghost-button session-action-button"
                          type="button"
                          onClick={() => copyToClipboard(currentUser.email, 'User email')}
                        >
                          Copy email
                        </button>
                      </div>
                      <div className="inline-actions">
                        <p>
                          <strong>Service:</strong> {currentUser.client_service_name} (ID:{' '}
                          {currentUser.client_service_id})
                        </p>
                        <button
                          className="ghost-button session-action-button"
                          type="button"
                          onClick={() =>
                            copyToClipboard(String(currentUser.client_service_id), 'Service ID')
                          }
                        >
                          Copy ID
                        </button>
                      </div>
                      <div className="inline-actions">
                        <p>
                          <strong>Roles:</strong> {currentUser.role_codes_csv}
                        </p>
                        <button
                          className="ghost-button session-action-button"
                          type="button"
                          onClick={() => copyToClipboard(currentUser.role_codes_csv, 'Role list')}
                        >
                          Copy roles
                        </button>
                      </div>
                      <div className="inline-actions">
                        <p className="token-preview">
                          <strong>Session token:</strong>{' '}
                          {tokenVisible ? sessionToken : maskSecret(sessionToken)}
                        </p>
                        <div className="button-row session-action-group">
                          <button
                            className="ghost-button session-action-button"
                            type="button"
                            onClick={() => setTokenVisible((value) => !value)}
                          >
                            {tokenVisible ? 'Hide token' : 'Show token'}
                          </button>
                          <button
                            className="ghost-button session-action-button"
                            type="button"
                            onClick={() => copyToClipboard(sessionToken, 'Session token')}
                          >
                            Copy token
                          </button>
                        </div>
                      </div>
                    </>
                  ) : (
                    <p className="muted">No active session. Register a service or log in first.</p>
                  )}
                </div>
              </section>
            </div>
          ) : null}

          {activeTab === 'experiments' && isAuthorized ? (
            <div className="tab-grid tab-grid-single">
              <section className="subpanel">
                <div className="panel-header">
                  <h2>Experiments</h2>
                  <button className="ghost-button" type="button" onClick={loadExperiments}>
                    Refresh
                  </button>
                </div>

                {isAdmin ? (
                  <form className="form" onSubmit={handleCreateExperimentSubmit}>
                    <label>
                      Experiment name
                      <input
                        required
                        value={createExperimentForm.name}
                        onChange={(event) => updateExperimentField('name', event.target.value)}
                      />
                    </label>
                    <label>
                      Description
                      <textarea
                        rows="3"
                        value={createExperimentForm.description}
                        onChange={(event) =>
                          updateExperimentField('description', event.target.value)
                        }
                      />
                    </label>
                    <div className="editor-grid">
                      <label>
                        Layer name
                        <input
                          required
                          value={createExperimentForm.layer_name}
                          onChange={(event) =>
                            updateExperimentField('layer_name', event.target.value)
                          }
                        />
                      </label>
                      <label>
                        Duration, days
                        <input
                          required
                          type="number"
                          min="1"
                          step="1"
                          value={createExperimentForm.duration_days}
                          onChange={(event) =>
                            updateExperimentField('duration_days', event.target.value)
                          }
                        />
                      </label>
                      <label>
                        Traffic per variant, %
                        <input
                          required
                          type="number"
                          min="0.01"
                          max="100"
                          step="0.01"
                          value={createExperimentForm.variant_traffic_percent}
                          onChange={(event) =>
                            updateExperimentField('variant_traffic_percent', event.target.value)
                          }
                        />
                      </label>
                    </div>
                    <label>
                      Layer description
                      <input
                        value={createExperimentForm.layer_description}
                        onChange={(event) =>
                          updateExperimentField('layer_description', event.target.value)
                        }
                      />
                    </label>

                    <details className="advanced-details">
                      <summary>Advanced</summary>
                      <div className="advanced-details-body">
                        <label>
                          Generated salt
                          <input readOnly value="Generated automatically after creation" />
                        </label>
                        <p className="muted">
                          New experiments are always created in <code>DRAFT</code>. Salt is
                          generated by backend and cannot be edited manually.
                        </p>
                      </div>
                    </details>

                    <div className="editor-toolbar">
                      <div>
                        <strong>Variants</strong>
                        <p className="muted">
                          Each variant gets {variantTrafficPercent.toFixed(2)}% of total traffic.
                          Experiment total: {experimentTrafficTotal.toFixed(2)}%
                        </p>
                      </div>
                      <div className="button-row">
                        <button className="ghost-button" type="button" onClick={addVariant}>
                          Add variant
                        </button>
                        <button
                          className="ghost-button"
                          type="button"
                          onClick={() => setCreateExperimentForm(createDefaultExperimentForm())}
                        >
                          Reset form
                        </button>
                      </div>
                    </div>

                    <div className="editor-stack">
                      {createExperimentForm.variants.map((variant, variantIndex) => (
                        <div key={variant.client_id} className="editor-card">
                          <div className="panel-header">
                            <h3>
                              Variant {variantIndex + 1}: {variant.name || variant.key || 'Unnamed'}
                            </h3>
                            <div className="button-row">
                              <button
                                className="ghost-button"
                                type="button"
                                onClick={() => addVariantFlag(variant.client_id)}
                              >
                                Add existing flag
                              </button>
                              <button
                                className="ghost-button"
                                type="button"
                                onClick={() => removeVariant(variant.client_id)}
                                disabled={createExperimentForm.variants.length <= 2}
                              >
                                Remove variant
                              </button>
                            </div>
                          </div>

                          <div className="editor-grid">
                            <label>
                              Variant key
                              <input
                                required
                                value={variant.key}
                                onChange={(event) =>
                                  updateVariantField(
                                    variant.client_id,
                                    'key',
                                    event.target.value
                                  )
                                }
                              />
                            </label>
                            <label>
                              Variant name
                              <input
                                required
                                value={variant.name}
                                onChange={(event) =>
                                  updateVariantField(
                                    variant.client_id,
                                    'name',
                                    event.target.value
                                  )
                                }
                              />
                            </label>
                            <div className="readonly-field">
                              <span className="readonly-label">Traffic allocation</span>
                              <strong>{variantTrafficPercent.toFixed(2)}%</strong>
                            </div>
                          </div>
                          <label>
                            Variant description
                            <input
                              value={variant.description}
                              onChange={(event) =>
                                updateVariantField(
                                  variant.client_id,
                                  'description',
                                  event.target.value
                                )
                              }
                            />
                          </label>

                          <div className="editor-stack">
                            {variant.flags.map((flag, flagIndex) => (
                              <div key={flag.client_id} className="editor-card editor-card-nested">
                                <div className="panel-header">
                                  <h3>
                                    Flag {flagIndex + 1}
                                  </h3>
                                  <button
                                    className="ghost-button"
                                    type="button"
                                    onClick={() =>
                                      removeVariantFlag(variant.client_id, flag.client_id)
                                    }
                                    disabled={variant.flags.length === 1}
                                  >
                                    Remove flag
                                  </button>
                                </div>
                                <div className="editor-grid">
                                  <label>
                                    Existing flag
                                    <select
                                      required
                                      value={flag.flag_id}
                                      onChange={(event) =>
                                        updateVariantFlagField(
                                          variant.client_id,
                                          flag.client_id,
                                          'flag_id',
                                          event.target.value
                                        )
                                      }
                                    >
                                      <option value="">Select flag</option>
                                      {flagsCatalog.map((catalogFlag) => (
                                        <option key={catalogFlag.id} value={catalogFlag.id}>
                                          {catalogFlag.name} ({catalogFlag.key})
                                        </option>
                                      ))}
                                    </select>
                                  </label>
                                  <label>
                                    Variant value
                                    <input
                                      required
                                      value={flag.variant_value}
                                      onChange={(event) =>
                                        updateVariantFlagField(
                                          variant.client_id,
                                          flag.client_id,
                                          'variant_value',
                                          event.target.value
                                        )
                                      }
                                    />
                                  </label>
                                </div>
                                {(() => {
                                  const selectedFlag = flagsCatalog.find(
                                    (catalogFlag) => String(catalogFlag.id) === String(flag.flag_id)
                                  );
                                  return selectedFlag ? (
                                    <div className="flag-meta">
                                      <span>
                                        <strong>{selectedFlag.name}</strong> · <code>{selectedFlag.key}</code>
                                      </span>
                                      <span className="muted">
                                        Type: {selectedFlag.value_type} · Default:{' '}
                                        {selectedFlag.default_value || 'not set'}
                                      </span>
                                      <span className="muted">
                                        {selectedFlag.description || 'No description'}
                                      </span>
                                    </div>
                                  ) : (
                                    <p className="muted">
                                      Select an existing flag from the catalog for this variant.
                                    </p>
                                  );
                                })()}
                              </div>
                            ))}
                          </div>
                        </div>
                      ))}
                    </div>

                    <button type="submit" disabled={experimentsLoading}>
                      {experimentsLoading ? 'Creating...' : 'Create experiment'}
                    </button>
                  </form>
                ) : (
                  <p className="muted">
                    You can inspect experiments and flags in this service, but only admins can
                    create new ones.
                  </p>
                )}

                {flagsCatalog.length === 0 ? (
                  <p className="muted">
                    Create at least one flag in the catalog before creating an experiment. Variants
                    can reference only existing flags.
                  </p>
                ) : null}
              </section>

              <section className="subpanel">
                <div className="panel-header">
                  <h2>Flag Catalog</h2>
                  <button className="ghost-button" type="button" onClick={loadFlags}>
                    Refresh
                  </button>
                </div>

                {isAdmin ? (
                  <form className="form" onSubmit={handleCreateFlagSubmit}>
                    <div className="editor-grid">
                      <label>
                        Flag key
                        <input
                          required
                          value={createFlagForm.key}
                          onChange={(event) =>
                            setCreateFlagForm((previous) => ({
                              ...previous,
                              key: event.target.value,
                            }))
                          }
                        />
                      </label>
                      <label>
                        Flag name
                        <input
                          required
                          value={createFlagForm.name}
                          onChange={(event) =>
                            setCreateFlagForm((previous) => ({
                              ...previous,
                              name: event.target.value,
                            }))
                          }
                        />
                      </label>
                      <label>
                        Value type
                        <select
                          value={createFlagForm.value_type}
                          onChange={(event) =>
                            setCreateFlagForm((previous) => ({
                              ...previous,
                              value_type: event.target.value,
                            }))
                          }
                        >
                          {flagTypeOptions.map((type) => (
                            <option key={type} value={type}>
                              {type}
                            </option>
                          ))}
                        </select>
                      </label>
                      <label>
                        Default value
                        <input
                          value={createFlagForm.default_value}
                          onChange={(event) =>
                            setCreateFlagForm((previous) => ({
                              ...previous,
                              default_value: event.target.value,
                            }))
                          }
                        />
                      </label>
                    </div>
                    <label>
                      Description
                      <input
                        value={createFlagForm.description}
                        onChange={(event) =>
                          setCreateFlagForm((previous) => ({
                            ...previous,
                            description: event.target.value,
                          }))
                        }
                      />
                    </label>
                    <button type="submit" disabled={flagsLoading}>
                      {flagsLoading ? 'Saving...' : 'Create flag'}
                    </button>
                  </form>
                ) : null}

                <div className="list">
                  {flagsCatalog.length === 0 ? (
                    <p className="muted">
                      {flagsLoading ? 'Loading flags...' : 'No flags registered yet.'}
                    </p>
                  ) : (
                    flagsCatalog.map((flag) => (
                      <div key={flag.id} className="list-item">
                        <div className="list-item-head">
                          <strong>{flag.name}</strong>
                          <span className="badge">{flag.value_type}</span>
                        </div>
                        <code>{flag.key}</code>
                        <span>{flag.description || 'No description'}</span>
                        <span className="muted">
                          Default value: {flag.default_value || 'not set'}
                        </span>
                      </div>
                    ))
                  )}
                </div>
              </section>

              <section className="subpanel">
                <div className="panel-header">
                  <h2>Feature Catalog</h2>
                  <button className="ghost-button" type="button" onClick={loadFeatures}>
                    Refresh
                  </button>
                </div>

                {isAdmin ? (
                  <form className="form" onSubmit={handleCreateFeatureSubmit}>
                    <div className="editor-grid">
                      <label>
                        Feature key
                        <input
                          required
                          value={createFeatureForm.key}
                          onChange={(event) =>
                            setCreateFeatureForm((previous) => ({
                              ...previous,
                              key: event.target.value,
                            }))
                          }
                        />
                      </label>
                      <label>
                        Feature name
                        <input
                          required
                          value={createFeatureForm.name}
                          onChange={(event) =>
                            setCreateFeatureForm((previous) => ({
                              ...previous,
                              name: event.target.value,
                            }))
                          }
                        />
                      </label>
                    </div>
                    <label>
                      Description
                      <input
                        value={createFeatureForm.description}
                        onChange={(event) =>
                          setCreateFeatureForm((previous) => ({
                            ...previous,
                            description: event.target.value,
                          }))
                        }
                      />
                    </label>
                    <button type="submit" disabled={featuresLoading}>
                      {featuresLoading ? 'Saving...' : 'Create feature'}
                    </button>
                  </form>
                ) : (
                  <p className="muted">
                    You can inspect features used for event ingestion, but only admins can create
                    new ones.
                  </p>
                )}

                <p className="muted">
                  Client services can send runtime events to <code>/api/v1/runtime/events</code>
                  {' '}using <code>X-API-Key</code> and a registered <code>feature_key</code>.
                </p>

                <div className="list">
                  {featuresCatalog.length === 0 ? (
                    <p className="muted">
                      {featuresLoading ? 'Loading features...' : 'No features registered yet.'}
                    </p>
                  ) : (
                    featuresCatalog.map((feature) => (
                      <div key={feature.id} className="list-item">
                        <div className="list-item-head">
                          <strong>{feature.name}</strong>
                          <span className="badge">Feature</span>
                        </div>
                        <code>{feature.key}</code>
                        <span>{feature.description || 'No description'}</span>
                      </div>
                    ))
                  )}
                </div>
              </section>

              <section className="subpanel">
                <div className="panel-header">
                  <h2>Metric Catalog</h2>
                  <button className="ghost-button" type="button" onClick={loadMetrics}>
                    Refresh
                  </button>
                </div>

                {isAdmin ? (
                  <form className="form" onSubmit={handleCreateMetricSubmit}>
                    <div className="editor-grid">
                      <label>
                        Metric code
                        <input
                          required
                          value={createMetricForm.code}
                          onChange={(event) =>
                            setCreateMetricForm((previous) => ({
                              ...previous,
                              code: event.target.value,
                            }))
                          }
                        />
                      </label>
                      <label>
                        Metric name
                        <input
                          required
                          value={createMetricForm.name}
                          onChange={(event) =>
                            setCreateMetricForm((previous) => ({
                              ...previous,
                              name: event.target.value,
                            }))
                          }
                        />
                      </label>
                      <label>
                        Metric type
                        <select
                          value={createMetricForm.metric_type}
                          onChange={(event) =>
                            setCreateMetricForm((previous) => ({
                              ...previous,
                              metric_type: event.target.value,
                            }))
                          }
                        >
                          {metricTypeOptions.map((type) => (
                            <option key={type} value={type}>
                              {type}
                            </option>
                          ))}
                        </select>
                      </label>
                      <label>
                        Feature key
                        <select
                          required
                          value={createMetricForm.feature_key}
                          onChange={(event) =>
                            setCreateMetricForm((previous) => ({
                              ...previous,
                              feature_key: event.target.value,
                            }))
                          }
                        >
                        <option value="">Select feature</option>
                        {featuresCatalog.map((feature) => (
                          <option key={feature.id} value={feature.key}>
                            {feature.name} ({feature.key})
                          </option>
                        ))}
                      </select>
                      </label>
                    </div>
                    <label>
                      Description
                      <input
                        value={createMetricForm.description}
                        onChange={(event) =>
                          setCreateMetricForm((previous) => ({
                            ...previous,
                            description: event.target.value,
                          }))
                        }
                      />
                    </label>
                    <button type="submit" disabled={metricsLoading}>
                      {metricsLoading ? 'Saving...' : 'Create metric'}
                    </button>
                  </form>
                ) : (
                  <p className="muted">
                    You can inspect metrics, but only admins can create new ones.
                  </p>
                )}

                <p className="muted">
                  First implementation uses request-level attribution: event rows are matched to an
                  experiment by <code>req_id</code>, assigned variant, and selected
                  <code> feature_key</code>.
                </p>

                <div className="list">
                  {metricsCatalog.length === 0 ? (
                    <p className="muted">
                      {metricsLoading ? 'Loading metrics...' : 'No metrics registered yet.'}
                    </p>
                  ) : (
                    metricsCatalog.map((metric) => (
                      <div key={metric.id} className="list-item">
                        <div className="list-item-head">
                          <strong>{metric.name}</strong>
                          <span className="badge">{metric.metric_type}</span>
                        </div>
                        <code>{metric.code}</code>
                        <span>{metric.description || 'No description'}</span>
                        <span className="muted">
                          Feature: {metric.feature_key} • Unit: {metric.aggregation_unit}
                        </span>
                      </div>
                    ))
                  )}
                </div>
              </section>

              <section className="subpanel">
                <div className="panel-header">
                  <h2>Experiment List</h2>
                  <span className="muted">{experiments.length} total</span>
                </div>

                <div className="list">
                  {experiments.length === 0 ? (
                    <p className="muted">
                      {experimentsLoading ? 'Loading experiments...' : 'No experiments created yet.'}
                    </p>
                  ) : (
                    experiments.map((experiment) => (
                      <div key={experiment.id} className="list-item">
                        <div className="list-item-head">
                          <strong>{experiment.name}</strong>
                          <span className="badge">{experiment.status}</span>
                        </div>
                        <span>
                          Layer: {experiment.layer_name} • Duration: {experiment.duration_days} days
                        </span>
                        <span>{experiment.description || 'No description'}</span>
                        <span className="muted">
                          Variant traffic: {Number(experiment.variant_traffic_percent ?? 0).toFixed(2)}%
                          {' '}each • Total experiment traffic:{' '}
                          {Number(experiment.total_traffic_percent ?? 0).toFixed(2)}%
                        </span>
                        <span className="muted">
                          Start: {experiment.start_at || 'not set'} • End:{' '}
                          {experiment.end_at || 'not set'}
                        </span>
                        <div className="editor-card editor-card-nested">
                          <div className="panel-header">
                            <h3>Metrics</h3>
                            <div className="button-row">
                              <button
                                className="ghost-button"
                                type="button"
                                disabled={metricResultsLoading}
                                onClick={loadMetricResults}
                              >
                                {metricResultsLoading ? 'Refreshing...' : 'Refresh results'}
                              </button>
                            </div>
                          </div>
                          <p className="muted">
                            {metricAutomationEnabled
                              ? 'Metric results are recalculated automatically in the background when new assignments, events, or missing metric bindings are detected.'
                              : 'Metric results can be refreshed here after background calculation completes.'}
                          </p>

                          {isAdmin ? (
                            <div className="editor-grid">
                              <label>
                                Attach existing metric
                                <select
                                  value={selectedMetricIdsByExperiment[experiment.id] ?? ''}
                                  onChange={(event) =>
                                    setSelectedMetricIdsByExperiment((previous) => ({
                                      ...previous,
                                      [experiment.id]: event.target.value,
                                    }))
                                  }
                                >
                                  <option value="">Select metric</option>
                                  {metricsCatalog
                                    .filter(
                                      (metric) =>
                                        !(experimentMetricsByExperiment[String(experiment.id)] ?? []).some(
                                          (item) => item.metric_id === metric.id
                                        )
                                    )
                                    .map((metric) => (
                                      <option key={metric.id} value={metric.id}>
                                        {metric.name} ({metric.code})
                                      </option>
                                    ))}
                                </select>
                              </label>
                              <label>
                                <input
                                  type="checkbox"
                                  checked={Boolean(
                                    metricBindingOptionsByExperiment[experiment.id]?.is_primary
                                  )}
                                  onChange={(event) =>
                                    setMetricBindingOptionsByExperiment((previous) => ({
                                      ...previous,
                                      [experiment.id]: {
                                        is_primary: event.target.checked,
                                        is_guardrail: Boolean(
                                          previous[experiment.id]?.is_guardrail
                                        ),
                                      },
                                    }))
                                  }
                                />
                                {' '}Mark as primary metric
                              </label>
                              <label>
                                <input
                                  type="checkbox"
                                  checked={Boolean(
                                    metricBindingOptionsByExperiment[experiment.id]?.is_guardrail
                                  )}
                                  onChange={(event) =>
                                    setMetricBindingOptionsByExperiment((previous) => ({
                                      ...previous,
                                      [experiment.id]: {
                                        is_primary: Boolean(previous[experiment.id]?.is_primary),
                                        is_guardrail: event.target.checked,
                                      },
                                    }))
                                  }
                                />
                                {' '}Mark as guardrail
                              </label>
                              <div className="button-row">
                                <button
                                  className="ghost-button"
                                  type="button"
                                  disabled={experimentMetricsLoading}
                                  onClick={() => handleAttachMetricToExperiment(experiment.id)}
                                >
                                  {experimentMetricsLoading ? 'Attaching...' : 'Attach metric'}
                                </button>
                              </div>
                            </div>
                          ) : null}

                          <div className="list">
                            {(experimentMetricsByExperiment[String(experiment.id)] ?? []).length === 0 ? (
                              <p className="muted">No metrics attached yet.</p>
                            ) : (
                              (experimentMetricsByExperiment[String(experiment.id)] ?? []).map((metric) => (
                                <div
                                  key={`${experiment.id}-${metric.metric_id}`}
                                  className="list-item list-item-compact"
                                >
                                  <div className="list-item-head">
                                    <strong>{metric.metric_name}</strong>
                                    <span className="badge">{metric.metric_type}</span>
                                  </div>
                                  <code>{metric.metric_code}</code>
                                  <span className="muted">
                                    Feature: {metric.feature_key} • Unit: {metric.aggregation_unit}
                                  </span>
                                  <span className="muted">
                                    {[metric.is_primary ? 'primary' : null, metric.is_guardrail ? 'guardrail' : null]
                                      .filter(Boolean)
                                      .join(' • ') || 'regular metric'}
                                  </span>
                                </div>
                              ))
                            )}
                          </div>

                          <div className="list">
                            {(metricResultsByExperiment[String(experiment.id)] ?? []).length === 0 ? (
                              <p className="muted">No metric results calculated yet.</p>
                            ) : (
                              (metricResultsByExperiment[String(experiment.id)] ?? []).map((result) => (
                                <div
                                  key={`${experiment.id}-${result.metric_id}-${result.variant_id}`}
                                  className="list-item list-item-compact"
                                >
                                  <div className="list-item-head">
                                    <strong>{result.metric_name}</strong>
                                    <span className="badge">
                                      {result.variant_name} ({result.variant_key})
                                    </span>
                                  </div>
                                  <span>
                                    Value: {Number(result.value ?? 0).toFixed(4)}
                                    {typeof result.lift === 'number'
                                      ? ` • Lift: ${(result.lift * 100).toFixed(2)}%`
                                      : ''}
                                  </span>
                                  <span className="muted">
                                    {typeof result.std_error === 'number'
                                      ? `SE: ${Number(result.std_error).toFixed(4)}`
                                      : 'SE: n/a'}
                                    {typeof result.p_value === 'number'
                                      ? ` • p-value: ${Number(result.p_value).toFixed(4)}`
                                      : ' • p-value: n/a'}
                                  </span>
                                  <span className="muted">
                                    {typeof result.ci_low === 'number' &&
                                    typeof result.ci_high === 'number'
                                      ? `95% CI: [${Number(result.ci_low).toFixed(4)}, ${Number(result.ci_high).toFixed(4)}]`
                                      : '95% CI: n/a'}
                                  </span>
                                  <span className="muted">
                                    Window: {result.period_start} {'->'} {result.period_end}
                                  </span>
                                </div>
                              ))
                            )}
                          </div>
                        </div>
                        {experiment.available_actions?.length ? (
                          <div className="button-row">
                            {experiment.available_actions.map((action) => (
                              <button
                                key={`${experiment.id}-${action}`}
                                className="ghost-button"
                                type="button"
                                disabled={experimentsLoading}
                                onClick={() => handleExperimentAction(experiment.id, action)}
                              >
                                {getExperimentActionLabel(action)}
                              </button>
                            ))}
                          </div>
                        ) : null}
                        <details className="advanced-details">
                          <summary>Advanced</summary>
                          <div className="advanced-details-body">
                            <label>
                              Salt
                              <input readOnly value={experiment.salt} />
                            </label>
                            <div className="list-item list-item-compact">
                              <div className="list-item-head">
                                <strong>Metric Calculation State</strong>
                                <span
                                  className={getMetricCalculationStateBadgeClass(
                                    experiment.metric_calculation_dirty
                                  )}
                                >
                                  {experiment.metric_calculation_dirty ? 'DIRTY' : 'CLEAN'}
                                </span>
                              </div>
                              <span className="muted">
                                Reason:{' '}
                                {getMetricCalculationReasonLabel(
                                  experiment.metric_calculation_dirty_reason
                                )}
                              </span>
                              <span className="muted">
                                Last assignment:{' '}
                                {experiment.metric_calculation_last_assignment_at || 'n/a'}
                              </span>
                              <span className="muted">
                                Last event: {experiment.metric_calculation_last_event_at || 'n/a'}
                              </span>
                              <span className="muted">
                                Last calculated:{' '}
                                {experiment.metric_calculation_last_calculated_at || 'n/a'}
                              </span>
                            </div>
                            <p className="muted">
                              Salt is generated automatically and is read-only to keep assignment
                              logic stable.
                            </p>
                          </div>
                        </details>
                        <div className="variant-list">
                          {experiment.variants.map((variant) => (
                            <div key={variant.id} className="editor-card editor-card-nested">
                              <div className="list-item-head">
                                <strong>
                                  {variant.name} ({variant.key})
                                </strong>
                                <span className="muted">
                                  {Number(variant.traffic_percent ?? 0).toFixed(2)}%
                                </span>
                              </div>
                              <span className="muted">
                                Variant ID: <code>{variant.id}</code>
                              </span>
                              <span>{variant.description || 'No description'}</span>
                              <div className="flag-chip-row">
                                {variant.flags.map((flag) => (
                                  <div key={`${variant.id}-${flag.flag_id}`} className="flag-chip">
                                    <strong>{flag.flag_key}</strong>
                                    <span>{flag.variant_value}</span>
                                    <span className="muted">
                                      default {flag.default_value || 'not set'} • {flag.value_type}
                                    </span>
                                  </div>
                                ))}
                              </div>
                            </div>
                          ))}
                        </div>
                      </div>
                    ))
                  )}
                </div>
              </section>

              <section className="subpanel">
                <div className="panel-header">
                  <h2>Config Versions</h2>
                  <div className="button-row">
                    <button className="ghost-button" type="button" onClick={loadConfigVersions}>
                      Refresh
                    </button>
                    {isAdmin ? (
                      <button
                        type="button"
                        disabled={configVersionsLoading}
                        onClick={handleBuildConfigVersion}
                      >
                        {configVersionsLoading ? 'Building...' : 'Build config version'}
                      </button>
                    ) : null}
                  </div>
                </div>

                {!isAdmin ? (
                  <p className="muted">
                    You can inspect config versions, but only admins can trigger a new build.
                  </p>
                ) : (
                  <p className="muted">
                    {configAutomationEnabled
                      ? 'Automatic background config builder is enabled. Manual build forces an immediate version generation.'
                      : 'Manual build moves queued experiments into config and excludes already finished running experiments.'}
                  </p>
                )}

                <div className="list">
                  {configVersions.length === 0 ? (
                    <p className="muted">
                      {configVersionsLoading
                        ? 'Loading config versions...'
                        : 'No config versions built yet.'}
                    </p>
                  ) : (
                    configVersions.map((configVersion) => (
                      <div key={configVersion.id} className="list-item">
                        <div className="list-item-head">
                          <strong>Version {configVersion.version}</strong>
                          <span className="badge">{configVersion.experiment_count} experiments</span>
                        </div>
                        <span className="muted">{configVersion.created_at}</span>
                        <details className="advanced-details">
                          <summary>Snapshot JSON</summary>
                          <div className="advanced-details-body">
                            <pre className="json-preview">
                              {JSON.stringify(configVersion.config_json, null, 2)}
                            </pre>
                          </div>
                        </details>
                      </div>
                    ))
                  )}
                </div>
              </section>
            </div>
          ) : null}

          {activeTab === 'users' && isAuthorized ? (
            <div className="tab-grid tab-grid-single">
              <section className="subpanel">
                <div className="panel-header">
                  <h2>Platform Users</h2>
                  <button className="ghost-button" type="button" onClick={loadUsers}>
                    Refresh
                  </button>
                </div>
                {isAdmin ? (
                  <form className="form" onSubmit={handleCreateUserSubmit}>
                    <label>
                      Email
                      <input
                        required
                        type="email"
                        value={createUserForm.email}
                        onChange={(event) =>
                          setCreateUserForm((previous) => ({
                            ...previous,
                            email: event.target.value,
                          }))
                        }
                      />
                    </label>
                    <label>
                      Full name
                      <input
                        value={createUserForm.full_name}
                        onChange={(event) =>
                          setCreateUserForm((previous) => ({
                            ...previous,
                            full_name: event.target.value,
                          }))
                        }
                      />
                    </label>
                    <label>
                      Password
                      <input
                        required
                        type="password"
                        value={createUserForm.password}
                        onChange={(event) =>
                          setCreateUserForm((previous) => ({
                            ...previous,
                            password: event.target.value,
                          }))
                        }
                      />
                    </label>
                    <label>
                      Role
                      <select
                        value={createUserForm.role_code}
                        onChange={(event) =>
                          setCreateUserForm((previous) => ({
                            ...previous,
                            role_code: event.target.value,
                          }))
                        }
                      >
                        {roleOptions.map((role) => (
                          <option key={role} value={role}>
                            {role}
                          </option>
                        ))}
                      </select>
                    </label>
                    <button type="submit" disabled={usersLoading}>
                      {usersLoading ? 'Saving...' : 'Create user'}
                    </button>
                  </form>
                ) : (
                  <p className="muted">
                    You can view users in this service, but only admins can create new ones.
                  </p>
                )}

                <div className="list">
                  {users.length === 0 ? (
                    <p className="muted">No users loaded yet.</p>
                  ) : (
                    users.map((user) => (
                      <div key={user.id} className="list-item">
                        <strong>{user.email}</strong>
                        <span>{user.full_name || 'No name'}</span>
                        <div className="role-badge-row">
                          {getRoleCodes(user).map((roleCode) => (
                            <span key={roleCode} className={getRoleBadgeClass(roleCode)}>
                              {roleCode}
                            </span>
                          ))}
                          <span className="muted">Status: {user.status}</span>
                        </div>
                      </div>
                    ))
                  )}
                </div>
              </section>
            </div>
          ) : null}

          {activeTab === 'api-keys' && canViewApiKeys ? (
            <div className="tab-grid tab-grid-single">
              <section className="subpanel">
                <div className="panel-header">
                  <h2>API Keys</h2>
                  <button className="ghost-button" type="button" onClick={loadApiKeys}>
                    Refresh
                  </button>
                </div>

                {isAdmin ? (
                  <form className="form" onSubmit={handleCreateApiKeySubmit}>
                    <label>
                      Key name
                      <input
                        required
                        value={createApiKeyForm.name}
                        onChange={(event) =>
                          setCreateApiKeyForm((previous) => ({
                            ...previous,
                            name: event.target.value,
                          }))
                        }
                      />
                    </label>
                    <button type="submit" disabled={apiKeysLoading}>
                      {apiKeysLoading ? 'Creating...' : 'Create API key'}
                    </button>
                  </form>
                ) : (
                  <p className="muted">
                    You can inspect API keys for this service, but only admins can create new ones.
                  </p>
                )}

                {latestCreatedApiKey ? (
                  <div className="secret-box">
                    <span>Latest generated API key. Copy it now, it will not be shown again.</span>
                    <strong>{latestCreatedApiKey.name}</strong>
                    <code>{latestCreatedApiKey.plain_api_key}</code>
                    <div className="button-row">
                      <button
                        className="ghost-button"
                        type="button"
                        onClick={() => copyToClipboard(latestCreatedApiKey.plain_api_key, 'API key')}
                      >
                        Copy API key
                      </button>
                      <button
                        className="ghost-button"
                        type="button"
                        onClick={() => setLatestCreatedApiKey(null)}
                      >
                        Dismiss
                      </button>
                    </div>
                  </div>
                ) : null}

                <div className="list">
                  {apiKeys.length === 0 ? (
                    <p className="muted">No API keys loaded yet.</p>
                  ) : (
                    apiKeys.map((apiKey) => (
                      <div key={apiKey.id} className="list-item">
                        <strong>{apiKey.name}</strong>
                        <span>ID: {apiKey.id}</span>
                        <span>
                          {apiKey.status} • {apiKey.created_at}
                        </span>
                        {isAdmin && apiKey.status !== 'REVOKED' ? (
                          <button
                            className="ghost-button"
                            type="button"
                            onClick={() => handleRevokeApiKey(apiKey.id)}
                          >
                            Revoke API key
                          </button>
                        ) : null}
                      </div>
                    ))
                  )}
                </div>
              </section>
            </div>
          ) : null}
        </article>
      </section>
    </main>
  );
}

export default App;

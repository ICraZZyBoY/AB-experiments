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

const roleOptions = ['ADMIN', 'ANALYST', 'DEVELOPER'];

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

  const [users, setUsers] = useState([]);
  const [apiKeys, setApiKeys] = useState([]);
  const [loginServiceOptions, setLoginServiceOptions] = useState([]);
  const [latestCreatedApiKey, setLatestCreatedApiKey] = useState(null);

  const [registerLoading, setRegisterLoading] = useState(false);
  const [loginLoading, setLoginLoading] = useState(false);
  const [usersLoading, setUsersLoading] = useState(false);
  const [apiKeysLoading, setApiKeysLoading] = useState(false);

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
      tabs.push({ id: 'users', label: 'Users' });
    }
    if (canViewApiKeys) {
      tabs.push({ id: 'api-keys', label: 'API Keys' });
    }
    return tabs;
  }, [isAuthorized, canViewApiKeys]);

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
    setLoginServiceOptions([]);
    setLatestCreatedApiKey(null);
    setLoginForm(initialLoginForm);
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
  }, [isAuthorized, canViewApiKeys, sessionToken]);

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
          <h1>Service onboarding and access management</h1>
          <p className="subtitle">
            A cleaner workspace for auth, users, and API keys with role-based tabs and quick
            actions.
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

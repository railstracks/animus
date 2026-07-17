import { defineStore } from 'pinia';
import { ref, computed } from 'vue';

export const useAuthStore = defineStore('auth', () => {
  const token = ref(localStorage.getItem('animus_auth_token') || '');
  const userId = ref('');
  const username = ref('');
  const role = ref('');
  const authRequired = ref(false);
  const hasUsers = ref(false);
  const hasStaticToken = ref(false);

  const isAuthenticated = computed(() => !!token.value);

  function setToken(t: string) {
    token.value = t;
    if (t) {
      localStorage.setItem('animus_auth_token', t);
    } else {
      localStorage.removeItem('animus_auth_token');
    }
  }

  function setUser(u: { id: string; username: string; role: string }) {
    userId.value = u.id;
    username.value = u.username;
    role.value = u.role;
  }

  async function checkStatus() {
    try {
      const resp = await fetch('/api/v1/auth/status');
      const data = await resp.json();
      authRequired.value = data.auth_required ?? false;
      hasUsers.value = data.has_users ?? false;
      hasStaticToken.value = data.has_static_token ?? false;
      return data;
    } catch {
      return { auth_required: false, has_users: false, has_static_token: false };
    }
  }

  async function fetchMe() {
    if (!token.value) return null;
    try {
      const resp = await fetch('/api/v1/auth/me', {
        headers: { Authorization: `Bearer ${token.value}` },
      });
      if (resp.status === 401) {
        logout();
        return null;
      }
      const data = await resp.json();
      if (data.auth_method === 'session') {
        setUser({ id: data.user_id, username: data.username, role: data.role });
      }
      return data;
    } catch {
      return null;
    }
  }

  async function login(user: string, pass: string): Promise<{ ok: boolean; error?: string }> {
    const resp = await fetch('/api/v1/auth/login', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ username: user, password: pass }),
    });
    const data = await resp.json();
    if (resp.ok) {
      setToken(data.token);
      setUser({ id: data.user_id, username: data.username, role: data.role });
      return { ok: true };
    }
    return { ok: false, error: data.error || 'Login failed' };
  }

  async function setup(user: string, pass: string): Promise<{ ok: boolean; error?: string }> {
    const resp = await fetch('/api/v1/auth/setup', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json', Authorization: `Bearer ${token.value}` },
      body: JSON.stringify({ username: user, password: pass, role: 'admin' }),
    });
    const data = await resp.json();
    if (resp.ok) {
      // Now login with the new account
      return login(user, pass);
    }
    return { ok: false, error: data.error || 'Setup failed' };
  }

  async function setupWithStaticToken(staticToken: string, user: string, pass: string): Promise<{ ok: boolean; error?: string }> {
    const resp = await fetch('/api/v1/auth/setup', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json', Authorization: `Bearer ${staticToken}` },
      body: JSON.stringify({ username: user, password: pass, role: 'admin' }),
    });
    const data = await resp.json();
    if (resp.ok) {
      // Login with the new account
      return login(user, pass);
    }
    return { ok: false, error: data.error || 'Setup failed' };
  }

  function logout() {
    // Best-effort token revocation
    if (token.value) {
      fetch('/api/v1/auth/logout', {
        method: 'POST',
        headers: { Authorization: `Bearer ${token.value}` },
      }).catch(() => {});
    }
    setToken('');
    userId.value = '';
    username.value = '';
    role.value = '';
  }

  return {
    token, userId, username, role,
    authRequired, hasUsers, hasStaticToken,
    isAuthenticated,
    setToken, setUser, checkStatus, fetchMe,
    login, setup, setupWithStaticToken, logout,
  };
});

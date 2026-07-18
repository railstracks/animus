export type HttpMethod = 'GET' | 'POST' | 'PUT' | 'PATCH' | 'DELETE';

const ADMIN_TOKEN_KEY = 'animus_auth_token';

export function resolveApiBaseUrl(): string {
  const fromEnv = import.meta.env.VITE_API_BASE_URL as string | undefined;
  if (fromEnv && fromEnv.trim().length > 0) {
    return fromEnv.replace(/\/$/, '');
  }

  if (typeof window !== 'undefined' && window.location?.origin) {
    return window.location.origin;
  }

  return 'http://127.0.0.1:8080';
}

export function readAdminToken(): string {
  if (typeof localStorage === 'undefined') {
    return '';
  }
  return localStorage.getItem(ADMIN_TOKEN_KEY) ?? '';
}

export function writeAdminToken(token: string): void {
  if (typeof localStorage === 'undefined') {
    return;
  }
  const trimmed = token.trim();
  if (trimmed.length == 0) {
    localStorage.removeItem(ADMIN_TOKEN_KEY);
    return;
  }
  localStorage.setItem(ADMIN_TOKEN_KEY, trimmed);
}

function withAuthHeaders(headers: Headers, token: string): Headers {
  if (token.trim().length > 0) {
    headers.set('Authorization', `Bearer ${token.trim()}`);
  }
  return headers;
}

export async function apiRequest<T = unknown>(
  method: HttpMethod,
  path: string,
  payload?: unknown,
  tokenOverride = ''
): Promise<T> {
  const headers = withAuthHeaders(new Headers(), tokenOverride || readAdminToken());
  headers.set('Accept', 'application/json');

  const init: RequestInit = {
    method,
    headers
  };

  if (payload !== undefined) {
    headers.set('Content-Type', 'application/json');
    init.body = JSON.stringify(payload);
  }

  const response = await fetch(`${resolveApiBaseUrl()}${path}`, init);
  const text = await response.text();

  if (!response.ok) {
    throw new Error(`HTTP ${response.status}: ${text}`);
  }

  if (text.length == 0) {
    return {} as T;
  }

  return JSON.parse(text) as T;
}

export function apiGet<T = unknown>(path: string, tokenOverride = ''): Promise<T> {
  return apiRequest<T>('GET', path, undefined, tokenOverride);
}

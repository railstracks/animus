import { readAdminToken, resolveApiBaseUrl } from './api';

export interface WsOptions {
  token?: string;
  params?: Record<string, string>;
  onOpen?: () => void;
  onClose?: (ev: CloseEvent) => void;
  onError?: (ev: Event) => void;
  onMessage?: (data: unknown) => void;
}

export function buildWebSocketUrl(path: string, options: WsOptions = {}): string {
  const base = new URL(resolveApiBaseUrl());
  base.protocol = base.protocol === 'https:' ? 'wss:' : 'ws:';
  base.pathname = path;

  const token = options.token ?? readAdminToken();
  if (token.trim().length > 0) {
    base.searchParams.set('token', token.trim());
  }

  if (options.params) {
    for (const [k, v] of Object.entries(options.params)) {
      base.searchParams.set(k, v);
    }
  }

  return base.toString();
}

export function connectJsonWebSocket(path: string, options: WsOptions = {}): WebSocket {
  const socket = new WebSocket(buildWebSocketUrl(path, options));

  socket.onopen = () => {
    options.onOpen?.();
  };
  socket.onclose = (ev) => {
    options.onClose?.(ev);
  };
  socket.onerror = (ev) => {
    options.onError?.(ev);
  };
  socket.onmessage = (ev) => {
    try {
      options.onMessage?.(JSON.parse(ev.data as string));
    } catch {
      options.onMessage?.(ev.data);
    }
  };

  return socket;
}

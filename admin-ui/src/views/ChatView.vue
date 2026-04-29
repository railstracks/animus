<script setup lang="ts">
import MarkdownIt from 'markdown-it';
import { computed, nextTick, onBeforeUnmount, onMounted, ref } from 'vue';

import { apiGet, readAdminToken, writeAdminToken } from '../lib/api';
import { connectJsonWebSocket } from '../lib/ws';

type WsConnectionState = 'closed' | 'connecting' | 'open';

interface SessionSummary {
  id: string;
  source: string;
  conversation_id: string;
  thread_id: string;
  message_count: number;
  last_active_unix_ms: number;
}

interface SessionTurn {
  turn_id: number;
  role: string;
  content: string;
  unix_ms: number;
}

interface SessionHistoryResponse {
  items?: SessionTurn[];
}

interface SessionContextResponse {
  session_id?: number | string;
  memory_layers_loaded?: string[];
  tools_available?: string[];
  note?: string;
  token_budget_state?: {
    configured_token_budget_per_prompt?: number;
    consumed_tokens?: number;
    remaining_tokens?: number;
  };
}

interface UiMessage {
  id: string;
  role: 'user' | 'assistant' | 'system';
  content: string;
  createdUnixMs: number;
  streaming?: boolean;
  interrupted?: boolean;
}

interface SessionContextState {
  layers: string[];
  tools: string[];
  note: string;
  tokenBudgetConfigured: number;
  tokenBudgetConsumed: number;
  tokenBudgetRemaining: number;
}

interface WsEnvelope {
  type?: string;
  [key: string]: unknown;
}

const NEW_SESSION_KEY = '__new__';

const markdown = new MarkdownIt({
  html: false,
  linkify: true,
  breaks: true
});

const socket = ref<WebSocket | null>(null);
const reconnectTimer = ref<number | null>(null);
const wsState = ref<WsConnectionState>('closed');
const lastWsError = ref('');

const adminToken = ref(readAdminToken());
const draft = ref('');
const isGenerating = ref(false);

const sessions = ref<SessionSummary[]>([]);
const selectedSession = ref<string>('new');
const currentSessionId = ref<string>('');

const messagesBySession = ref<Record<string, UiMessage[]>>({
  [NEW_SESSION_KEY]: []
});
const streamingMessageIdBySession = ref<Record<string, string>>({});
const historyLoadedBySession = ref<Record<string, boolean>>({});
const contextBySession = ref<Record<string, SessionContextState>>({});

const messagesContainer = ref<HTMLElement | null>(null);
const autoScroll = ref(true);

const currentSessionKey = computed(() => (currentSessionId.value ? currentSessionId.value : NEW_SESSION_KEY));

const activeMessages = computed<UiMessage[]>(() => {
  const key = currentSessionKey.value;
  if (!messagesBySession.value[key]) {
    messagesBySession.value[key] = [];
  }
  return messagesBySession.value[key];
});

const activeContext = computed<SessionContextState>(() => {
  const key = currentSessionKey.value;
  return (
    contextBySession.value[key] ?? {
      layers: [],
      tools: [],
      note: 'No context loaded yet.',
      tokenBudgetConfigured: 0,
      tokenBudgetConsumed: 0,
      tokenBudgetRemaining: 0
    }
  );
});

const sessionItems = computed(() => {
  const base = [{ title: 'New Session', value: 'new' }];
  const mapped = sessions.value
    .slice()
    .sort((a, b) => b.last_active_unix_ms - a.last_active_unix_ms)
    .map((s) => ({
      title: `#${s.id} • ${s.source} (${s.message_count})`,
      value: s.id
    }));
  return base.concat(mapped);
});

const showScrollToBottom = computed(() => !autoScroll.value);

function nowUnixMs(): number {
  return Date.now();
}

function asString(value: unknown): string {
  if (typeof value === 'string') {
    return value;
  }
  if (typeof value === 'number' || typeof value === 'bigint') {
    return String(value);
  }
  return '';
}

function renderMessageMarkdown(message: UiMessage): string {
  if (message.role !== 'assistant') {
    return '';
  }
  return markdown.render(message.content);
}

function ensureMessageBucket(sessionKey: string): UiMessage[] {
  if (!messagesBySession.value[sessionKey]) {
    messagesBySession.value[sessionKey] = [];
  }
  return messagesBySession.value[sessionKey];
}

function pushMessage(sessionKey: string, message: UiMessage): void {
  const bucket = ensureMessageBucket(sessionKey);
  bucket.push(message);
  maybeAutoScroll();
}

function maybeAutoScroll(force = false): void {
  const el = messagesContainer.value;
  if (!el) {
    return;
  }
  if (!autoScroll.value && !force) {
    return;
  }
  nextTick(() => {
    if (!messagesContainer.value) {
      return;
    }
    messagesContainer.value.scrollTop = messagesContainer.value.scrollHeight;
  });
}

function onMessagesScroll(): void {
  const el = messagesContainer.value;
  if (!el) {
    return;
  }
  const distanceFromBottom = el.scrollHeight - (el.scrollTop + el.clientHeight);
  autoScroll.value = distanceFromBottom < 96;
}

function scrollToBottom(): void {
  autoScroll.value = true;
  maybeAutoScroll(true);
}

function clearReconnectTimer(): void {
  if (reconnectTimer.value !== null) {
    window.clearTimeout(reconnectTimer.value);
    reconnectTimer.value = null;
  }
}

function scheduleReconnect(): void {
  clearReconnectTimer();
  reconnectTimer.value = window.setTimeout(() => {
    connectSocket();
  }, 1200);
}

function closeSocket(): void {
  clearReconnectTimer();
  if (socket.value) {
    try {
      socket.value.close();
    } catch {
      // noop
    }
    socket.value = null;
  }
  wsState.value = 'closed';
}

function requestSessionsOverSocket(): void {
  if (!socket.value || socket.value.readyState !== WebSocket.OPEN) {
    return;
  }
  socket.value.send(JSON.stringify({ type: 'list_sessions' }));
}

function connectSocket(): void {
  if (socket.value && (socket.value.readyState === WebSocket.OPEN || socket.value.readyState === WebSocket.CONNECTING)) {
    return;
  }

  wsState.value = 'connecting';
  lastWsError.value = '';

  socket.value = connectJsonWebSocket('/ws/chat', {
    token: adminToken.value,
    onOpen: () => {
      wsState.value = 'open';
      requestSessionsOverSocket();
    },
    onClose: () => {
      wsState.value = 'closed';
      isGenerating.value = false;
      if (document.visibilityState === 'visible') {
        scheduleReconnect();
      }
    },
    onError: () => {
      lastWsError.value = 'WebSocket error';
    },
    onMessage: (payload) => {
      handleSocketMessage(payload);
    }
  });
}

function stopGenerating(): void {
  const sid = currentSessionId.value;
  if (!socket.value || socket.value.readyState !== WebSocket.OPEN) {
    isGenerating.value = false;
    return;
  }
  socket.value.send(JSON.stringify({ type: 'stop' }));
  if (sid) {
    const streamingId = streamingMessageIdBySession.value[sid];
    if (streamingId) {
      const bucket = ensureMessageBucket(sid);
      const msg = bucket.find((item) => item.id === streamingId);
      if (msg) {
        msg.streaming = false;
        msg.interrupted = true;
      }
      delete streamingMessageIdBySession.value[sid];
    }
  }
  isGenerating.value = false;
}

async function loadSessionHistory(sessionId: string): Promise<void> {
  if (historyLoadedBySession.value[sessionId]) {
    return;
  }
  const payload = await apiGet<SessionHistoryResponse>(
    `/api/v1/sessions/${sessionId}/history?page=1&limit=200`,
    adminToken.value
  );

  const items = Array.isArray(payload.items) ? payload.items : [];
  const chronological = items.slice().reverse();
  messagesBySession.value[sessionId] = chronological.map((turn) => ({
    id: `hist-${sessionId}-${turn.turn_id}`,
    role: turn.role === 'assistant' ? 'assistant' : turn.role === 'user' ? 'user' : 'system',
    content: turn.content ?? '',
    createdUnixMs: turn.unix_ms ?? nowUnixMs(),
    streaming: false,
    interrupted: false
  }));
  historyLoadedBySession.value[sessionId] = true;
}

async function loadSessionContext(sessionId: string): Promise<void> {
  const payload = await apiGet<SessionContextResponse>(
    `/api/v1/sessions/${sessionId}/context`,
    adminToken.value
  );

  contextBySession.value[sessionId] = {
    layers: Array.isArray(payload.memory_layers_loaded) ? payload.memory_layers_loaded : [],
    tools: Array.isArray(payload.tools_available) ? payload.tools_available : [],
    note: payload.note ?? '',
    tokenBudgetConfigured: payload.token_budget_state?.configured_token_budget_per_prompt ?? 0,
    tokenBudgetConsumed: payload.token_budget_state?.consumed_tokens ?? 0,
    tokenBudgetRemaining: payload.token_budget_state?.remaining_tokens ?? 0
  };
}

async function switchSession(value: string): Promise<void> {
  if (value === 'new') {
    currentSessionId.value = '';
    if (!messagesBySession.value[NEW_SESSION_KEY]) {
      messagesBySession.value[NEW_SESSION_KEY] = [];
    }
    maybeAutoScroll(true);
    return;
  }

  currentSessionId.value = value;
  try {
    await loadSessionHistory(value);
    await loadSessionContext(value);
  } catch (err) {
    const bucket = ensureMessageBucket(value);
    bucket.push({
      id: `system-${nowUnixMs()}`,
      role: 'system',
      content: `Failed to load session ${value}: ${err instanceof Error ? err.message : String(err)}`,
      createdUnixMs: nowUnixMs()
    });
  }
  maybeAutoScroll(true);
}

function adoptNewSessionId(sessionId: string): void {
  if (!sessionId) {
    return;
  }

  const previous = ensureMessageBucket(NEW_SESSION_KEY);
  if (previous.length > 0) {
    const existing = ensureMessageBucket(sessionId);
    if (existing.length === 0) {
      messagesBySession.value[sessionId] = previous.slice();
    }
    messagesBySession.value[NEW_SESSION_KEY] = [];
  }

  if (streamingMessageIdBySession.value[NEW_SESSION_KEY]) {
    streamingMessageIdBySession.value[sessionId] = streamingMessageIdBySession.value[NEW_SESSION_KEY];
    delete streamingMessageIdBySession.value[NEW_SESSION_KEY];
  }

  currentSessionId.value = sessionId;
  selectedSession.value = sessionId;
}

function findOrCreateStreamingAssistantMessage(sessionId: string): UiMessage {
  const bucket = ensureMessageBucket(sessionId);
  const existingId = streamingMessageIdBySession.value[sessionId];
  if (existingId) {
    const existing = bucket.find((message) => message.id === existingId);
    if (existing) {
      return existing;
    }
  }

  const created: UiMessage = {
    id: `assistant-stream-${sessionId}-${nowUnixMs()}`,
    role: 'assistant',
    content: '',
    createdUnixMs: nowUnixMs(),
    streaming: true
  };
  bucket.push(created);
  streamingMessageIdBySession.value[sessionId] = created.id;
  return created;
}

function handleSocketMessage(payload: unknown): void {
  if (!payload || typeof payload !== 'object') {
    return;
  }
  const envelope = payload as WsEnvelope;
  const type = asString(envelope.type);

  if (type === 'sessions') {
    const raw = Array.isArray(envelope.data) ? envelope.data : [];
    sessions.value = raw
      .filter((item) => item && typeof item === 'object')
      .map((item) => {
        const obj = item as Record<string, unknown>;
        return {
          id: asString(obj.id),
          source: asString(obj.source),
          conversation_id: asString(obj.conversation_id),
          thread_id: asString(obj.thread_id),
          message_count:
            typeof obj.message_count === 'number' ? obj.message_count : Number(obj.message_count ?? 0),
          last_active_unix_ms:
            typeof obj.last_active_unix_ms === 'number'
              ? obj.last_active_unix_ms
              : Number(obj.last_active_unix_ms ?? 0)
        } satisfies SessionSummary;
      })
      .filter((session) => session.id.length > 0);
    return;
  }

  if (type === 'context') {
    const sid = asString(envelope.session_id);
    if (!sid) {
      return;
    }
    if (!currentSessionId.value) {
      adoptNewSessionId(sid);
    }
    contextBySession.value[sid] = {
      layers: Array.isArray(envelope.layers) ? envelope.layers.map((v) => asString(v)).filter(Boolean) : [],
      tools: Array.isArray(envelope.tools) ? envelope.tools.map((v) => asString(v)).filter(Boolean) : [],
      note: '',
      tokenBudgetConfigured: 0,
      tokenBudgetConsumed: 0,
      tokenBudgetRemaining: 0
    };
    maybeAutoScroll();
    return;
  }

  if (type === 'token') {
    const sid = asString(envelope.session_id) || currentSessionId.value;
    if (!sid) {
      return;
    }
    const content = asString(envelope.content);
    const message = findOrCreateStreamingAssistantMessage(sid);
    message.content += content;
    message.streaming = true;
    if (!currentSessionId.value) {
      adoptNewSessionId(sid);
    }
    maybeAutoScroll();
    return;
  }

  if (type === 'done') {
    const sid = asString(envelope.session_id) || currentSessionId.value;
    const interrupted = Boolean(envelope.interrupted);
    if (sid) {
      const streamingId = streamingMessageIdBySession.value[sid];
      if (streamingId) {
        const bucket = ensureMessageBucket(sid);
        const message = bucket.find((item) => item.id === streamingId);
        if (message) {
          message.streaming = false;
          message.interrupted = interrupted;
        }
        delete streamingMessageIdBySession.value[sid];
      }
      requestSessionsOverSocket();
      void loadSessionContext(sid);
    }
    isGenerating.value = false;
    maybeAutoScroll();
    return;
  }

  if (type === 'error') {
    const text = asString(envelope.message);
    pushMessage(currentSessionKey.value, {
      id: `error-${nowUnixMs()}`,
      role: 'system',
      content: text.length > 0 ? text : 'Unknown websocket error',
      createdUnixMs: nowUnixMs()
    });
    isGenerating.value = false;
    return;
  }
}

async function sendMessage(): Promise<void> {
  if (isGenerating.value) {
    return;
  }

  const content = draft.value.trim();
  if (content.length === 0) {
    return;
  }

  if (!socket.value || socket.value.readyState !== WebSocket.OPEN) {
    connectSocket();
  }
  if (!socket.value || socket.value.readyState !== WebSocket.OPEN) {
    pushMessage(currentSessionKey.value, {
      id: `system-${nowUnixMs()}`,
      role: 'system',
      content: 'WebSocket is not connected yet. Please try again.',
      createdUnixMs: nowUnixMs()
    });
    return;
  }

  const key = currentSessionKey.value;
  pushMessage(key, {
    id: `user-${nowUnixMs()}`,
    role: 'user',
    content,
    createdUnixMs: nowUnixMs()
  });

  const outbound: Record<string, string> = {
    type: 'message',
    content
  };
  if (currentSessionId.value) {
    outbound.session_id = currentSessionId.value;
  }

  draft.value = '';
  isGenerating.value = true;
  socket.value.send(JSON.stringify(outbound));
  maybeAutoScroll(true);
}

function saveToken(): void {
  writeAdminToken(adminToken.value);
  closeSocket();
  connectSocket();
}

onMounted(() => {
  connectSocket();
});

onBeforeUnmount(() => {
  closeSocket();
});

async function refreshSessions(): Promise<void> {
  requestSessionsOverSocket();
  if (currentSessionId.value) {
    await loadSessionContext(currentSessionId.value);
  }
}

async function onSessionSelectionChanged(): Promise<void> {
  await switchSession(selectedSession.value);
}
</script>

<template>
  <div class="chat-layout">
    <section class="chat-panel">
      <v-card class="chat-shell" rounded="xl">
        <div class="chat-toolbar">
          <div class="toolbar-left">
            <v-select
              v-model="selectedSession"
              label="Session"
              variant="solo-filled"
              density="compact"
              hide-details
              :items="sessionItems"
              class="session-select"
              @update:model-value="onSessionSelectionChanged"
            />
            <v-btn size="small" variant="tonal" @click="selectedSession = 'new'; onSessionSelectionChanged()">
              New Session
            </v-btn>
            <v-btn size="small" variant="text" @click="refreshSessions">Refresh</v-btn>
          </div>
          <div class="toolbar-right">
            <span class="status-chip" :data-state="wsState">{{ wsState }}</span>
            <v-btn
              size="small"
              color="warning"
              variant="tonal"
              :disabled="!isGenerating"
              @click="stopGenerating"
            >
              Stop Generating
            </v-btn>
          </div>
        </div>

        <div ref="messagesContainer" class="messages" @scroll="onMessagesScroll">
          <div v-if="activeMessages.length === 0" class="empty-state">
            <h3>Start a conversation</h3>
            <p>Ask Animus about memory, configuration, or a live session to get rolling.</p>
          </div>

          <article
            v-for="message in activeMessages"
            :key="message.id"
            class="message-row"
            :class="[`role-${message.role}`]"
          >
            <div class="bubble">
              <div v-if="message.role === 'assistant'" class="markdown-body" v-html="renderMessageMarkdown(message)" />
              <div v-else class="plain-text">{{ message.content }}</div>
              <div class="meta">
                <span v-if="message.streaming">streaming…</span>
                <span v-else-if="message.interrupted">stopped</span>
                <span v-else>{{ new Date(message.createdUnixMs).toLocaleTimeString() }}</span>
              </div>
            </div>
          </article>
        </div>

        <v-btn
          v-if="showScrollToBottom"
          class="scroll-jump"
          size="small"
          variant="tonal"
          @click="scrollToBottom"
        >
          Jump To Latest
        </v-btn>

        <div class="composer">
          <v-textarea
            v-model="draft"
            auto-grow
            rows="2"
            max-rows="8"
            hide-details
            density="comfortable"
            variant="outlined"
            placeholder="Send a message to Animus..."
            @keydown.enter.exact.prevent="sendMessage"
          />
          <div class="composer-actions">
            <v-text-field
              v-model="adminToken"
              type="password"
              variant="underlined"
              density="compact"
              hide-details
              label="Admin Token (optional)"
              class="token-input"
              @change="saveToken"
            />
            <v-btn color="primary" :disabled="isGenerating || draft.trim().length === 0" @click="sendMessage">
              Send
            </v-btn>
          </div>
          <p v-if="lastWsError" class="ws-error">{{ lastWsError }}</p>
        </div>
      </v-card>
    </section>

    <aside class="context-panel">
      <v-card rounded="xl" class="context-card">
        <v-card-title>Context</v-card-title>
        <v-card-text>
          <p class="context-line"><strong>Session:</strong> {{ currentSessionId || 'new' }}</p>
          <p class="context-line"><strong>Layers:</strong> {{ activeContext.layers.join(', ') || 'none' }}</p>
          <p class="context-line"><strong>Tools:</strong> {{ activeContext.tools.join(', ') || 'none' }}</p>
          <p class="context-line">
            <strong>Budget:</strong>
            {{ activeContext.tokenBudgetRemaining }}/{{ activeContext.tokenBudgetConfigured }}
          </p>
          <p class="context-note">{{ activeContext.note || 'Context snapshot updates as sessions change.' }}</p>
        </v-card-text>
      </v-card>
    </aside>
  </div>
</template>

<style scoped>
.chat-layout {
  display: grid;
  grid-template-columns: minmax(0, 1fr) 320px;
  gap: 1rem;
}

.chat-shell {
  position: relative;
  min-height: calc(100vh - 160px);
  display: grid;
  grid-template-rows: auto 1fr auto;
  overflow: hidden;
  background: linear-gradient(170deg, rgba(23, 26, 35, 0.95), rgba(16, 18, 24, 0.98));
}

.chat-toolbar {
  display: flex;
  justify-content: space-between;
  gap: 0.75rem;
  padding: 0.9rem;
  border-bottom: 1px solid rgba(255, 255, 255, 0.08);
}

.toolbar-left {
  display: flex;
  gap: 0.6rem;
  align-items: center;
  min-width: 0;
}

.session-select {
  min-width: 240px;
}

.toolbar-right {
  display: flex;
  gap: 0.6rem;
  align-items: center;
}

.status-chip {
  text-transform: uppercase;
  letter-spacing: 0.08em;
  font-size: 0.7rem;
  border-radius: 999px;
  border: 1px solid rgba(255, 255, 255, 0.14);
  padding: 0.2rem 0.55rem;
}

.status-chip[data-state='open'] {
  color: #63d471;
  border-color: rgba(99, 212, 113, 0.35);
}

.status-chip[data-state='connecting'] {
  color: #ffbf69;
  border-color: rgba(255, 191, 105, 0.35);
}

.status-chip[data-state='closed'] {
  color: #ff5d73;
  border-color: rgba(255, 93, 115, 0.35);
}

.messages {
  overflow-y: auto;
  padding: 1rem;
  display: flex;
  flex-direction: column;
  gap: 0.75rem;
}

.empty-state {
  margin: auto;
  text-align: center;
  max-width: 380px;
  opacity: 0.82;
}

.message-row {
  display: flex;
}

.message-row.role-user {
  justify-content: flex-end;
}

.message-row.role-assistant,
.message-row.role-system {
  justify-content: flex-start;
}

.bubble {
  max-width: min(80ch, 88%);
  padding: 0.75rem 0.9rem;
  border-radius: 14px;
  border: 1px solid rgba(255, 255, 255, 0.1);
  background: rgba(255, 255, 255, 0.03);
}

.role-user .bubble {
  background: linear-gradient(130deg, rgba(46, 196, 182, 0.2), rgba(58, 134, 255, 0.12));
}

.role-system .bubble {
  background: linear-gradient(130deg, rgba(231, 29, 54, 0.16), rgba(255, 159, 28, 0.12));
}

.plain-text {
  white-space: pre-wrap;
}

.meta {
  margin-top: 0.5rem;
  opacity: 0.64;
  font-size: 0.72rem;
}

.markdown-body :deep(pre) {
  padding: 0.6rem;
  background: rgba(255, 255, 255, 0.06);
  border-radius: 8px;
  overflow-x: auto;
}

.markdown-body :deep(code) {
  font-family: 'IBM Plex Mono', 'Fira Code', monospace;
}

.composer {
  padding: 0.9rem;
  border-top: 1px solid rgba(255, 255, 255, 0.08);
  background: rgba(0, 0, 0, 0.18);
}

.composer-actions {
  margin-top: 0.5rem;
  display: flex;
  justify-content: space-between;
  align-items: center;
  gap: 0.75rem;
}

.token-input {
  flex: 1;
}

.ws-error {
  margin-top: 0.35rem;
  color: #ff5d73;
  font-size: 0.8rem;
}

.scroll-jump {
  position: absolute;
  right: 1rem;
  bottom: 8.7rem;
  z-index: 2;
}

.context-card {
  min-height: calc(100vh - 160px);
  background: linear-gradient(170deg, rgba(22, 24, 33, 0.95), rgba(13, 15, 20, 0.98));
}

.context-line {
  margin: 0.4rem 0;
}

.context-note {
  margin-top: 0.85rem;
  opacity: 0.8;
}

@media (max-width: 1100px) {
  .chat-layout {
    grid-template-columns: 1fr;
  }

  .context-card {
    min-height: auto;
  }
}

@media (max-width: 780px) {
  .chat-toolbar {
    flex-direction: column;
    align-items: stretch;
  }

  .toolbar-left,
  .toolbar-right {
    flex-wrap: wrap;
  }

  .session-select {
    min-width: 0;
    width: 100%;
  }
}
</style>

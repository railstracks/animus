<script setup lang="ts">
import MarkdownIt from 'markdown-it';
import { computed, nextTick, onBeforeUnmount, onMounted, ref, watch } from 'vue';
import { useI18n } from 'vue-i18n';

import { apiGet, apiRequest, readAdminToken, writeAdminToken } from '../lib/api';
import { connectJsonWebSocket } from '../lib/ws';
import AttachmentMessage from '../components/chat/AttachmentMessage.vue';

type WsConnectionState = 'closed' | 'connecting' | 'open';

interface SessionSummary {
  id: string;
  source: string;
  conversation_id: string;
  thread_id: string;
  message_count: number;
  last_active_unix_ms: number;
}

interface AgentOption {
  id: string;
  name: string;
}

interface AgentDetail {
  id: string;
  model?: {
    provider?: string;
    model_id?: string;
  };
  reasoning?: {
    enabled?: boolean;
    effort?: string;
  };
}

interface SessionTurn {
  turn_id: number;
  role: string;
  content: string;
  unix_ms: number;
  tool_call_id?: string;
  tool_name?: string;
  tool_calls?: Array<{
    id?: string;
    name?: string;
    arguments?: string;
  }>;
  thinking_content?: string;
  attachments?: Array<{
    id: string;
    filename: string;
    mime_type: string;
    size_bytes: number;
    filepath: string;
    has_inline_data?: boolean;
    access_token?: string;
  }>;
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
  role: 'user' | 'assistant' | 'system' | 'tool' | 'tool_call' | 'compaction';
  content: string;
  createdUnixMs: number;
  streaming?: boolean;
  interrupted?: boolean;
  thinkingContent?: string;
  thinkingExpanded?: boolean;
  toolName?: string;
  toolCallId?: string;
  toolExpanded?: boolean;
  toolCalls?: Array<{ id?: string; name?: string; arguments?: string }>;
  isSummary?: boolean;
  _compactionExpanded?: boolean;
  _thoughtMarkerBuffer?: string;
  _thoughtMarkerOpen?: boolean;
  attachments?: Array<{
    id: string;
    filename: string;
    mime_type: string;
    size_bytes: number;
    filepath: string;
    has_inline_data?: boolean;
    access_token?: string;
  }>;
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
const { t } = useI18n();

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

// --- Session list pagination + search state ---
const sessionPage = ref(1);
const sessionPageSize = ref(50);
const sessionSearch = ref('');
const sessionTotal = ref(0);
const sessionSearchTimer = ref<ReturnType<typeof setTimeout> | null>(null);
const sessionTotalPages = computed(() =>
  sessionTotal.value > 0 ? Math.ceil(sessionTotal.value / sessionPageSize.value) : 1
);
const selectedSession = ref<string>('new');
const currentSessionId = ref<string>('');
const rightPanelTab = ref<'context' | 'sessions'>('context');
const agents = ref<AgentOption[]>([]);
const selectedAgentId = ref('');

const messagesBySession = ref<Record<string, UiMessage[]>>({
  [NEW_SESSION_KEY]: []
});
const streamingMessageIdBySession = ref<Record<string, string>>({});
const streamingReplyIdBySession = ref<Record<string, string>>({});
const historyLoadedBySession = ref<Record<string, boolean>>({});
const contextBySession = ref<Record<string, SessionContextState>>({});

// Reasoning mode state
const reasoningEnabled = ref(false);
const reasoningEffort = ref('high');
const reasoningInstruction = ref('');
const reasoningLoading = ref(false);

// Token estimate state
interface TokenEstimate {
  estimated_tokens: number;
  context_window: number;
  compaction_threshold: number;
  compaction_trigger_tokens: number;
  needs_compaction: boolean;
  breakdown: {
    system_prompt: number;
    compaction_summary: number;
    session_turns: number;
    draft_message: number;
  };
  model: string;
  provider_id: string;
}
const tokenEstimate = ref<TokenEstimate | null>(null);

// Compaction timeline state
interface CompactionEntry {
  id: number;
  session_id: number;
  message_id: number;
  summary: string;
  token_count: number;
  model: string;
  created_at_unix_ms: number;
}
const compactions = ref<CompactionEntry[]>([]);
const compactionModalOpen = ref(false);
const compactionModalEntry = ref<CompactionEntry | null>(null);
let tokenEstimateTimer: ReturnType<typeof setInterval> | null = null;
let tokenEstimateDebounce: ReturnType<typeof setTimeout> | null = null;

const tokenPercent = computed(() => {
  if (!tokenEstimate.value || tokenEstimate.value.context_window === 0) return 0;
  return Math.min(100, Math.round((tokenEstimate.value.estimated_tokens / tokenEstimate.value.context_window) * 100));
});

const tokenLevel = computed<'ok' | 'warn' | 'danger'>(() => {
  const pct = tokenPercent.value;
  if (pct >= 80) return 'danger';
  if (pct >= 60) return 'warn';
  return 'ok';
});

function formatNumber(n: number): string {
  return n.toLocaleString();
}

function formatTokenK(n: number): string {
  if (n >= 1_000_000) return (n / 1_000_000).toFixed(1).replace(/\.0$/, '') + 'M';
  if (n >= 1_000) return Math.round(n / 1_000) + 'K';
  return String(n);
}

// Provider/model selection state
interface ProviderOption {
  id: string;
  type: string;
  defaultModel: string;
  capabilities?: Capabilities;
}
interface Capabilities {
  model_id: string;
  supports_reasoning: boolean;
  supports_tools: boolean;
  supports_tool_choice: boolean;
  supports_vision: boolean;
  raw_features: string[];
}
const providers = ref<ProviderOption[]>([]);
const selectedProviderId = ref('');
const selectedModel = ref('');
const modelsForProvider = ref<string[]>([]);
const modelsLoading = ref(false);
const fetchedCapabilities = ref<Capabilities | null>(null);

const activeProvider = computed<ProviderOption | undefined>(() =>
  providers.value.find(p => p.id === selectedProviderId.value)
);

const activeCapabilities = computed<Capabilities | undefined>(() =>
  fetchedCapabilities.value ?? activeProvider.value?.capabilities
);

const reasoningSupported = computed(() =>
  activeCapabilities.value?.supports_reasoning ?? true
);

// Fetch capabilities when selected model changes
watch(selectedModel, async (newModel) => {
  if (!newModel || !selectedProviderId.value) return;
  await fetchCapabilitiesForModel(selectedProviderId.value, newModel);
});

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
      note: '',
      tokenBudgetConfigured: 0,
      tokenBudgetConsumed: 0,
      tokenBudgetRemaining: 0
    }
  );
});

const wsStateLabel = computed(() => t(`chat.status.${wsState.value}`));

const sessionItems = computed(() => {
  const base = [{ title: t('chat.newSession'), value: 'new' }];
  const mapped = sessions.value
    .slice()
    .sort((a, b) => b.last_active_unix_ms - a.last_active_unix_ms)
    .map((s) => ({
      title: `#${s.id} • ${s.source} (${s.message_count})`,
      value: s.id
    }));
  return base.concat(mapped);
});

const sortedSessions = computed<SessionSummary[]>(() => sessions.value);

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

interface ThoughtExtractionResult {
  cleanContent: string;
  extractedThought: string;
}

function normalizePossiblyEscapedChannelMarkers(input: string): string {
  let normalized = input;
  normalized = normalized.replace(/\\u003c\|channel\\u003e/gi, '<|channel|>');
  normalized = normalized.replace(/u003c\|channelu003e/gi, '<|channel|>');
  normalized = normalized.replace(/\\u003cchannel\|\\u003e/gi, '<channel|>');
  normalized = normalized.replace(/u003cchannel\|u003e/gi, '<channel|>');
  normalized = normalized.replace(/&lt;\|channel\|&gt;/gi, '<|channel|>');
  normalized = normalized.replace(/&lt;channel\|&gt;/gi, '<channel|>');
  normalized = normalized.replace(/\\u003c/gi, '<');
  normalized = normalized.replace(/\\u003e/gi, '>');
  normalized = normalized.replace(/\\u0026/g, '&');
  return normalized;
}

function extractThoughtChannelContent(input: string): ThoughtExtractionResult {
  const normalized = normalizePossiblyEscapedChannelMarkers(input);
  const extractedParts: string[] = [];
  const thoughtPattern = /<\|channel\|>\s*thought\b([\s\S]*?)(?=<channel\|>|$)/gi;

  let cleanContent = normalized.replace(thoughtPattern, (_full, body: string) => {
    const segment = (body || '').trim();
    if (segment.length > 0) extractedParts.push(segment);
    return '';
  });

  cleanContent = cleanContent.replace(/<\|channel\|>/gi, '');
  cleanContent = cleanContent.replace(/<channel\|>/gi, '');
  cleanContent = cleanContent.trim();

  return {
    cleanContent,
    extractedThought: extractedParts.join('\n\n').trim()
  };
}

function mergeThoughtContent(existing: string | undefined, next: string): string {
  const left = (existing || '').trim();
  const right = next.trim();
  if (!left) return right;
  if (!right) return left;
  return `${left}\n\n${right}`;
}

function appendThoughtSegment(message: UiMessage, segment: string): void {
  const trimmed = segment.trim();
  if (!trimmed) return;
  message.thinkingContent = mergeThoughtContent(message.thinkingContent, trimmed);
  message.thinkingExpanded = true;
}

function longestSuffixThatIsCandidatePrefix(text: string, candidates: string[]): number {
  if (!text) return 0;
  const lower = text.toLowerCase();
  const maxCandidateLen = Math.max(...candidates.map((c) => c.length));
  const maxCheck = Math.min(maxCandidateLen, lower.length);

  for (let len = maxCheck; len > 0; len -= 1) {
    const suffix = lower.slice(lower.length - len);
    if (candidates.some((candidate) => candidate.startsWith(suffix))) {
      return len;
    }
  }
  return 0;
}

function flushAssistantParserState(message: UiMessage): void {
  const pending = message._thoughtMarkerBuffer || '';
  if (!pending) {
    message._thoughtMarkerOpen = false;
    return;
  }
  if (message._thoughtMarkerOpen) {
    appendThoughtSegment(message, pending);
  } else {
    message.content += pending;
  }
  message._thoughtMarkerBuffer = '';
  message._thoughtMarkerOpen = false;
}

function appendAssistantChunk(message: UiMessage, chunk: string): void {
  if (!chunk) return;
  const normalizedChunk = normalizePossiblyEscapedChannelMarkers(chunk);
  let text = (message._thoughtMarkerBuffer || '') + normalizedChunk;
  message._thoughtMarkerBuffer = '';

  const startMarkerPattern = /<\|channel\|>\s*thought\b/i;
  const startCandidateTokens = ['<|channel|>', '<|channel|>thought'];
  const endMarker = '<channel|>';
  const endMarkerLower = endMarker.toLowerCase();

  while (text.length > 0) {
    if (message._thoughtMarkerOpen) {
      const lowerText = text.toLowerCase();
      const closeIndex = lowerText.indexOf(endMarkerLower);
      if (closeIndex === -1) {
        const holdbackLen = longestSuffixThatIsCandidatePrefix(text, [endMarkerLower]);
        const appendable = holdbackLen > 0 ? text.slice(0, text.length - holdbackLen) : text;
        appendThoughtSegment(message, appendable);
        message._thoughtMarkerBuffer = holdbackLen > 0 ? text.slice(text.length - holdbackLen) : '';
        return;
      }
      appendThoughtSegment(message, text.slice(0, closeIndex));
      message._thoughtMarkerOpen = false;
      text = text.slice(closeIndex + endMarker.length);
      continue;
    }

    const startMatch = text.match(startMarkerPattern);
    if (!startMatch || startMatch.index === undefined) {
      const holdbackLen = longestSuffixThatIsCandidatePrefix(text, startCandidateTokens);
      const appendable = holdbackLen > 0 ? text.slice(0, text.length - holdbackLen) : text;
      message.content += appendable;
      message._thoughtMarkerBuffer = holdbackLen > 0 ? text.slice(text.length - holdbackLen) : '';
      return;
    }

    const markerStart = startMatch.index;
    if (markerStart > 0) {
      message.content += text.slice(0, markerStart);
    }
    text = text.slice(markerStart + startMatch[0].length);
    message._thoughtMarkerOpen = true;
  }
}

function renderMessageMarkdown(message: UiMessage): string {
  if (message.role !== 'assistant') {
    return '';
  }
  return markdown.render(message.content);
}

function formatToolContent(content: string): string {
  const input = (content || '').trim();
  if (!input) return '';

  const prettyPrint = (value: unknown): string => {
    if (typeof value === 'string') {
      const nested = value.trim();
      if ((nested.startsWith('{') && nested.endsWith('}')) || (nested.startsWith('[') && nested.endsWith(']'))) {
        try {
          return JSON.stringify(JSON.parse(nested), null, 2);
        } catch {
          return value;
        }
      }
      return value;
    }
    return JSON.stringify(value, null, 2);
  };

  if ((input.startsWith('{') && input.endsWith('}')) || (input.startsWith('[') && input.endsWith(']')) || (input.startsWith('"') && input.endsWith('"'))) {
    try {
      return prettyPrint(JSON.parse(input));
    } catch {
      return content;
    }
  }
  return content;
}

function toggleCompactionDetail(message: UiMessage): void {
  message._compactionExpanded = !message._compactionExpanded;
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
  const offset = (sessionPage.value - 1) * sessionPageSize.value;
  socket.value.send(JSON.stringify({
    type: 'list_sessions',
    limit: sessionPageSize.value,
    offset,
    search: sessionSearch.value,
  }));
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
      lastWsError.value = t('chat.errors.websocket');
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
  const typedChronological = (items as SessionTurn[]).slice().reverse();
  const uiMessages: UiMessage[] = [];
  for (const turn of typedChronological) {
    const role: UiMessage['role'] =
      turn.role === 'assistant'
        ? 'assistant'
        : turn.role === 'user'
          ? 'user'
          : turn.role === 'tool'
            ? 'tool'
            : 'system';
    const parsed = role === 'assistant'
      ? extractThoughtChannelContent(turn.content ?? '')
      : { cleanContent: turn.content ?? '', extractedThought: '' };
    const thinking = role === 'assistant'
      ? mergeThoughtContent(turn.thinking_content || '', parsed.extractedThought)
      : '';

    // For assistant turns with tool_calls, emit tool_call UiMessages
    // as separate messages before the corresponding tool-result turns.
    // For compaction summary turns, show as a compact marker.
    if (turn.is_summary) {
      uiMessages.push({
        id: `hist-${sessionId}-${turn.turn_id}-compaction`,
        role: 'compaction' as UiMessage['role'],
        content: turn.content ?? '',
        createdUnixMs: turn.unix_ms ?? nowUnixMs(),
        streaming: false,
        interrupted: false,
        isSummary: true,
      });
      continue;
    } else if (role === 'assistant' && turn.tool_calls && turn.tool_calls.length > 0) {
      uiMessages.push({
        id: `hist-${sessionId}-${turn.turn_id}`,
        role,
        content: parsed.cleanContent,
        createdUnixMs: turn.unix_ms ?? nowUnixMs(),
        streaming: false,
        interrupted: false,
        thinkingContent: thinking,
        thinkingExpanded: false,
        toolExpanded: false
      });
      // Emit a tool_call message for each call in this assistant turn
      for (const tc of turn.tool_calls) {
        uiMessages.push({
          id: `hist-${sessionId}-${turn.turn_id}-tc-${tc.id || tc.name}`,
          role: 'tool_call' as UiMessage['role'],
          content: tc.arguments || '',
          createdUnixMs: turn.unix_ms ?? nowUnixMs(),
          streaming: false,
          interrupted: false,
          toolName: tc.name || '',
          toolCallId: tc.id || '',
          toolExpanded: false,
          toolCalls: turn.tool_calls
        });
      }
    } else {
      uiMessages.push({
        id: `hist-${sessionId}-${turn.turn_id}`,
        role,
        content: parsed.cleanContent,
        createdUnixMs: turn.unix_ms ?? nowUnixMs(),
        streaming: false,
        interrupted: false,
        thinkingContent: thinking,
        thinkingExpanded: false,
      toolName: turn.tool_name ?? '',
      toolCallId: turn.tool_call_id ?? '',
      toolExpanded: false
    });
    }

    // If this turn has attachments, emit them as a separate message
    if (turn.attachments && turn.attachments.length > 0) {
      uiMessages.push({
        id: `hist-${sessionId}-${turn.turn_id}-att`,
        role: 'assistant' as UiMessage['role'],
        content: '',
        createdUnixMs: turn.unix_ms ?? nowUnixMs(),
        streaming: false,
        interrupted: false,
        attachments: turn.attachments,
      });
  }
  }
  messagesBySession.value[sessionId] = uiMessages;
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

// ---- Token estimate polling ----

async function fetchTokenEstimate(): Promise<void> {
  const sid = currentSessionId.value;
  if (!sid || isGenerating.value || document.visibilityState !== 'visible') return;
  try {
    const draftParam = draft.value.trim()
      ? `?draft=${encodeURIComponent(draft.value.slice(0, 2000))}`
      : '';
    const payload = await apiGet<TokenEstimate>(
      `/api/v1/sessions/${sid}/token-estimate${draftParam}`,
      adminToken.value
    );
    tokenEstimate.value = payload;
  } catch {
    // Silent fail — gauge is best-effort
  }
}

async function fetchCompactions(sessionId: string): Promise<void> {
  try {
    const payload = await apiGet<{ compactions: CompactionEntry[] }>(
      `/api/v1/sessions/${sessionId}/compactions`,
      adminToken.value
    );
    compactions.value = Array.isArray(payload.compactions) ? payload.compactions : [];
  } catch {
    compactions.value = [];
  }
}

function openCompactionModal(entry: CompactionEntry): void {
  compactionModalEntry.value = entry;
  compactionModalOpen.value = true;
}

function closeCompactionModal(): void {
  compactionModalOpen.value = false;
  compactionModalEntry.value = null;
}

function truncateSummary(text: string, maxLen = 120): string {
  if (text.length <= maxLen) return text;
  return text.slice(0, maxLen).trim() + '…';
}

function formatTime(unixMs: number): string {
  return new Date(unixMs).toLocaleString();
}

function startTokenEstimatePolling(): void {
  stopTokenEstimatePolling();
  fetchTokenEstimate();
  tokenEstimateTimer = setInterval(fetchTokenEstimate, 5000);
}

function stopTokenEstimatePolling(): void {
  if (tokenEstimateTimer) {
    clearInterval(tokenEstimateTimer);
    tokenEstimateTimer = null;
  }
}

function onDraftInputDebounced(): void {
  if (tokenEstimateDebounce) clearTimeout(tokenEstimateDebounce);
  tokenEstimateDebounce = setTimeout(fetchTokenEstimate, 600);
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
    await fetchCompactions(value);
  } catch (err) {
    pushMessage(value, {
      id: `system-${nowUnixMs()}`,
      role: 'system',
      content: t('chat.errors.sessionLoadFailed', {
        sessionId: value,
        message: err instanceof Error ? err.message : String(err)
      }),
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

async function selectSessionFromSidebar(sessionId: string): Promise<void> {
  selectedSession.value = sessionId;
  await onSessionSelectionChanged();
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

    // Read total from pagination envelope
    const totalVal = (envelope as Record<string, unknown>).total;
    if (typeof totalVal === 'number') {
      sessionTotal.value = totalVal;
    } else if (typeof totalVal === 'string') {
      sessionTotal.value = Number(totalVal);
    }
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
    appendAssistantChunk(message, content);
    message.streaming = true;
    if (!currentSessionId.value) {
      adoptNewSessionId(sid);
    }
    maybeAutoScroll();
    return;
  }

  if (type === 'text') {
    const sid = asString(envelope.session_id) || currentSessionId.value;
    if (!sid) {
      return;
    }
    const content = asString(envelope.content);
    // Text from tools with stream_to_user result mode (e.g., the final answer).
    // Create or append to a streaming assistant message.
    let replyId = streamingReplyIdBySession.value[sid];
    let bucket = ensureMessageBucket(sid);
    let replyMsg = replyId ? bucket.find((m) => m.id === replyId) : undefined;
    if (!replyMsg) {
      replyMsg = {
        id: `reply-stream-${sid}-${nowUnixMs()}`,
        role: 'assistant',
        content: '',
        createdUnixMs: nowUnixMs(),
        streaming: true
      };
      bucket.push(replyMsg);
      streamingReplyIdBySession.value[sid] = replyMsg.id;
    }
    appendAssistantChunk(replyMsg, content);
    replyMsg.streaming = true;
    if (!currentSessionId.value) {
      adoptNewSessionId(sid);
    }
    maybeAutoScroll();
    return;
  }

  if (type === 'thinking') {
    const sid = asString(envelope.session_id) || currentSessionId.value;
    if (!sid) {
      return;
    }
    const content = asString(envelope.content);
    // Append thinking content to the streaming assistant message
    const message = findOrCreateStreamingAssistantMessage(sid);
    message.thinkingContent = (message.thinkingContent || '') + content;
    message.thinkingExpanded = true;
    message.streaming = true;
    if (!currentSessionId.value) {
      adoptNewSessionId(sid);
    }
    maybeAutoScroll();
    return;
  }

  if (type === 'tool_call') {
    const sid = asString(envelope.session_id) || currentSessionId.value;
    if (!sid) {
      return;
    }
    // Finalize the current streaming assistant message — this is a chain step boundary.
    // The agent has finished generating text and is about to call a tool.
    // When tokens resume after the tool result, a new assistant message will be created.
    const streamingId = streamingMessageIdBySession.value[sid];
    if (streamingId) {
      const bucket = ensureMessageBucket(sid);
      const msg = bucket.find((item) => item.id === streamingId);
      if (msg) {
        flushAssistantParserState(msg);
        msg.streaming = false;
        // Collapse thinking if present
        if (msg.thinkingContent) {
          msg.thinkingExpanded = false;
        }
      }
      // Clear so the next token event creates a fresh streaming message
      delete streamingMessageIdBySession.value[sid];
    }

    const toolName = asString(envelope.tool_name);
    const toolCallId = asString(envelope.tool_call_id);
    const toolArguments = asString(envelope.arguments);
    pushMessage(sid, {
      id: `tc-${sid}-${nowUnixMs()}`,
      role: 'tool_call' as UiMessage['role'],
      content: toolArguments,
      createdUnixMs: nowUnixMs(),
      streaming: false,
      interrupted: false,
      toolName,
      toolCallId,
      toolExpanded: false
    });
    if (!currentSessionId.value) {
      adoptNewSessionId(sid);
    }
    maybeAutoScroll();
    return;
  }

  if (type === 'tool_result') {
    const sid = asString(envelope.session_id) || currentSessionId.value;
    if (!sid) {
      return;
    }
    const content = asString(envelope.content);
    const toolName = asString(envelope.tool_name);
    const toolCallId = asString(envelope.tool_call_id);
    pushMessage(sid, {
      id: `tool-${sid}-${nowUnixMs()}`,
      role: 'tool',
      content,
      createdUnixMs: nowUnixMs(),
      streaming: false,
      interrupted: false,
      toolName,
      toolCallId,
      toolExpanded: false
    });
    if (!currentSessionId.value) {
      adoptNewSessionId(sid);
    }
    maybeAutoScroll();
    return;
  }

  if (type === 'attachment') {
    const sid = asString(envelope.session_id) || currentSessionId.value;
    if (!sid) return;
    const att = envelope.attachment;
    if (att && typeof att === 'object') {
      pushMessage(sid, {
        id: `att-${sid}-${nowUnixMs()}`,
        role: 'assistant' as UiMessage['role'],
        content: '',
        createdUnixMs: nowUnixMs(),
        streaming: false,
        interrupted: false,
        attachments: [att],
      });
    }
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
          flushAssistantParserState(message);
          message.streaming = false;
          message.interrupted = interrupted;
          // Collapse thinking block when done
          if (message.thinkingContent) {
            message.thinkingExpanded = false;
          }
        }
        delete streamingMessageIdBySession.value[sid];
      }
      // Finalize streaming reply message
      const replyId = streamingReplyIdBySession.value[sid];
      if (replyId) {
        const bucket = ensureMessageBucket(sid);
        const replyMsg = bucket.find((item) => item.id === replyId);
        if (replyMsg) {
          flushAssistantParserState(replyMsg);
          replyMsg.streaming = false;
        }
        delete streamingReplyIdBySession.value[sid];
      }
      requestSessionsOverSocket();
      void loadSessionContext(sid);
      void fetchCompactions(sid);
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
      content: text.length > 0 ? text : t('chat.errors.unknownWebsocket'),
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
      content: t('chat.errors.websocketNotConnected'),
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
  if (selectedProviderId.value) {
    outbound.provider = selectedProviderId.value;
  }
  if (selectedModel.value) {
    outbound.model_id = selectedModel.value;
  }
  if (currentSessionId.value) {
    outbound.session_id = currentSessionId.value;
  } else if (selectedAgentId.value) {
    outbound.agent_id = selectedAgentId.value;
  } else {
    pushMessage(currentSessionKey.value, {
      id: `system-${nowUnixMs()}`,
      role: 'system',
      content: t('chat.errors.agentNotReady'),
      createdUnixMs: nowUnixMs()
    });
    return;
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

async function fetchReasoningState(): Promise<void> {
  try {
    const payload = await apiGet<{reasoning?: {enabled?: boolean; effort?: string; instruction?: string}}>(
      '/api/v1/agent', adminToken.value
    );
    if (payload.reasoning) {
      reasoningEnabled.value = !!payload.reasoning.enabled;
      reasoningEffort.value = payload.reasoning.effort ?? 'high';
      reasoningInstruction.value = payload.reasoning.instruction ?? '';
    }
  } catch {
    // Reasoning state unavailable; leave defaults
  }
}

async function fetchCapabilitiesForModel(providerId: string, model: string) {
  try {
    const data = await apiGet<{
      capabilities: Capabilities;
      fetched: boolean;
    }>(`/api/v1/providers/${providerId}/capabilities?model=${encodeURIComponent(model)}`, adminToken.value);
    if (data.capabilities) {
      fetchedCapabilities.value = data.capabilities;
      // Also update the provider's cached capabilities
      const p = providers.value.find(x => x.id === providerId);
      if (p) p.capabilities = data.capabilities;
    }
  } catch {
    // Silently fail — keep existing capabilities
  }
}

async function onProviderChange(id: string | null, preferredModel?: string) {
  const p = providers.value.find(x => x.id === id);
  if (!p) return;
  selectedModel.value = preferredModel || p.defaultModel;
  modelsForProvider.value = [];
  fetchedCapabilities.value = null;

  // All providers: fetch model list + capabilities via backend
  modelsLoading.value = true;
  try {
    // Fetch models
    const mData = await apiGet<{models: string[], fetched: boolean}>(
      `/api/v1/providers/${p.id}/models`, adminToken.value
    );
    if (Array.isArray(mData.models)) {
      const sorted = mData.models.slice().sort();
      if (preferredModel && preferredModel.length > 0 && !sorted.includes(preferredModel)) {
        sorted.unshift(preferredModel);
      }
      modelsForProvider.value = sorted;
      if (preferredModel && preferredModel.length > 0) {
        selectedModel.value = preferredModel;
      }
    }
    // Fetch capabilities for the default model
    if (selectedModel.value) {
      await fetchCapabilitiesForModel(p.id, selectedModel.value);
    }
  } catch {
    // Silently fail — user can still type manually
  } finally {
    modelsLoading.value = false;
  }
}

async function fetchProviders(): Promise<void> {
  try {
    const data = await apiGet<{
      default_provider: string;
      providers: Array<{
        provider_id: string;
        provider_type: string;
        default_model: string;
        capabilities?: Capabilities;
      }>;
    }>('/api/v1/providers', adminToken.value);

    providers.value = data.providers.map(p => ({
      id: p.provider_id,
      type: p.provider_type,
      defaultModel: p.default_model,
      capabilities: p.capabilities,
    }));

    // Set default provider selection and fetch models/capabilities
    if (!selectedProviderId.value && data.default_provider) {
      selectedProviderId.value = data.default_provider;
      const defaultP = providers.value.find(p => p.id === data.default_provider);
      selectedModel.value = defaultP?.defaultModel ?? '';
    }

    // Fetch models + capabilities for the current selection
    const currentProvider = providers.value.find(p => p.id === selectedProviderId.value);
    if (currentProvider) {
      try {
        const mData = await apiGet<{models: string[], fetched: boolean}>(
          `/api/v1/providers/${currentProvider.id}/models`, adminToken.value
        );
        if (Array.isArray(mData.models)) {
          const sorted = mData.models.slice().sort();
          if (selectedModel.value && !sorted.includes(selectedModel.value)) {
            sorted.unshift(selectedModel.value);
          }
          modelsForProvider.value = sorted;
        }
        const preferred = selectedModel.value || currentProvider.defaultModel;
        if (preferred) {
          await fetchCapabilitiesForModel(currentProvider.id, preferred);
        }
      } catch { /* ok */ }
    }
  } catch {
    // Providers unavailable
  }
}

async function fetchAgents(): Promise<void> {
  try {
    const data = await apiGet<{
      agents: Array<{ id: string; name?: string }>;
    }>('/api/v1/agents', adminToken.value);

    agents.value = Array.isArray(data.agents)
      ? data.agents
          .filter((a) => a && typeof a.id === 'string' && a.id.length > 0)
          .map((a) => ({
            id: a.id,
            name: (a.name || a.id).trim() || a.id
          }))
      : [];

    if (!agents.value.find((a) => a.id === selectedAgentId.value)) {
      selectedAgentId.value =
        agents.value.find((a) => a.id === 'default')?.id
        || agents.value[0]?.id
        || '';
    }
  } catch {
    agents.value = [];
    selectedAgentId.value = '';
  }
}

async function syncContextSettingsFromAgent(agentId: string): Promise<void> {
  if (!agentId) return;
  try {
    const agent = await apiGet<AgentDetail>(`/api/v1/agents/${encodeURIComponent(agentId)}`, adminToken.value);

    if (agent.reasoning) {
      reasoningEnabled.value = Boolean(agent.reasoning.enabled);
      reasoningEffort.value = agent.reasoning.effort ?? 'high';
    }

    const providerId = agent.model?.provider ?? '';
    const modelId = agent.model?.model_id ?? '';
    if (providerId) {
      selectedProviderId.value = providerId;
      await onProviderChange(providerId, modelId || undefined);
    } else if (modelId) {
      selectedModel.value = modelId;
    }
  } catch {
    // Ignore sync failures; keep current UI values.
  }
}

async function toggleReasoning(enabled: boolean | null): Promise<void> {
  const val = !!enabled;
  reasoningLoading.value = true;
  try {
    const response = await apiRequest<{reasoning?: {enabled?: boolean; effort?: string; instruction?: string}}>(
      'PATCH',
      '/api/v1/agent',
      { reasoning: { enabled: val, effort: reasoningEffort.value } },
      adminToken.value
    );
    if (response.reasoning) {
      reasoningEnabled.value = !!response.reasoning.enabled;
      reasoningEffort.value = response.reasoning.effort ?? 'high';
      reasoningInstruction.value = response.reasoning.instruction ?? '';
    }
  } catch {
    // Revert on failure
    reasoningEnabled.value = !val;
  } finally {
    reasoningLoading.value = false;
  }
}

async function updateReasoningEffort(effort: string): Promise<void> {
  reasoningLoading.value = true;
  try {
    const response = await apiRequest<{reasoning?: {enabled?: boolean; effort?: string; instruction?: string}}>(
      'PATCH',
      '/api/v1/agent',
      { reasoning: { enabled: reasoningEnabled.value, effort } },
      adminToken.value
    );
    if (response.reasoning) {
      reasoningEffort.value = response.reasoning.effort ?? 'high';
    }
  } catch {
    // Revert on failure — keep previous value
  } finally {
    reasoningLoading.value = false;
  }
}

onMounted(async () => {
  connectSocket();
  await fetchProviders();
  await fetchAgents();

  if (selectedSession.value === 'new' && selectedAgentId.value) {
    await syncContextSettingsFromAgent(selectedAgentId.value);
  } else {
    await fetchReasoningState();
  }
});

onBeforeUnmount(() => {
  closeSocket();
  stopTokenEstimatePolling();
});

async function refreshSessions(): Promise<void> {
  requestSessionsOverSocket();
  await fetchAgents();
  if (selectedSession.value === 'new' && selectedAgentId.value) {
    await syncContextSettingsFromAgent(selectedAgentId.value);
  }
  if (currentSessionId.value) {
    await loadSessionContext(currentSessionId.value);
  }
}

async function onSessionSelectionChanged(): Promise<void> {
  await switchSession(selectedSession.value);
}

watch(
  [selectedAgentId, selectedSession],
  async ([agentId, session]) => {
    if (!agentId || session !== 'new') return;
    await syncContextSettingsFromAgent(agentId);
  },
  { immediate: true }
);

// Token estimate lifecycle
watch(currentSessionId, (sid) => {
  if (sid) {
    startTokenEstimatePolling();
  } else {
    stopTokenEstimatePolling();
    tokenEstimate.value = null;
  }
});

watch(draft, () => {
  if (currentSessionId.value && !isGenerating.value) {
    onDraftInputDebounced();
  }
});

watch(isGenerating, (gen) => {
  if (!gen && currentSessionId.value) {
    // Generation finished — refresh estimate immediately
    fetchTokenEstimate();
  }
});

// --- Session pagination watchers ---
watch(sessionPage, () => requestSessionsOverSocket());
watch(sessionPageSize, () => {
  sessionPage.value = 1;
  requestSessionsOverSocket();
});
watch(sessionSearch, () => {
  if (sessionSearchTimer.value) clearTimeout(sessionSearchTimer.value);
  sessionSearchTimer.value = setTimeout(() => {
    sessionPage.value = 1;
    requestSessionsOverSocket();
  }, 300);
});
</script>

<template>
  <div class="chat-layout">
    <section class="chat-panel">
        <div class="chat-toolbar">
          <div class="toolbar-left">
            <v-select
              v-model="selectedSession"
              :label="t('chat.sessionLabel')"
              variant="solo-filled"
              density="compact"
              hide-details
              :items="sessionItems"
              class="session-select"
              @update:model-value="onSessionSelectionChanged"
            />
            <v-btn size="small" variant="tonal" @click="selectedSession = 'new'; onSessionSelectionChanged()">
              {{ t('chat.newSession') }}
            </v-btn>
            <v-select
              v-model="selectedAgentId"
              :label="t('chat.agent.label')"
              :items="agents.map(a => ({ title: `${a.name} (${a.id})`, value: a.id }))"
              class="agent-select"
              variant="solo-filled"
              density="compact"
              hide-details
              :disabled="selectedSession !== 'new'"
            />
            <v-btn size="small" variant="text" @click="refreshSessions">{{ t('chat.refresh') }}</v-btn>
          </div>
          <div class="toolbar-right">
            <div
              v-if="tokenEstimate && tokenEstimate.context_window > 0"
              class="token-gauge"
              :title="`${formatNumber(tokenEstimate.estimated_tokens)} / ${formatNumber(tokenEstimate.context_window)} tokens (${tokenPercent}%) — system: ${formatNumber(tokenEstimate.breakdown.system_prompt)}, turns: ${formatNumber(tokenEstimate.breakdown.session_turns)}, draft: ${formatNumber(tokenEstimate.breakdown.draft_message)}`"
            >
              <div class="token-gauge-bar" :style="{ width: tokenPercent + '%' }" :data-level="tokenLevel"></div>
              <span class="token-gauge-label">{{ formatTokenK(tokenEstimate.estimated_tokens) }} / {{ formatTokenK(tokenEstimate.context_window) }} ({{ tokenPercent }}%)</span>
            </div>
            <span class="status-chip" :data-state="wsState">{{ wsStateLabel }}</span>
            <v-btn
              size="small"
              color="warning"
              variant="tonal"
              :disabled="!isGenerating"
              @click="stopGenerating"
            >
              {{ t('chat.stopGenerating') }}
            </v-btn>
          </div>
        </div>

        <div ref="messagesContainer" class="messages" @scroll="onMessagesScroll">
          <div v-if="activeMessages.length === 0" class="empty-state">
            <h3>{{ t('chat.emptyTitle') }}</h3>
            <p>{{ t('chat.emptyDescription') }}</p>
          </div>

          <article
            v-for="message in activeMessages"
            :key="message.id"
            class="message-row"
            :class="[`role-${message.role}`]"
          >
            <div class="message-content-group">
              <div v-if="message.thinkingContent" class="thinking-section">
                <div class="thinking-header" @click="message.thinkingExpanded = !message.thinkingExpanded">
                  <span class="thinking-icon">{{ message.thinkingExpanded ? '\u25BC' : '\u25B6' }}</span>
                  <span class="thinking-label">{{ t('chat.thinking.label') }}</span>
                  <span v-if="message.streaming" class="thinking-status">{{ t('chat.streamState.streaming') }}</span>
                </div>
                <div v-if="message.thinkingExpanded" class="thinking-content markdown-body" v-html="markdown.render(message.thinkingContent)" />
              </div>
              <div v-if="message.role === 'tool_call'" class="tool-call-section">
                <div class="tool-call-header" @click="message.toolExpanded = !message.toolExpanded">
                  <span class="tool-call-icon">{{ message.toolExpanded ? '\u25BC' : '\u25B6' }}</span>
                  <span class="tool-call-label">{{ t('chat.toolCall.label') }}</span>
                  <span v-if="message.toolName" class="tool-call-name">{{ message.toolName }}</span>
                </div>
                <pre v-if="message.toolExpanded" class="tool-call-content">{{ formatToolContent(message.content) }}</pre>
                <div class="meta">
                  <span>{{ new Date(message.createdUnixMs).toLocaleTimeString() }}</span>
                </div>
              </div>
              <div v-else-if="message.role === 'tool' && message.content" class="tool-section">
                <div class="tool-header" @click="message.toolExpanded = !message.toolExpanded">
                  <span class="tool-icon">{{ message.toolExpanded ? '\u25BC' : '\u25B6' }}</span>
                  <span class="tool-label">{{ t('chat.toolResult.label') }}</span>
                  <span v-if="message.toolName" class="tool-name">{{ message.toolName }}</span>
                </div>
                <pre v-if="message.toolExpanded" class="tool-content">{{ formatToolContent(message.content) }}</pre>
                <div class="meta">
                  <span>{{ new Date(message.createdUnixMs).toLocaleTimeString() }}</span>
                </div>
              </div>
              <div v-else-if="message.isSummary && message.role === 'compaction'" class="compaction-marker">
                <span class="compaction-icon">📃</span>
                <span class="compaction-label">Compaction summary</span>
                <v-btn size="x-small" variant="text" @click="toggleCompactionDetail(message)">{{ message._compactionExpanded ? 'Hide' : 'Show' }}</v-btn>
                <div v-if="message._compactionExpanded" class="compaction-detail markdown-body" v-html="markdown.render(message.content)" />
              </div>
              <div v-else-if="message.content" class="bubble">
                <div v-if="message.role === 'assistant'" class="markdown-body" v-html="renderMessageMarkdown(message)" />
                <div v-else class="plain-text">{{ message.content }}</div>
                <div class="meta">
                  <span v-if="message.streaming">{{ t('chat.streamState.streaming') }}</span>
                  <span v-else-if="message.interrupted">{{ t('chat.streamState.stopped') }}</span>
                  <span v-else>{{ new Date(message.createdUnixMs).toLocaleTimeString() }}</span>
                </div>
              </div>
              <AttachmentMessage
                v-if="message.attachments && message.attachments.length > 0"
                :attachments="message.attachments"
                :session-key="currentSessionId"
              />
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
          {{ t('chat.jumpToLatest') }}
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
            :placeholder="t('chat.composerPlaceholder')"
            @keydown.enter.exact.prevent="sendMessage"
          />
          <div class="composer-actions">
            <v-text-field
              v-model="adminToken"
              type="password"
              variant="underlined"
              density="compact"
              hide-details
              :label="t('chat.adminTokenLabel')"
              class="token-input"
              @change="saveToken"
            />
            <v-btn color="primary" :disabled="isGenerating || draft.trim().length === 0" @click="sendMessage">
              {{ t('chat.send') }}
            </v-btn>
          </div>
          <p v-if="lastWsError" class="ws-error">{{ lastWsError }}</p>
        </div>
    </section>

    <aside class="context-panel">
      <v-card rounded="xl" class="context-shell">
        <v-tabs v-model="rightPanelTab" density="compact" color="primary" grow>
          <v-tab value="context">{{ t('chat.sidebarTabs.context') }}</v-tab>
          <v-tab value="sessions">{{ t('chat.sidebarTabs.sessions') }}</v-tab>
        </v-tabs>
        <v-divider />
        <v-tabs-window v-model="rightPanelTab" class="context-tabs-window">
          <v-tabs-window-item value="context">
            <div class="context-tab-pane">
              <v-card rounded="xl" class="context-card">
                <v-card-title>{{ t('chat.contextTitle') }}</v-card-title>
                <v-card-text>
                  <p class="context-line">
                    <strong>{{ t('chat.context.session') }}:</strong>
                    {{ currentSessionId || t('common.newSession') }}
                  </p>
                  <p class="context-line">
                    <strong>{{ t('chat.context.layers') }}:</strong>
                    {{ activeContext.layers.join(', ') || t('common.none') }}
                  </p>
                  <p class="context-line">
                    <strong>{{ t('chat.context.tools') }}:</strong>
                    {{ activeContext.tools.join(', ') || t('common.none') }}
                  </p>
                  <p class="context-line">
                    <strong>{{ t('chat.context.budget') }}:</strong>
                    {{ activeContext.tokenBudgetRemaining }}/{{ activeContext.tokenBudgetConfigured }}
                  </p>
                  <p class="context-note">{{ activeContext.note || t('chat.context.fallbackNote') }}</p>
                </v-card-text>
              </v-card>

              <v-card rounded="xl" class="context-card">
                <v-card-title>{{ t('chat.provider.label') }}</v-card-title>
                <v-card-text>
                  <v-select
                    v-model="selectedProviderId"
                    :label="t('chat.provider.select')"
                    :items="providers.map(p => ({ title: p.id, value: p.id }))"
                    density="compact"
                    variant="outlined"
                    hide-details
                    @update:model-value="(id: string | null) => onProviderChange(id)"
                  />
                  <v-combobox
                    v-model="selectedModel"
                    :label="t('chat.provider.model')"
                    :items="modelsForProvider"
                    :loading="modelsLoading"
                    density="compact"
                    variant="outlined"
                    hide-details
                    clearable
                    style="margin-top: 0.5rem"
                  />
                  <div v-if="activeCapabilities" class="capability-badges">
                    <v-chip v-if="activeCapabilities.supports_tools" size="x-small" variant="tonal" color="info" class="mt-1 mr-1">tools</v-chip>
                    <v-chip v-if="activeCapabilities.supports_reasoning" size="x-small" variant="tonal" color="purple" class="mt-1 mr-1">reasoning</v-chip>
                    <v-chip v-if="activeCapabilities.supports_vision" size="x-small" variant="tonal" color="teal" class="mt-1 mr-1">vision</v-chip>
                  </div>
                </v-card-text>
              </v-card>

              <v-card rounded="xl" class="context-card">
                <v-card-title>{{ t('chat.reasoning.label') }}</v-card-title>
                <v-card-text>
                  <v-switch
                    v-model="reasoningEnabled"
                    :label="reasoningEnabled ? t('chat.reasoning.enabled') : t('chat.reasoning.disabled')"
                    :loading="reasoningLoading"
                    :disabled="!reasoningSupported"
                    color="primary"
                    density="compact"
                    hide-details
                    @update:model-value="toggleReasoning"
                  />
                  <p v-if="!reasoningSupported" class="text-caption text-medium-emphasis mt-1">
                    {{ t('chat.reasoning.notSupported') }}
                  </p>
                  <v-select
                    v-if="reasoningEnabled && reasoningSupported"
                    v-model="reasoningEffort"
                    :items="['low', 'medium', 'high', 'xhigh']"
                    :label="t('chat.reasoning.effort')"
                    density="compact"
                    variant="outlined"
                    hide-details
                    style="margin-top: 0.5rem"
                    @update:model-value="updateReasoningEffort"
                  />
                </v-card-text>
              </v-card>

              <!-- Compaction Timeline -->
              <v-card rounded="xl" class="context-card" v-if="compactions.length > 0">
                <v-card-title>Compactions</v-card-title>
                <v-card-text>
                  <div class="compaction-timeline">
                    <div
                      v-for="entry in compactions"
                      :key="entry.id"
                      class="compaction-timeline-item"
                      @click="openCompactionModal(entry)"
                    >
                      <div class="compaction-timeline-dot" />
                      <div class="compaction-timeline-content">
                        <div class="compaction-timeline-time">{{ formatTime(entry.created_at_unix_ms) }}</div>
                        <div class="compaction-timeline-preview">{{ truncateSummary(entry.summary) }}</div>
                        <div class="compaction-timeline-meta">
                          <span>{{ entry.token_count }} tokens</span>
                          <span>{{ entry.model }}</span>
                        </div>
                      </div>
                    </div>
                  </div>
                </v-card-text>
              </v-card>
            </div>
          </v-tabs-window-item>

          <v-tabs-window-item value="sessions">
            <div class="sessions-tab-pane">
              <!-- Search + Pagination Controls -->
              <div class="sessions-controls pa-2">
                <v-text-field
                  v-model="sessionSearch"
                  density="compact"
                  variant="outlined"
                  :placeholder="t('chat.sidebarTabs.searchSessions')"
                  hide-details
                  clearable
                  prepend-inner-icon="mdi-magnify"
                />
                <div class="d-flex align-center ga-2 mt-2">
                  <v-btn
                    icon="mdi-chevron-left"
                    size="x-small"
                    variant="text"
                    :disabled="sessionPage <= 1"
                    @click="sessionPage--"
                  />
                  <span class="text-body-2 text-grey-darken-1" style="min-width: 60px; text-align: center">
                    {{ sessionPage }} / {{ sessionTotalPages }}
                  </span>
                  <v-btn
                    icon="mdi-chevron-right"
                    size="x-small"
                    variant="text"
                    :disabled="sessionPage >= sessionTotalPages"
                    @click="sessionPage++"
                  />
                  <v-select
                    v-model="sessionPageSize"
                    :items="[25, 50, 100, 200]"
                    density="compact"
                    variant="outlined"
                    hide-details
                    style="max-width: 80px"
                  />
                </div>
              </div>
              <v-list class="sessions-list" density="compact" lines="two">
                <v-list-item
                  :active="selectedSession === 'new'"
                  rounded="lg"
                  @click="selectSessionFromSidebar('new')"
                >
                  <v-list-item-title>{{ t('chat.newSession') }}</v-list-item-title>
                  <v-list-item-subtitle>{{ t('chat.sidebarTabs.newSessionHint') }}</v-list-item-subtitle>
                </v-list-item>
                <v-list-item
                  v-for="session in sortedSessions"
                  :key="session.id"
                  :active="selectedSession === session.id"
                  rounded="lg"
                  @click="selectSessionFromSidebar(session.id)"
                >
                  <v-list-item-title>#{{ session.id }} • {{ session.source }}</v-list-item-title>
                  <v-list-item-subtitle>
                    {{ session.message_count }} {{ t('chat.sidebarTabs.messages') }} •
                    {{ new Date(session.last_active_unix_ms).toLocaleString() }}
                  </v-list-item-subtitle>
                </v-list-item>
              </v-list>
              <p v-if="sortedSessions.length === 0" class="sessions-empty">
                {{ t('chat.sidebarTabs.noSessions') }}
              </p>
            </div>
          </v-tabs-window-item>
        </v-tabs-window>
      </v-card>
    </aside>

    <!-- Compaction Detail Modal -->
    <v-dialog v-model="compactionModalOpen" max-width="700">
      <v-card rounded="xl" class="compaction-modal-card">
        <v-card-title class="d-flex align-center ga-2">
          <span>📃 Compaction Summary</span>
          <v-spacer />
          <v-btn icon="mdi-close" size="small" variant="text" @click="closeCompactionModal" />
        </v-card-title>
        <v-card-text v-if="compactionModalEntry">
          <div class="compaction-modal-meta mb-3">
            <span><strong>Created:</strong> {{ formatTime(compactionModalEntry.created_at_unix_ms) }}</span>
            <span><strong>Model:</strong> {{ compactionModalEntry.model }}</span>
            <span><strong>Tokens:</strong> {{ compactionModalEntry.token_count }}</span>
          </div>
          <div class="compaction-modal-summary markdown-body" v-html="markdown.render(compactionModalEntry.summary)" />
        </v-card-text>
      </v-card>
    </v-dialog>
  </div>
</template>

<style scoped>
.chat-layout {
  display: grid;
  grid-template-columns: minmax(0, 1fr) 320px;
  gap: 1rem;
  height: calc(100vh - 120px);
}

.chat-panel {
  position: relative;
  display: flex;
  flex-direction: column;
  min-height: 0;
  border-radius: 16px;
  overflow: hidden;
  background: linear-gradient(170deg, rgba(23, 26, 35, 0.95), rgba(16, 18, 24, 0.98));
  border: 1px solid rgba(255, 255, 255, 0.06);
}

.chat-toolbar {
  flex-shrink: 0;
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

.agent-select {
  min-width: 220px;
}

.toolbar-right {
  display: flex;
  gap: 0.6rem;
  align-items: center;
}

.token-gauge {
  display: flex;
  align-items: center;
  gap: 0.35rem;
  min-width: 160px;
  height: 18px;
  position: relative;
}

.token-gauge-bar {
  height: 5px;
  border-radius: 3px;
  transition: width 0.4s ease, background-color 0.3s ease;
  min-width: 2px;
}

.token-gauge-bar[data-level='ok'] {
  background-color: #63d471;
}

.token-gauge-bar[data-level='warn'] {
  background-color: #ffbf69;
}

.token-gauge-bar[data-level='danger'] {
  background-color: #ff5d73;
}

.token-gauge-label {
  font-size: 0.65rem;
  color: rgba(255, 255, 255, 0.55);
  white-space: nowrap;
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
  flex: 1;
  min-height: 0;
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

.message-content-group {
  display: flex;
  flex-direction: column;
  max-width: min(80ch, 88%);
}

.message-row {
  display: flex;
}

.message-row.role-user {
  justify-content: flex-end;
}

.message-row.role-assistant,
.message-row.role-system,
.message-row.role-tool,
.message-row.role-tool_call {
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
  flex-shrink: 0;
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
  bottom: 12rem;
  z-index: 2;
}

.context-card {
  background: linear-gradient(170deg, rgba(22, 24, 33, 0.95), rgba(13, 15, 20, 0.98));
}

.compaction-timeline {
  position: relative;
  padding-left: 1.2rem;
}

.compaction-timeline::before {
  content: '';
  position: absolute;
  left: 4px;
  top: 0;
  bottom: 0;
  width: 2px;
  background: rgba(139, 92, 246, 0.3);
}

.compaction-timeline-item {
  position: relative;
  padding: 0.5rem 0;
  cursor: pointer;
  transition: background-color 0.2s;
  border-radius: 6px;
  padding-left: 0.5rem;
}

.compaction-timeline-item:hover {
  background: rgba(139, 92, 246, 0.06);
}

.compaction-timeline-dot {
  position: absolute;
  left: -1.2rem;
  top: 0.75rem;
  width: 10px;
  height: 10px;
  border-radius: 50%;
  background: #8b5cf6;
  border: 2px solid rgba(22, 24, 33, 0.95);
}

.compaction-timeline-time {
  font-size: 0.72rem;
  color: rgba(255, 255, 255, 0.5);
  margin-bottom: 0.2rem;
}

.compaction-timeline-preview {
  font-size: 0.8rem;
  color: rgba(199, 210, 254, 0.85);
  line-height: 1.4;
}

.compaction-timeline-meta {
  display: flex;
  gap: 0.75rem;
  margin-top: 0.25rem;
  font-size: 0.68rem;
  color: rgba(255, 255, 255, 0.4);
}

.compaction-modal-card {
  background: linear-gradient(170deg, rgba(22, 24, 33, 0.98), rgba(13, 15, 20, 0.99));
}

.compaction-modal-meta {
  display: flex;
  gap: 1.5rem;
  font-size: 0.82rem;
  color: rgba(255, 255, 255, 0.6);
}

.compaction-modal-summary {
  max-height: 400px;
  overflow-y: auto;
  line-height: 1.6;
}

.context-shell {
  position: sticky;
  top: 0;
  max-height: calc(100vh - 120px);
  overflow: hidden;
  background: linear-gradient(170deg, rgba(22, 24, 33, 0.95), rgba(13, 15, 20, 0.98));
}

.context-tabs-window {
  max-height: calc(100vh - 180px);
  overflow-y: auto;
}

.context-tab-pane {
  display: flex;
  flex-direction: column;
  gap: 0.5rem;
  padding: 0.5rem;
}

.sessions-tab-pane {
  padding: 0.5rem;
}

.sessions-controls {
  border-bottom: 1px solid rgba(255, 255, 255, 0.08);
  margin-bottom: 0.5rem;
}

.sessions-list {
  background: transparent;
}

.sessions-empty {
  margin-top: 0.75rem;
  opacity: 0.72;
  font-size: 0.86rem;
}

.context-line {
  margin: 0.4rem 0;
}

.context-note {
  margin-top: 0.85rem;
  opacity: 0.8;
}

.capability-badges {
  display: flex;
  flex-wrap: wrap;
  gap: 0.25rem;
  margin-top: 0.5rem;
}

@media (max-width: 1100px) {
  .chat-layout {
    grid-template-columns: 1fr;
  }

  .context-shell {
    min-height: auto;
  }
}

.thinking-bubble {
  background: linear-gradient(130deg, rgba(139, 92, 246, 0.12), rgba(99, 102, 241, 0.08));
  border-color: rgba(139, 92, 246, 0.2);
}

.thinking-section {
  background: linear-gradient(130deg, rgba(139, 92, 246, 0.12), rgba(99, 102, 241, 0.08));
  border: 1px solid rgba(139, 92, 246, 0.2);
  border-radius: 6px;
  padding: 0.5rem 0.75rem;
  margin-bottom: 0.75rem;
}

.thinking-header {
  display: flex;
  align-items: center;
  gap: 0.4rem;
  cursor: pointer;
  user-select: none;
  padding: 0.15rem 0;
}

.thinking-header:hover {
  opacity: 0.85;
}

.thinking-icon {
  font-size: 0.65rem;
  opacity: 0.7;
}

.thinking-label {
  font-size: 0.82rem;
  font-weight: 500;
  color: rgba(167, 139, 250, 0.9);
  letter-spacing: 0.02em;
}

.thinking-status {
  font-size: 0.7rem;
  opacity: 0.6;
  margin-left: 0.3rem;
}

.thinking-content {
  margin-top: 0.5rem;
  padding-top: 0.5rem;
  border-top: 1px solid rgba(139, 92, 246, 0.15);
  opacity: 0.85;
  font-size: 0.88rem;
  font-style: italic;
  color: rgba(199, 210, 254, 0.92);
}

.tool-section {
  background: linear-gradient(130deg, rgba(16, 185, 129, 0.12), rgba(6, 182, 212, 0.08));
  border: 1px solid rgba(45, 212, 191, 0.24);
  border-radius: 6px;
  padding: 0.5rem 0.75rem;
  margin-bottom: 0.75rem;
}

.tool-header {
  display: flex;
  align-items: center;
  gap: 0.4rem;
  cursor: pointer;
  user-select: none;
  padding: 0.15rem 0;
}

.tool-header:hover {
  opacity: 0.85;
}

.tool-icon {
  font-size: 0.65rem;
  opacity: 0.7;
}

.tool-label {
  font-size: 0.82rem;
  font-weight: 600;
  color: rgba(94, 234, 212, 0.95);
  letter-spacing: 0.02em;
}

.tool-name {
  font-size: 0.74rem;
  color: rgba(153, 246, 228, 0.88);
  opacity: 0.9;
}

.tool-content {
  margin-top: 0.5rem;
  padding: 0.55rem 0.65rem;
  border-radius: 8px;
  border: 1px solid rgba(94, 234, 212, 0.2);
  background: rgba(7, 12, 18, 0.5);
  color: rgba(210, 255, 246, 0.9);
  font-family: 'IBM Plex Mono', 'Fira Code', monospace;
  font-size: 0.78rem;
  line-height: 1.35;
  white-space: pre-wrap;
  overflow-x: auto;
}

.tool-call-section {
  background: linear-gradient(130deg, rgba(245, 158, 11, 0.12), rgba(234, 88, 12, 0.08));
  border: 1px solid rgba(251, 191, 36, 0.24);
  border-radius: 6px;
  padding: 0.5rem 0.75rem;
  margin-bottom: 0.25rem;
}

.compaction-marker {
  background: linear-gradient(130deg, rgba(99, 102, 241, 0.1), rgba(139, 92, 246, 0.06));
  border: 1px solid rgba(99, 102, 241, 0.3);
  border-radius: 8px;
  padding: 0.5rem 0.75rem;
  margin-bottom: 0.5rem;
  display: flex;
  align-items: center;
  gap: 0.5rem;
  font-size: 0.85rem;
  color: rgba(139, 92, 246, 0.9);
}

.compaction-icon {
  font-size: 1rem;
}

.compaction-label {
  font-weight: 500;
}

.compaction-detail {
  margin-top: 0.5rem;
  padding: 0.5rem;
  background: rgba(99, 102, 241, 0.06);
  border-radius: 4px;
  max-height: 200px;
  overflow-y: auto;
  font-size: 0.8rem;
}

.tool-call-header {
  display: flex;
  align-items: center;
  gap: 0.4rem;
  cursor: pointer;
  user-select: none;
  padding: 0.15rem 0;
}

.tool-call-header:hover {
  opacity: 0.85;
}

.tool-call-icon {
  font-size: 0.65rem;
  opacity: 0.7;
}

.tool-call-label {
  font-size: 0.82rem;
  font-weight: 600;
  color: rgba(251, 191, 36, 0.95);
  letter-spacing: 0.02em;
}

.tool-call-name {
  font-size: 0.74rem;
  color: rgba(253, 224, 71, 0.88);
  opacity: 0.9;
}

.tool-call-content {
  margin-top: 0.5rem;
  padding: 0.55rem 0.65rem;
  border-radius: 8px;
  border: 1px solid rgba(251, 191, 36, 0.2);
  background: rgba(7, 12, 18, 0.5);
  color: rgba(253, 224, 71, 0.9);
  font-family: 'IBM Plex Mono', 'Fira Code', monospace;
  font-size: 0.78rem;
  line-height: 1.35;
  white-space: pre-wrap;
  overflow-x: auto;
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

  .agent-select {
    min-width: 0;
    width: 100%;
  }
}
</style>

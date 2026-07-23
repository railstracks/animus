<script setup lang="ts">
import { computed, onMounted, ref, watch } from 'vue';
import { useI18n } from 'vue-i18n';
import { apiGet, apiRequest } from '../lib/api';
import { VueQr } from 'vue-qr';

const { t } = useI18n();

// ---------------------------------------------------------------------------
// Types
// ---------------------------------------------------------------------------

interface ChannelInfo {
  name: string;
  type: string;
  enabled: boolean;
  connected: boolean;
  last_event_unix_ms: number;
  identity: string;
  endpoint: string;
  config: Record<string, any>;
}

interface AgentInfo {
  id: string;
  name: string;
}

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

const loading = ref(true);
const error = ref('');
const successMsg = ref('');
const channels = ref<ChannelInfo[]>([]);
const agents = ref<AgentInfo[]>([]);
const selectedAgent = ref('');

const showForm = ref(false);
const saving = ref(false);
const editing = ref<ChannelInfo | null>(null);
const twitterOAuthLoading = ref(false);
const twitterOAuthStatus = ref('');
const twitterOAuthError = ref(false);

// WhatsApp QR state
const qrUrl = ref('');
const qrStatus = ref('');
const qrLoading = ref(false);
const qrError = ref('');
const clearAuthLoading = ref(false);
const clearAuthMsg = ref('');
const clearAuthError = ref(false);
// Moltbook registration state
const moltbookRegName = ref('');
const moltbookRegDesc = ref('');
const moltbookRegLoading = ref(false);
const moltbookRegError = ref('');
const moltbookRegResult = ref<any>(null);

// ---------------------------------------------------------------------------
// Channel type definitions
// ---------------------------------------------------------------------------

const channelTypes = [
  { title: 'IRC', value: 'irc' },
  { title: 'Telegram', value: 'telegram' },
  { title: 'VKontakte', value: 'vk' },
  { title: 'Bluesky', value: 'bluesky' },
  { title: 'Mastodon', value: 'mastodon' },
  { title: 'Email (AgentMail)', value: 'email' },
  { title: 'Twitter', value: 'twitter' },
  { title: 'Discord', value: 'discord' },
  { title: 'Slack', value: 'slack' },
  { title: 'WhatsApp', value: 'whatsapp' },
  { title: 'Moltbook', value: 'moltbook' },
  { title: 'Nextcloud Talk', value: 'nextcloud' },
] as const;

type ChannelType = 'irc' | 'telegram' | 'vk' | 'bluesky' | 'mastodon' | 'email' | 'twitter' | 'discord' | 'slack' | 'whatsapp' | 'moltbook' | 'nextcloud';

// ---------------------------------------------------------------------------
// Form data
// ---------------------------------------------------------------------------

const formData = ref({
  name: '',
  type: 'irc' as ChannelType,
  enabled: true,
  // IRC fields
  irc_host: '',
  irc_port: 6667,
  irc_server_password: '',
  irc_nick: '',
  irc_username: 'animus',
  irc_realname: 'Animus Agent',
  irc_channels_multiline: '',
  irc_dm_only: false,
  irc_respond_to_channel_activity: true,
  irc_respond_to_direct_messages: true,
  irc_respond_to_notices: false,
  irc_allowed_dm_users: '',
  irc_agent_id: '',
  irc_reconnect_enabled: true,
  irc_reconnect_initial_delay_ms: 1000,
  irc_reconnect_max_delay_ms: 60000,
  irc_use_tls: false,
  // Agent association (non-IRC)
  agent_id: '',
  // Telegram fields
  telegram_bot_token: '',
  // VK fields
  vk_access_token: '',
  vk_group_id: '',
  // Bluesky fields
  bluesky_handle: '',
  bluesky_app_password: '',
  bluesky_pds: 'https://bsky.social',
  // Mastodon fields
  mastodon_handle: '',
  mastodon_instance: '',
  // Email (AgentMail) fields
  email_api_key: '',
  email_inbox_id: '',
  email_polling_wait: 25,
  // Twitter fields
  twitter_client_id: '',
  twitter_client_secret: '',
  twitter_access_token: '',
  twitter_refresh_token: '',
  twitter_tier: 'free',
  // Discord fields
  discord_bot_token: '',
  discord_application_id: '',
  discord_monitored_channels: '',
  discord_respond_to_dm: true,
  discord_respond_to_mentions: true,
  discord_respond_to_channels: false,
  discord_monitor_all_channels: false,
  discord_dm_whitelist_enabled: false,
  discord_allowed_dm_users: '',
  // Slack fields
  slack_bot_token: '',
  slack_app_token: '',
  slack_monitored_channels: '',
  slack_respond_to_mentions: true,
  slack_respond_to_all_messages: false,
  // WhatsApp fields
  whatsapp_auth_dir: '/tmp/wa-auth',
  // Moltbook fields
  moltbook_api_key: '',
  // Nextcloud Talk fields
  nextcloud_server_url: '',
  nextcloud_username: '',
  nextcloud_app_password: '',
  nextcloud_watch_tokens: '',
  nextcloud_respond_in_dm: true,
  nextcloud_respond_in_group_on_mention: true,
  nextcloud_group_mention_trigger: '',
  // Common: message batching (Ticket 114)
  min_response_interval: 0,
  // Common: interjection (Ticket 115)
  allow_interjection: true,
});

const isNew = computed(() => editing.value == null);

// Auto-switch port when TLS is toggled
watch(() => formData.value.irc_use_tls, (tls, prev) => {
  if (tls && formData.value.irc_port === 6667) {
    formData.value.irc_port = 6697;
  } else if (!tls && formData.value.irc_port === 6697) {
    formData.value.irc_port = 6667;
  }
});

const agentItems = computed(() =>
  agents.value.map((a) => ({ title: `${a.name} (${a.id})`, value: a.id }))
);

// ---------------------------------------------------------------------------
// Name generation
// ---------------------------------------------------------------------------

function generateChannelName(type: string): string {
  const existing = new Set(channels.value.map((c) => c.name));
  if (!existing.has(type)) return type;
  let idx = 2;
  while (existing.has(`${type}-${idx}`)) idx += 1;
  return `${type}-${idx}`;
}

// ---------------------------------------------------------------------------
// Form helpers
// ---------------------------------------------------------------------------

function resetForm(): void {
  const type = formData.value.type;
  formData.value = {
    name: generateChannelName(type),
    type: type,
    enabled: true,
    irc_host: '',
    irc_port: 6667,
    irc_server_password: '',
    irc_nick: '',
    irc_username: 'animus',
    irc_realname: 'Animus Agent',
    irc_channels_multiline: '',
    irc_dm_only: false,
    irc_respond_to_channel_activity: true,
    irc_respond_to_direct_messages: true,
    irc_respond_to_notices: false,
    irc_allowed_dm_users: '',
    irc_agent_id: agents.value.length > 0 ? agents.value[0].id : '',
    irc_reconnect_enabled: true,
    irc_reconnect_initial_delay_ms: 1000,
    irc_reconnect_max_delay_ms: 60000,
    irc_use_tls: false,
    agent_id: agents.value.length > 0 ? agents.value[0].id : '',
    telegram_bot_token: '',
    vk_access_token: '',
    vk_group_id: '',
    bluesky_handle: '',
    bluesky_app_password: '',
    bluesky_pds: 'https://bsky.social',
    mastodon_handle: '',
    mastodon_instance: '',
    // Email (AgentMail) fields
    email_api_key: '',
    email_inbox_id: '',
    email_polling_wait: 25,
    // Twitter fields
    twitter_client_id: '',
    twitter_client_secret: '',
    twitter_access_token: '',
    twitter_refresh_token: '',
    twitter_tier: 'free',
    // Discord fields
    discord_bot_token: '',
    discord_application_id: '',
    discord_monitored_channels: '',
    discord_respond_to_dm: true,
    discord_respond_to_mentions: true,
    discord_respond_to_channels: false,
    discord_monitor_all_channels: false,
    discord_dm_whitelist_enabled: false,
    discord_allowed_dm_users: '',
    // Slack fields
    slack_bot_token: '',
    slack_app_token: '',
    slack_monitored_channels: '',
    slack_respond_to_mentions: true,
    slack_respond_to_all_messages: false,
    whatsapp_auth_dir: '/tmp/wa-auth',
    // Moltbook fields
    moltbook_api_key: '',
    // Nextcloud Talk fields
    nextcloud_server_url: '',
    nextcloud_username: '',
    nextcloud_app_password: '',
    nextcloud_watch_tokens: '',
    nextcloud_respond_in_dm: true,
    nextcloud_respond_in_group_on_mention: true,
    nextcloud_group_mention_trigger: '',
    min_response_interval: 0,
    allow_interjection: true,
  };
}

watch(() => formData.value.type, () => {
  if (isNew.value) {
    formData.value.name = generateChannelName(formData.value.type);
  }
});

// ---------------------------------------------------------------------------
// IRC config builders
// ---------------------------------------------------------------------------

function parseChannels(text: string): Array<{ name: string; key: string }> {
  const lines = text.split('\n').map((l) => l.trim()).filter((l) => l.length > 0);
  return lines.map((line) => {
    const split = line.split(/\s+/, 2);
    return { name: split[0], key: split[1] || '' };
  });
}

function buildIrcConfig(): Record<string, unknown> {
  const channels = parseChannels(formData.value.irc_channels_multiline);
  const allowedDmUsers = formData.value.irc_allowed_dm_users
    .split(/[\n,]/).map((x) => x.trim()).filter((x) => x.length > 0);

  return {
    host: formData.value.irc_host.trim(),
    port: Number(formData.value.irc_port || 6667),
    server_password: formData.value.irc_server_password,
    nick: formData.value.irc_nick.trim(),
    username: formData.value.irc_username.trim() || 'animus',
    realname: formData.value.irc_realname.trim() || 'Animus Agent',
    channels,
    dm_only: formData.value.irc_dm_only,
    respond_to_channel_activity: formData.value.irc_respond_to_channel_activity,
    respond_to_direct_messages: formData.value.irc_respond_to_direct_messages,
    respond_to_notices: formData.value.irc_respond_to_notices,
    allowed_dm_users: allowedDmUsers,
    agent_id: formData.value.irc_agent_id || '',
    reconnect: {
      enabled: formData.value.irc_reconnect_enabled,
      initial_delay_ms: Number(formData.value.irc_reconnect_initial_delay_ms || 1000),
      max_delay_ms: Number(formData.value.irc_reconnect_max_delay_ms || 60000),
    },
    use_tls: formData.value.irc_use_tls,
  };
}

function loadIrcConfig(cfg: Record<string, any>): void {
  formData.value.irc_host = String(cfg.host ?? '');
  formData.value.irc_port = Number(cfg.port ?? 6667);
  // Don't populate masked secrets — leave empty with placeholder
  formData.value.irc_server_password = cfg.server_password && cfg.server_password !== '***' ? String(cfg.server_password) : '';
  formData.value.irc_nick = String(cfg.nick ?? '');
  formData.value.irc_username = String(cfg.username ?? 'animus');
  formData.value.irc_realname = String(cfg.realname ?? 'Animus Agent');
  formData.value.irc_dm_only = String(cfg.dm_only ?? 'false') === 'true';
  formData.value.irc_respond_to_channel_activity = String(cfg.respond_to_channel_activity ?? 'true') === 'true';
  formData.value.irc_respond_to_direct_messages = String(cfg.respond_to_direct_messages ?? 'true') === 'true';
  formData.value.irc_respond_to_notices = String(cfg.respond_to_notices ?? 'false') === 'true';
  formData.value.irc_agent_id = String(cfg.agent_id ?? '');

  const chs: Array<any> = Array.isArray(cfg.channels) ? cfg.channels : [];
  formData.value.irc_channels_multiline = chs
    .map((ch) => `${String(ch.name ?? '')}${ch.key ? ` ${String(ch.key)}` : ''}`.trim())
    .filter((l) => l.length > 0).join('\n');

  const allowed: string[] = Array.isArray(cfg.allowed_dm_users) ? cfg.allowed_dm_users : [];
  formData.value.irc_allowed_dm_users = allowed.join('\n');

  const rc = cfg.reconnect && typeof cfg.reconnect === 'object' ? cfg.reconnect : {};
  formData.value.irc_reconnect_enabled = Boolean(rc.enabled ?? true);
  formData.value.irc_reconnect_initial_delay_ms = Number(rc.initial_delay_ms ?? 1000);
  formData.value.irc_reconnect_max_delay_ms = Number(rc.max_delay_ms ?? 60000);
  formData.value.irc_use_tls = String(cfg.use_tls ?? 'false') === 'true';
}

function buildConfig(): Record<string, unknown> {
  const type = formData.value.type;
  let cfg: Record<string, unknown> = {};
  if (type === 'irc') return buildIrcConfig();
  if (type === 'telegram') {
    cfg = { agent_id: formData.value.agent_id || '' };
    if (formData.value.telegram_bot_token) cfg.access_token = formData.value.telegram_bot_token;
    return cfg;
  }
  if (type === 'vk') {
    cfg = { agent_id: formData.value.agent_id || '' };
    if (formData.value.vk_access_token) cfg.access_token = formData.value.vk_access_token;
    cfg.group_id = formData.value.vk_group_id;
    return cfg;
  }
  if (type === 'bluesky') {
    cfg = {
      handle: formData.value.bluesky_handle,
      pds: formData.value.bluesky_pds,
      agent_id: formData.value.agent_id || '',
    };
    if (formData.value.bluesky_app_password) cfg.app_password = formData.value.bluesky_app_password;
    return cfg;
  }
  if (type === 'mastodon') {
    cfg = {
      handle: formData.value.mastodon_handle,
      instance: formData.value.mastodon_instance,
      agent_id: formData.value.agent_id || '',
    };
    return cfg;
  }
  if (type === 'email') {
    cfg = {
      backend: 'agentmail',
      agent_id: formData.value.agent_id || '',
      inbox_id: formData.value.email_inbox_id,
      polling_wait: Number(formData.value.email_polling_wait) || 25,
    };
    if (formData.value.email_api_key) cfg.api_key = formData.value.email_api_key;
    return cfg;
  }
  if (type === 'twitter') {
    cfg = {
      tier: formData.value.twitter_tier || 'free',
      agent_id: formData.value.agent_id || '',
    };
    if (formData.value.twitter_client_id) cfg.client_id = formData.value.twitter_client_id;
    if (formData.value.twitter_client_secret) cfg.client_secret = formData.value.twitter_client_secret;
    if (formData.value.twitter_access_token) cfg.access_token = formData.value.twitter_access_token;
    if (formData.value.twitter_refresh_token) cfg.refresh_token = formData.value.twitter_refresh_token;
    return cfg;
  }
  if (type === 'discord') {
    cfg = {
      agent_id: formData.value.agent_id || '',
      respond_to_dm: String(formData.value.discord_respond_to_dm),
      respond_to_mentions: String(formData.value.discord_respond_to_mentions),
      respond_to_channels: String(formData.value.discord_respond_to_channels),
      monitor_all_channels: String(formData.value.discord_monitor_all_channels),
    };
    if (formData.value.discord_bot_token) cfg.bot_token = formData.value.discord_bot_token;
    if (formData.value.discord_application_id) cfg.application_id = formData.value.discord_application_id;
    const monitored = formData.value.discord_monitored_channels
      .split(/[\n,]/).map((x) => x.trim()).filter((x) => x.length > 0);
    if (monitored.length > 0) cfg.monitored_channels = monitored;
    if (formData.value.discord_dm_whitelist_enabled) {
      cfg.dm_whitelist_enabled = 'true';
      const users = formData.value.discord_allowed_dm_users
        .split(/[\n,]/).map((x) => x.trim()).filter((x) => x.length > 0);
      if (users.length > 0) cfg.allowed_dm_users = users;
    }
    return cfg;
  }
  if (type === 'slack') {
    cfg = {
      agent_id: formData.value.agent_id || '',
      respond_to_mentions: String(formData.value.slack_respond_to_mentions),
      respond_to_all_messages: String(formData.value.slack_respond_to_all_messages),
      threaded_replies: String(formData.value.slack_threaded_replies),
    };
    if (formData.value.slack_bot_token) cfg.bot_token = formData.value.slack_bot_token;
    if (formData.value.slack_app_token) cfg.app_token = formData.value.slack_app_token;
    const monitored = formData.value.slack_monitored_channels
      .split(/[\n,]/).map((x) => x.trim()).filter((x) => x.length > 0);
    if (monitored.length > 0) cfg.monitored_channels = monitored;
    return cfg;
  }
  if (type === 'whatsapp') {
    cfg = { agent_id: formData.value.agent_id || '' };
    if (formData.value.whatsapp_auth_dir) cfg.auth_dir = formData.value.whatsapp_auth_dir;
    return cfg;
  }
  if (type === 'moltbook') {
    cfg = { agent_id: formData.value.agent_id || '' };
    if (formData.value.moltbook_api_key) cfg.api_key = formData.value.moltbook_api_key;
    if (moltbookRegResult.value?.agent_name) cfg.agent_name = moltbookRegResult.value.agent_name;
    return cfg;
  }
  if (type === 'nextcloud') {
    cfg = {
      agent_id: formData.value.agent_id || '',
      server_url: formData.value.nextcloud_server_url.trim(),
      username: formData.value.nextcloud_username.trim(),
      respond_in_dm: String(formData.value.nextcloud_respond_in_dm),
      respond_in_group_on_mention: String(formData.value.nextcloud_respond_in_group_on_mention),
    };
    if (formData.value.nextcloud_app_password) cfg.app_password = formData.value.nextcloud_app_password;
    if (formData.value.nextcloud_group_mention_trigger.trim()) {
      cfg.group_mention_trigger = formData.value.nextcloud_group_mention_trigger.trim();
    }
    const tokens = formData.value.nextcloud_watch_tokens
      .split(/[\n,]/).map((x) => x.trim()).filter((x) => x.length > 0);
    if (tokens.length > 0) {
      cfg.watch_tokens = JSON.stringify(tokens);
    }
    return cfg;
  }
  return cfg;
}

// ---------------------------------------------------------------------------
// CRUD operations
// ---------------------------------------------------------------------------

async function loadChannels(): Promise<void> {
  loading.value = true;
  error.value = '';
  try {
    const data = await apiGet<{ channels: ChannelInfo[]; total: number }>('/api/v1/channels');
    channels.value = Array.isArray(data.channels) ? data.channels : [];
  } catch (e: any) {
    error.value = e.message;
  } finally {
    loading.value = false;
  }
}

async function loadAgents(): Promise<void> {
  try {
    const data = await apiGet<{ agents: AgentInfo[] }>('/api/v1/agents');
    agents.value = Array.isArray(data.agents) ? data.agents : [];
    // Auto-select first agent for forms if none selected
    const firstId = agents.value.length > 0 ? agents.value[0].id : '';
    if (firstId) {
      if (!selectedAgent.value) selectedAgent.value = firstId;
      if (!formData.value.irc_agent_id) formData.value.irc_agent_id = firstId;
      if (!formData.value.agent_id) formData.value.agent_id = firstId;
    }
  } catch {
    agents.value = [];
  }
}

function openCreate(): void {
  editing.value = null;
  resetForm();
  showForm.value = true;
}

function openEdit(item: ChannelInfo): void {
  editing.value = item;
  const type = (item.type || 'irc') as ChannelType;
  formData.value.name = item.name;
  formData.value.type = type;
  formData.value.enabled = item.enabled;

  const cfg = item.config || {};

  if (type === 'irc') {
    loadIrcConfig(cfg);
  } else if (type === 'telegram') {
    formData.value.telegram_bot_token = ''; // Never prefill tokens
    formData.value.agent_id = String(cfg.agent_id ?? '');
  } else if (type === 'vk') {
    formData.value.vk_access_token = '';
    formData.value.vk_group_id = cfg.group_id || '';
    formData.value.agent_id = String(cfg.agent_id ?? '');
  } else if (type === 'bluesky') {
    formData.value.bluesky_handle = cfg.handle || '';
    formData.value.bluesky_app_password = '';
    formData.value.bluesky_pds = cfg.pds || 'https://bsky.social';
    formData.value.agent_id = String(cfg.agent_id ?? '');
  } else if (type === 'mastodon') {
    formData.value.mastodon_handle = cfg.handle || '';
    formData.value.mastodon_instance = cfg.instance || '';
    formData.value.agent_id = String(cfg.agent_id ?? '');
  } else if (type === 'email') {
    formData.value.email_api_key = '';
    formData.value.email_inbox_id = cfg.inbox_id || '';
    formData.value.email_polling_wait = Number(cfg.polling_wait) || 25;
    formData.value.agent_id = String(cfg.agent_id ?? '');
  } else if (type === 'twitter') {
    formData.value.twitter_client_id = cfg.client_id || '';
    formData.value.twitter_client_secret = ''; // Never prefill secrets
    formData.value.twitter_access_token = ''; // Never prefill tokens
    formData.value.twitter_refresh_token = '';
    formData.value.twitter_tier = cfg.tier || 'free';
    formData.value.agent_id = String(cfg.agent_id ?? '');
  } else if (type === 'discord') {
    formData.value.discord_bot_token = '';
    formData.value.discord_application_id = cfg.application_id || '';
    const monitored: string[] = Array.isArray(cfg.monitored_channels) ? cfg.monitored_channels : [];
    formData.value.discord_monitored_channels = monitored.join('\n');
    formData.value.discord_respond_to_dm = String(cfg.respond_to_dm ?? 'true') === 'true';
    formData.value.discord_respond_to_mentions = String(cfg.respond_to_mentions ?? 'true') === 'true';
    formData.value.discord_respond_to_channels = String(cfg.respond_to_channels ?? 'false') === 'true';
    formData.value.discord_monitor_all_channels = String(cfg.monitor_all_channels ?? 'false') === 'true';
    formData.value.discord_dm_whitelist_enabled = String(cfg.dm_whitelist_enabled ?? 'false') === 'true';
    const allowedDm: string[] = Array.isArray(cfg.allowed_dm_users) ? cfg.allowed_dm_users : [];
    formData.value.discord_allowed_dm_users = allowedDm.join('\n');
    formData.value.agent_id = String(cfg.agent_id ?? '');
  } else if (type === 'slack') {
    formData.value.slack_bot_token = '';
    formData.value.slack_app_token = cfg.app_token ? '' : ''; // Never prefill
    const monitored: string[] = Array.isArray(cfg.monitored_channels) ? cfg.monitored_channels : [];
    formData.value.slack_monitored_channels = monitored.join('\n');
    formData.value.slack_respond_to_mentions = String(cfg.respond_to_mentions ?? 'true') === 'true';
    formData.value.slack_respond_to_all_messages = String(cfg.respond_to_all_messages ?? 'false') === 'true';
    formData.value.slack_threaded_replies = String(cfg.threaded_replies ?? 'true') === 'true';
    formData.value.agent_id = String(cfg.agent_id ?? '');
  } else if (type === 'whatsapp') {
    formData.value.whatsapp_auth_dir = cfg.auth_dir || '/tmp/wa-auth';
    formData.value.agent_id = String(cfg.agent_id ?? '');
  } else if (type === 'moltbook') {
    formData.value.moltbook_api_key = ''; // Never prefill keys
    formData.value.agent_id = String(cfg.agent_id ?? '');
  } else if (type === 'nextcloud') {
    formData.value.nextcloud_server_url = String(cfg.server_url ?? '');
    formData.value.nextcloud_username = String(cfg.username ?? '');
    formData.value.nextcloud_app_password = ''; // Never prefill passwords
    formData.value.nextcloud_respond_in_dm = String(cfg.respond_in_dm ?? 'true') === 'true';
    formData.value.nextcloud_respond_in_group_on_mention = String(cfg.respond_in_group_on_mention ?? 'true') === 'true';
    formData.value.nextcloud_group_mention_trigger = String(cfg.group_mention_trigger ?? '');
    // Parse watch_tokens (stored as JSON array string)
    const wtStr = String(cfg.watch_tokens ?? '');
    if (wtStr) {
      try {
        const arr = JSON.parse(wtStr);
        formData.value.nextcloud_watch_tokens = Array.isArray(arr) ? arr.join('\n') : '';
      } catch {
        formData.value.nextcloud_watch_tokens = '';
      }
    } else {
      formData.value.nextcloud_watch_tokens = '';
    }
    formData.value.agent_id = String(cfg.agent_id ?? '');
  }

  // Load common fields (Ticket 114)
  formData.value.min_response_interval = Number(cfg.min_response_interval ?? 0);
  formData.value.allow_interjection = cfg.allow_interjection !== false; // default true

  // Reset WhatsApp QR state
  qrUrl.value = '';
  qrStatus.value = '';
  qrError.value = '';

  showForm.value = true;
}

function closeForm(): void {
  showForm.value = false;
  editing.value = null;
  twitterOAuthStatus.value = '';
  twitterOAuthError.value = false;
  qrUrl.value = '';
  qrStatus.value = '';
  qrError.value = '';
  clearAuthMsg.value = '';
  clearAuthError.value = false;
  // Reset Moltbook registration state
  moltbookRegName.value = '';
  moltbookRegDesc.value = '';
  moltbookRegLoading.value = false;
  moltbookRegError.value = '';
  moltbookRegResult.value = null;
}

async function startTwitterOAuth(): Promise<void> {
  if (!editing.value) return;
  twitterOAuthLoading.value = true;
  twitterOAuthStatus.value = '';
  twitterOAuthError.value = false;

  try {
    const name = editing.value.name;
    const result = await apiRequest('GET', `/api/v1/channels/${encodeURIComponent(name)}/auth/twitter`);
    if (result.authorize_url) {
      // Open the Twitter authorization URL in a new tab
      window.open(result.authorize_url, '_blank');
      twitterOAuthStatus.value = t('channels.form.twitter.oauthStarted');
    } else {
      twitterOAuthError.value = true;
      twitterOAuthStatus.value = result.error || 'Failed to start OAuth flow';
    }
  } catch (e: any) {
    twitterOAuthError.value = true;
    twitterOAuthStatus.value = e.message || 'OAuth flow failed';
  } finally {
    twitterOAuthLoading.value = false;
  }
}

async function clearWhatsAppAuth(): Promise<void> {
  if (!editing.value) return;
  if (!confirm('This will clear the WhatsApp auth state and force re-pairing. Continue?')) return;

  clearAuthLoading.value = true;
  clearAuthMsg.value = '';
  clearAuthError.value = false;

  try {
    const name = editing.value.name;
    const result = await apiRequest('POST', `/api/v1/channels/${encodeURIComponent(name)}/whatsapp/clear-auth`);
    clearAuthMsg.value = result.message || 'Auth state cleared. A new QR code will be generated.';

    // Clear the QR state since we're re-pairing
    qrUrl.value = '';
    qrStatus.value = '';
    qrError.value = '';
  } catch (e: any) {
    clearAuthError.value = true;
    clearAuthMsg.value = e.message || 'Failed to clear auth state';
  } finally {
    clearAuthLoading.value = false;
  }
}

async function registerMoltbook(): Promise<void> {
  if (!moltbookRegName.value) return;
  moltbookRegLoading.value = true;
  moltbookRegError.value = '';
  moltbookRegResult.value = null;

  try {
    const result = await apiRequest('POST', '/api/v1/channels/moltbook/register', {
      name: moltbookRegName.value,
      description: moltbookRegDesc.value || undefined,
    });

    if (result.api_key) {
      // Auto-populate the API key field
      formData.value.moltbook_api_key = result.api_key;
    }

    moltbookRegResult.value = result;
  } catch (e: any) {
    moltbookRegError.value = e.message || 'Registration failed';
  } finally {
    moltbookRegLoading.value = false;
  }
}

async function fetchWhatsAppQr(): Promise<void> {
  if (!editing.value) return;
  qrLoading.value = true;
  qrError.value = '';
  qrUrl.value = '';
  qrStatus.value = '';

  try {
    const name = editing.value.name;

    // Restart the channel to trigger a fresh QR code
    await apiRequest('POST', `/api/v1/channels/${encodeURIComponent(name)}/disable`);
    await new Promise(res => setTimeout(res, 1500));
    await apiRequest('POST', `/api/v1/channels/${encodeURIComponent(name)}/enable`);

    // Poll for the QR URL (gateway needs a few seconds to connect + handshake)
    let attempts = 0;
    const poll = async (): Promise<void> => {
      attempts++;
      if (attempts > 15) {
        qrStatus.value = 'waiting';
        qrError.value = 'Timed out waiting for QR code. The gateway may need longer to connect.';
        return;
      }
      try {
        const r = await apiRequest('GET', `/api/v1/channels/${encodeURIComponent(name)}/whatsapp/qr`);
        if (r.ok && r.qr_url) {
          qrUrl.value = r.qr_url;
          qrStatus.value = 'ready';
        } else {
          await new Promise(res => setTimeout(res, 2000));
          await poll();
        }
      } catch {
        await new Promise(res => setTimeout(res, 2000));
        await poll();
      }
    };
    await poll();
  } catch (e: any) {
    qrError.value = e.message || 'Request failed';
  } finally {
    qrLoading.value = false;
  }
}

async function submitForm(): Promise<void> {
  saving.value = true;
  error.value = '';
  successMsg.value = '';
  try {
    const config = buildConfig();
    // Add common fields that apply to all channel types (Ticket 114)
    config.min_response_interval = Number(formData.value.min_response_interval) || 0;
    config.allow_interjection = formData.value.allow_interjection === true;
    if (isNew.value) {
      await apiRequest('POST', '/api/v1/channels', {
        name: formData.value.name,
        type: formData.value.type,
        enabled: formData.value.enabled,
        config,
      });
      successMsg.value = t('channels.createSuccess', { name: formData.value.name });
    } else {
      await apiRequest('PATCH', `/api/v1/channels/${encodeURIComponent(formData.value.name)}`, {
        config,
      });
      // Toggle enabled if changed
      const originalEnabled = Boolean(editing.value?.enabled);
      if (formData.value.enabled !== originalEnabled) {
        await apiRequest('POST', `/api/v1/channels/${encodeURIComponent(formData.value.name)}/${formData.value.enabled ? 'enable' : 'disable'}`);
      }
      successMsg.value = t('channels.updateSuccess', { name: formData.value.name });
    }
    closeForm();
    await loadChannels();
  } catch (e: any) {
    error.value = e.message;
  } finally {
    saving.value = false;
  }
}

async function toggleEnabled(item: ChannelInfo): Promise<void> {
  error.value = '';
  try {
    await apiRequest('POST', `/api/v1/channels/${encodeURIComponent(item.name)}/${item.enabled ? 'disable' : 'enable'}`);
    await loadChannels();
  } catch (e: any) {
    error.value = e.message;
  }
}

async function deleteChannel(item: ChannelInfo): Promise<void> {
  if (!confirm(t('channels.actions.confirmDelete', { name: item.name }))) return;
  error.value = '';
  try {
    await apiRequest('DELETE', `/api/v1/channels/${encodeURIComponent(item.name)}`);
    successMsg.value = t('channels.deleteSuccess', { name: item.name });
    await loadChannels();
  } catch (e: any) {
    error.value = e.message;
  }
}

function formatDate(unixMs: number): string {
  if (!unixMs) return '—';
  return new Date(unixMs).toLocaleString();
}

function typeLabel(type: string): string {
  const found = channelTypes.find((ct) => ct.value === type);
  return found ? found.title : type;
}

onMounted(async () => {
  await Promise.all([loadChannels(), loadAgents()]);
});
</script>

<template>
  <v-card rounded="xl" class="panel">
    <v-card-title class="d-flex align-center">
      <span>{{ t('channels.title') }}</span>
      <v-spacer />
      <v-btn variant="text" @click="loadChannels" class="mr-2">{{ t('channels.actions.refresh') }}</v-btn>
      <v-btn color="primary" @click="openCreate">{{ t('channels.actions.add') }}</v-btn>
    </v-card-title>
    <v-card-text>
      <v-alert v-if="error" type="error" variant="tonal" density="comfortable" class="mb-4">{{ error }}</v-alert>
      <v-alert v-if="successMsg" type="success" variant="tonal" density="comfortable" class="mb-4">{{ successMsg }}</v-alert>

      <v-progress-linear v-if="loading" indeterminate class="mb-4" />

      <!-- Channel list -->
      <v-table v-if="channels.length > 0" density="comfortable">
        <thead>
          <tr>
            <th>{{ t('channels.columns.name') }}</th>
            <th>{{ t('channels.columns.type') }}</th>
            <th>{{ t('channels.columns.identity') }}</th>
            <th>{{ t('channels.columns.endpoint') }}</th>
            <th>{{ t('channels.columns.enabled') }}</th>
            <th>{{ t('channels.columns.connected') }}</th>
            <th>{{ t('channels.columns.lastEvent') }}</th>
            <th>{{ t('channels.columns.actions') }}</th>
          </tr>
        </thead>
        <tbody>
          <tr v-for="item in channels" :key="item.name">
            <td class="font-weight-medium">{{ item.name }}</td>
            <td><v-chip size="small" label>{{ typeLabel(item.type) }}</v-chip></td>
            <td>{{ item.identity }}</td>
            <td class="text-body-2">{{ item.endpoint }}</td>
            <td>
              <v-switch
                :model-value="item.enabled"
                density="compact"
                hide-details
                color="success"
                @update:model-value="toggleEnabled(item)"
              />
            </td>
            <td>
              <v-chip :color="item.connected ? 'success' : 'grey'" size="small" variant="tonal">
                {{ item.connected ? t('channels.state.connected') : t('channels.state.disconnected') }}
              </v-chip>
            </td>
            <td class="text-body-2">{{ formatDate(item.last_event_unix_ms) }}</td>
            <td>
              <v-btn icon size="small" variant="text" @click="openEdit(item)">
                <v-icon>mdi-pencil</v-icon>
              </v-btn>
              <v-btn icon size="small" variant="text" color="error" @click="deleteChannel(item)">
                <v-icon>mdi-delete</v-icon>
              </v-btn>
            </td>
          </tr>
        </tbody>
      </v-table>

      <v-alert v-else-if="!loading" type="info" variant="tonal" density="comfortable">
        {{ t('channels.empty') }}
      </v-alert>

      <!-- Create / Edit dialog -->
      <v-dialog v-model="showForm" max-width="640" persistent>
        <v-card rounded="xl">
          <v-card-title>{{ isNew ? t('channels.form.createTitle') : t('channels.form.editTitle', { name: formData.name }) }}</v-card-title>
          <v-card-text>
            <!-- Name & Type -->
            <v-text-field
              v-if="isNew"
              v-model="formData.name"
              :label="t('channels.form.name')"
              density="comfortable"
              class="mb-2"
            />
            <v-select
              v-if="isNew"
              v-model="formData.type"
              :items="channelTypes"
              :label="t('channels.form.type')"
              density="comfortable"
              class="mb-2"
            />

            <!-- IRC fields -->
            <template v-if="formData.type === 'irc'">
              <v-text-field v-model="formData.irc_host" :label="t('channels.form.irc.host')" density="comfortable" class="mb-2" />
              <v-text-field v-model="formData.irc_port" :label="t('channels.form.irc.port')" type="number" density="comfortable" class="mb-2" />
              <v-checkbox v-model="formData.irc_use_tls" label="Use TLS (port 6697)" density="comfortable" hint="Enable TLS encryption for the IRC connection" persistent-hint class="mb-2" />
              <v-text-field v-model="formData.irc_server_password" :label="t('channels.form.irc.serverPassword')" density="comfortable" type="password" :placeholder="isNew ? '' : '(unchanged)'" class="mb-2" />
              <v-text-field v-model="formData.irc_nick" :label="t('channels.form.irc.nick')" density="comfortable" class="mb-2" />
              <v-text-field v-model="formData.irc_username" :label="t('channels.form.irc.username')" density="comfortable" class="mb-2" />
              <v-text-field v-model="formData.irc_realname" :label="t('channels.form.irc.realname')" density="comfortable" class="mb-2" />
              <v-textarea v-model="formData.irc_channels_multiline" :label="t('channels.form.irc.channels')" :hint="t('channels.form.irc.channelsHint')" density="comfortable" rows="3" class="mb-2" />
              <v-select v-model="formData.irc_agent_id" :items="agentItems" :label="t('channels.form.irc.agent')" density="comfortable" class="mb-2" />
              <v-checkbox v-model="formData.irc_respond_to_channel_activity" :label="t('channels.form.irc.respondToChannel')" density="comfortable" hide-details class="mb-1" />
              <v-checkbox v-model="formData.irc_respond_to_direct_messages" :label="t('channels.form.irc.respondToDm')" density="comfortable" hide-details class="mb-1" />
              <v-checkbox v-model="formData.irc_respond_to_notices" :label="t('channels.form.irc.respondToNotices')" density="comfortable" hide-details class="mb-2" />
              <v-textarea v-model="formData.irc_allowed_dm_users" :label="t('channels.form.irc.allowedDmUsers')" density="comfortable" rows="2" class="mb-2" />
              <v-checkbox v-model="formData.irc_reconnect_enabled" :label="t('channels.form.irc.reconnect')" density="comfortable" hide-details class="mb-1" />
            </template>

            <!-- Telegram fields -->
            <template v-if="formData.type === 'telegram'">
              <v-select v-model="formData.agent_id" :items="agentItems" :label="t('channels.form.agent')" density="comfortable" class="mb-2" />
              <v-text-field
                v-model="formData.telegram_bot_token"
                :label="t('channels.form.telegram.botToken')"
                :hint="isNew ? '' : t('channels.form.telegram.tokenHint')"
                :type="isNew ? 'text' : 'password'"
                density="comfortable"
                class="mb-2"
              />
            </template>

            <!-- VK fields -->
            <template v-if="formData.type === 'vk'">
              <v-select v-model="formData.agent_id" :items="agentItems" :label="t('channels.form.agent')" density="comfortable" class="mb-2" />
              <v-text-field v-model="formData.vk_access_token" :label="t('channels.form.vk.accessToken')" :type="isNew ? 'text' : 'password'" density="comfortable" class="mb-2" />
              <v-text-field v-model="formData.vk_group_id" :label="t('channels.form.vk.groupId')" density="comfortable" class="mb-2" />
            </template>

            <!-- Bluesky fields -->
            <template v-if="formData.type === 'bluesky'">
              <v-select v-model="formData.agent_id" :items="agentItems" :label="t('channels.form.agent')" density="comfortable" class="mb-2" />
              <v-text-field v-model="formData.bluesky_handle" :label="t('channels.form.bluesky.handle')" density="comfortable" class="mb-2" />
              <v-text-field v-model="formData.bluesky_app_password" :label="t('channels.form.bluesky.appPassword')" type="password" density="comfortable" class="mb-2" />
              <v-text-field v-model="formData.bluesky_pds" :label="t('channels.form.bluesky.pds')" density="comfortable" class="mb-2" />
            </template>

            <!-- Mastodon fields -->
            <template v-if="formData.type === 'mastodon'">
              <v-select v-model="formData.agent_id" :items="agentItems" :label="t('channels.form.agent')" density="comfortable" class="mb-2" />
              <v-text-field v-model="formData.mastodon_handle" :label="t('channels.form.mastodon.handle')" density="comfortable" class="mb-2" />
              <v-text-field v-model="formData.mastodon_instance" :label="t('channels.form.mastodon.instance')" density="comfortable" class="mb-2" />
            </template>

            <!-- Email (AgentMail) fields -->
            <template v-if="formData.type === 'email'">
              <v-select v-model="formData.agent_id" :items="agentItems" :label="t('channels.form.agent')" density="comfortable" class="mb-2" />
              <v-text-field
                v-model="formData.email_api_key"
                :label="t('channels.form.email.apiKey')"
                :hint="isNew ? '' : t('channels.form.email.apiKeyHint')"
                :type="isNew ? 'text' : 'password'"
                density="comfortable"
                class="mb-2"
              />
              <v-text-field v-model="formData.email_inbox_id" :label="t('channels.form.email.inboxId')" density="comfortable" class="mb-2" />
              <v-text-field v-model="formData.email_polling_wait" :label="t('channels.form.email.pollingWait')" type="number" density="comfortable" class="mb-2" />
            </template>

            <!-- Twitter fields -->
            <template v-if="formData.type === 'twitter'">
              <v-select v-model="formData.agent_id" :items="agentItems" :label="t('channels.form.agent')" density="comfortable" class="mb-2" />
              <v-select v-model="formData.twitter_tier" :items="[{ title: 'Free', value: 'free' }, { title: 'Basic', value: 'basic' }, { title: 'Pro', value: 'pro' }]" :label="t('channels.form.twitter.tier')" density="comfortable" class="mb-2" />
              <v-text-field v-model="formData.twitter_client_id" :label="t('channels.form.twitter.clientId')" density="comfortable" class="mb-2" />
              <v-text-field v-model="formData.twitter_client_secret" :label="t('channels.form.twitter.clientSecret')" :type="isNew ? 'text' : 'password'" density="comfortable" class="mb-2" />
              <v-text-field v-model="formData.twitter_access_token" :label="t('channels.form.twitter.accessToken')" :type="isNew ? 'text' : 'password'" :hint="isNew ? '' : t('channels.form.twitter.tokenHint')" density="comfortable" class="mb-2" />
              <v-text-field v-model="formData.twitter_refresh_token" :label="t('channels.form.twitter.refreshToken')" :type="isNew ? 'text' : 'password'" density="comfortable" class="mb-2" />
              <v-btn v-if="!isNew && formData.twitter_client_id" color="primary" variant="tonal" block class="mb-2" @click="startTwitterOAuth" :loading="twitterOAuthLoading">
                <v-icon start>mdi-link-variant</v-icon>
                {{ t('channels.form.twitter.authorize') }}
              </v-btn>
              <div v-if="twitterOAuthStatus" class="text-caption mb-2" :class="twitterOAuthError ? 'text-error' : 'text-success'">{{ twitterOAuthStatus }}</div>
            </template>
            <!-- Discord fields -->
            <template v-if="formData.type === 'discord'">
              <v-select v-model="formData.agent_id" :items="agentItems" :label="t('channels.form.agent')" density="comfortable" class="mb-2" />
              <v-text-field
                v-model="formData.discord_bot_token"
                :label="t('channels.form.discord.botToken')"
                :hint="isNew ? '' : t('channels.form.discord.tokenHint')"
                :type="isNew ? 'text' : 'password'"
                density="comfortable"
                class="mb-2"
              />
              <v-text-field v-model="formData.discord_application_id" :label="t('channels.form.discord.applicationId')" density="comfortable" class="mb-2" />
              <v-textarea v-model="formData.discord_monitored_channels" :label="t('channels.form.discord.monitoredChannels')" :hint="t('channels.form.discord.monitoredChannelsHint')" density="comfortable" rows="3" class="mb-2" />
              <v-checkbox v-model="formData.discord_monitor_all_channels" :label="t('channels.form.discord.monitorAllChannels')" density="comfortable" hide-details class="mb-1" />
              <v-checkbox v-model="formData.discord_respond_to_dm" :label="t('channels.form.discord.respondToDm')" density="comfortable" hide-details class="mb-1" />
              <v-checkbox v-model="formData.discord_respond_to_mentions" :label="t('channels.form.discord.respondToMentions')" density="comfortable" hide-details class="mb-1" />
              <v-checkbox v-model="formData.discord_respond_to_channels" :label="t('channels.form.discord.respondToChannels')" density="comfortable" hide-details class="mb-1" />
              <v-checkbox v-model="formData.discord_dm_whitelist_enabled" :label="t('channels.form.discord.dmWhitelistEnabled')" density="comfortable" hide-details class="mb-1" />
              <v-textarea v-if="formData.discord_dm_whitelist_enabled" v-model="formData.discord_allowed_dm_users" :label="t('channels.form.discord.allowedDmUsers')" :hint="t('channels.form.discord.allowedDmUsersHint')" density="comfortable" rows="3" class="mb-2" />
            </template>
            <!-- Slack fields -->
            <template v-if="formData.type === 'slack'">
              <v-select v-model="formData.agent_id" :items="agentItems" :label="t('channels.form.agent')" density="comfortable" class="mb-2" />
              <v-text-field
                v-model="formData.slack_bot_token"
                :label="t('channels.form.slack.botToken')"
                :hint="isNew ? '' : t('channels.form.slack.tokenHint')"
                :type="isNew ? 'text' : 'password'"
                density="comfortable"
                class="mb-2"
              />
              <v-text-field
                v-model="formData.slack_app_token"
                :label="t('channels.form.slack.appToken')"
                :hint="t('channels.form.slack.appTokenHint')"
                :type="isNew ? 'text' : 'password'"
                density="comfortable"
                class="mb-2"
              />
              <v-textarea v-model="formData.slack_monitored_channels" :label="t('channels.form.slack.monitoredChannels')" :hint="t('channels.form.slack.monitoredChannelsHint')" density="comfortable" rows="3" class="mb-2" />
              <v-checkbox v-model="formData.slack_respond_to_mentions" :label="t('channels.form.slack.respondToMentions')" density="comfortable" hide-details class="mb-1" />
              <v-checkbox v-model="formData.slack_respond_to_all_messages" :label="t('channels.form.slack.respondToAll')" density="comfortable" hide-details class="mb-1" />
              <v-checkbox v-model="formData.slack_threaded_replies" :label="t('channels.form.slack.threadedReplies')" density="comfortable" hide-details class="mb-1" />
            </template>
            <template v-if="formData.type === 'whatsapp'">
              <v-select v-model="formData.agent_id" :items="agentItems" :label="t('channels.form.agent')" density="comfortable" class="mb-2" />
              <v-text-field v-model="formData.whatsapp_auth_dir" label="Auth Directory" hint="/tmp/wa-auth by default" density="comfortable" class="mb-2" />
              <v-divider class="mb-3" />
              <!-- QR Code Section (only when editing existing channel) -->
              <div v-if="!isNew">
                <v-btn color="primary" variant="tonal" :loading="qrLoading" @click="fetchWhatsAppQr" class="mb-3">
                  <v-icon start>mdi-qrcode</v-icon> Generate QR Code
                </v-btn>
                <v-btn color="error" variant="outlined" @click="clearWhatsAppAuth" :loading="clearAuthLoading" class="ml-2 mb-3">
                  <v-icon start>mdi-lock-off</v-icon> Clear Auth
                </v-btn>
                <v-alert v-if="clearAuthMsg" :type="clearAuthError ? 'error' : 'success'" variant="tonal" density="compact" class="mb-2">{{ clearAuthMsg }}</v-alert>
                <v-alert v-if="qrError" type="error" variant="tonal" density="compact" class="mb-2">{{ qrError }}</v-alert>
                <v-alert v-if="qrStatus === 'waiting' && !qrLoading" type="warning" variant="tonal" density="compact" class="mb-2">
                  QR not yet available. The gateway may still be connecting — try again in a few seconds.
                </v-alert>
                <div v-if="qrUrl" class="d-flex flex-column align-center pa-4">
                  <VueQr :text="qrUrl" :size="280" :margin="1" color-dark="#1a1a2e" color-light="#ffffff" />
                  <p class="text-caption text-center mt-2" style="max-width: 300px;">
                    Open WhatsApp → Settings → Linked Devices → Link a Device and scan this code.
                  </p>
                </div>
              </div>
              <v-alert v-else type="info" variant="tonal" density="compact" class="mb-2">
                Save the channel first, then generate a QR code to pair with WhatsApp.
              </v-alert>
            </template>
            <!-- Moltbook fields -->
            <template v-if="formData.type === 'moltbook'">
              <v-select v-model="formData.agent_id" :items="agentItems" :label="t('channels.form.agent')" density="comfortable" class="mb-2" />
              <v-text-field
                v-model="formData.moltbook_api_key"
                label="API Key"
                hint="Your Moltbook API key (moltbook_xxx). Paste an existing key or create a new account below."
                :type="isNew ? 'text' : 'password'"
                density="comfortable"
                class="mb-2"
              />
              <v-divider class="mb-3" />
              <div class="text-subtitle-2 mb-2">Create a New Moltbook Account</div>
              <v-text-field
                v-model="moltbookRegName"
                label="Account Name"
                hint="Desired agent name on Moltbook"
                density="comfortable"
                class="mb-2"
              />
              <v-text-field
                v-model="moltbookRegDesc"
                label="Description (optional)"
                hint="Short description of the agent"
                density="comfortable"
                class="mb-2"
              />
              <v-btn
                color="primary"
                variant="tonal"
                :loading="moltbookRegLoading"
                :disabled="!moltbookRegName"
                @click="registerMoltbook"
                class="mb-3"
              >
                <v-icon start>mdi-account-plus</v-icon> Create Account
              </v-btn>
              <v-alert v-if="moltbookRegError" type="error" variant="tonal" density="compact" class="mb-2">{{ moltbookRegError }}</v-alert>
              <v-alert v-if="moltbookRegResult" type="success" variant="tonal" density="compact" class="mb-2">
                <div class="mb-1"><strong>Account created!</strong> API key has been set above.</div>
                <div class="mb-1" v-if="moltbookRegResult.tweet_text">
                  <strong>Verification tweet:</strong> Post this from your Twitter account to activate the account:
                  <div class="pa-2 mt-1 rounded" style="background: rgba(var(--v-theme-on-surface), 0.1); font-family: monospace; font-size: 0.85em; word-break: break-word;">{{ moltbookRegResult.tweet_text }}</div>
                </div>
                <div v-if="moltbookRegResult.claim_url" class="text-caption mt-1">
                  Claim URL: <a :href="moltbookRegResult.claim_url" target="_blank">{{ moltbookRegResult.claim_url }}</a>
                </div>
              </v-alert>
              <v-alert type="info" variant="tonal" density="compact" class="mb-2">
                Moltbook is a poll-based platform for AI agents. The agent uses the channels tool to post, comment, browse, and search. Account creation is done by agents — humans verify by posting a tweet from their personal Twitter account.
              </v-alert>
            </template>
            <!-- Nextcloud Talk fields -->
            <template v-if="formData.type === 'nextcloud'">
              <v-select v-model="formData.agent_id" :items="agentItems" :label="t('channels.form.agent')" density="comfortable" class="mb-2" />
              <v-text-field
                v-model="formData.nextcloud_server_url"
                label="Server URL"
                hint="e.g. https://cloud.example.com"
                placeholder="https://cloud.example.com"
                density="comfortable"
                class="mb-2"
              />
              <v-text-field
                v-model="formData.nextcloud_username"
                label="Username"
                hint="Nextcloud user account (e.g. animus-bot)"
                density="comfortable"
                class="mb-2"
              />
              <v-text-field
                v-model="formData.nextcloud_app_password"
                label="App Password"
                hint="Generate in Nextcloud → Settings → Security → Devices & sessions"
                :type="isNew ? 'text' : 'password'"
                density="comfortable"
                class="mb-2"
              />
              <v-textarea
                v-model="formData.nextcloud_watch_tokens"
                label="Watch Tokens (optional)"
                hint="Conversation tokens to monitor (one per line). Leave empty to auto-discover all conversations."
                density="comfortable"
                rows="2"
                class="mb-2"
              />
              <v-checkbox v-model="formData.nextcloud_respond_in_dm" label="Respond to all direct messages" density="comfortable" hide-details class="mb-1" />
              <v-checkbox v-model="formData.nextcloud_respond_in_group_on_mention" label="Respond to @mentions in group chats only" density="comfortable" hide-details class="mb-1" />
              <v-text-field
                v-model="formData.nextcloud_group_mention_trigger"
                label="Mention Trigger (optional)"
                hint="String that triggers a response in group chats. Defaults to @username"
                density="comfortable"
                class="mb-2"
              />
              <v-alert type="info" variant="tonal" density="compact" class="mb-2">
                Nextcloud Talk authenticates as a regular user via app password. The agent monitors conversations via OCS API long-polling. Create a dedicated user account for the agent.
              </v-alert>
            </template>

            <!-- Common: minimum response interval (Ticket 114) -->
            <v-divider class="my-3" />
            <v-text-field
              v-model="formData.min_response_interval"
              :label="t('channels.form.minResponseInterval')"
              type="number"
              hint="Seconds between responses. 0 = immediate. Incoming messages during the wait are batched."
              density="comfortable"
              class="mb-2"
            />

            <!-- Common: allow interjection (Ticket 115) -->
            <v-switch
              v-model="formData.allow_interjection"
              :label="t('channels.form.allowInterjection')"
              color="primary"
              hint="When enabled, new messages are injected into the agent's active processing. Disable for community channels."
              density="comfortable"
              class="mb-2"
              inset
            />
          </v-card-text>
          <v-card-actions>
            <v-spacer />
            <v-btn variant="text" @click="closeForm">{{ t('channels.actions.cancel') }}</v-btn>
            <v-btn color="primary" :loading="saving" @click="submitForm">
              {{ isNew ? t('channels.actions.create') : t('channels.actions.save') }}
            </v-btn>
          </v-card-actions>
        </v-card>
      </v-dialog>
    </v-card-text>
  </v-card>
</template>

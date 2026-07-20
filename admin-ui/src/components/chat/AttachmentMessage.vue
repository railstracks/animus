<template>
  <div class="attachment-container">
    <div
      v-for="att in attachments"
      :key="att.id"
      class="attachment-item"
    >
      <!-- Image -->
      <div v-if="att.mime_type.startsWith('image/')" class="att-image-wrap" @click="openLightbox(att)">
        <img
          :src="attachmentUrl(att)"
          :alt="att.filename"
          class="att-image"
          loading="lazy"
        />
        <div class="att-meta">
          <span class="att-filename">{{ att.filename }}</span>
          <span class="att-size">{{ formatSize(att.size_bytes) }}</span>
        </div>
      </div>

      <!-- Audio -->
      <div v-else-if="att.mime_type.startsWith('audio/')" class="att-audio-wrap">
        <div class="att-meta">
          <v-icon icon="mdi-music-note" size="small" />
          <span class="att-filename">{{ att.filename }}</span>
          <span class="att-size">{{ formatSize(att.size_bytes) }}</span>
        </div>
        <audio
          :src="attachmentUrl(att)"
          controls
          class="att-audio"
        />
      </div>

      <!-- Video -->
      <div v-else-if="att.mime_type.startsWith('video/')" class="att-video-wrap">
        <div class="att-meta">
          <v-icon icon="mdi-video" size="small" />
          <span class="att-filename">{{ att.filename }}</span>
          <span class="att-size">{{ formatSize(att.size_bytes) }}</span>
        </div>
        <video
          :src="attachmentUrl(att)"
          controls
          class="att-video"
        />
      </div>

      <!-- Text -->
      <div v-else-if="att.mime_type.startsWith('text/')" class="att-text-wrap">
        <div class="att-header" @click="toggleText(att.id)">
          <div class="att-meta">
            <v-icon :icon="textExpanded[att.id] ? 'mdi-chevron-down' : 'mdi-chevron-right'" size="small" />
            <v-icon icon="mdi-file-document-outline" size="small" />
            <span class="att-filename">{{ att.filename }}</span>
            <span class="att-size">{{ formatSize(att.size_bytes) }}</span>
          </div>
        </div>
        <pre v-if="textExpanded[att.id]" class="att-text">{{ textCache[att.id] || 'Loading…' }}</pre>
      </div>

      <!-- Download / fallback -->
      <div v-else class="att-download-wrap">
        <div class="att-meta">
          <v-icon icon="mdi-download" size="small" />
          <span class="att-filename">{{ att.filename }}</span>
          <span class="att-size">{{ formatSize(att.size_bytes) }}</span>
        </div>
        <a
          :href="attachmentUrl(att)"
          download
          class="att-download-link"
        >
          Download
        </a>
      </div>
    </div>
  </div>
</template>

<script setup lang="ts">
import { reactive } from 'vue';

interface Attachment {
  id: string;
  filename: string;
  mime_type: string;
  size_bytes: number;
  filepath: string;
  has_inline_data?: boolean;
  access_token?: string;
}

const props = defineProps<{
  attachments: Attachment[];
  sessionKey: string;
}>();

const textExpanded = reactive<Record<string, boolean>>({});
const textCache = reactive<Record<string, string>>({});

function attachmentUrl(att: Attachment): string {
  const base = `/api/v1/sessions/${encodeURIComponent(props.sessionKey)}/attachments/${encodeURIComponent(att.id)}`;
  return att.access_token ? `${base}?token=${att.access_token}` : base;
}

function formatSize(bytes: number): string {
  if (bytes >= 1_000_000) return (bytes / 1_000_000).toFixed(1) + ' MB';
  if (bytes >= 1_000) return Math.round(bytes / 1_000) + ' KB';
  return bytes + ' B';
}

function openLightbox(att: Attachment) {
  window.open(attachmentUrl(att), '_blank');
}

async function toggleText(attId: string) {
  if (textExpanded[attId]) {
    textExpanded[attId] = false;
    return;
  }
  // Fetch text content if not cached
  if (!textCache[attId]) {
    const att = props.attachments.find(a => a.id === attId);
    if (!att) return;
    try {
      const resp = await fetch(attachmentUrl(att));
      textCache[attId] = await resp.text();
    } catch {
      textCache[attId] = 'Failed to load text content.';
    }
  }
  textExpanded[attId] = true;
}
</script>

<style scoped>
.attachment-container {
  display: flex;
  flex-direction: column;
  gap: 8px;
  margin: 8px 0;
}

.attachment-item {
  border: 1px solid rgba(var(--v-theme-on-surface), 0.12);
  border-radius: 8px;
  overflow: hidden;
  max-width: 480px;
}

.att-image-wrap {
  cursor: pointer;
}

.att-image {
  max-width: 100%;
  max-height: 320px;
  object-fit: contain;
  display: block;
  border-radius: 4px;
}

.att-meta {
  display: flex;
  align-items: center;
  gap: 8px;
  padding: 6px 10px;
  font-size: 0.8rem;
  color: rgba(var(--v-theme-on-surface), 0.7);
}

.att-header {
  cursor: pointer;
  user-select: none;
}

.att-filename {
  font-weight: 500;
}

.att-size {
  opacity: 0.6;
}

.att-audio {
  width: 100%;
  max-width: 400px;
  margin: 4px 10px 8px;
}

.att-video {
  max-width: 100%;
  max-height: 320px;
  display: block;
}

.att-text-wrap {
  /* allows expanding beyond max-width if needed */
}

.att-text {
  max-height: 300px;
  overflow: auto;
  padding: 8px 10px;
  margin: 0;
  font-family: monospace;
  font-size: 0.8rem;
  white-space: pre-wrap;
  word-break: break-all;
  background: rgba(var(--v-theme-on-surface), 0.04);
  border-top: 1px solid rgba(var(--v-theme-on-surface), 0.08);
}

.att-download-wrap {
  padding: 10px;
}

.att-download-link {
  color: rgb(var(--v-theme-primary));
  text-decoration: none;
  font-size: 0.85rem;
}

.att-download-link:hover {
  text-decoration: underline;
}
</style>
// Reusable formatting helpers for the Animus admin UI

/**
 * Format seconds into a humanized uptime string.
 * 39292 → "10h 52m"
 * 86400 → "1d 0h"
 * 45 → "45s"
 */
export function formatUptime(seconds: number): string {
  if (seconds < 60) return `${Math.round(seconds)}s`;
  const days = Math.floor(seconds / 86400);
  const hours = Math.floor((seconds % 86400) / 3600);
  const mins = Math.floor((seconds % 3600) / 60);
  if (days > 0) return `${days}d ${hours}h`;
  if (hours > 0) return `${hours}h ${mins}m`;
  return `${mins}m`;
}

/**
 * Format an ISO timestamp or Unix ms epoch into a relative time string.
 * "2026-07-01T20:30:00Z" → "3m ago", "2h ago", "5d ago"
 * Returns "—" for null/undefined, "just now" for < 30s.
 */
export function relativeTime(timestamp: string | number | null | undefined): string {
  if (timestamp == null) return '—';
  const date = typeof timestamp === 'number'
    ? new Date(timestamp > 1e12 ? timestamp : timestamp * 1000)
    : new Date(timestamp);
  const now = Date.now();
  const diff = now - date.getTime();
  if (isNaN(diff)) return '—';
  if (diff < 0) return 'in the future';
  if (diff < 30_000) return 'just now';
  const secs = Math.floor(diff / 1000);
  const mins = Math.floor(secs / 60);
  const hours = Math.floor(mins / 60);
  const days = Math.floor(hours / 24);
  if (days > 30) return date.toLocaleDateString();
  if (days > 0) return `${days}d ago`;
  if (hours > 0) return `${hours}h ago`;
  if (mins > 0) return `${mins}m ago`;
  return `${secs}s ago`;
}

/**
 * Format a future timestamp as "in Xh", "in Xm", etc.
 */
export function relativeFuture(timestamp: string | number | null | undefined): string {
  if (timestamp == null) return '—';
  const date = typeof timestamp === 'number'
    ? new Date(timestamp > 1e12 ? timestamp : timestamp * 1000)
    : new Date(timestamp);
  const diff = date.getTime() - Date.now();
  if (isNaN(diff)) return '—';
  if (diff < 0) return 'overdue';
  if (diff < 30_000) return 'imminent';
  const secs = Math.floor(diff / 1000);
  const mins = Math.floor(secs / 60);
  const hours = Math.floor(mins / 60);
  const days = Math.floor(hours / 24);
  if (days > 0) return `in ${days}d`;
  if (hours > 0) return `in ${hours}h`;
  if (mins > 0) return `in ${mins}m`;
  return `in ${secs}s`;
}

/**
 * Truncate a string to maxLen, appending "…" if truncated.
 */
export function truncate(text: string, maxLen: number): string {
  if (text.length <= maxLen) return text;
  return text.slice(0, maxLen - 1).trimEnd() + '…';
}

/**
 * Format a number with thousands separators.
 * 39292 → "39,292"
 */
export function formatNumber(n: number): string {
  return n.toLocaleString();
}

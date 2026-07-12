# Ticket 065 — S3 Storage Tool

**Created:** 2026-06-05
**Status:** Draft
**Priority:** P2
**Depends on:** None (standalone tool)

## Summary

Add a built-in Animus tool that provides S3-compatible object storage as a mock filesystem. The agent can store, retrieve, list, move, and delete files on any S3-compatible server. This gives the agent a universal persistence layer that works across all deployment environments.

## Motivation

Agents need persistent file storage that survives container restarts and works across environments. Current options:

- **Local filesystem** — ephemeral, container-local, not portable
- **Agent-specific APIs** (AgentMail attachments, etc.) — narrow, not general-purpose
- **No solution** — agents simply can't persist arbitrary data reliably

S3 is the de facto standard for object storage. Every major cloud provider (AWS, Scaleway, Hetzner, Wasabi) and self-hosted option (MinIO, Garage, SeaweedFS) speaks S3. One protocol implementation covers the entire ecosystem.

The "mock filesystem" framing is deliberate: the tool should feel like a filesystem to the agent (ls, cat, cp, mv, rm) while mapping naturally to S3's object model (buckets, keys, prefixes, objects).

## Tool Design

### Actions

| Action | Description | S3 Operation |
|--------|-------------|-------------|
| `ls` | List objects under a prefix | `ListObjectsV2` |
| `cat` | Read object content as text | `GetObject` |
| `get` | Download object to local path | `GetObject` (stream to file) |
| `put` | Upload file or string content | `PutObject` |
| `cp` | Copy object (cross-bucket supported) | `CopyObject` |
| `mv` | Move object (copy + delete source) | `CopyObject` + `DeleteObject` |
| `rm` | Delete object(s) | `DeleteObject` / `DeleteObjects` |
| `stat` | Object metadata (size, content-type, etag, modified) | `HeadObject` |
| `mkdir` | Create bucket (if it doesn't exist) | `CreateBucket` / `HeadBucket` |
| `url` | Generate presigned URL for temporary access | `GetPresignedUrl` |

### Configuration

Admin configures one or more S3 endpoints in the agent's channel config:

```json
{
  "tools": {
    "s3": {
      "endpoints": {
        "default": {
          "endpoint": "https://s3.scaleway.com",
          "region": "fr-par",
          "access_key": "${S3_ACCESS_KEY}",
          "secret_key": "${S3_SECRET_KEY}",
          "default_bucket": "animus-agent-data"
        },
        "backup": {
          "endpoint": "https://your-minio.example.com",
          "region": "us-east-1",
          "access_key": "${BACKUP_S3_ACCESS_KEY}",
          "secret_key": "${BACKUP_S3_SECRET_KEY}"
        }
      }
    }
  }
}
```

- Secrets resolved from environment variables (never stored in config)
- `default_bucket` is optional; if omitted, agent must specify bucket in actions
- Multiple endpoints supported for redundancy or workload separation

### Key Design Decisions

1. **Path-style keys, not flat naming.** The tool presents objects as paths (`documents/reports/2026-Q2.pdf`), not opaque keys. Prefix delimiters (`/`) are handled automatically in `ls` operations (using `delimiter` and `prefix` parameters for efficient directory-like listing).

2. **Text vs binary awareness.** `cat` returns content as text (with content-type detection). `get`/`put` handle binary files. The agent doesn't need to know the difference — the tool handles it based on content-type or extension.

3. **Presigned URLs for sharing.** The `url` action generates time-limited presigned URLs. Useful for sharing files with humans or external services without granting permanent access.

4. **No bucket management beyond creation.** The agent can create buckets but not delete them or modify policies. This is a safety boundary — the agent can store data but not destroy storage infrastructure.

5. **Error mapping to plain language.** S3 error codes (NoSuchKey, AccessDenied, etc.) are mapped to human-readable messages. The agent sees "File not found: reports/2026-Q2.pdf" not "404 NoSuchKey".

### Path Syntax

Actions use a path syntax that maps to S3 keys:

```
s3:ls /path/to/prefix          → ListObjectsV2(prefix="path/to/prefix/")
s3:cat /path/to/file.txt       → GetObject(key="path/to/file.txt")
s3:put /path/to/file.txt       → PutObject(key="path/to/file.txt")
s3:rm /path/to/file.txt        → DeleteObject(key="path/to/file.txt")
s3:stat /path/to/file.txt      → HeadObject(key="path/to/file.txt")
s3:url /path/to/file.txt 60    → Presigned GET URL, expires in 60 minutes
s3:mkdir bucket-name           → CreateBucket("bucket-name")
```

Multi-endpoint: prefix with endpoint name — `s3:ls backup:/archives/` uses the "backup" endpoint config.

## Implementation Notes

- **HTTP client**: Use the existing `httplib::Client` or `Cpr` already in the Animus dependency tree (both are available). No new HTTP library needed.
- **AWS Signature V4**: Required for most S3-compatible servers. Implement the signing algorithm in `animus::s3` namespace. This is ~200 lines of code (canonical request, string to sign, signing key derivation). Well-documented spec.
- **Presigned URLs**: Same V4 signing, but with a `X-Amz-Expires` parameter instead of the `x-amz-date` header.
- **Multipart upload**: Optional for v1. Required for files >5MB. Can be added later.
- **Server-side encryption**: Not in v1. The agent shouldn't manage encryption keys — that's admin infrastructure.
- **Testing**: Use MinIO in a Docker container for integration tests. MinIO is fully S3-compatible and easy to spin up in CI.

## Scope

### v1 (This ticket)
- [ ] S3 tool implementation (ls, cat, get, put, cp, mv, rm, stat, url, mkdir)
- [ ] AWS Signature V4 signing
- [ ] Multi-endpoint configuration
- [ ] Admin UI section for S3 config
- [ ] Integration tests with MinIO

### Future
- [ ] Multipart upload for large files
- [ ] Bucket policy management (admin-only)
- [ ] Versioning support
- [ ] Server-side encryption headers
- [ ] Notification webhooks (S3 events → Animus notices)

## Provider Compatibility

The S3 protocol is universal. Any S3-compatible server works:

| Provider | Endpoint | Notes |
|----------|----------|-------|
| AWS S3 | `s3.amazonaws.com` | Original, most expensive |
| Scaleway | `s3.%region%.scw.cloud` | EU, good pricing |
| Hetzner | `fsn1.your-objectstorage.com` | EU, very cheap |
| Wasabi | `s3.wasabisys.com` | No egress fees |
| MinIO | Self-hosted | Development, testing |
| Garage | Self-hosted | Lightweight, Rust |
| Clever Cloud Cellar | `cellar-cdn.services.clever-cloud.com` | PaaS add-on |
| Any S3-compatible | Varies | If it speaks S3, it works |

The tool doesn't need to know about providers — it speaks S3 protocol. Provider choice is configuration.
# Ticket 025 — Channel Architecture (DEPRECATED)

> **Deprecated 2026-05-09.** This ticket was drafted May 3 before the codebase stabilized on the InterfaceManager pattern.

## Deprecation Notes (2026-05-09)

**Why deprecated:**
- **Codebase chose a different path.** `InterfaceManager` + per-type dispatch already handles interface lifecycle. `IrcInterfaceRuntime` is concrete, not behind a polymorphic `IChannel`. Each interface type (IRC raw socket, Slack WebSocket, Telegram HTTP polling) shares almost nothing at the runtime level — a shared abstract interface adds indirection without reuse.
- **Nomenclature settled.** The codebase uses "interface" consistently (`InterfaceManager`, `InterfaceState`, `IrcInterface`, `interface_name` in session keys).
- **`reply_channel` tool rejected.** Direct messaging belongs on the interface implementations themselves (`IrcInterfaceRuntime::SendPrivmsg()` already exists). A separate tool wrapping interface send methods adds an unnecessary abstraction layer.
- **Interface→Agent routing already settled.** `agent_id` on the interface config is the working routing direction (interface picks its agent). `bound_channels` (the reverse direction) was removed as dead code (commit `df67577`).

**What survived:**
- The platform table (Slack/Telegram/Discord/WhatsApp/Signal) remains useful as a reference for prioritizing future work.
- Interface implementations will be ticketed individually (e.g., Slack interface ticket, Telegram interface ticket) with their own SDK-specific architectures.

---

## Original Content (historical)

**Priority:** P2 (after web tools, enables agent-to-user communication)
**Status:** Deprecated
**Dependencies:** Ticket 021, Ticket 007
**Drafted:** 2026-05-03

## Summary

Design and implement the channel abstraction layer that lets the agent communicate with users across multiple platforms. Each channel (Slack, WhatsApp, Telegram, Discord, Signal, WebChat, IRC) is a concrete implementation of a unified interface, enabling the agent to send messages, receive events, and interact with users regardless of platform.

## Motivation

The agent needs to reach users where they are. Different users prefer different platforms. Rather than hardcoding each platform's API into the agent's tool calls, we abstract channels behind a common interface. The agent says "send a DM to user X" — the channel layer figures out how.

This also provides the foundation for the `reply_channel` tool, which lets the agent send messages to specific channels during chain execution.

## Channel Table (target platforms)

| Channel    | Stability | Setup Difficulty | Best For                     | DM  | Group |
|------------|-----------|-----------------|------------------------------|-----|-------|
| Slack      | High      | Easy             | Work, teams, logging         | Yes | Yes   |
| WhatsApp   | Medium    | Hard (Baileys)   | Global messaging             | Yes | Yes   |
| Telegram   | High      | Easy             | Tech users, bots              | Yes | Yes   |
| Discord    | Medium    | Medium           | Communities                  | Yes | Yes   |
| Signal     | High      | Hard             | Privacy-focused              | Yes | Yes   |
| WebChat    | High      | None (built-in)  | Testing, quick access        | Yes | No    |
| IRC        | High      | Easy             | Accessible, immediately testable | Yes | Yes |

WebChat is the admin UI chat interface (already implemented). IRC is included as an immediately testable, low-dependency channel.

## Architecture: Abstract Channel Interface

```cpp
class IChannel {
public:
    virtual ~IChannel() = default;

    // Identity
    virtual std::string channelId() const = 0;     // e.g. "slack:C01234567"
    virtual std::string channelType() const = 0;   // e.g. "slack", "telegram"
    virtual std::string displayName() const = 0;   // e.g. "Team Slack"

    // Lifecycle
    virtual bool connect(const ChannelConfig& config) = 0;
    virtual void disconnect() = 0;
    virtual bool isConnected() const = 0;

    // Messaging
    virtual bool sendDm(const std::string& userId, const std::string& message) = 0;
    virtual bool sendToChannel(const std::string& channelId, const std::string& message) = 0;

    // Event hooks — called by the channel when events occur
    // The kernel registers callbacks for these
    virtual void onMessage(MessageCallback cb) = 0;
    virtual void onReaction(ReactionCallback cb) = 0;
    virtual void onPresence(PresenceCallback cb) = 0;

    // Capabilities — what this channel supports
    virtual bool supportsReactions() const = 0;
    virtual bool supportsPresence() const = 0;
    virtual bool supportsFormatting(FormatType type) const = 0;
};
```

### Common event types:

```cpp
struct ChannelEvent {
    std::string event_type;    // "message", "reaction", "presence"
    std::string channel_id;    // originating channel
    std::string user_id;       // originating user
    std::string content;       // message content or reaction emoji
    std::string thread_id;     // thread/conversation ID (if applicable)
    std::string reply_to;      // message being replied to (if applicable)
    std::chrono::system_clock::time_point timestamp;
};
```

### Channel Registry:

```cpp
class ChannelRegistry {
public:
    void registerChannel(std::unique_ptr<IChannel> channel);
    IChannel* getChannel(const std::string& channelId) const;
    std::vector<IChannel*> getChannelsByType(const std::string& type) const;
    std::vector<IChannel*> getConnectedChannels() const;
};
```

## Tool: `reply_channel`

The `reply_channel` tool is the agent's interface to the channel layer:

```json
{
    "name": "reply_channel",
    "description": "Send a message to a specific channel or interface. Use this to communicate on a different channel than the current conversation — for example, posting to a Slack channel, sending a WhatsApp message, or writing to a different session.",
    "parameters": {
        "channel": {
            "type": "string",
            "description": "Target channel identifier (e.g. 'slack:C01234567', 'whatsapp:+31612345678', 'webchat:session-42')"
        },
        "message": {
            "type": "string",
            "description": "Message content to send"
        }
    }
}
```

**Changes from original 023 spec:**
- Removed `node` parameter — `reply_channel` is node-agnostic; it speaks over a configured channel. Cross-node routing is the kernel's job, not the tool's.
- Channel format stays `{type}:{ref}` — resolved by the channel registry

**Implementation notes:**
- Channel format: `{interface_type}:{channel_id}` — the kernel's channel registry resolves this
- Interface must be connected and authenticated; return error if not
- Format conversion is the kernel's job (Slack mrkdwn, WhatsApp plain text, etc.), not the agent's
- The agent doesn't need to know which node a channel lives on

## Channel Implementation Tickets

Each channel warrants its own ticket due to SDK integration, auth flows, and testing. Suggested ordering:

1. **WebChat** — already partially built (admin UI). Extend to implement IChannel interface.
2. **IRC** — low barrier, immediately testable. Good validation of the abstraction.
3. **Slack** — WebSocket-based, well-documented API, high value.
4. **Telegram** — Bot API is straightforward, good next step.
5. **Discord** — Similar to Slack but different event model.
6. **WhatsApp** — Baileys library, harder setup, highest value for personal use.
7. **Signal** — Most difficult, privacy-focused, lower priority.

## Acceptance Criteria

- [ ] `IChannel` abstract interface defined with lifecycle, messaging, and event hook methods
- [ ] `ChannelRegistry` manages channel instances and lookup
- [ ] `ChannelEvent` struct unifies inbound events across platforms
- [ ] `reply_channel` tool sends messages via channel registry
- [ ] `reply_channel` returns error for disconnected/unauthenticated channels
- [ ] Format conversion handled by channel implementations (not the tool)
- [ ] At least one concrete channel implemented as proof (WebChat or IRC)
- [ ] Channel config persists in kernel config (SQLite)

## Notes

- `notify` was considered and rejected (May 3). It created a false expectation that the agent can push notifications to users. `reply_channel` covers the real use case.
- Channel implementations should handle their own reconnection logic — the kernel shouldn't need to babysit.
- Auth credentials are channel-specific and stored encrypted in config, not hardcoded.
- The abstraction needs to accommodate both WebSocket-based (Slack, Discord) and polling-based (IRC, some Telegram) connection models.
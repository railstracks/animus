export const channels = {
    title: 'Channels',
    empty: 'No channels configured. Add one to get started.',
    createSuccess: 'Channel "{name}" created successfully.',
    updateSuccess: 'Channel "{name}" updated successfully.',
    deleteSuccess: 'Channel "{name}" deleted.',
    columns: {
      name: 'Name',
      type: 'Type',
      identity: 'Identity',
      endpoint: 'Endpoint',
      enabled: 'Enabled',
      connected: 'Connected',
      lastEvent: 'Last Event',
      actions: 'Actions'
    },
    state: {
      connected: 'Connected',
      disconnected: 'Disconnected'
    },
    actions: {
      refresh: 'Refresh',
      add: 'Add Channel',
      cancel: 'Cancel',
      create: 'Create',
      save: 'Save',
      confirmDelete: 'Delete channel "{name}"? This cannot be undone.'
    },
    form: {
      createTitle: 'Add Channel',
      editTitle: 'Edit "{name}"',
      name: 'Channel Name',
      type: 'Channel Type',
      agent: 'Agent',
      minResponseInterval: 'Minimum Response Interval (seconds)',
      allowInterjection: 'Allow Interjection',
      irc: {
        host: 'IRC Server',
        port: 'Port',
        serverPassword: 'Server Password',
        nick: 'Nickname',
        username: 'Username',
        realname: 'Real Name',
        channels: 'Channels',
        channelsHint: 'One per line. Format: #channel [key]',
        agent: 'Agent',
        respondToChannel: 'Respond to channel messages',
        respondToDm: 'Respond to direct messages',
        respondToNotices: 'Respond to notices',
        allowedDmUsers: 'Allowed DM users',
        reconnect: 'Auto-reconnect'
      },
      telegram: {
        botToken: 'Bot Token',
        tokenHint: 'Leave blank to keep existing token'
      },
      vk: {
        accessToken: 'Community Access Token',
        groupId: 'Group ID'
      },
      bluesky: {
        handle: 'Handle',
        appPassword: 'App Password',
        pds: 'PDS URL'
      },
      mastodon: {
        handle: 'Handle',
        instance: 'Instance URL'
      },
      email: {
        apiKey: 'AgentMail API Key',
        apiKeyHint: 'Leave empty to keep current key',
        inboxId: 'Inbox ID',
        pollingWait: 'Poll Interval (seconds)'
      },
      twitter: {
        tier: 'API Tier',
        clientId: 'OAuth Client ID',
        clientSecret: 'OAuth Client Secret',
        accessToken: 'Access Token',
        tokenHint: 'Leave blank to keep existing token',
        refreshToken: 'Refresh Token',
        authorize: 'Authorize with Twitter',
        oauthStarted: 'Authorization window opened. Complete the flow in your browser.'
      },
      discord: {
        botToken: 'Bot Token',
        tokenHint: 'Leave blank to keep existing token',
        applicationId: 'Application ID (for slash commands)',
        monitoredChannels: 'Monitored Channels',
        monitoredChannelsHint: 'One channel ID per line. Bot must be in the server.',
        respondToDm: 'Respond to DMs',
        respondToMentions: 'Respond to mentions',
        dmWhitelistEnabled: 'Restrict DMs to allowed users',
        allowedDmUsers: 'Allowed DM users',
        allowedDmUsersHint: 'Discord usernames (one per line). Only these users can DM the bot.'
      },
      slack: {
        botToken: 'Bot Token (xoxb-)',
        tokenHint: 'Leave blank to keep existing token',
        appToken: 'App Token (xapp-)',
        appTokenHint: 'For Socket Mode (Phase 2). Optional for Phase 1.',
        monitoredChannels: 'Monitored Channels (override)',
        monitoredChannelsHint: 'Bot auto-monitors all channels it\'s a member of. Only add IDs here to restrict scope.',
        respondToMentions: "Respond to {'@'}mentions",
        respondToAll: 'Respond to all messages (ignores mention filter)',
        threadedReplies: 'Thread replies in channels (each message starts a thread)'
      },
      nextcloud: {
        serverUrl: 'Server URL',
        username: 'Username',
        appPassword: 'App Password',
        watchTokens: 'Watch Tokens (optional)',
        watchTokensHint: 'Conversation tokens to monitor. Leave empty for all.',
        respondInDm: 'Respond to direct messages',
        respondInGroupOnMention: 'Respond to mentions in group chats',
        mentionTrigger: 'Mention trigger string'
      }
    }
  } as const;

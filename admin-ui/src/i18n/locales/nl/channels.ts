export const channels = {
  title: 'Kanalen',
  empty: 'Geen kanalen geconfigureerd. Voeg er een toe om te beginnen.',
  createSuccess: 'Kanaal "{name}" succesvol aangemaakt.',
  updateSuccess: 'Kanaal "{name}" bijgewerkt.',
  deleteSuccess: 'Kanaal "{name}" verwijderd.',
  columns: {
    name: 'Naam',
    type: 'Type',
    identity: 'Identiteit',
    endpoint: 'Endpoint',
    enabled: 'Ingeschakeld',
    connected: 'Verbonden',
    lastEvent: 'Laatste gebeurtenis',
    actions: 'Acties'
  },
  state: {
    connected: 'Verbonden',
    disconnected: 'Verbroken'
  },
  actions: {
    refresh: 'Vernieuwen',
    add: 'Kanaal toevoegen',
    cancel: 'Annuleren',
    create: 'Aanmaken',
    save: 'Opslaan',
    confirmDelete: 'Kanaal "{name}" verwijderen? Dit kan niet ongedaan worden gemaakt.'
  },
  form: {
    createTitle: 'Kanaal toevoegen',
    editTitle: 'Bewerken "{name}"',
    name: 'Kanaalnaam',
    type: 'Kanaaltype',
    agent: 'Agent',
    minResponseInterval: 'Minimale antwoordinterval (seconden)',
    allowInterjection: 'Onderbrekingen toestaan',
    irc: {
      host: 'IRC-server',
      port: 'Poort',
      serverPassword: 'Serverwachtwoord',
      nick: 'Bijnaam',
      username: 'Gebruikersnaam',
      realname: 'Weergavenaam',
      channels: 'Kanalen',
      channelsHint: 'Eén per regel. Formaat: #kanaal [sleutel]',
      agent: 'Agent',
      respondToChannel: 'Reageren op kanaalberichten',
      respondToDm: 'Reageren op directe berichten',
      respondToNotices: 'Reageren op meldingen',
      allowedDmUsers: 'Toegestane DM-gebruikers',
      reconnect: 'Automatisch opnieuw verbinden'
    },
    telegram: {
      botToken: 'Bot-token',
      tokenHint: 'Leeg laten om bestaande token te behouden'
    },
    vk: {
      accessToken: 'Community-toegangstoken',
      groupId: 'Groeps-ID'
    },
    bluesky: {
      handle: 'Handle',
      appPassword: 'App-wachtwoord',
      pds: 'PDS-URL'
    },
    mastodon: {
      handle: 'Handle',
      instance: 'Instantie-URL'
    },
    email: {
      apiKey: 'AgentMail API-sleutel',
      apiKeyHint: 'Leeg laten om huidige sleutel te behouden',
      inboxId: 'Inbox-ID',
      pollingWait: 'Polling-interval (seconden)'
    },
    twitter: {
      tier: 'API-tier',
      clientId: 'OAuth client-ID',
      clientSecret: 'OAuth clientgeheim',
      accessToken: 'Toegangstoken',
      tokenHint: 'Leeg laten om bestaande token te behouden',
      refreshToken: 'Refresh-token',
      authorize: 'Autoriseren met Twitter',
      oauthStarted: 'Autorisatievenster geopend. Voltooi het proces in je browser.'
    },
    discord: {
      botToken: 'Bot-token',
      tokenHint: 'Leeg laten om bestaande token te behouden',
      applicationId: 'Applicatie-ID (voor slash-commando’s)',
      monitoredChannels: 'Gevolgde kanalen',
      monitoredChannelsHint: 'Kanaal-ID\'s om te volgen voor context (één per regel). Berichten worden gelogd zonder reacties te triggeren.',
      respondToDm: 'Reageren op DM’s',
      respondToMentions: 'Reageren op vermeldingen',
      monitorAllChannels: 'Alle kanalen volgen',
      respondToChannels: 'Reageren op elk bericht in gevolgde kanalen',
        dmWhitelistEnabled: 'DM\'s beperken tot toegestane gebruikers',
        allowedDmUsers: 'Toegestane DM-gebruikers',
        allowedDmUsersHint: 'Discord-gebruikersnamen (één per regel). Alleen deze gebruikers kunnen de bot DM-en.'
    },
    slack: {
      botToken: 'Bot-token (xoxb-)',
      tokenHint: 'Leeg laten om bestaande token te behouden',
      appToken: 'App-token (xapp-)',
      appTokenHint: 'Voor Socket Mode (fase 2). Optioneel voor fase 1.',
      monitoredChannels: 'Gemonitorde kanalen (override)',
      monitoredChannelsHint: 'Bot monitort standaard alle kanalen waarvan hij lid is. Alleen toevoegen om het bereik te beperken.',
      respondToMentions: 'Reageren op @vermeldingen',
      respondToAll: 'Reageren op alle berichten (negeert mention-filter)',
      threadedReplies: 'Antwoorden in threads (elk bericht start een thread)'
    }
  }
} as const;

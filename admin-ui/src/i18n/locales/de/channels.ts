export const channels = {
    title: 'Kanäle',
    empty: 'Keine Kanäle konfiguriert. Fügen Sie eines hinzu, um loszulegen.',
    createSuccess: 'Kanal „{name}“ erfolgreich erstellt.',
    updateSuccess: 'Kanal „{name}“ wurde erfolgreich aktualisiert.',
    deleteSuccess: 'Kanal „{name}“ gelöscht.',
    columns: {
      name: 'Name',
      type: 'Typ',
      identity: 'Identität',
      endpoint: 'Endpunkt',
      enabled: 'Aktiviert',
      connected: 'Verbunden',
      lastEvent: 'Letzte Veranstaltung',
      actions: 'Aktionen'
    },
    state: {
      connected: 'Verbunden',
      disconnected: 'Nicht verbunden'
    },
    actions: {
      refresh: 'Aktualisieren',
      add: 'Kanal hinzufügen',
      cancel: 'Abbrechen',
      create: 'Erstellen',
      save: 'Speichern',
      confirmDelete: 'Kanal „{name}“ löschen? Dies kann nicht rückgängig gemacht werden.'
    },
    form: {
      createTitle: 'Kanal hinzufügen',
      editTitle: 'Bearbeiten Sie „{name}“',
      name: 'Kanalname',
      type: 'Kanaltyp',
      agent: 'Agent',
      minResponseInterval: 'Mindestantwortintervall (Sekunden)',
      allowInterjection: 'Einwürfe zulassen',
      irc: {
        host: 'IRC-Server',
        port: 'Hafen',
        serverPassword: 'Serverpasswort',
        nick: 'Spitzname',
        username: 'Benutzername',
        realname: 'Echter Name',
        channels: 'Kanäle',
        channelsHint: 'Eine pro Zeile. Format: #channel [Schlüssel]',
        agent: 'Agent',
        respondToChannel: 'Auf Kanalnachrichten antworten',
        respondToDm: 'Reagieren Sie auf Direktnachrichten',
        respondToNotices: 'Reagieren Sie auf Hinweise',
        allowedDmUsers: 'Zugelassene DM-Benutzer',
        reconnect: 'Automatische Wiederverbindung'
      },
      telegram: {
        botToken: 'Bot-Token',
        tokenHint: 'Lassen Sie das Feld leer, um das vorhandene Token beizubehalten'
      },
      vk: {
        accessToken: 'Community-Zugriffstoken',
        groupId: 'Gruppen-ID'
      },
      bluesky: {
        handle: 'Griff',
        appPassword: 'App-Passwort',
        pds: 'PDS-URL'
      },
      mastodon: {
        handle: 'Griff',
        instance: 'Instanz-URL'
      },
      email: {
        apiKey: 'AgentMail-API-Schlüssel',
        apiKeyHint: 'Lassen Sie das Feld leer, um den aktuellen Schlüssel beizubehalten',
        inboxId: 'Posteingangs-ID',
        pollingWait: 'Abfrageintervall (Sekunden)'
      },
      twitter: {
        tier: 'API-Stufe',
        clientId: 'OAuth-Client-ID',
        clientSecret: 'OAuth-Client-Geheimnis',
        accessToken: 'Zugriffstoken',
        tokenHint: 'Lassen Sie das Feld leer, um das vorhandene Token beizubehalten',
        refreshToken: 'Aktualisierungstoken',
        authorize: 'Autorisieren Sie mit Twitter',
        oauthStarted: 'Autorisierungsfenster geöffnet. Schließen Sie den Ablauf in Ihrem Browser ab.'
      },
      discord: {
        botToken: 'Bot-Token',
        tokenHint: 'Lassen Sie das Feld leer, um das vorhandene Token beizubehalten',
        applicationId: 'Anwendungs-ID (für Slash-Befehle)',
        monitoredChannels: 'Überwachte Kanäle',
        monitoredChannelsHint: 'Eine Kanal-ID pro Zeile. Bot muss auf dem Server sein.',
        respondToDm: 'Auf Direktnachrichten antworten',
        respondToMentions: 'Reagieren Sie auf Erwähnungen'
      },
      slack: {
        botToken: 'Bot-Token (xoxb-)',
        tokenHint: 'Lassen Sie das Feld leer, um das vorhandene Token beizubehalten',
        appToken: 'App-Token (xapp-)',
        appTokenHint: 'Für den Socket-Modus (Phase 2). Optional für Phase 1.',
        monitoredChannels: 'Überwachte Kanäle (Überschreibung)',
        monitoredChannelsHint: 'Der Bot überwacht automatisch alle Kanäle, in denen er Mitglied ist. Fügen Sie hier nur IDs hinzu, um den Bereich einzuschränken.',
        respondToMentions: "Auf {'@'}Erwähnungen antworten",
        respondToAll: 'Auf alle Nachrichten antworten (Erwähnungsfilter ignorieren)',
        threadedReplies: 'Thread-Antworten in Kanälen (jede Nachricht startet einen Thread)'
      }
    }
  } as const;

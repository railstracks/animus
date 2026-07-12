export const interfaces = {
    title: 'Schnittstellenverwaltung',
    actions: {
      refresh: 'Aktualisieren',
      add: 'Schnittstelle hinzufügen',
      edit: 'Bearbeiten',
      enable: 'Aktivieren',
      disable: 'Deaktivieren',
      delete: 'Löschen',
      confirmDelete: 'Schnittstelle "{name}" löschen?'
    },
    columns: {
      name: 'Name',
      type: 'Typ',
      module: 'Modul-ID',
      enabled: 'Aktiviert',
      connected: 'Verbunden',
      lastEvent: 'Letztes Ereignis',
      actions: 'Aktionen'
    },
    state: {
      enabled: 'ja',
      disabled: 'nein',
      connected: 'verbunden',
      disconnected: 'getrennt'
    },
    empty: 'Noch keine Schnittstellen konfiguriert.',
    form: {
      createTitle: 'Schnittstelle erstellen',
      editTitle: 'Schnittstelle bearbeiten: {name}',
      name: 'Instanzname',
      type: 'Schnittstellentyp',
      moduleId: 'Modul-ID',
      enabled: 'Aktiviert',
      configJson: 'Konfigurations-JSON',
      create: 'Erstellen',
      save: 'Speichern',
      cancel: 'Abbrechen',
      irc: {
        host: 'Host',
        port: 'Port',
        nick: 'Nick',
        serverPassword: 'Serverpasswort',
        username: 'Benutzername',
        realname: 'Echter Name',
        dmOnly: 'Nur-DM-Modus',
        respondToChannel: 'Auf Kanalaktivität antworten',
        respondToDm: 'Auf Direktnachrichten antworten',
        respondToNotices: 'Auf Notices antworten',
        allowedDmUsers: 'Erlaubte DM-Benutzer',
        allowedDmUsersHint: 'Optionale Positivliste; durch Kommas oder Zeilenumbrüche getrennt.',
        agentId: 'Agenten-ID',
        reconnectEnabled: 'Wiederverbindung aktiviert',
        reconnectInitialDelay: 'Anfängliche Wiederverbindungsverzögerung (ms)',
        reconnectMaxDelay: 'Max. Wiederverbindungsverzögerung (ms)'
      }
    },
    createSuccess: 'Schnittstelle "{name}" erstellt.',
    updateSuccess: 'Schnittstelle "{name}" aktualisiert.',
    deleteSuccess: 'Schnittstelle "{name}" gelöscht.'
  } as const;

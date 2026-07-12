export const interfaces = {
    title: 'Interfacebeheer',
    actions: {
      refresh: 'Vernieuwen',
      add: 'Interface toevoegen',
      edit: 'Bewerken',
      enable: 'Inschakelen',
      disable: 'Uitschakelen',
      delete: 'Verwijderen',
      confirmDelete: 'Interface "{name}" verwijderen?'
    },
    columns: {
      name: 'Naam',
      type: 'Type',
      module: 'Module-ID',
      enabled: 'Ingeschakeld',
      connected: 'Verbonden',
      lastEvent: 'Laatste gebeurtenis',
      actions: 'Acties'
    },
    state: {
      enabled: 'ja',
      disabled: 'nee',
      connected: 'verbonden',
      disconnected: 'niet verbonden'
    },
    empty: 'Nog geen interfaces geconfigureerd.',
    form: {
      createTitle: 'Interface aanmaken',
      editTitle: 'Interface bewerken: {name}',
      name: 'Instantienaam',
      type: 'Interfacetype',
      moduleId: 'Module-ID',
      enabled: 'Ingeschakeld',
      configJson: 'Configuratie-JSON',
      create: 'Aanmaken',
      save: 'Opslaan',
      cancel: 'Annuleren',
      irc: {
        host: 'Host',
        port: 'Poort',
        nick: 'Nick',
        serverPassword: 'Serverwachtwoord',
        username: 'Gebruikersnaam',
        realname: 'Echte naam',
        channels: 'Kanalen',
        channelsHint: 'Eén per regel: "#kanaal" of "#kanaal <sleutel>"',
        dmOnly: 'Alleen-DM-modus',
        respondToChannel: 'Reageren op kanaalactiviteit',
        respondToDm: 'Reageren op privéberichten',
        respondToNotices: 'Reageren op notices',
        allowedDmUsers: 'Toegestane DM-gebruikers',
        allowedDmUsersHint: 'Optionele allowlist; gescheiden door komma’s of nieuwe regels.',
        agentId: 'Agent-ID',
        reconnectEnabled: 'Opnieuw verbinden ingeschakeld',
        reconnectInitialDelay: 'Initiële herverbindingsvertraging (ms)',
        reconnectMaxDelay: 'Max. herverbindingsvertraging (ms)'
      }
    },
    createSuccess: 'Interface "{name}" aangemaakt.',
    updateSuccess: 'Interface "{name}" bijgewerkt.',
    deleteSuccess: 'Interface "{name}" verwijderd.'
  } as const;

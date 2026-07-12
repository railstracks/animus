export const interfaces = {
    title: 'Zarządzanie interfejsami',
    actions: {
      refresh: 'Odśwież',
      add: 'Dodaj interfejs',
      edit: 'Edytuj',
      enable: 'Włącz',
      disable: 'Wyłącz',
      delete: 'Usuń',
      confirmDelete: 'Usunąć interfejs "{name}"?'
    },
    columns: {
      name: 'Nazwa',
      type: 'Typ',
      module: 'ID modułu',
      enabled: 'Włączony',
      connected: 'Połączony',
      lastEvent: 'Ostatnie zdarzenie',
      actions: 'Działania'
    },
    state: {
      enabled: 'tak',
      disabled: 'nie',
      connected: 'połączony',
      disconnected: 'rozłączony'
    },
    empty: 'Nie skonfigurowano jeszcze żadnych interfejsów.',
    form: {
      createTitle: 'Utwórz interfejs',
      editTitle: 'Edytuj interfejs: {name}',
      name: 'Nazwa instancji',
      type: 'Typ interfejsu',
      moduleId: 'ID modułu',
      enabled: 'Włączony',
      configJson: 'JSON konfiguracji',
      create: 'Utwórz',
      save: 'Zapisz',
      cancel: 'Anuluj',
      irc: {
        host: 'Host',
        port: 'Port',
        nick: 'Nick',
        serverPassword: 'Hasło serwera',
        username: 'Nazwa użytkownika',
        realname: 'Prawdziwe imię',
        dmOnly: 'Tryb tylko PW',
        respondToChannel: 'Odpowiadaj na aktywność kanału',
        respondToDm: 'Odpowiadaj na wiadomości bezpośrednie',
        respondToNotices: 'Odpowiadaj na powiadomienia',
        allowedDmUsers: 'Dozwoleni użytkownicy PW',
        allowedDmUsersHint: 'Opcjonalna lista dozwolonych; rozdzielana przecinkami lub nowymi wierszami.',
        agentId: 'ID agenta',
        reconnectEnabled: 'Ponowne łączenie włączone',
        reconnectInitialDelay: 'Początkowe opóźnienie ponownego łączenia (ms)',
        reconnectMaxDelay: 'Maks. opóźnienie ponownego łączenia (ms)'
      }
    },
    createSuccess: 'Interfejs "{name}" utworzony.',
    updateSuccess: 'Interfejs "{name}" zaktualizowany.',
    deleteSuccess: 'Interfejs "{name}" usunięty.'
  } as const;

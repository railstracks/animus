export const channels = {
    title: 'Kanały',
    empty: 'Nie skonfigurowano żadnych kanałów. Dodaj jeden, aby rozpocząć.',
    createSuccess: 'Kanał „{name}” został pomyślnie utworzony.',
    updateSuccess: 'Kanał „{name}” został pomyślnie zaktualizowany.',
    deleteSuccess: 'Kanał „{name}” został usunięty.',
    columns: {
      name: 'Imię',
      type: 'Wpisz',
      identity: 'Tożsamość',
      endpoint: 'Punkt końcowy',
      enabled: 'Włączone',
      connected: 'Połączono',
      lastEvent: 'Ostatnie wydarzenie',
      actions: 'Działania'
    },
    state: {
      connected: 'Połączono',
      disconnected: 'Rozłączony'
    },
    actions: {
      refresh: 'Odśwież',
      add: 'Dodaj kanał',
      cancel: 'Anuluj',
      create: 'Utwórz',
      save: 'Zapisz',
      confirmDelete: 'Usunąć kanał „{name}”? Tego nie można cofnąć.'
    },
    form: {
      createTitle: 'Dodaj kanał',
      editTitle: 'Edytuj „{name}”',
      name: 'Nazwa kanału',
      type: 'Typ kanału',
      agent: 'Agent',
      minResponseInterval: 'Minimalny odstęp odpowiedzi (sekundy)',
      allowInterjection: 'Zezwalaj na wtrącenia',
      irc: {
        host: 'Serwer IRC',
        port: 'Portu',
        serverPassword: 'Hasło serwera',
        nick: 'Pseudonim',
        username: 'Nazwa użytkownika',
        realname: 'Prawdziwe imię',
        channels: 'Kanały',
        channelsHint: 'Jeden na linię. Format: #kanał [klucz]',
        agent: 'Agent',
        respondToChannel: 'Odpowiadaj na wiadomości na kanale',
        respondToDm: 'Odpowiadaj na wiadomości bezpośrednie',
        respondToNotices: 'Odpowiadaj na powiadomienia',
        allowedDmUsers: 'Dozwoleni użytkownicy DM',
        reconnect: 'Automatyczne ponowne połączenie'
      },
      telegram: {
        botToken: 'Token bota',
        tokenHint: 'Pozostaw puste, aby zachować istniejący token'
      },
      vk: {
        accessToken: 'Token dostępu do społeczności',
        groupId: 'Identyfikator grupy'
      },
      bluesky: {
        handle: 'Uchwyt',
        appPassword: 'Hasło aplikacji',
        pds: 'Adres URL PSS'
      },
      mastodon: {
        handle: 'Uchwyt',
        instance: 'Adres URL instancji'
      },
      email: {
        apiKey: 'Klucz API AgentMail',
        apiKeyHint: 'Pozostaw puste, aby zachować bieżący klucz',
        inboxId: 'Identyfikator skrzynki odbiorczej',
        pollingWait: 'Interwał sondowania (sekundy)'
      },
      twitter: {
        tier: 'Poziom interfejsu API',
        clientId: 'Identyfikator klienta OAuth',
        clientSecret: 'Sekret klienta OAuth',
        accessToken: 'Token dostępu',
        tokenHint: 'Pozostaw puste, aby zachować istniejący token',
        refreshToken: 'Odśwież token',
        authorize: 'Autoryzuj za pomocą Twittera',
        oauthStarted: 'Otworzyło się okno autoryzacji. Zakończ proces w przeglądarce.'
      },
      discord: {
        botToken: 'Token bota',
        tokenHint: 'Pozostaw puste, aby zachować istniejący token',
        applicationId: 'Identyfikator aplikacji (dla poleceń z ukośnikiem)',
        monitoredChannels: 'Monitorowane kanały',
        monitoredChannelsHint: 'Jeden identyfikator kanału w każdym wierszu. Bot musi być na serwerze.',
        respondToDm: 'Odpowiadaj na DM',
        respondToMentions: 'Odpowiadaj na wzmianki'
      },
      slack: {
        botToken: 'Token bota (xoxb-)',
        tokenHint: 'Pozostaw puste, aby zachować istniejący token',
        appToken: 'Token aplikacji (xapp-)',
        appTokenHint: 'Dla trybu gniazda (faza 2). Opcjonalnie dla fazy 1.',
        monitoredChannels: 'Monitorowane kanały (nadpisanie)',
        monitoredChannelsHint: 'Bot automatycznie monitoruje wszystkie kanały, których jest członkiem. Dodaj tutaj tylko identyfikatory, aby ograniczyć zakres.',
        respondToMentions: "Odpowiadaj na {'@'}wzmianki",
        respondToAll: 'Odpowiadaj na wszystkie wiadomości (ignoruje filtr wzmianek)',
        threadedReplies: 'Odpowiedzi na wątki w kanałach (każda wiadomość rozpoczyna wątek)'
      }
    }
  } as const;

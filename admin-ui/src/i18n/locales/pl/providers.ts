export const providers = {
    title: 'Konfiguracja dostawcy',
    actions: {
      refresh: 'Odśwież',
      add: 'Dodaj dostawcę',
      test: 'Testuj',
      edit: 'Edytuj',
      delete: 'Usuń',
      setDefault: 'Ustaw domyślne',
      confirmDelete: 'Usunąć dostawcę „{id}”? Tego nie można cofnąć.'
    },
    columns: {
      status: 'Stan',
      provider: 'Imię',
      type: 'Wpisz',
      baseUrl: 'Bazowy adres URL',
      defaultModel: 'Modelka',
      default: 'Domyślne',
      actions: 'Działania'
    },
    empty: {
      title: 'Brak dostawców',
      description: 'Aby rozpocząć, dodaj dostawcę LLM.'
    },
    form: {
      createTitle: 'Dodaj dostawcę',
      editTitle: 'Edytuj dostawcę',
      providerType: 'Typ dostawcy',
      instanceName: 'Nazwa instancji',
      instanceNameHint: 'Unikalna nazwa tej konfiguracji dostawcy.',
      baseUrl: 'Bazowy adres URL',
      defaultModel: 'Domyślny model',
      defaultContextWindow: 'Domyślne okno kontekstowe',
      modelContextWindows: 'Zastąpienia kontekstu dla modelu',
      modelContextWindowsHint: 'Obiekt JSON, np. gpt-5.2: 200000',
      modelContextWindowsInvalid: 'Zastąpienia kontekstu dla poszczególnych modeli muszą mieć prawidłowy format JSON.',
      authType: 'Typ autoryzacji',
      apiKey: 'Klucz API',
      apiKeyPlaceholder: 'Pozostaw puste, aby zachować bieżący klucz',
      authFile: 'Plik uwierzytelniający',
      oauthBrowserTitle: 'Logowanie do przeglądarki (kod autoryzacyjny)',
      oauthRedirectUri: 'URI przekierowania',
      oauthStartBrowser: 'Rozpocznij logowanie do przeglądarki',
      oauthAuthorizationUrl: 'Adres URL autoryzacji',
      oauthState: 'Stan',
      oauthCallbackUrl: 'Wklej adres URL wywołania zwrotnego',
      oauthCallbackHint: 'Wklej pełny adres URL wywołania zwrotnego zawierający kod i stan.',
      oauthCompleteBrowser: 'Dokończ logowanie do przeglądarki',
      concurrency: 'Maksymalna współbieżność',
      create: 'Utwórz',
      save: 'Zapisz',
      cancel: 'Anuluj',
      modelManualHint: 'Wprowadź ręcznie identyfikator modelu. Lista modeli nie jest dostępna dla tego typu dostawcy.',
      modelsLockedHint: 'Zapisz dostawcę kluczem API, aby odblokować wybór modelu.',
      savedContinue: 'Dostawca zapisany. Możesz teraz wybrać model.',
      saveAndContinue: 'Zapisz i kontynuuj'
    },
    testSuccess: 'Dostawca „{id}” jest osiągalny.',
    testFailed: 'Test dostawcy „{id}” nie powiódł się',
    createSuccess: 'Utworzono dostawcę „{id}”.',
    updateSuccess: 'Dostawca „{id}” został zaktualizowany.',
    deleteSuccess: 'Dostawca „{id}” został usunięty.',
    defaultSuccess: 'Domyślny dostawca ustawiony na „{id}”.',
    errors: {
      loadFailed: 'Nie udało się załadować dostawców'
    }
  } as const;

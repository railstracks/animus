export const providers = {
    title: 'Providerconfiguratie',
    actions: {
      refresh: 'Vernieuwen',
      add: 'Provider toevoegen',
      test: 'Testen',
      edit: 'Bewerken',
      delete: 'Verwijderen',
      setDefault: 'Als standaard instellen',
      confirmDelete: 'Provider "{id}" verwijderen? Dit kan niet ongedaan worden gemaakt.'
    },
    columns: {
      status: 'Status',
      provider: 'Naam',
      type: 'Type',
      baseUrl: 'Basis-URL',
      defaultModel: 'Model',
      default: 'Standaard',
      actions: 'Acties'
    },
    empty: {
      title: 'Geen providers',
      description: 'Voeg een LLM-provider toe om te beginnen.'
    },
    form: {
      createTitle: 'Provider toevoegen',
      editTitle: 'Provider bewerken',
      providerType: 'Providertype',
      instanceName: 'Instantienaam',
      instanceNameHint: 'Een unieke naam voor deze providerconfiguratie.',
      baseUrl: 'Basis-URL',
      defaultModel: 'Standaardmodel',
      defaultContextWindow: 'Standaardcontextvenster',
      modelContextWindows: 'Contextoverschrijvingen per model',
      modelContextWindowsHint: 'JSON-object, bijv. gpt-5.2: 200000',
      modelContextWindowsInvalid: 'Contextoverschrijvingen per model moeten geldige JSON zijn.',
      authType: 'Authenticatietype',
      apiKey: 'API-sleutel',
      apiKeyPlaceholder: 'Leeg laten om huidige sleutel te behouden',
      authFile: 'Authenticatiebestand',
      oauthBrowserTitle: 'Browserlogin (autorisatiecode)',
      oauthRedirectUri: 'Omleidings-URI',
      oauthStartBrowser: 'Browserlogin starten',
      oauthAuthorizationUrl: 'Autorisatie-URL',
      oauthState: 'Status',
      oauthCallbackUrl: 'Callback-URL plakken',
      oauthCallbackHint: 'Plak de volledige callback-URL met code en status.',
      oauthCompleteBrowser: 'Browserlogin voltooien',
      concurrency: 'Max. gelijktijdigheid',
      create: 'Aanmaken',
      save: 'Opslaan',
      cancel: 'Annuleren',
      modelManualHint: 'Voer de model-ID handmatig in. Modellijst is niet beschikbaar voor dit providertype.',
      modelsLockedHint: 'Sla de provider op met een API-sleutel om modelselectie te ontgrendelen.',
      savedContinue: 'Provider opgeslagen. Je kunt nu een model selecteren.',
      saveAndContinue: 'Opslaan en doorgaan'
    },
    testSuccess: 'Provider "{id}" is bereikbaar.',
    testFailed: 'Provider "{id}"-test mislukt',
    createSuccess: 'Provider "{id}" aangemaakt.',
    updateSuccess: 'Provider "{id}" bijgewerkt.',
    deleteSuccess: 'Provider "{id}" verwijderd.',
    defaultSuccess: 'Standaardprovider ingesteld op "{id}".',
    errors: {
      loadFailed: 'Laden van providers mislukt'
    }
  } as const;

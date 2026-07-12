export const providers = {
    title: 'Anbieterkonfiguration',
    actions: {
      refresh: 'Aktualisieren',
      add: 'Anbieter hinzufügen',
      test: 'Testen',
      edit: 'Bearbeiten',
      delete: 'Löschen',
      setDefault: 'Standard festlegen',
      confirmDelete: 'Anbieter „{id}“ entfernen? Dies kann nicht rückgängig gemacht werden.'
    },
    columns: {
      status: 'Status',
      provider: 'Name',
      type: 'Typ',
      baseUrl: 'Basis-URL',
      defaultModel: 'Modell',
      default: 'Standard',
      actions: 'Aktionen'
    },
    empty: {
      title: 'Keine Anbieter',
      description: 'Fügen Sie einen LLM-Anbieter hinzu, um loszulegen.'
    },
    form: {
      createTitle: 'Anbieter hinzufügen',
      editTitle: 'Anbieter bearbeiten',
      providerType: 'Anbietertyp',
      instanceName: 'Instanzname',
      instanceNameHint: 'Ein eindeutiger Name für diese Anbieterkonfiguration.',
      baseUrl: 'Basis-URL',
      defaultModel: 'Standardmodell',
      defaultContextWindow: 'Standardkontextfenster',
      modelContextWindows: 'Kontextüberschreibungen pro Modell',
      modelContextWindowsHint: 'JSON-Objekt, z.B. gpt-5.2: 200000',
      modelContextWindowsInvalid: 'Kontextüberschreibungen pro Modell müssen gültiges JSON sein.',
      authType: 'Authentifizierungstyp',
      apiKey: 'API-Schlüssel',
      apiKeyPlaceholder: 'Lassen Sie das Feld leer, um den aktuellen Schlüssel beizubehalten',
      authFile: 'Auth-Datei',
      oauthBrowserTitle: 'Browser-Login (Autorisierungscode)',
      oauthRedirectUri: 'Umleitungs-URI',
      oauthStartBrowser: 'Starten Sie die Browser-Anmeldung',
      oauthAuthorizationUrl: 'Autorisierungs-URL',
      oauthState: 'Staat',
      oauthCallbackUrl: 'Rückruf-URL einfügen',
      oauthCallbackHint: 'Fügen Sie die vollständige Rückruf-URL mit Code und Status ein.',
      oauthCompleteBrowser: 'Vollständige Browser-Anmeldung',
      concurrency: 'Maximale Parallelität',
      create: 'Erstellen',
      save: 'Speichern',
      cancel: 'Abbrechen',
      modelManualHint: 'Geben Sie die Modell-ID manuell ein. Für diesen Anbietertyp ist keine Modellliste verfügbar.',
      modelsLockedHint: 'Speichern Sie den Anbieter mit einem API-Schlüssel, um die Modellauswahl freizuschalten.',
      savedContinue: 'Anbieter gespeichert. Sie können nun ein Modell auswählen.',
      saveAndContinue: 'Speichern und fortfahren'
    },
    testSuccess: 'Anbieter „{id}“ ist erreichbar.',
    testFailed: 'Der Anbietertest „{id}“ ist fehlgeschlagen',
    createSuccess: 'Anbieter „{id}“ erstellt.',
    updateSuccess: 'Anbieter „{id}“ aktualisiert.',
    deleteSuccess: 'Anbieter „{id}“ gelöscht.',
    defaultSuccess: 'Der Standardanbieter ist auf „{id}“ festgelegt.',
    errors: {
      loadFailed: 'Anbieter konnten nicht geladen werden'
    }
  } as const;

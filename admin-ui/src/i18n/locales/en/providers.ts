export const providers = {
    title: 'Provider Configuration',
    actions: {
      refresh: 'Refresh',
      add: 'Add Provider',
      test: 'Test',
      edit: 'Edit',
      delete: 'Delete',
      setDefault: 'Set Default',
      confirmDelete: 'Remove provider "{id}"? This cannot be undone.'
    },
    columns: {
      status: 'Status',
      provider: 'Name',
      type: 'Type',
      baseUrl: 'Base URL',
      defaultModel: 'Model',
      default: 'Default',
      actions: 'Actions'
    },
    empty: {
      title: 'No Providers',
      description: 'Add an LLM provider to get started.'
    },
    form: {
      createTitle: 'Add Provider',
      editTitle: 'Edit Provider',
      providerType: 'Provider Type',
      instanceName: 'Instance Name',
      instanceNameHint: 'A unique name for this provider configuration.',
      baseUrl: 'Base URL',
      defaultModel: 'Default Model',
      defaultContextWindow: 'Default Context Window',
      modelContextWindows: 'Per-Model Context Overrides',
      modelContextWindowsHint: 'JSON object, e.g. gpt-5.2: 200000',
      modelContextWindowsInvalid: 'Per-model context overrides must be valid JSON.',
      authType: 'Auth Type',
      apiKey: 'API Key',
      apiKeyPlaceholder: 'Leave empty to keep current key',
      authFile: 'Auth File',
      oauthBrowserTitle: 'Browser Login (Authorization Code)',
      oauthRedirectUri: 'Redirect URI',
      oauthStartBrowser: 'Start Browser Login',
      oauthAuthorizationUrl: 'Authorization URL',
      oauthState: 'State',
      oauthCallbackUrl: 'Paste Callback URL',
      oauthCallbackHint: 'Paste the full callback URL containing code and state.',
      oauthCompleteBrowser: 'Complete Browser Login',
      concurrency: 'Max Concurrency',
      create: 'Create',
      save: 'Save',
      cancel: 'Cancel',
      modelManualHint: 'Enter the model ID manually. Model listing is not available for this provider type.',
      modelsLockedHint: 'Save the provider with an API key to unlock model selection.',
      savedContinue: 'Provider saved. You can now select a model.',
      saveAndContinue: 'Save & Continue'
    },
    testSuccess: 'Provider "{id}" is reachable.',
    testFailed: 'Provider "{id}" test failed',
    createSuccess: 'Provider "{id}" created.',
    updateSuccess: 'Provider "{id}" updated.',
    deleteSuccess: 'Provider "{id}" deleted.',
    defaultSuccess: 'Default provider set to "{id}".',
    errors: {
      loadFailed: 'Failed to load providers'
    }
  } as const;

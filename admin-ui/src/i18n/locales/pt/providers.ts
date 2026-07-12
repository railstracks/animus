export const providers = {
    title: 'Configuração do provedor',
    actions: {
      refresh: 'Atualizar',
      add: 'Adicionar provedor',
      test: 'Teste',
      edit: 'Editar',
      delete: 'Excluir',
      setDefault: 'Definir padrão',
      confirmDelete: 'Remover provedor "{id}"? Isto não pode ser desfeito.'
    },
    columns: {
      status: 'Estado',
      provider: 'Nome',
      type: 'Tipo',
      baseUrl: 'URL base',
      defaultModel: 'Modelo',
      default: 'Padrão',
      actions: 'Ações'
    },
    empty: {
      title: 'Nenhum provedor',
      description: 'Adicione um provedor LLM para começar.'
    },
    form: {
      createTitle: 'Adicionar provedor',
      editTitle: 'Editar provedor',
      providerType: 'Tipo de provedor',
      instanceName: 'Nome da instância',
      instanceNameHint: 'Um nome exclusivo para esta configuração de provedor.',
      baseUrl: 'URL base',
      defaultModel: 'Modelo padrão',
      defaultContextWindow: 'Janela de contexto padrão',
      modelContextWindows: 'Substituições de contexto por modelo',
      modelContextWindowsHint: 'Objeto JSON, por ex. gpt-5.2: 200.000',
      modelContextWindowsInvalid: 'As substituições de contexto por modelo devem ser JSON válidas.',
      authType: 'Tipo de autenticação',
      apiKey: 'Chave de API',
      apiKeyPlaceholder: 'Deixe em branco para manter a chave atual',
      authFile: 'Arquivo de autenticação',
      oauthBrowserTitle: 'Login do navegador (código de autorização)',
      oauthRedirectUri: 'Redirecionar URI',
      oauthStartBrowser: 'Iniciar login do navegador',
      oauthAuthorizationUrl: 'URL de autorização',
      oauthState: 'Estado',
      oauthCallbackUrl: 'Colar URL de retorno de chamada',
      oauthCallbackHint: 'Cole o URL de retorno de chamada completo contendo código e estado.',
      oauthCompleteBrowser: 'Login completo do navegador',
      concurrency: 'Simultaneidade máxima',
      create: 'Criar',
      save: 'Salvar',
      cancel: 'Cancelar',
      modelManualHint: 'Insira o ID do modelo manualmente. A listagem de modelos não está disponível para este tipo de provedor.',
      modelsLockedHint: 'Salve o provedor com uma chave de API para desbloquear a seleção do modelo.',
      savedContinue: 'Provedor salvo. Agora você pode selecionar um modelo.',
      saveAndContinue: 'Salvar e continuar'
    },
    testSuccess: 'O provedor "{id}" está acessível.',
    testFailed: 'Falha no teste do provedor "{id}"',
    createSuccess: 'Provedor "{id}" criado.',
    updateSuccess: 'Provedor "{id}" atualizado.',
    deleteSuccess: 'Provedor "{id}" excluído.',
    defaultSuccess: 'Provedor padrão definido como "{id}".',
    errors: {
      loadFailed: 'Falha ao carregar provedores'
    }
  } as const;

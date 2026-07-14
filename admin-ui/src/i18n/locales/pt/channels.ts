export const channels = {
    title: 'Canais',
    empty: 'Nenhum canal configurado. Adicione um para começar.',
    createSuccess: 'Canal "{name}" criado com sucesso.',
    updateSuccess: 'Canal "{name}" atualizado com sucesso.',
    deleteSuccess: 'Canal "{name}" excluído.',
    columns: {
      name: 'Nome',
      type: 'Tipo',
      identity: 'Identidade',
      endpoint: 'Ponto final',
      enabled: 'Habilitado',
      connected: 'Conectado',
      lastEvent: 'Último Evento',
      actions: 'Ações'
    },
    state: {
      connected: 'Conectado',
      disconnected: 'Desconectado'
    },
    actions: {
      refresh: 'Atualizar',
      add: 'Adicionar canal',
      cancel: 'Cancelar',
      create: 'Criar',
      save: 'Salvar',
      confirmDelete: 'Excluir canal "{name}"? Isto não pode ser desfeito.'
    },
    form: {
      createTitle: 'Adicionar canal',
      editTitle: 'Editar "{name}"',
      name: 'Nome do canal',
      type: 'Tipo de canal',
      agent: 'Agente',
      minResponseInterval: 'Intervalo mínimo de resposta (segundos)',
      allowInterjection: 'Permitir interjeição',
      irc: {
        host: 'Servidor IRC',
        port: 'Porto',
        serverPassword: 'Senha do servidor',
        nick: 'Apelido',
        username: 'Nome de usuário',
        realname: 'Nome verdadeiro',
        channels: 'Canais',
        channelsHint: 'Um por linha. Formato: #canal [chave]',
        agent: 'Agente',
        respondToChannel: 'Responder às mensagens do canal',
        respondToDm: 'Responder a mensagens diretas',
        respondToNotices: 'Responder a avisos',
        allowedDmUsers: 'Usuários DM permitidos',
        reconnect: 'Reconectar automaticamente'
      },
      telegram: {
        botToken: 'Token de bot',
        tokenHint: 'Deixe em branco para manter o token existente'
      },
      vk: {
        accessToken: 'Token de acesso à comunidade',
        groupId: 'ID do grupo'
      },
      bluesky: {
        handle: 'Alça',
        appPassword: 'Senha do aplicativo',
        pds: 'URL do PDS'
      },
      mastodon: {
        handle: 'Alça',
        instance: 'URL da instância'
      },
      email: {
        apiKey: 'Chave de API AgentMail',
        apiKeyHint: 'Deixe em branco para manter a chave atual',
        inboxId: 'ID da caixa de entrada',
        pollingWait: 'Intervalo de pesquisa (segundos)'
      },
      twitter: {
        tier: 'Nível de API',
        clientId: 'ID do cliente OAuth',
        clientSecret: 'Segredo do cliente OAuth',
        accessToken: 'Token de acesso',
        tokenHint: 'Deixe em branco para manter o token existente',
        refreshToken: 'Atualizar token',
        authorize: 'Autorizar com Twitter',
        oauthStarted: 'Janela de autorização aberta. Conclua o fluxo no seu navegador.'
      },
      discord: {
        botToken: 'Token de bot',
        tokenHint: 'Deixe em branco para manter o token existente',
        applicationId: 'ID do aplicativo (para comandos de barra)',
        monitoredChannels: 'Canais Monitorados',
        monitoredChannelsHint: 'Um ID de canal por linha. O bot deve estar no servidor.',
        respondToDm: 'Responder a mensagens diretas',
        respondToMentions: 'Responder às menções'
      },
      slack: {
        botToken: 'Token de bot (xoxb-)',
        tokenHint: 'Deixe em branco para manter o token existente',
        appToken: 'Token de aplicativo (xapp-)',
        appTokenHint: 'Para Modo Socket (Fase 2). Opcional para a Fase 1.',
        monitoredChannels: 'Canais monitorados (substituição)',
        monitoredChannelsHint: 'O bot monitora automaticamente todos os canais dos quais é membro. Adicione IDs aqui apenas para restringir o escopo.',
        respondToMentions: "Responder a {'@'}menções",
        respondToAll: 'Responder a todas as mensagens (ignora filtro de menção)',
        threadedReplies: 'Respostas de tópicos em canais (cada mensagem inicia um tópico)'
      }
    }
  } as const;

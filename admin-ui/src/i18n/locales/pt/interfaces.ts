export const interfaces = {
    title: 'Gerenciamento de interfaces',
    actions: {
      refresh: 'Atualizar',
      add: 'Adicionar interface',
      edit: 'Editar',
      enable: 'Ativar',
      disable: 'Desativar',
      delete: 'Excluir',
      confirmDelete: 'Excluir a interface "{name}"?'
    },
    columns: {
      name: 'Nome',
      type: 'Tipo',
      module: 'ID do módulo',
      enabled: 'Ativada',
      connected: 'Conectada',
      lastEvent: 'Último evento',
      actions: 'Ações'
    },
    state: {
      enabled: 'sim',
      disabled: 'não',
      connected: 'conectada',
      disconnected: 'desconectada'
    },
    empty: 'Ainda não há interfaces configuradas.',
    form: {
      createTitle: 'Criar interface',
      editTitle: 'Editar interface: {name}',
      name: 'Nome da instância',
      type: 'Tipo de interface',
      moduleId: 'ID do módulo',
      enabled: 'Ativada',
      configJson: 'JSON de configuração',
      create: 'Criar',
      save: 'Salvar',
      cancel: 'Cancelar',
      irc: {
        host: 'Host',
        port: 'Porta',
        nick: 'Apelido',
        serverPassword: 'Senha do servidor',
        username: 'Nome de usuário',
        realname: 'Nome real',
        dmOnly: 'Modo apenas DM',
        respondToChannel: 'Responder à atividade do canal',
        respondToDm: 'Responder a mensagens diretas',
        respondToNotices: 'Responder a avisos',
        allowedDmUsers: 'Usuários de DM permitidos',
        allowedDmUsersHint: 'Lista de permissão opcional; separada por vírgulas ou quebras de linha.',
        agentId: 'ID do agente',
        reconnectEnabled: 'Reconexão ativada',
        reconnectInitialDelay: 'Atraso inicial de reconexão (ms)',
        reconnectMaxDelay: 'Atraso máximo de reconexão (ms)'
      }
    },
    createSuccess: 'Interface "{name}" criada.',
    updateSuccess: 'Interface "{name}" atualizada.',
    deleteSuccess: 'Interface "{name}" excluída.'
  } as const;

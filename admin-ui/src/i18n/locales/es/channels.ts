export const channels = {
    title: 'Canales',
    empty: 'No hay canales configurados. Agregue uno para comenzar.',
    createSuccess: 'Canal "{name}" creado exitosamente.',
    updateSuccess: 'El canal "{name}" se actualizó correctamente.',
    deleteSuccess: 'Canal "{name}" eliminado.',
    columns: {
      name: 'Nombre',
      type: 'Tipo',
      identity: 'Identidad',
      endpoint: 'Punto final',
      enabled: 'Habilitado',
      connected: 'Conectado',
      lastEvent: 'Último evento',
      actions: 'Acciones'
    },
    state: {
      connected: 'Conectado',
      disconnected: 'desconectado'
    },
    actions: {
      refresh: 'Actualizar',
      add: 'Agregar canal',
      cancel: 'Cancelar',
      create: 'crear',
      save: 'Guardar',
      confirmDelete: '¿Eliminar el canal "{name}"? Esto no se puede deshacer.'
    },
    form: {
      createTitle: 'Agregar canal',
      editTitle: 'Editar "{name}"',
      name: 'Nombre del canal',
      type: 'Tipo de canal',
      agent: 'Agente',
      minResponseInterval: 'Intervalo mínimo de respuesta (segundos)',
      allowInterjection: 'Permitir interjección',
      irc: {
        host: 'Servidor IRC',
        port: 'Puerto',
        serverPassword: 'Contraseña del servidor',
        nick: 'Apodo',
        username: 'Nombre de usuario',
        realname: 'Nombre real',
        channels: 'Canales',
        channelsHint: 'Uno por línea. Formato: #canal [clave]',
        agent: 'Agente',
        respondToChannel: 'Responder a los mensajes del canal.',
        respondToDm: 'Responder a mensajes directos',
        respondToNotices: 'Responder a avisos',
        allowedDmUsers: 'Usuarios DM permitidos',
        reconnect: 'Reconexión automática'
      },
      telegram: {
        botToken: 'Ficha de robot',
        tokenHint: 'Déjelo en blanco para conservar el token existente'
      },
      vk: {
        accessToken: 'Token de acceso a la comunidad',
        groupId: 'ID de grupo'
      },
      bluesky: {
        handle: 'Manejar',
        appPassword: 'Contraseña de la aplicación',
        pds: 'URL del PDS'
      },
      mastodon: {
        handle: 'Manejar',
        instance: 'URL de instancia'
      },
      email: {
        apiKey: 'Clave API de AgentMail',
        apiKeyHint: 'Déjelo vacío para mantener la clave actual',
        inboxId: 'ID de bandeja de entrada',
        pollingWait: 'Intervalo de encuesta (segundos)'
      },
      twitter: {
        tier: 'Nivel API',
        clientId: 'ID de cliente de OAuth',
        clientSecret: 'Secreto del cliente OAuth',
        accessToken: 'Token de acceso',
        tokenHint: 'Déjelo en blanco para conservar el token existente',
        refreshToken: 'Actualizar ficha',
        authorize: 'Autorizar con Twitter',
        oauthStarted: 'Se abrió la ventana de autorización. Complete el flujo en su navegador.'
      },
      discord: {
        botToken: 'Ficha de robot',
        tokenHint: 'Déjelo en blanco para conservar el token existente',
        applicationId: 'ID de aplicación (para comandos de barra diagonal)',
        monitoredChannels: 'Canales monitoreados',
        monitoredChannelsHint: 'Un ID de canal por línea. El bot debe estar en el servidor.',
        respondToDm: 'Responder a los DM',
        respondToMentions: 'Responder a las menciones',
        dmWhitelistEnabled: 'Restringir DMs a usuarios permitidos',
        allowedDmUsers: 'Usuarios DM permitidos',
        allowedDmUsersHint: 'Nombres de usuario de Discord (uno por línea). Solo estos usuarios pueden enviar DMs al bot.'
      },
      slack: {
        botToken: 'Token de robot (xoxb-)',
        tokenHint: 'Déjelo en blanco para conservar el token existente',
        appToken: 'Token de aplicación (xapp-)',
        appTokenHint: 'Para Modo Enchufe (Fase 2). Opcional para la Fase 1.',
        monitoredChannels: 'Canales monitoreados (anulación)',
        monitoredChannelsHint: 'El bot monitorea automáticamente todos los canales de los que es miembro. Solo agregue ID aquí para restringir el alcance.',
        respondToMentions: "Responder a {'@'}menciones",
        respondToAll: 'Responder a todos los mensajes (ignora el filtro de menciones)',
        threadedReplies: 'Respuestas de hilos en canales (cada mensaje inicia un hilo)'
      }
    }
  } as const;

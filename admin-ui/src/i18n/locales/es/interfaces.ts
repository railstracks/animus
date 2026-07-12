export const interfaces = {
    title: 'Gestión de interfaces',
    actions: {
      refresh: 'Actualizar',
      add: 'Añadir interfaz',
      edit: 'Editar',
      enable: 'Activar',
      disable: 'Desactivar',
      delete: 'Eliminar',
      confirmDelete: '¿Eliminar la interfaz "{name}"?'
    },
    columns: {
      name: 'Nombre',
      type: 'Tipo',
      module: 'ID del módulo',
      enabled: 'Activada',
      connected: 'Conectada',
      lastEvent: 'Último evento',
      actions: 'Acciones'
    },
    state: {
      enabled: 'sí',
      disabled: 'no',
      connected: 'conectada',
      disconnected: 'desconectada'
    },
    empty: 'Aún no hay interfaces configuradas.',
    form: {
      createTitle: 'Crear interfaz',
      editTitle: 'Editar interfaz: {name}',
      name: 'Nombre de instancia',
      type: 'Tipo de interfaz',
      moduleId: 'ID del módulo',
      enabled: 'Activada',
      configJson: 'JSON de configuración',
      create: 'Crear',
      save: 'Guardar',
      cancel: 'Cancelar',
      irc: {
        host: 'Host',
        port: 'Puerto',
        nick: 'Nick',
        serverPassword: 'Contraseña del servidor',
        username: 'Nombre de usuario',
        realname: 'Nombre real',
        dmOnly: 'Modo solo MD',
        respondToChannel: 'Responder a actividad de canales',
        respondToDm: 'Responder a mensajes directos',
        respondToNotices: 'Responder a avisos',
        allowedDmUsers: 'Usuarios MD permitidos',
        allowedDmUsersHint: 'Lista de permitidos opcional; separada por comas o saltos de línea.',
        agentId: 'ID del agente',
        reconnectEnabled: 'Reconexión activada',
        reconnectInitialDelay: 'Retraso inicial de reconexión (ms)',
        reconnectMaxDelay: 'Retraso máximo de reconexión (ms)'
      }
    },
    createSuccess: 'Interfaz "{name}" creada.',
    updateSuccess: 'Interfaz "{name}" actualizada.',
    deleteSuccess: 'Interfaz "{name}" eliminada.'
  } as const;

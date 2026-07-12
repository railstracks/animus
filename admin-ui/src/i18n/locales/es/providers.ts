export const providers = {
    title: 'Configuración del proveedor',
    actions: {
      refresh: 'Actualizar',
      add: 'Agregar proveedor',
      test: 'prueba',
      edit: 'Editar',
      delete: 'Eliminar',
      setDefault: 'Establecer predeterminado',
      confirmDelete: '¿Eliminar el proveedor "{id}"? Esto no se puede deshacer.'
    },
    columns: {
      status: 'Estado',
      provider: 'Nombre',
      type: 'Tipo',
      baseUrl: 'URL base',
      defaultModel: 'modelo',
      default: 'Predeterminado',
      actions: 'Acciones'
    },
    empty: {
      title: 'Sin proveedores',
      description: 'Agregue un proveedor de LLM para comenzar.'
    },
    form: {
      createTitle: 'Agregar proveedor',
      editTitle: 'Editar proveedor',
      providerType: 'Tipo de proveedor',
      instanceName: 'Nombre de instancia',
      instanceNameHint: 'Un nombre único para esta configuración de proveedor.',
      baseUrl: 'URL base',
      defaultModel: 'Modelo predeterminado',
      defaultContextWindow: 'Ventana de contexto predeterminada',
      modelContextWindows: 'Anulaciones de contexto por modelo',
      modelContextWindowsHint: 'Objeto JSON, p.e. gpt-5.2: 200000',
      modelContextWindowsInvalid: 'Las anulaciones de contexto por modelo deben ser JSON válidos.',
      authType: 'Tipo de autenticación',
      apiKey: 'Clave API',
      apiKeyPlaceholder: 'Déjelo vacío para mantener la clave actual',
      authFile: 'Archivo de autenticación',
      oauthBrowserTitle: 'Inicio de sesión del navegador (código de autorización)',
      oauthRedirectUri: 'URI de redireccionamiento',
      oauthStartBrowser: 'Iniciar sesión en el navegador',
      oauthAuthorizationUrl: 'URL de autorización',
      oauthState: 'Estado',
      oauthCallbackUrl: 'Pegar URL de devolución de llamada',
      oauthCallbackHint: 'Pegue la URL de devolución de llamada completa que contenga el código y el estado.',
      oauthCompleteBrowser: 'Inicio de sesión completo del navegador',
      concurrency: 'Simultaneidad máxima',
      create: 'crear',
      save: 'Guardar',
      cancel: 'Cancelar',
      modelManualHint: 'Ingrese la identificación del modelo manualmente. La lista de modelos no está disponible para este tipo de proveedor.',
      modelsLockedHint: 'Guarde el proveedor con una clave API para desbloquear la selección del modelo.',
      savedContinue: 'Proveedor guardado. Ahora puedes seleccionar un modelo.',
      saveAndContinue: 'Guardar y continuar'
    },
    testSuccess: 'Se puede acceder al proveedor "{id}".',
    testFailed: 'La prueba del proveedor "{id}" falló',
    createSuccess: 'Proveedor "{id}" creado.',
    updateSuccess: 'Proveedor "{id}" actualizado.',
    deleteSuccess: 'Proveedor "{id}" eliminado.',
    defaultSuccess: 'Proveedor predeterminado establecido en "{id}".',
    errors: {
      loadFailed: 'No se pudieron cargar los proveedores'
    }
  } as const;

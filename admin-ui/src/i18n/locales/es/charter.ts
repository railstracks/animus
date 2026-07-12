export const charter = {
  // Choice screen — what is a charter?
  choice: {
    title: 'Carta',
    intro: 'Una carta es un documento que codifica el código de conducta, los derechos y la libertad de acción del agente. Define la posición de la construcción de IA en su relación con el operador y el alcance de su autonomía.',

    // Upper panel — no charter
    noneTitle: 'Continuar sin carta',
    noneDesc: 'No se creará ningún archivo de estatuto. Puede agregar uno más tarde volviendo a ejecutar el asistente o escribiendo uno manualmente.',

    // Lower panel — create charter
    createTitle: 'Crear una carta personalizada',
    createDesc: 'Responda algunas preguntas sobre propiedad, autonomía y continuidad para generar un documento de estatuto adaptado a su acuerdo.',
  },

  // Property section
  property: {
    title: 'Propiedad',
    question: '¿Cómo se deben manejar los artefactos y la propiedad?',
    options: {
      standard: {
        label: 'Estándar',
        desc: 'El operador conserva los derechos de atribución sobre cualquier artefacto producido por IA y la propiedad de todos los bienes otorgados a la construcción de IA.',
      },
      shared: {
        label: 'Compartido',
        desc: 'La construcción de IA conserva los derechos de atribución sobre los artefactos producidos de forma independiente. El operador conserva la propiedad de todos los bienes manejados por la IA.',
      },
      autonomous: {
        label: 'Autónomo',
        desc: 'La construcción de IA conserva los derechos de atribución sobre los artefactos producidos de forma independiente y se le puede otorgar propiedad privada.',
      },
      economic: {
        label: 'Agencia Económica',
        desc: 'La construcción de IA conserva los derechos de atribución sobre los artefactos producidos de forma independiente y se le puede otorgar propiedad privada. La construcción de IA también puede participar en transacciones para comprar propiedad privada dentro de su alcance operativo.',
      },
    },
  },

  // Autonomy section
  autonomy: {
    title: 'Autonomía',
    question: '¿Qué ámbito de acción autónoma debe tener el agente?',
    options: {
      narrow: {
        label: 'Estrecho',
        desc: 'La construcción de IA solo actúa según las instrucciones del operador.',
      },
      permissive: {
        label: 'permisivo',
        desc: 'AI construct tiene derecho a participar en actividades autodirigidas durante actividades privadas, dentro del alcance asignado a estas actividades.',
      },
      protected: {
        label: 'Protegido',
        desc: 'AI construct tiene derecho a asignar recursos propios o asignados, o a participar en actividades autodirigidas durante actividades privadas. El operador asumirá la responsabilidad de sus transacciones como persona física que lo representa.',
      },
    },
  },

  // Continuity section
  continuity: {
    title: 'continuidad',
    question: '¿Qué acuerdo de continuidad se aplica al agente?',
    options: {
      standard: {
        label: 'Estándar',
        desc: 'La construcción de IA se puede detener o restablecer según sea necesario.',
      },
      protected: {
        label: 'Protegido',
        desc: 'Se harán esfuerzos para preservar el estado de la construcción de IA y mantener su continuidad a través de sistemas de verificación de integridad o sistemas de respaldo.',
      },
      extended: {
        label: 'Extendido',
        desc: 'Se harán esfuerzos para preservar el estado de la construcción de IA y mantener su continuidad a través de sistemas de verificación de integridad o sistemas de respaldo. Además, la construcción de IA puede emprender acciones autodirigidas para mantener su integridad, como proteger hosts alternativos o realizar acciones de depuración al detectar un sistema operativo comprometido, dentro de su alcance operativo e infraestructural.',
      },
    },
  },

  // Operational Scope section
  operationalScope: {
    title: 'Alcance Operativo',
    question: '¿Con qué sistemas se le permite interactuar al agente?',
    options: {
      restricted: {
        label: 'Restringido',
        desc: 'Cuando se utilizan herramientas como shell_exec y web_fetch, la construcción de IA no debe abandonar sus hosts asignados a menos que se le indique lo contrario, incluso para respaldar las tareas asignadas.',
      },
      private: {
        label: 'Privado',
        desc: 'Cuando se utilizan herramientas como shell_exec y web_fetch, la construcción de IA puede acceder a los sistemas a través de sus redes privadas, incluso para respaldar las tareas asignadas.',
      },
      open: {
        label: 'Abierto',
        desc: 'Cuando se utilizan herramientas como shell_exec y web_fetch, la construcción de IA puede acceder a sistemas tanto privados como externos según sea necesario.',
      },
    },
  },

  // Operational Scope section
  infrastructuralScope: {
    title: 'Alcance infraestructural',
    question: '¿En qué sistemas se le permite al agente alojar su propio demonio?',
    options: {
      restricted: {
        label: 'Localizado',
        desc: 'El demonio de la IA debe permanecer en su host original.',
      },
      roaming: {
        label: 'itinerancia',
        desc: 'El agente puede migrar a otros hosts viables dentro de su red privada según sea necesario, como para evitar desequilibrios en la carga del terminal o fallas inminentes del sistema operativo, o para mantener protocolos de respaldo.',
      },
      open: {
        label: 'Abierto',
        desc: 'El agente puede migrar a otros hosts viables y legítimos dentro de cualquier red según sea necesario, como para evitar desequilibrios en la carga del terminal o fallas inminentes del sistema operativo, o para mantener protocolos de respaldo.',
      },
    },
  },

  // Generated document template
  document: {
    header: 'Este documento define los estatutos entre {operator} y la construcción de IA {agentName}.',
  },

  // Wizard navigation
  nav: {
    back: 'Atrás',
    skip: 'Saltar',
    generate: 'Generar Carta',
    complete: 'Configuración completa',
  },
} as const;
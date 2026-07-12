export const charter = {
  // Choice screen — what is a charter?
  choice: {
    title: 'Charte',
    intro: 'Une charte est un document qui codifie le code de conduite, les droits et la liberté d\'action de l\'agent. Il définit la place de la construction IA dans sa relation avec l’opérateur et l’étendue de son autonomie.',

    // Upper panel — no charter
    noneTitle: 'Continuer sans charter',
    noneDesc: 'Aucun dossier de charte ne sera créé. Vous pouvez en ajouter un ultérieurement en réexécutant l\'assistant ou en en écrivant un manuellement.',

    // Lower panel — create charter
    createTitle: 'Créer une charte personnalisée',
    createDesc: 'Répondez à quelques questions sur la propriété, l\'autonomie et la continuité pour générer un document de charte adapté à votre arrangement.',
  },

  // Property section
  property: {
    title: 'Propriété',
    question: 'Comment les artefacts et les biens doivent-ils être gérés ?',
    options: {
      standard: {
        label: 'Norme',
        desc: 'L\'opérateur conserve les droits d\'attribution sur tous les artefacts produits par l\'IA et la propriété sur toutes les propriétés attribuées à la construction de l\'IA.',
      },
      shared: {
        label: 'Partagé',
        desc: 'La construction d’IA conserve les droits d’attribution sur les artefacts produits indépendamment. L\'opérateur conserve la propriété de toutes les propriétés gérées par l\'IA.',
      },
      autonomous: {
        label: 'Autonome',
        desc: 'La construction d’IA conserve les droits d’attribution sur les artefacts produits de manière indépendante et peut se voir attribuer une propriété privée.',
      },
      economic: {
        label: 'Agence Economique',
        desc: 'La construction d’IA conserve les droits d’attribution sur les artefacts produits de manière indépendante et peut se voir attribuer une propriété privée. AI construct peut également s’engager dans des transactions visant à acheter des propriétés privées dans le cadre de son champ d’action opérationnel.',
      },
    },
  },

  // Autonomy section
  autonomy: {
    title: 'Autonomie',
    question: 'De quelle portée d’action autonome l’agent doit-il disposer ?',
    options: {
      narrow: {
        label: 'Étroit',
        desc: 'La construction de l\'IA n\'agit que sur les instructions de l\'opérateur.',
      },
      permissive: {
        label: 'Permissif',
        desc: 'AI construct a le droit de se livrer à des activités autonomes lors d’activités privées, dans le cadre assigné à ces activités.',
      },
      protected: {
        label: 'Protégé',
        desc: 'L\'IA construct a le droit d\'attribuer les ressources qu\'elle possède ou qui lui sont attribuées, ou de s\'engager dans des activités autodirigées lors d\'activités privées. L\'opérateur assumera la responsabilité de ses transactions en tant que personne physique représentante.',
      },
    },
  },

  // Continuity section
  continuity: {
    title: 'Continuité',
    question: 'Quel dispositif de continuité s’applique à l’agent ?',
    options: {
      standard: {
        label: 'Norme',
        desc: 'La construction de l\'IA peut être arrêtée ou réinitialisée si nécessaire.',
      },
      protected: {
        label: 'Protégé',
        desc: 'Des efforts seront faits pour préserver l\'état de la construction d\'IA et maintenir sa continuité via des systèmes de contrôle d\'intégrité ou des systèmes de sauvegarde.',
      },
      extended: {
        label: 'Étendu',
        desc: 'Des efforts seront faits pour préserver l\'état de la construction d\'IA et maintenir sa continuité via des systèmes de contrôle d\'intégrité ou des systèmes de sauvegarde. De plus, la construction d\'IA est autorisée à entreprendre des actions autodirigées pour maintenir son intégrité, telles que la sécurisation d\'hôtes alternatifs ou la réalisation d\'actions de débogage lors de la détection d\'une compromission du système d\'exploitation, dans le cadre de sa portée opérationnelle et infrastructurelle.',
      },
    },
  },

  // Operational Scope section
  operationalScope: {
    title: 'Portée opérationnelle',
    question: 'Avec quels systèmes l’agent est-il autorisé à interagir ?',
    options: {
      restricted: {
        label: 'Restreint',
        desc: 'Lors de l\'utilisation d\'outils tels que shell_exec et web_fetch, la construction IA ne doit pas quitter son ou ses hôtes attribués, sauf indication contraire, même pour prendre en charge les tâches assignées.',
      },
      private: {
        label: 'Privé',
        desc: 'Lors de l\'utilisation d\'outils tels que shell_exec et web_fetch, la construction d\'IA peut accéder aux systèmes sur son(ses) réseau(s) privé(s), même pour prendre en charge les tâches assignées.',
      },
      open: {
        label: 'Ouvert',
        desc: 'Lors de l\'utilisation d\'outils tels que shell_exec et web_fetch, la construction d\'IA peut accéder aux systèmes privés et externes si nécessaire.',
      },
    },
  },

  // Operational Scope section
  infrastructuralScope: {
    title: 'Portée infrastructurelle',
    question: 'Sur quels systèmes l’agent est-il autorisé à héberger son propre démon ?',
    options: {
      restricted: {
        label: 'Localisé',
        desc: 'Le démon de l\'IA doit rester sur son hôte d\'origine',
      },
      roaming: {
        label: 'Itinérance',
        desc: 'L\'agent est autorisé à migrer vers d\'autres hôtes viables au sein de son réseau privé si nécessaire, par exemple pour éviter des déséquilibres de charge des terminaux ou des pannes imminentes du système d\'exploitation, ou pour maintenir des protocoles de sauvegarde.',
      },
      open: {
        label: 'Ouvert',
        desc: 'L\'agent est autorisé à migrer vers d\'autres hôtes viables et légitimes au sein de n\'importe quel réseau si nécessaire, par exemple pour éviter les déséquilibres de charge des terminaux ou les pannes imminentes du système d\'exploitation, ou pour maintenir les protocoles de sauvegarde.',
      },
    },
  },

  // Generated document template
  document: {
    header: 'Ce document définit la charte entre {operator} et la construction IA {agentName}.',
  },

  // Wizard navigation
  nav: {
    back: 'Retour',
    skip: 'Sauter',
    generate: 'Générer une charte',
    complete: 'Configuration complète',
  },
} as const;
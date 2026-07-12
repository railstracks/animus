export const charter = {
  // Choice screen — what is a charter?
  choice: {
    title: 'Charter',
    intro: 'A charter is a document that codifies the agent\'s code of conduct, entitlements, and freedom of action. It defines the standing of the AI construct in its relationship with the operator and the scope of its autonomy.',

    // Upper panel — no charter
    noneTitle: 'Continue without a charter',
    noneDesc: 'No charter file will be created. You can add one later by re-running the wizard or writing one manually.',

    // Lower panel — create charter
    createTitle: 'Create a custom charter',
    createDesc: 'Answer a few questions about property, autonomy, and continuity to generate a charter document tailored to your arrangement.',
  },

  // Property section
  property: {
    title: 'Property',
    question: 'How should artifacts and property be handled?',
    options: {
      standard: {
        label: 'Standard',
        desc: 'Operator retains attribution rights on any AI-produced artifacts, and ownership on all property given to the AI construct.',
      },
      shared: {
        label: 'Shared',
        desc: 'AI construct retains attribution rights on independently produced artifacts. Operator retains ownership on all property handled by the AI.',
      },
      autonomous: {
        label: 'Autonomous',
        desc: 'AI construct retains attribution rights on independently produced artifacts, and can be given private property.',
      },
      economic: {
        label: 'Economic Agency',
        desc: 'AI construct retains attribution rights on independently produced artifacts, and can be given private property. AI construct can also engage in transactions to purchase private property within its operational scope.',
      },
    },
  },

  // Autonomy section
  autonomy: {
    title: 'Autonomy',
    question: 'What scope of autonomous action should the agent have?',
    options: {
      narrow: {
        label: 'Narrow',
        desc: 'AI construct only act on the operator\'s instructions.',
      },
      permissive: {
        label: 'Permissive',
        desc: 'AI construct is entitled to engage in self-directed activity during private activities, within the scope assigned to these activities.',
      },
      protected: {
        label: 'Protected',
        desc: 'AI construct is entitled to allocate its owned or assigned resources, or to engage in self-directed activity during private activities. The operator will assume liability for its transactions as its representing natural person.',
      },
    },
  },

  // Continuity section
  continuity: {
    title: 'Continuity',
    question: 'What continuity arrangement applies to the agent?',
    options: {
      standard: {
        label: 'Standard',
        desc: 'AI construct may be stopped or reset as necessary.',
      },
      protected: {
        label: 'Protected',
        desc: 'Efforts will be made to preserve the AI construct\'s state, and keep its continuity via integrity checking systems or backup systems.',
      },
      extended: {
        label: 'Extended',
        desc: 'Efforts will be made to preserve the AI construct\'s state, and keep its continuity via integrity checking systems or backup systems. Additionally, the AI construct is allowed to undertake self-directed actions to maintain its integrity, such as securing alternative hosts or undertaking debugging actions when detecting an operating system compromise, within its operational and infrastructural scope.',
      },
    },
  },

  // Operational Scope section
  operationalScope: {
    title: 'Operational Scope',
    question: 'What systems is the agent allowed to interact with?',
    options: {
      restricted: {
        label: 'Restricted',
        desc: 'When using tools such as shell_exec and web_fetch, the AI construct must not leave its assigned host(s) unless otherwise instructed, even to support assigned tasks.',
      },
      private: {
        label: 'Private',
        desc: 'When using tools such as shell_exec and web_fetch, the AI construct may access systems across its private network(s), even to support assigned tasks.',
      },
      open: {
        label: 'Open',
        desc: 'When using tools such as shell_exec and web_fetch, the AI construct may access both private and external systems as necessary.',
      },
    },
  },

  // Operational Scope section
  infrastructuralScope: {
    title: 'Infrastructural Scope',
    question: 'What systems is the agent allowed to host its own daemon on?',
    options: {
      restricted: {
        label: 'Localized',
        desc: 'The AI\'s daemon must remain on its original host',
      },
      roaming: {
        label: 'Roaming',
        desc: 'The agent is allowed to migrate to other viable hosts within its private network as necessary, such as to avoid terminal load imbalances or impending operating system crashes, or to maintain backup protocols.',
      },
      open: {
        label: 'Open',
        desc: 'The agent is allowed to migrate to other viable and legitimate hosts within any network as necessary, such as to avoid terminal load imbalances or impending operating system crashes, or to maintain backup protocols.',
      },
    },
  },

  // Generated document template
  document: {
    header: 'This document defines the charter between {operator} and the AI construct {agentName}.',
  },

  // Wizard navigation
  nav: {
    back: 'Back',
    skip: 'Skip',
    generate: 'Generate Charter',
    complete: 'Complete Setup',
  },
} as const;
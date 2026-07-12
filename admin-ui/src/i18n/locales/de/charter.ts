export const charter = {
  // Choice screen — what is a charter?
  choice: {
    title: 'Charta',
    intro: 'Eine Charta ist ein Dokument, das den Verhaltenskodex, die Rechte und die Handlungsfreiheit des Agenten festlegt. Es definiert den Stellenwert des KI-Konstrukts in seiner Beziehung zum Bediener und den Umfang seiner Autonomie.',

    // Upper panel — no charter
    noneTitle: 'Weiter ohne Charter',
    noneDesc: 'Es wird keine Charterdatei erstellt. Sie können später einen hinzufügen, indem Sie den Assistenten erneut ausführen oder manuell einen erstellen.',

    // Lower panel — create charter
    createTitle: 'Erstellen Sie eine individuelle Charta',
    createDesc: 'Beantworten Sie einige Fragen zu Eigentum, Autonomie und Kontinuität, um ein auf Ihre Vereinbarung zugeschnittenes Charterdokument zu erstellen.',
  },

  // Property section
  property: {
    title: 'Eigentum',
    question: 'Wie soll mit Artefakten und Eigentum umgegangen werden?',
    options: {
      standard: {
        label: 'Standard',
        desc: 'Der Betreiber behält die Namensnennungsrechte an allen von der KI hergestellten Artefakten und das Eigentum an sämtlichem dem KI-Konstrukt überlassenen Eigentum.',
      },
      shared: {
        label: 'Geteilt',
        desc: 'AI Construct behält sich die Namensnennungsrechte für unabhängig hergestellte Artefakte vor. Der Betreiber behält das Eigentum an sämtlichem Eigentum, das von der KI verwaltet wird.',
      },
      autonomous: {
        label: 'Autonom',
        desc: 'Das KI-Konstrukt behält die Namensnennungsrechte an unabhängig hergestellten Artefakten und kann Privateigentum erhalten.',
      },
      economic: {
        label: 'Wirtschaftsagentur',
        desc: 'Das KI-Konstrukt behält die Namensnennungsrechte an unabhängig hergestellten Artefakten und kann Privateigentum erhalten. AI-Konstrukt kann im Rahmen seines Geschäftsumfangs auch Transaktionen zum Erwerb von Privatgrundstücken durchführen.',
      },
    },
  },

  // Autonomy section
  autonomy: {
    title: 'Autonomie',
    question: 'Welchen autonomen Handlungsspielraum sollte der Agent haben?',
    options: {
      narrow: {
        label: 'Schmal',
        desc: 'KI-Konstrukte handeln nur auf Anweisung des Bedieners.',
      },
      permissive: {
        label: 'Freizügig',
        desc: 'AI-Konstrukt ist berechtigt, im Rahmen privater Tätigkeiten im Rahmen dieser Tätigkeiten selbstgesteuerte Tätigkeiten auszuüben.',
      },
      protected: {
        label: 'Geschützt',
        desc: 'Das KI-Konstrukt ist berechtigt, seine eigenen oder zugewiesenen Ressourcen zuzuweisen oder sich im Rahmen privater Aktivitäten an selbstgesteuerten Aktivitäten zu beteiligen. Der Betreiber übernimmt die Haftung für seine Geschäfte als vertretende natürliche Person.',
      },
    },
  },

  // Continuity section
  continuity: {
    title: 'Kontinuität',
    question: 'Welche Kontinuitätsvereinbarung gilt für den Agenten?',
    options: {
      standard: {
        label: 'Standard',
        desc: 'Das KI-Konstrukt kann bei Bedarf gestoppt oder zurückgesetzt werden.',
      },
      protected: {
        label: 'Geschützt',
        desc: 'Es werden Anstrengungen unternommen, um den Zustand des KI-Konstrukts zu bewahren und seine Kontinuität durch Integritätsprüfsysteme oder Backup-Systeme aufrechtzuerhalten.',
      },
      extended: {
        label: 'Erweitert',
        desc: 'Es werden Anstrengungen unternommen, um den Zustand des KI-Konstrukts zu bewahren und seine Kontinuität durch Integritätsprüfsysteme oder Backup-Systeme aufrechtzuerhalten. Darüber hinaus ist es dem KI-Konstrukt möglich, innerhalb seines betrieblichen und infrastrukturellen Umfangs selbstgesteuerte Maßnahmen zur Aufrechterhaltung seiner Integrität durchzuführen, z. B. die Sicherung alternativer Hosts oder die Durchführung von Debugging-Aktionen, wenn eine Kompromittierung des Betriebssystems festgestellt wird.',
      },
    },
  },

  // Operational Scope section
  operationalScope: {
    title: 'Einsatzbereich',
    question: 'Mit welchen Systemen darf der Agent interagieren?',
    options: {
      restricted: {
        label: 'Eingeschränkt',
        desc: 'Bei der Verwendung von Tools wie „shell_exec“ und „web_fetch“ darf das KI-Konstrukt seinen/ihre(n) zugewiesenen Host(s) nicht verlassen, sofern nicht anders angegeben, auch nicht, um zugewiesene Aufgaben zu unterstützen.',
      },
      private: {
        label: 'Privat',
        desc: 'Bei der Verwendung von Tools wie Shell_exec und Web_fetch kann das KI-Konstrukt über seine privaten Netzwerke hinweg auf Systeme zugreifen, sogar um zugewiesene Aufgaben zu unterstützen.',
      },
      open: {
        label: 'Offen',
        desc: 'Bei der Verwendung von Tools wie shell_exec und web_fetch kann das KI-Konstrukt bei Bedarf sowohl auf private als auch auf externe Systeme zugreifen.',
      },
    },
  },

  // Operational Scope section
  infrastructuralScope: {
    title: 'Infrastruktureller Umfang',
    question: 'Auf welchen Systemen darf der Agent seinen eigenen Daemon hosten?',
    options: {
      restricted: {
        label: 'Lokalisiert',
        desc: 'Der Daemon der KI muss auf seinem ursprünglichen Host verbleiben',
      },
      roaming: {
        label: 'Roaming',
        desc: 'Dem Agenten ist es gestattet, bei Bedarf auf andere funktionsfähige Hosts innerhalb seines privaten Netzwerks zu migrieren, etwa um Ungleichgewichte bei der Terminallast oder drohende Betriebssystemabstürze zu vermeiden oder um Sicherungsprotokolle aufrechtzuerhalten.',
      },
      open: {
        label: 'Offen',
        desc: 'Dem Agenten ist es gestattet, bei Bedarf auf andere funktionsfähige und legitime Hosts in jedem Netzwerk zu migrieren, etwa um Ungleichgewichte bei der Terminallast oder drohende Betriebssystemabstürze zu vermeiden oder um Sicherungsprotokolle aufrechtzuerhalten.',
      },
    },
  },

  // Generated document template
  document: {
    header: 'Dieses Dokument definiert die Charta zwischen {operator} und dem KI-Konstrukt {agentName}.',
  },

  // Wizard navigation
  nav: {
    back: 'Zurück',
    skip: 'Überspringen',
    generate: 'Charta erstellen',
    complete: 'Komplette Einrichtung',
  },
} as const;
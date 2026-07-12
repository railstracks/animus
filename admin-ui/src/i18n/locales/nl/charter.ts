export const charter = {
  // Choice screen — what is a charter?
  choice: {
    title: 'Handvest',
    intro: 'Een charter is een document dat de gedragscode, rechten en vrijheid van handelen van de agent codificeert. Het definieert de status van het AI-construct in zijn relatie met de operator en de reikwijdte van zijn autonomie.',

    // Upper panel — no charter
    noneTitle: 'Ga verder zonder charter',
    noneDesc: 'Er wordt geen charterbestand aangemaakt. U kunt er later een toevoegen door de wizard opnieuw uit te voeren of er handmatig een te schrijven.',

    // Lower panel — create charter
    createTitle: 'Maak een aangepast charter',
    createDesc: 'Beantwoord een paar vragen over eigendom, autonomie en continuïteit om een charterdocument te genereren dat is afgestemd op uw arrangement.',
  },

  // Property section
  property: {
    title: 'Eigendom',
    question: 'Hoe moet er met artefacten en eigendommen worden omgegaan?',
    options: {
      standard: {
        label: 'Standaard',
        desc: 'De operator behoudt de toewijzingsrechten op alle door AI geproduceerde artefacten en het eigendomsrecht op alle eigendommen die aan de AI-constructie worden gegeven.',
      },
      shared: {
        label: 'Gedeeld',
        desc: 'AI-constructie behoudt de attributierechten op onafhankelijk geproduceerde artefacten. De exploitant behoudt het eigendom van alle eigendommen die door de AI worden beheerd.',
      },
      autonomous: {
        label: 'Autonoom',
        desc: 'AI-constructie behoudt de toewijzingsrechten op onafhankelijk geproduceerde artefacten en kan privé-eigendom worden gegeven.',
      },
      economic: {
        label: 'Economisch Agentschap',
        desc: 'AI-constructie behoudt de toewijzingsrechten op onafhankelijk geproduceerde artefacten en kan privé-eigendom worden gegeven. AI-construct kan ook transacties aangaan om privé-eigendom te kopen binnen zijn operationele reikwijdte.',
      },
    },
  },

  // Autonomy section
  autonomy: {
    title: 'Autonomie',
    question: 'Welke reikwijdte van autonoom handelen moet de agent hebben?',
    options: {
      narrow: {
        label: 'Smal',
        desc: 'AI-constructies handelen alleen op basis van de instructies van de operator.',
      },
      permissive: {
        label: 'Toegeeflijk',
        desc: 'AI Construct heeft het recht om tijdens privéactiviteiten zelfgestuurde activiteiten te ontplooien, binnen de reikwijdte die aan deze activiteiten is toegewezen.',
      },
      protected: {
        label: 'Beschermd',
        desc: 'AI Construct heeft het recht om zijn eigen of toegewezen middelen toe te wijzen, of om tijdens privéactiviteiten zelfgestuurde activiteiten uit te voeren. De exploitant aanvaardt de aansprakelijkheid voor zijn transacties als zijn vertegenwoordigende natuurlijke persoon.',
      },
    },
  },

  // Continuity section
  continuity: {
    title: 'Continuïteit',
    question: 'Welke continuïteitsregeling geldt voor de agent?',
    options: {
      standard: {
        label: 'Standaard',
        desc: 'De AI-constructie kan indien nodig worden gestopt of gereset.',
      },
      protected: {
        label: 'Beschermd',
        desc: 'Er zullen inspanningen worden geleverd om de staat van het AI-construct te behouden en de continuïteit ervan te behouden via integriteitscontrolesystemen of back-upsystemen.',
      },
      extended: {
        label: 'Uitgebreid',
        desc: 'Er zullen inspanningen worden geleverd om de staat van het AI-construct te behouden en de continuïteit ervan te behouden via integriteitscontrolesystemen of back-upsystemen. Bovendien mag de AI-constructie zelfgestuurde acties ondernemen om de integriteit ervan te behouden, zoals het beveiligen van alternatieve hosts of het ondernemen van foutopsporingsacties bij het detecteren van een compromittering van het besturingssysteem, binnen zijn operationele en infrastructurele reikwijdte.',
      },
    },
  },

  // Operational Scope section
  operationalScope: {
    title: 'Operationele reikwijdte',
    question: 'Met welke systemen mag de agent communiceren?',
    options: {
      restricted: {
        label: 'Beperkt',
        desc: 'Bij het gebruik van tools zoals shell_exec en web_fetch mag de AI-constructie de toegewezen host(s) niet verlaten tenzij anders aangegeven, zelfs niet om toegewezen taken te ondersteunen.',
      },
      private: {
        label: 'Privé',
        desc: 'Bij het gebruik van tools zoals shell_exec en web_fetch kan de AI-constructie toegang krijgen tot systemen via zijn privénetwerk(en), zelfs om toegewezen taken te ondersteunen.',
      },
      open: {
        label: 'Openen',
        desc: 'Bij het gebruik van tools zoals shell_exec en web_fetch kan de AI-constructie indien nodig toegang krijgen tot zowel privé- als externe systemen.',
      },
    },
  },

  // Operational Scope section
  infrastructuralScope: {
    title: 'Infrastructurele reikwijdte',
    question: 'Op welke systemen mag de agent zijn eigen daemon hosten?',
    options: {
      restricted: {
        label: 'Gelokaliseerd',
        desc: 'De daemon van de AI moet op de oorspronkelijke host blijven',
      },
      roaming: {
        label: 'Roamen',
        desc: 'De agent mag indien nodig migreren naar andere levensvatbare hosts binnen zijn privénetwerk, bijvoorbeeld om onevenwichtigheden in de terminalbelasting of dreigende crashes van het besturingssysteem te voorkomen, of om back-upprotocollen te onderhouden.',
      },
      open: {
        label: 'Openen',
        desc: 'De agent mag indien nodig naar andere levensvatbare en legitieme hosts binnen elk netwerk migreren, bijvoorbeeld om onevenwichtigheden in de terminalbelasting of dreigende crashes van het besturingssysteem te voorkomen, of om back-upprotocollen te onderhouden.',
      },
    },
  },

  // Generated document template
  document: {
    header: 'Dit document definieert het charter tussen {operator} en de AI-constructie {agentName}.',
  },

  // Wizard navigation
  nav: {
    back: 'Terug',
    skip: 'Overslaan',
    generate: 'Charter genereren',
    complete: 'Volledige installatie',
  },
} as const;
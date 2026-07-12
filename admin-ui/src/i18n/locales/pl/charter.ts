export const charter = {
  // Choice screen — what is a charter?
  choice: {
    title: 'Karta',
    intro: 'Karta to dokument, który kodyfikuje kodeks postępowania agenta, jego uprawnienia i swobodę działania. Określa pozycję konstruktu AI w relacji z operatorem oraz zakres jego autonomii.',

    // Upper panel — no charter
    noneTitle: 'Kontynuuj bez karty',
    noneDesc: 'Nie zostanie utworzony żaden plik czarterowy. Możesz dodać go później, ponownie uruchamiając kreatora lub pisząc go ręcznie.',

    // Lower panel — create charter
    createTitle: 'Utwórz niestandardową kartę',
    createDesc: 'Odpowiedz na kilka pytań dotyczących własności, autonomii i ciągłości, aby wygenerować dokument czarterowy dostosowany do Twojej aranżacji.',
  },

  // Property section
  property: {
    title: 'Własność',
    question: 'Jak należy postępować z artefaktami i mieniem?',
    options: {
      standard: {
        label: 'Standardowe',
        desc: 'Operator zachowuje prawa do wszelkich artefaktów wytworzonych przez sztuczną inteligencję oraz własność do całej własności przekazanej konstrukcji AI.',
      },
      shared: {
        label: 'Udostępnione',
        desc: 'Konstrukcja AI zachowuje prawa do niezależnie wyprodukowanych artefaktów. Operator zachowuje własność całej nieruchomości obsługiwanej przez sztuczną inteligencję.',
      },
      autonomous: {
        label: 'Autonomiczny',
        desc: 'Konstrukt AI zachowuje prawa do niezależnie wyprodukowanych artefaktów i może zostać przekazany na własność prywatną.',
      },
      economic: {
        label: 'Agencja Gospodarcza',
        desc: 'Konstrukt AI zachowuje prawa do niezależnie wyprodukowanych artefaktów i może zostać przekazany na własność prywatną. Konstrukt AI może także angażować się w transakcje zakupu własności prywatnej w ramach swojego zakresu operacyjnego.',
      },
    },
  },

  // Autonomy section
  autonomy: {
    title: 'Autonomia',
    question: 'Jaki zakres autonomicznego działania powinien posiadać agent?',
    options: {
      narrow: {
        label: 'Wąskie',
        desc: 'Konstrukcja AI działa wyłącznie zgodnie z instrukcjami operatora.',
      },
      permissive: {
        label: 'Zezwalający',
        desc: 'Konstrukt AI jest uprawniony do prowadzenia samodzielnej działalności w ramach czynności prywatnych, w zakresie przypisanym tej działalności.',
      },
      protected: {
        label: 'Chronione',
        desc: 'Konstrukt AI jest uprawniony do alokowania posiadanych lub przydzielonych mu zasobów lub angażowania się w samodzielną działalność w czasie zajęć prywatnych. Operator przejmie odpowiedzialność za swoje transakcje jako reprezentująca go osoba fizyczna.',
      },
    },
  },

  // Continuity section
  continuity: {
    title: 'Ciągłość',
    question: 'Jakie ustalenia dotyczące ciągłości dotyczą agenta?',
    options: {
      standard: {
        label: 'Standardowe',
        desc: 'W razie potrzeby konstrukcję AI można zatrzymać lub zresetować.',
      },
      protected: {
        label: 'Chronione',
        desc: 'Zostaną podjęte wysiłki, aby zachować stan konstrukcji sztucznej inteligencji i zachować jej ciągłość za pomocą systemów sprawdzania integralności lub systemów tworzenia kopii zapasowych.',
      },
      extended: {
        label: 'Rozszerzony',
        desc: 'Zostaną podjęte wysiłki, aby zachować stan konstrukcji sztucznej inteligencji i zachować jej ciągłość za pomocą systemów sprawdzania integralności lub systemów tworzenia kopii zapasowych. Dodatkowo konstrukcja AI może podejmować samodzielne działania w celu zachowania swojej integralności, takie jak zabezpieczanie alternatywnych hostów lub podejmowanie działań debugowania po wykryciu naruszenia bezpieczeństwa systemu operacyjnego, w ramach swojego zakresu operacyjnego i infrastrukturalnego.',
      },
    },
  },

  // Operational Scope section
  operationalScope: {
    title: 'Zakres operacyjny',
    question: 'Z jakimi systemami agent może wchodzić w interakcję?',
    options: {
      restricted: {
        label: 'Ograniczone',
        desc: 'Podczas korzystania z narzędzi takich jak Shell_exec i web_fetch konstrukcja AI nie może opuszczać przypisanych hostów, chyba że otrzyma inne polecenie, nawet w celu obsługi przydzielonych zadań.',
      },
      private: {
        label: 'Prywatny',
        desc: 'Podczas korzystania z narzędzi takich jak Shell_exec i web_fetch konstrukcja AI może uzyskiwać dostęp do systemów w swoich sieciach prywatnych, nawet w celu obsługi przydzielonych zadań.',
      },
      open: {
        label: 'Otwórz',
        desc: 'Podczas korzystania z narzędzi takich jak Shell_exec i web_fetch konstrukcja AI może w razie potrzeby uzyskiwać dostęp zarówno do systemów prywatnych, jak i zewnętrznych.',
      },
    },
  },

  // Operational Scope section
  infrastructuralScope: {
    title: 'Zakres infrastrukturalny',
    question: 'Na jakich systemach agent może hostować własnego demona?',
    options: {
      restricted: {
        label: 'Zlokalizowane',
        desc: 'Demon AI musi pozostać na swoim pierwotnym hoście',
      },
      roaming: {
        label: 'Roaming',
        desc: 'Agent może migrować do innych działających hostów w swojej sieci prywatnej, jeśli jest to konieczne, na przykład w celu uniknięcia braku równowagi obciążenia terminala lub zbliżających się awarii systemu operacyjnego lub w celu utrzymania protokołów kopii zapasowych.',
      },
      open: {
        label: 'Otwórz',
        desc: 'Agent może migrować do innych sprawnych i legalnych hostów w dowolnej sieci, jeśli jest to konieczne, na przykład w celu uniknięcia braku równowagi obciążenia terminala lub zbliżających się awarii systemu operacyjnego lub w celu utrzymania protokołów kopii zapasowych.',
      },
    },
  },

  // Generated document template
  document: {
    header: 'W tym dokumencie zdefiniowano umowę pomiędzy {operator} a konstrukcją AI {agentName}.',
  },

  // Wizard navigation
  nav: {
    back: 'Powrót',
    skip: 'Pomiń',
    generate: 'Wygeneruj Kartę',
    complete: 'Zakończ konfigurację',
  },
} as const;
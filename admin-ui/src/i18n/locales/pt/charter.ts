export const charter = {
  // Choice screen — what is a charter?
  choice: {
    title: 'Carta',
    intro: 'Uma carta é um documento que codifica o código de conduta, direitos e liberdade de ação do agente. Define a posição do construto IA na sua relação com o operador e o âmbito da sua autonomia.',

    // Upper panel — no charter
    noneTitle: 'Continuar sem carta',
    noneDesc: 'Nenhum arquivo de carta será criado. Você pode adicionar um posteriormente executando novamente o assistente ou escrevendo um manualmente.',

    // Lower panel — create charter
    createTitle: 'Crie um estatuto personalizado',
    createDesc: 'Responda a algumas perguntas sobre propriedade, autonomia e continuidade para gerar um documento de alvará adaptado ao seu acordo.',
  },

  // Property section
  property: {
    title: 'Propriedade',
    question: 'Como os artefatos e propriedades devem ser tratados?',
    options: {
      standard: {
        label: 'Padrão',
        desc: 'O Operador retém os direitos de atribuição sobre quaisquer artefatos produzidos por IA e a propriedade sobre todas as propriedades atribuídas à construção de IA.',
      },
      shared: {
        label: 'Compartilhado',
        desc: 'A construção de IA retém direitos de atribuição sobre artefatos produzidos de forma independente. O operador retém a propriedade de todas as propriedades administradas pela IA.',
      },
      autonomous: {
        label: 'Autônomo',
        desc: 'A construção de IA retém direitos de atribuição sobre artefatos produzidos de forma independente e pode receber propriedade privada.',
      },
      economic: {
        label: 'Agência Económica',
        desc: 'A construção de IA retém direitos de atribuição sobre artefatos produzidos de forma independente e pode receber propriedade privada. A construção de IA também pode participar em transações de compra de propriedade privada dentro do seu âmbito operacional.',
      },
    },
  },

  // Autonomy section
  autonomy: {
    title: 'Autonomia',
    question: 'Que âmbito de atuação autônoma o agente deve ter?',
    options: {
      narrow: {
        label: 'Estreito',
        desc: 'A construção de IA atua apenas de acordo com as instruções do operador.',
      },
      permissive: {
        label: 'Permissivo',
        desc: 'A construção de IA tem o direito de se envolver em atividades autodirigidas durante atividades privadas, dentro do âmbito atribuído a essas atividades.',
      },
      protected: {
        label: 'Protegido',
        desc: 'A construção de IA tem o direito de alocar recursos próprios ou atribuídos, ou de se envolver em atividades autodirigidas durante atividades privadas. O operador assumirá a responsabilidade pelas suas transações como pessoa física que o representa.',
      },
    },
  },

  // Continuity section
  continuity: {
    title: 'Continuidade',
    question: 'Que arranjo de continuidade se aplica ao agente?',
    options: {
      standard: {
        label: 'Padrão',
        desc: 'A construção da IA pode ser interrompida ou reiniciada conforme necessário.',
      },
      protected: {
        label: 'Protegido',
        desc: 'Serão feitos esforços para preservar o estado da construção de IA e manter a sua continuidade através de sistemas de verificação de integridade ou sistemas de backup.',
      },
      extended: {
        label: 'Estendido',
        desc: 'Serão feitos esforços para preservar o estado da construção de IA e manter a sua continuidade através de sistemas de verificação de integridade ou sistemas de backup. Além disso, a construção de IA pode realizar ações autodirigidas para manter a sua integridade, como proteger hosts alternativos ou realizar ações de depuração ao detectar um comprometimento do sistema operacional, dentro do seu escopo operacional e de infraestrutura.',
      },
    },
  },

  // Operational Scope section
  operationalScope: {
    title: 'Escopo Operacional',
    question: 'Com quais sistemas o agente pode interagir?',
    options: {
      restricted: {
        label: 'Restrito',
        desc: 'Ao usar ferramentas como shell_exec e web_fetch, a construção de IA não deve deixar seu(s) host(s) atribuído(s), a menos que seja instruído de outra forma, mesmo para suportar tarefas atribuídas.',
      },
      private: {
        label: 'Privado',
        desc: 'Ao usar ferramentas como shell_exec e web_fetch, a construção de IA pode acessar sistemas em suas redes privadas, até mesmo para dar suporte a tarefas atribuídas.',
      },
      open: {
        label: 'Abrir',
        desc: 'Ao usar ferramentas como shell_exec e web_fetch, a construção de IA pode acessar sistemas privados e externos conforme necessário.',
      },
    },
  },

  // Operational Scope section
  infrastructuralScope: {
    title: 'Escopo Infraestrutural',
    question: 'Em quais sistemas o agente pode hospedar seu próprio daemon?',
    options: {
      restricted: {
        label: 'Localizado',
        desc: 'O daemon da IA deve permanecer em seu host original',
      },
      roaming: {
        label: 'Roaming',
        desc: 'O agente pode migrar para outros hosts viáveis ​​dentro de sua rede privada conforme necessário, como para evitar desequilíbrios de carga do terminal ou falhas iminentes do sistema operacional, ou para manter protocolos de backup.',
      },
      open: {
        label: 'Abrir',
        desc: 'O agente tem permissão para migrar para outros hosts viáveis e legítimos em qualquer rede, conforme necessário, como para evitar desequilíbrios de carga do terminal ou falhas iminentes do sistema operacional, ou para manter protocolos de backup.',
      },
    },
  },

  // Generated document template
  document: {
    header: 'Este documento define o estatuto entre {operator} e a construção de IA {agentName}.',
  },

  // Wizard navigation
  nav: {
    back: 'Voltar',
    skip: 'Pular',
    generate: 'Gerar Carta',
    complete: 'Configuração completa',
  },
} as const;
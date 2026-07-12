export const activeMemory = {
    title: 'Memória Ativa',
    subtitle: 'Contexto do agente montado — o que o agente vê em seu preâmbulo',
    empty: 'Selecione um agente para visualizar seu contexto montado.',
    actions: {
      refresh: 'Atualizar'
    },
    labels: {
      agent: 'Agente',
      session: 'Sessão',
      syntheticSession: 'Sintético (sessão vazia para teste)',
      blocks: 'blocos'
    },
    flags: {
      synthetic: 'Sessão sintética',
      live: 'Sessão ao vivo'
    },
    sections: {
      rendered: 'Saída renderizada',
      blocks: 'Divisão de blocos'
    }
  } as const;

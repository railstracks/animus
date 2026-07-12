export const activeMemory = {
  title: 'Actief geheugen',
  subtitle: 'Samengestelde agentcontext — wat de agent ziet in de preambule',
  empty: 'Selecteer een agent om de samengestelde context te bekijken.',
  actions: {
    refresh: 'Vernieuwen'
  },
  labels: {
    agent: 'Agent',
    session: 'Sessie',
    syntheticSession: 'Synthetisch (lege sessie voor testen)',
    blocks: 'blokken'
  },
  flags: {
    synthetic: 'Synthetische sessie',
    live: 'Live-sessie'
  },
  sections: {
    rendered: 'Gegenereerde uitvoer',
    blocks: 'Blokoverzicht'
  }
} as const;
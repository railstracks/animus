export const activeMemory = {
    title: 'Aktives Gedächtnis',
    subtitle: 'Zusammengesetzter Agentenkontext – was der Agent in seiner Präambel sieht',
    empty: 'Wählen Sie einen Agenten aus, um seinen zusammengestellten Kontext anzuzeigen.',
    actions: {
      refresh: 'Aktualisieren'
    },
    labels: {
      agent: 'Agent',
      session: 'Sitzung',
      syntheticSession: 'Synthetisch (leere Sitzung zum Testen)',
      blocks: 'Blöcke'
    },
    flags: {
      synthetic: 'Synthetische Sitzung',
      live: 'Live-Sitzung'
    },
    sections: {
      rendered: 'Gerenderte Ausgabe',
      blocks: 'Blockaufschlüsselung'
    }
  } as const;

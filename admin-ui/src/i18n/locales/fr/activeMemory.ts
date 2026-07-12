export const activeMemory = {
    title: 'Mémoire active',
    subtitle: 'Contexte de l\'agent assemblé : ce que l\'agent voit dans son préambule',
    empty: 'Sélectionnez un agent pour afficher son contexte assemblé.',
    actions: {
      refresh: 'Actualiser'
    },
    labels: {
      agent: 'Agent',
      session: 'Séance',
      syntheticSession: 'Synthétique (session vide pour les tests)',
      blocks: 'blocs'
    },
    flags: {
      synthetic: 'Séance synthétique',
      live: 'Séance en direct'
    },
    sections: {
      rendered: 'Sortie rendue',
      blocks: 'Répartition des blocs'
    }
  } as const;

export const activeMemory = {
    title: 'Active Memory',
    subtitle: 'Assembled agent context — what the agent sees in its preamble',
    empty: 'Select an agent to view its assembled context.',
    actions: {
      refresh: 'Refresh'
    },
    labels: {
      agent: 'Agent',
      session: 'Session',
      syntheticSession: 'Synthetic (empty session for testing)',
      blocks: 'blocks'
    },
    flags: {
      synthetic: 'Synthetic session',
      live: 'Live session'
    },
    sections: {
      rendered: 'Rendered Output',
      blocks: 'Block Breakdown'
    }
  } as const;

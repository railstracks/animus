export const chat = {
    sessionLabel: 'សម័យ',
    newSession: 'វគ្គថ្មី។',
    refresh: 'ធ្វើឱ្យស្រស់',
    stopGenerating: 'បញ្ឈប់ការបង្កើត',
    emptyTitle: 'ចាប់ផ្តើមការសន្ទនា',
    emptyDescription: 'សួរ Animus អំពីអង្គចងចាំ ការកំណត់រចនាសម្ព័ន្ធ ឬវគ្គបន្តផ្ទាល់ ដើម្បីដំណើរការ។',
    streamState: {
      streaming: 'ការផ្សាយ...',
      stopped: 'ឈប់'
    },
    jumpToLatest: 'លោតទៅចុងក្រោយ',
    composerPlaceholder: 'ផ្ញើសារទៅកាន់ Animus...',
    adminTokenLabel: 'និមិត្តសញ្ញាគ្រប់គ្រង (ជាជម្រើស)',
    send: 'ផ្ញើ',
    contextTitle: 'បរិបទ',
    context: {
      session: 'សម័យ',
      layers: 'ស្រទាប់',
      tools: 'ឧបករណ៍',
      budget: 'ថវិកា',
      fallbackNote: 'ការធ្វើបច្ចុប្បន្នភាពរូបថតបរិបទនៅពេលវគ្គផ្លាស់ប្តូរ។'
    },
    sidebarTabs: {
      context: 'បរិបទ',
      sessions: 'វគ្គ',
      messages: 'សារ',
      noSessions: 'មិនទាន់មានវគ្គណាមួយនៅឡើយទេ។',
      searchSessions: 'Search sessions...',
      newSessionHint: 'សរសេរសារដើម្បីចាប់ផ្តើមខ្សែស្រឡាយថ្មី។'
    },
    thinking: {
      label: "ការគិត"
    },
    toolResult: {
      label: "លទ្ធផលឧបករណ៍"
    },
    toolCall: {
      label: "ការហៅឧបករណ៍"
    },
    reasoning: {
      label: 'របៀបវែកញែក',
      enabled: 'បើក',
      disabled: 'បិទ',
      effort: 'ការខិតខំប្រឹងប្រែង',
      instructionLabel: 'ការណែនាំអំពីប្រព័ន្ធ',
      instructionPlaceholder: 'ការណែនាំអំពីហេតុផលផ្ទាល់ខ្លួន (ប្រើលំនាំដើមប្រសិនបើទទេ)',
      notSupported: 'ហេតុផលមិនមានសម្រាប់ម៉ូដែលនេះទេ។'
    },
    provider: {
      label: 'អ្នកផ្តល់សេវា',
      select: 'អ្នកផ្តល់សេវា',
      model: 'គំរូ'
    },
    agent: {
      label: 'ភ្នាក់ងារ (វគ្គថ្មី)'
    },
    status: {
      closed: 'បិទ',
      connecting: 'ការភ្ជាប់',
      open: 'បានភ្ជាប់'
    },
    errors: {
      websocket: 'កំហុស WebSocket',
      websocketNotConnected: 'WebSocket មិនទាន់បានភ្ជាប់នៅឡើយទេ។ សូមព្យាយាមម្តងទៀត។',
      agentNotReady: 'ការជ្រើសរើសភ្នាក់ងារនៅតែដំណើរការ។ សូមរង់ចាំមួយភ្លែត ហើយព្យាយាមម្តងទៀត។',
      unknownWebsocket: 'កំហុសរន្ធបណ្តាញមិនស្គាល់',
      sessionLoadFailed: 'បរាជ័យក្នុងការផ្ទុកវគ្គ {sessionId}៖ {message}'
    }
  } as const;

export const chat = {
    sessionLabel: 'Oturum',
    newSession: 'Yeni Oturum',
    refresh: 'Yenile',
    stopGenerating: 'Oluşturmayı Durdur',
    emptyTitle: 'Konuşma başlat',
    emptyDescription: 'Devam etmek için Animus\'a bellek, yapılandırma veya canlı oturum hakkında sorular sorun.',
    streamState: {
      streaming: 'akış...',
      stopped: 'durduruldu'
    },
    jumpToLatest: 'En Yeniye Atla',
    composerPlaceholder: 'Animus\'a bir mesaj gönder...',
    adminTokenLabel: 'Yönetici Jetonu (isteğe bağlı)',
    send: 'Gönder',
    contextTitle: 'Bağlam',
    context: {
      session: 'Oturum',
      layers: 'Katmanlar',
      tools: 'Araçlar',
      budget: 'Bütçe',
      fallbackNote: 'Oturumlar değiştikçe bağlam anlık görüntüsü güncellenir.'
    },
    sidebarTabs: {
      context: 'Bağlam',
      sessions: 'Oturumlar',
      messages: 'mesajlar',
      noSessions: 'Henüz mevcut oturum yok.',
      searchSessions: 'Search sessions...',
      newSessionHint: 'Yeni bir konu başlatmak için bir mesaj oluşturun.'
    },
    thinking: {
      label: "Düşünme"
    },
    toolResult: {
      label: "Araç Sonucu"
    },
    toolCall: {
      label: "Araç Çağrısı"
    },
    reasoning: {
      label: 'Muhakeme Modu',
      enabled: 'Açık',
      disabled: 'Kapalı',
      effort: 'Çaba',
      instructionLabel: 'Sistem talimatı',
      instructionPlaceholder: 'Özel akıl yürütme talimatı (boşsa varsayılanı kullanır)',
      notSupported: 'Bu model için muhakeme mevcut değildir.'
    },
    provider: {
      label: 'sağlayıcı',
      select: 'sağlayıcı',
      model: 'Modeli'
    },
    agent: {
      label: 'Temsilci (Yeni Oturum)'
    },
    status: {
      closed: 'Kapalı',
      connecting: 'Bağlanıyor',
      open: 'Bağlı'
    },
    errors: {
      websocket: 'WebSocket hatası',
      websocketNotConnected: 'WebSocket henüz bağlanmadı. Lütfen tekrar deneyin.',
      agentNotReady: 'Temsilci seçimi hâlâ yükleniyor. Lütfen biraz bekleyip tekrar deneyin.',
      unknownWebsocket: 'Bilinmeyen websoket hatası',
      sessionLoadFailed: '{sessionId} oturumu yüklenemedi: {message}'
    }
  } as const;

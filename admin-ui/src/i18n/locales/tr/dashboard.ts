export const dashboard = {
  // First-run banner
  banner: {
    welcome: 'Animus\'a hoş geldiniz.',
    welcomeSub: 'Başlamak için ilk temsilcinizi ayarlayın.',
    beginSetup: 'Kurulumu Başlat →',
  },
  // Status ribbon
  ribbon: {
    uptime: 'Çalışma süresi',
    agents: 'Temsilciler',
    sessions: 'Oturumlar',
    providers: 'Sağlayıcılar',
    errors: 'hata',
  },
  // Recent Sessions card
  recentSessions: {
    title: 'Son Oturumlar',
    viewAll: 'Tümünü görüntüle',
    empty: 'Henüz oturum yok.',
    startChat: 'Sohbet başlat →',
    messages: 'mesajlar',
  },
  // Memory card
  memory: {
    title: 'Bellek',
    inspect: 'İncele',
    empty: 'Yapılandırılmış bellek katmanı yok.',
    totalObservations: 'toplam gözlemler',
  },
  // Provider card
  providers: {
    title: 'Sağlayıcılar',
    manage: 'Yönet',
    empty: 'Hiçbir sağlayıcı yapılandırılmadı.',
    runWizard: 'Kurulum sihirbazını çalıştırın →',
  },
  // Quick Actions card
  quickActions: {
    title: 'Hızlı Eylemler',
    newChat: 'Yeni Sohbet',
    addAgent: 'Temsilci Ekle',
    provider: 'sağlayıcı',
    logs: 'Günlükler',
    scheduler: 'Zamanlayıcı',
    memory: 'Bellek',
  },
  // Daemon Info card
  daemonInfo: {
    title: 'Daemon Bilgisi',
    status: 'Durum',
    uptime: 'Çalışma süresi',
    agents: 'Temsilciler',
    activeSessions: 'Aktif oturumlar',
    providers: 'Sağlayıcılar',
  },
  // Memory Layers card
  memoryLayers: {
    title: 'Bellek Katmanları',
    empty: 'Yapılandırılmış bellek katmanı yok.',
  },
  // State labels
  state: {
    loading: 'yükleniyor',
    unknown: 'bilinmiyor',
  },
  // Error messages
  errors: {
    sessions: 'Oturumlar yüklenemedi',
    providers: 'Sağlayıcılar yüklenemedi',
    memory: 'Bellek API\'si kullanılamıyor',
  },
} as const;
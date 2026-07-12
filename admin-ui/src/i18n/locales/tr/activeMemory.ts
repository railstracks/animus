export const activeMemory = {
    title: 'Aktif Bellek',
    subtitle: 'Birleştirilmiş aracı bağlamı - aracının giriş bölümünde gördüğü şey',
    empty: 'Birleştirilmiş içeriğini görüntülemek için bir aracı seçin.',
    actions: {
      refresh: 'Yenile'
    },
    labels: {
      agent: 'Temsilci',
      session: 'Oturum',
      syntheticSession: 'Sentetik (test için boş oturum)',
      blocks: 'bloklar'
    },
    flags: {
      synthetic: 'Sentetik oturum',
      live: 'Canlı oturum'
    },
    sections: {
      rendered: 'İşlenmiş Çıktı',
      blocks: 'Blok Dağılımı'
    }
  } as const;

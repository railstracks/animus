export const interfaces = {
    title: 'Arayüz Yönetimi',
    actions: {
      refresh: 'Yenile',
      add: 'Arayüz Ekle',
      edit: 'Düzenle',
      enable: 'Etkinleştir',
      disable: 'Devre Dışı Bırak',
      delete: 'Sil',
      confirmDelete: '"{name}" arayüzü silinsin mi?'
    },
    columns: {
      name: 'Ad',
      type: 'Tür',
      module: 'Modül ID’si',
      enabled: 'Etkin',
      connected: 'Bağlı',
      lastEvent: 'Son Olay',
      actions: 'Eylemler'
    },
    state: {
      enabled: 'evet',
      disabled: 'hayır',
      connected: 'bağlı',
      disconnected: 'bağlantı kesildi'
    },
    empty: 'Henüz yapılandırılmış arayüz yok.',
    form: {
      createTitle: 'Arayüz Oluştur',
      editTitle: 'Arayüzü Düzenle: {name}',
      name: 'Örnek Adı',
      type: 'Arayüz Türü',
      moduleId: 'Modül ID’si',
      enabled: 'Etkin',
      configJson: 'Yapılandırma JSON’u',
      create: 'Oluştur',
      save: 'Kaydet',
      cancel: 'İptal',
      irc: {
        host: 'Ana makine',
        port: 'Bağlantı noktası',
        nick: 'Takma ad',
        serverPassword: 'Sunucu Parolası',
        username: 'Kullanıcı Adı',
        realname: 'Gerçek Ad',
        dmOnly: 'Yalnızca DM Modu',
        respondToChannel: 'Kanal Etkinliğine Yanıt Ver',
        respondToDm: 'Doğrudan Mesajlara Yanıt Ver',
        respondToNotices: 'Bildirimlere Yanıt Ver',
        allowedDmUsers: 'İzin Verilen DM Kullanıcıları',
        allowedDmUsersHint: 'İsteğe bağlı izin listesi; virgül veya yeni satırla ayrılmış.',
        agentId: 'Ajan ID’si',
        reconnectEnabled: 'Yeniden Bağlanma Etkin',
        reconnectInitialDelay: 'İlk Yeniden Bağlanma Gecikmesi (ms)',
        reconnectMaxDelay: 'Maks. Yeniden Bağlanma Gecikmesi (ms)'
      }
    },
    createSuccess: '"{name}" arayüzü oluşturuldu.',
    updateSuccess: '"{name}" arayüzü güncellendi.',
    deleteSuccess: '"{name}" arayüzü silindi.'
  } as const;

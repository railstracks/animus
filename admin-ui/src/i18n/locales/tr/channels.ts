export const channels = {
    title: 'Kanallar',
    empty: 'Hiçbir kanal yapılandırılmadı. Başlamak için bir tane ekleyin.',
    createSuccess: '"{name}" kanalı başarıyla oluşturuldu.',
    updateSuccess: '"{name}" kanalı başarıyla güncellendi.',
    deleteSuccess: '"{name}" kanalı silindi.',
    columns: {
      name: 'İsim',
      type: 'Tür',
      identity: 'Kimlik',
      endpoint: 'Uç nokta',
      enabled: 'Etkin',
      connected: 'Bağlı',
      lastEvent: 'Son Etkinlik',
      actions: 'Eylemler'
    },
    state: {
      connected: 'Bağlı',
      disconnected: 'Bağlantı kesildi'
    },
    actions: {
      refresh: 'Yenile',
      add: 'Kanal Ekle',
      cancel: 'İptal',
      create: 'Oluştur',
      save: 'Kaydet',
      confirmDelete: '"{name}" kanalı silinsin mi? Bu geri alınamaz.'
    },
    form: {
      createTitle: 'Kanal Ekle',
      editTitle: '"{name}" ifadesini düzenle',
      name: 'Kanal Adı',
      type: 'Kanal Tipi',
      agent: 'Temsilci',
      minResponseInterval: 'En düşük yanıt aralığı (saniye)',
      allowInterjection: 'Araya girmeye izin ver',
      irc: {
        host: 'IRC Sunucusu',
        port: 'Liman',
        serverPassword: 'Sunucu Şifresi',
        nick: 'Takma ad',
        username: 'Kullanıcı adı',
        realname: 'Gerçek Ad',
        channels: 'Kanallar',
        channelsHint: 'Her satıra bir tane. Biçim: #kanal [anahtar]',
        agent: 'Temsilci',
        respondToChannel: 'Kanal mesajlarına yanıt verme',
        respondToDm: 'Doğrudan mesajlara yanıt verin',
        respondToNotices: 'Bildirimlere yanıt verme',
        allowedDmUsers: 'İzin verilen DM kullanıcıları',
        reconnect: 'Otomatik yeniden bağlan'
      },
      telegram: {
        botToken: 'Bot Jetonu',
        tokenHint: 'Mevcut jetonu korumak için boş bırakın'
      },
      vk: {
        accessToken: 'Topluluk Erişim Jetonu',
        groupId: 'Grup Kimliği'
      },
      bluesky: {
        handle: 'Kolu',
        appPassword: 'Uygulama Şifresi',
        pds: 'PDS URL\'si'
      },
      mastodon: {
        handle: 'Kolu',
        instance: 'Örnek URL\'si'
      },
      email: {
        apiKey: 'AgentMail API Anahtarı',
        apiKeyHint: 'Mevcut anahtarı korumak için boş bırakın',
        inboxId: 'Gelen Kutusu Kimliği',
        pollingWait: 'Anket Aralığı (saniye)'
      },
      twitter: {
        tier: 'API Katmanı',
        clientId: 'OAuth İstemci Kimliği',
        clientSecret: 'OAuth İstemci Sırrı',
        accessToken: 'Erişim Jetonu',
        tokenHint: 'Mevcut jetonu korumak için boş bırakın',
        refreshToken: 'Jetonu Yenile',
        authorize: 'Twitter ile yetkilendir',
        oauthStarted: 'Yetkilendirme penceresi açıldı. Akışı tarayıcınızda tamamlayın.'
      },
      discord: {
        botToken: 'Bot Jetonu',
        tokenHint: 'Mevcut jetonu korumak için boş bırakın',
        applicationId: 'Uygulama Kimliği (eğik çizgi komutları için)',
        monitoredChannels: 'İzlenen kanallar',
        monitoredChannelsHint: 'Bağlam için izlenecek kanal ID'leri (her satıra bir tane). Mesajlar yanıt tetiklemeden günlüğe kaydedilir.',
        respondToDm: 'DM\'lere yanıt verin',
        respondToMentions: 'Bahsedilenlere yanıt verme',
        monitorAllChannels: 'Tüm kanalları izle',
        respondToChannels: 'İzlenen kanallardaki her mesaja yanıt ver',
        dmWhitelistEnabled: 'DM\'leri izin verilen kullanıcılarla sınırla',
        allowedDmUsers: 'İzin verilen DM kullanıcıları',
        allowedDmUsersHint: 'Discord kullanıcı adları (her satıra bir tane). Sadece bu kullanıcılar bota DM gönderebilir.'
      },
      slack: {
        botToken: 'Bot Jetonu (xoxb-)',
        tokenHint: 'Mevcut jetonu korumak için boş bırakın',
        appToken: 'Uygulama Jetonu (xapp-)',
        appTokenHint: 'Soket Modu için (Faz 2). Aşama 1 için isteğe bağlıdır.',
        monitoredChannels: 'İzlenen Kanallar (geçersiz kıl)',
        monitoredChannelsHint: 'Bot, üyesi olduğu tüm kanalları otomatik olarak izler. Kapsamı kısıtlamak için buraya yalnızca kimlik ekleyin.',
        respondToMentions: "{'@'}bahislere yanıt verin",
        respondToAll: 'Tüm mesajları yanıtlayın (bahsetme filtresi dikkate alınmaz)',
        threadedReplies: 'Konu kanallarında yanıt verir (her mesaj bir konu başlatır)'
      }
    }
  } as const;

export const interfaces = {
    title: 'ការគ្រប់គ្រងចំណុចប្រទាក់',
    actions: {
      refresh: 'ធ្វើឱ្យស្រស់',
      add: 'បន្ថែមចំណុចប្រទាក់',
      edit: 'កែសម្រួល',
      enable: 'បើក',
      disable: 'បិទ',
      delete: 'លុប',
      confirmDelete: 'លុបចំណុចប្រទាក់ "{name}" ឬ?'
    },
    columns: {
      name: 'ឈ្មោះ',
      type: 'ប្រភេទ',
      module: 'ID ម៉ូឌុល',
      enabled: 'បានបើក',
      connected: 'បានភ្ជាប់',
      lastEvent: 'ព្រឹត្តិការណ៍ចុងក្រោយ',
      actions: 'សកម្មភាព'
    },
    state: {
      enabled: 'បាទ/ចាស',
      disabled: 'ទេ',
      connected: 'បានភ្ជាប់',
      disconnected: 'បានផ្តាច់'
    },
    empty: 'មិនទាន់បានកំណត់រចនាសម្ព័ន្ធចំណុចប្រទាក់នៅឡើយទេ។',
    form: {
      createTitle: 'បង្កើតចំណុចប្រទាក់',
      editTitle: 'កែសម្រួលចំណុចប្រទាក់៖ {name}',
      name: 'ឈ្មោះអាំងស្តង់',
      type: 'ប្រភេទចំណុចប្រទាក់',
      moduleId: 'ID ម៉ូឌុល',
      enabled: 'បានបើក',
      configJson: 'JSON កំណត់រចនាសម្ព័ន្ធ',
      create: 'បង្កើត',
      save: 'រក្សាទុក',
      cancel: 'បោះបង់',
      irc: {
        host: 'ម៉ាស៊ីនមេ',
        port: 'ច្រក',
        nick: 'ឈ្មោះហៅក្រៅ',
        serverPassword: 'ពាក្យសម្ងាត់ម៉ាស៊ីនមេ',
        username: 'ឈ្មោះអ្នកប្រើ',
        realname: 'ឈ្មោះពិត',
        dmOnly: 'របៀប DM តែប៉ុណ្ណោះ',
        respondToChannel: 'ឆ្លើយតបនឹងសកម្មភាពឆានែល',
        respondToDm: 'ឆ្លើយតបនឹងសារផ្ទាល់',
        respondToNotices: 'ឆ្លើយតបនឹងការជូនដំណឹង',
        allowedDmUsers: 'អ្នកប្រើ DM ដែលបានអនុញ្ញាត',
        allowedDmUsersHint: 'បញ្ជីអនុញ្ញាតស្រេចចិត្ត; បំបែកដោយក្បៀស ឬបន្ទាត់ថ្មី។',
        agentId: 'ID ភ្នាក់ងារ',
        reconnectEnabled: 'បានបើកការភ្ជាប់ឡើងវិញ',
        reconnectInitialDelay: 'ការពន្យារពេលដំបូងសម្រាប់ការភ្ជាប់ឡើងវិញ (ms)',
        reconnectMaxDelay: 'ការពន្យារពេលអតិបរមាសម្រាប់ការភ្ជាប់ឡើងវិញ (ms)'
      }
    },
    createSuccess: 'បានបង្កើតចំណុចប្រទាក់ "{name}"។',
    updateSuccess: 'បានធ្វើបច្ចុប្បន្នភាពចំណុចប្រទាក់ "{name}"។',
    deleteSuccess: 'បានលុបចំណុចប្រទាក់ "{name}"។'
  } as const;

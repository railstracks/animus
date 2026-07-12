export const providers = {
    title: 'תצורת ספק',
    actions: {
      refresh: 'רענן',
      add: 'הוסף ספק',
      test: 'מבחן',
      edit: 'ערוך',
      delete: 'מחק',
      setDefault: 'הגדר כברירת מחדל',
      confirmDelete: 'להסיר את הספק "{id}"? לא ניתן לבטל זאת.'
    },
    columns: {
      status: 'סטטוס',
      provider: 'שם',
      type: 'הקלד',
      baseUrl: 'כתובת האתר הבסיסית',
      defaultModel: 'דגם',
      default: 'ברירת מחדל',
      actions: 'פעולות'
    },
    empty: {
      title: 'אין ספקים',
      description: 'הוסף ספק LLM כדי להתחיל.'
    },
    form: {
      createTitle: 'הוסף ספק',
      editTitle: 'ערוך ספק',
      providerType: 'סוג ספק',
      instanceName: 'שם מופע',
      instanceNameHint: 'שם ייחודי עבור תצורת ספק זו.',
      baseUrl: 'כתובת האתר הבסיסית',
      defaultModel: 'דגם ברירת מחדל',
      defaultContextWindow: 'חלון ההקשר המוגדר כברירת מחדל',
      modelContextWindows: 'עקיפת הקשר לפי דגם',
      modelContextWindowsHint: 'אובייקט JSON, למשל. gpt-5.2: 200000',
      modelContextWindowsInvalid: 'עקיפות של הקשר לפי דגם חייבות להיות JSON חוקי.',
      authType: 'סוג אישור',
      apiKey: 'מפתח API',
      apiKeyPlaceholder: 'השאר ריק כדי לשמור על המפתח הנוכחי',
      authFile: 'קובץ אישור',
      oauthBrowserTitle: 'התחברות לדפדפן (קוד הרשאה)',
      oauthRedirectUri: 'כתובת URL להפניה מחדש',
      oauthStartBrowser: 'התחל את התחברות לדפדפן',
      oauthAuthorizationUrl: 'כתובת אתר להרשאה',
      oauthState: 'מדינה',
      oauthCallbackUrl: 'הדבק כתובת URL להתקשרות חוזרת',
      oauthCallbackHint: 'הדבק את כתובת האתר המלאה להתקשרות חוזרת המכילה קוד ומצב.',
      oauthCompleteBrowser: 'השלם את הכניסה לדפדפן',
      concurrency: 'מקסימום מקיפות',
      create: 'צור',
      save: 'שמור',
      cancel: 'בטל',
      modelManualHint: 'הזן את מזהה הדגם באופן ידני. רישום הדגמים אינו זמין עבור סוג ספק זה.',
      modelsLockedHint: 'שמור את הספק עם מפתח API כדי לפתוח את בחירת הדגם.',
      savedContinue: 'הספק נשמר. כעת תוכל לבחור דגם.',
      saveAndContinue: 'שמור והמשך'
    },
    testSuccess: 'ניתן להגיע לספק "{id}".',
    testFailed: 'בדיקת הספק "{id}" נכשלה',
    createSuccess: 'הספק "{id}" נוצר.',
    updateSuccess: 'הספק "{id}" עודכן.',
    deleteSuccess: 'הספק "{id}" נמחק.',
    defaultSuccess: 'ספק ברירת המחדל מוגדר ל-"{id}".',
    errors: {
      loadFailed: 'טעינת הספקים נכשלה'
    }
  } as const;

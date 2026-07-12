# Telegram Bot API
> The Bot API is an HTTP-based interface created for developers keen on building bots for Telegram.  
> To learn how to create and set up a bot, please consult our [**Introduction to Bots**](https://core.telegram.org/bots) and [**Bot FAQ**](https://core.telegram.org/bots/faq).

### [](#recent-changes)Recent changes

> Subscribe to [@BotNews](https://t.me/botnews) to be the first to know about the latest updates and join the discussion in [@BotTalk](https://t.me/bottalk)

#### [](#may-8-2026)May 8, 2026

**Bot API 10.0**

**Guest Mode**

*   Introduced support for [guest mode](about:/bots/features#guest-bots), allowing bots to receive certain messages and issue replies within chats they are not a member of.
*   Added the field _supports\_guest\_queries_ to the class [User](#user).
*   Added the fields _guest\_bot\_caller\_user_ and _guest\_bot\_caller\_chat_ to the class [Message](#message).
*   Added the field _guest\_query\_id_ to the class [Message](#message).
*   Added the field _guest\_message_ to the class [Update](#update).
*   Added the class [SentGuestMessage](#sentguestmessage) and the method [answerGuestQuery](#answerguestquery).

**Chat Management**

*   Added the field _can\_react\_to\_messages_ to the classes [ChatMemberRestricted](#chatmemberrestricted) and [ChatPermissions](#chatpermissions).
*   Added the parameter _return\_bots_ to the method [getChatAdministrators](#getchatadministrators).
*   Added the method [deleteAllMessageReactions](#deleteallmessagereactions).
*   Added the method [deleteMessageReaction](#deletemessagereaction).
*   Added the ability to see certain messages sent by other bots in groups.

**Polls**

*   Added the classes [InputMediaSticker](#inputmediasticker), [InputMediaLocation](#inputmedialocation), and [InputMediaVenue](#inputmediavenue).
*   Added the class [PollMedia](#pollmedia), representing a media in a poll.
*   Added the field _media_ to the class [Poll](#poll), allowing bots to see media in polls.
*   Added the field _explanation\_media_ to the class [Poll](#poll), allowing bots to see media in quiz explanations.
*   Added the field _media_ to the class [PollOption](#polloption), allowing bots to see media in poll options.
*   Added the class [InputPollMedia](#inputpollmedia) and the parameters _media_ and _explanation\_media_ to the method [sendPoll](#sendpoll), allowing bots to add media to polls.
*   Added the class [InputPollOptionMedia](#inputpolloptionmedia) and the field _media_ to the class [InputPollOption](#inputpolloption), allowing bots to add media to poll options.
*   Added the field _members\_only_ to the class [Poll](#poll).
*   Added the parameter _members\_only_ to the method [sendPoll](#sendpoll).
*   Added the field _country\_codes_ to the class [Poll](#poll).
*   Added the parameter _country\_codes_ to the method [sendPoll](#sendpoll).
*   Decreased the minimum number of poll options from 2 to 1.

**Live photos**

*   Added the class [LivePhoto](#livephoto), which represents a photo with a short video.
*   Added the class [InputMediaLivePhoto](#inputmedialivephoto).
*   Added the field _live\_photo_ to the classes [Message](#message) and [ExternalReplyInfo](#externalreplyinfo).
*   Added the method [sendLivePhoto](#sendlivephoto), allowing bots to send live photos.
*   Added the class [PaidMediaLivePhoto](#paidmedialivephoto), which describes a paid media with a live photo.
*   Added the class [InputPaidMediaLivePhoto](#inputpaidmedialivephoto), allowing bots to send live photos as paid media.
*   Allowed to use live photos in [sendMediaGroup](#sendmediagroup) and [editMessageMedia](#editmessagemedia),

**General**

*   Allowed [Secretary Bots](https://core.telegram.org/bots/features#secretary-bots) to manage accounts of users without a Telegram Premium subscription.
*   Added the ability to send messages to other bots via username if both bots enabled bot-to-bot communication.
*   Added the ability to reply to other bots from a business bot if the business bot enabled bot-to-bot communication.
*   Allowed bots to pass an empty text in the method [sendMessageDraft](#sendmessagedraft).
*   Added the class [BotAccessSettings](#botaccesssettings) and the method [getManagedBotAccessSettings](#getmanagedbotaccesssettings).
*   Added the method [setManagedBotAccessSettings](#setmanagedbotaccesssettings).
*   Added the method [getUserPersonalChatMessages](#getuserpersonalchatmessages).

#### [](#april-3-2026)April 3, 2026

**Bot API 9.6**

**Managed Bots**

*   Added the field _can\_manage\_bots_ to the class [User](#user).
*   Added the class [KeyboardButtonRequestManagedBot](#keyboardbuttonrequestmanagedbot) and the field _request\_managed\_bot_ to the class [KeyboardButton](#keyboardbutton).
*   Added the class [ManagedBotCreated](#managedbotcreated) and the field _managed\_bot\_created_ to the class [Message](#message).
*   Added updates about the creation of managed bots and the change of their token, represented by the class [ManagedBotUpdated](#managedbotupdated) and the field _managed\_bot_ in the class [Update](#update).
*   Added the methods [getManagedBotToken](#getmanagedbottoken) and [replaceManagedBotToken](#replacemanagedbottoken).
*   Added the class [PreparedKeyboardButton](#preparedkeyboardbutton) and the method [savePreparedKeyboardButton](#savepreparedkeyboardbutton), allowing bots to request users, chats and managed bots from Mini Apps.
*   Added the method _requestChat_ to the class [WebApp](about:/bots/webapps#initializing-mini-apps).
*   Added support for `https://t.me/newbot/{manager_bot_username}/{suggested_bot_username}[?name={suggested_bot_name}]` links, allowing bots to request the creation of a managed bot via a link.

**Polls**

*   Added support for quizzes with multiple correct answers.
*   Replaced the field _correct\_option\_id_ with the field _correct\_option\_ids_ in the class [Poll](#poll).
*   Replaced the parameter _correct\_option\_id_ with the parameter _correct\_option\_ids_ in the method [sendPoll](#sendpoll).
*   Allowed to pass _allows\_multiple\_answers_ for quizzes in the method [sendPoll](#sendpoll).
*   Increased the maximum time for automatic poll closure to 2628000 seconds.
*   Added the field _allows\_revoting_ to the class [Poll](#poll).
*   Added the parameter _allows\_revoting_ to the method [sendPoll](#sendpoll).
*   Added the parameter _shuffle\_options_ to the method [sendPoll](#sendpoll).
*   Added the parameter _allow\_adding\_options_ to the method [sendPoll](#sendpoll).
*   Added the parameter _hide\_results\_until\_closes_ to the method [sendPoll](#sendpoll).
*   Added the fields _description_ and _description\_entities_ to the class [Poll](#poll).
*   Added the parameters _description_, _description\_parse\_mode_, and _description\_entities_ to the method [sendPoll](#sendpoll).
*   Added the field _persistent\_id_ to the class [PollOption](#polloption), representing a persistent identifier for the option.
*   Added the field _option\_persistent\_ids_ to the class [PollAnswer](#pollanswer).
*   Added the fields _added\_by\_user_ and _added\_by\_chat_ to the class [PollOption](#polloption), denoting the user and the chat which added the option.
*   Added the field _addition\_date_ to the class [PollOption](#polloption), describing the date when the option was added.
*   Added the class [PollOptionAdded](#polloptionadded) and the field _poll\_option\_added_ to the class [Message](#message).
*   Added the class [PollOptionDeleted](#polloptiondeleted) and the field _poll\_option\_deleted_ to the class [Message](#message).
*   Added the field _poll\_option\_id_ to the class [ReplyParameters](#replyparameters), allowing bots to reply to a specific poll option.
*   Added the field _reply\_to\_poll\_option\_id_ to the class [Message](#message).
*   Allowed “date\_time” entities in [checklist](#inputchecklist) title, [checklist task](#inputchecklisttask) text, [TextQuote](#textquote), [ReplyParameters](#replyparameters) quote, [sendGift](#sendgift), and [giftPremiumSubscription](#giftpremiumsubscription).

#### [](#march-1-2026)March 1, 2026

**Bot API 9.5**

*   Added the [MessageEntity](#messageentity) type “date\_time”, allowing bots to show a formatted date and time to the user.
*   Allowed all bots to use the method [sendMessageDraft](#sendmessagedraft).
*   Added the field _tag_ to the classes [ChatMemberMember](#chatmembermember) and [ChatMemberRestricted](#chatmemberrestricted).
*   Added the method [setChatMemberTag](#setchatmembertag).
*   Added the field _can\_edit\_tag_ to the classes [ChatMemberRestricted](#chatmemberrestricted) and [ChatPermissions](#chatpermissions).
*   Added the field _can\_manage\_tags_ to the classes [ChatMemberAdministrator](#chatmemberadministrator) and [ChatAdministratorRights](#chatadministratorrights).
*   Added the parameter _can\_manage\_tags_ to the method [promoteChatMember](#promotechatmember).
*   Added the field _sender\_tag_ to the class [Message](#message).
*   Added the field _iconCustomEmojiId_ to the class [BottomButton](about:/bots/webapps#bottombutton).

#### [](#february-9-2026)February 9, 2026

**Bot API 9.4**

*   Allowed bots to use custom emoji in messages directly sent by the bot to private, group and supergroup chats if the owner of the bot has a Telegram Premium subscription.
*   Allowed bots to create topics in private chats using the method [createForumTopic](#createforumtopic).
*   Allowed bots to prevent users from creating and deleting topics in private chats through a new setting in the [@BotFather](https://t.me/BotFather) Mini App.
*   Added the field _allows\_users\_to\_create\_topics_ to the class [User](#user).
*   Added the field _icon\_custom\_emoji\_id_ to the classes [KeyboardButton](#keyboardbutton) and [InlineKeyboardButton](#inlinekeyboardbutton), allowing bots to show a custom emoji on buttons if they are able to use custom emoji in the message.
*   Added the field _style_ to the classes [KeyboardButton](#keyboardbutton) and [InlineKeyboardButton](#inlinekeyboardbutton), allowing bots to change the color of buttons.
*   Added the class [ChatOwnerLeft](#chatownerleft) and the field _chat\_owner\_left_ to the class [Message](#message).
*   Added the class [ChatOwnerChanged](#chatownerchanged) and the field _chat\_owner\_changed_ to the class [Message](#message).
*   Added the methods [setMyProfilePhoto](#setmyprofilephoto) and [removeMyProfilePhoto](#removemyprofilephoto), allowing bots to manage their profile picture.
*   Added the class [VideoQuality](#videoquality) and the field _qualities_ to the class [Video](#video) allowing bots to get information about other available qualities of a video.
*   Added the field _first\_profile\_audio_ to the class [ChatFullInfo](#chatfullinfo).
*   Added the class [UserProfileAudios](#userprofileaudios) and the method [getUserProfileAudios](#getuserprofileaudios), allowing bots to fetch a list of audios added to the profile of a user.
*   Added the field _rarity_ to the class [UniqueGiftModel](#uniquegiftmodel).
*   Added the field _is\_burned_ to the class [UniqueGift](#uniquegift).

**[See earlier changes »](https://core.telegram.org/bots/api-changelog)**

### [](#authorizing-your-bot)Authorizing your bot

Each bot is given a unique authentication token [when it is created](about:/bots/features#botfather). The token looks something like `123456:ABC-DEF1234ghIkl-zyx57W2v1u123ew11`, but we'll use simply **<token>** in this document instead. You can learn about obtaining tokens and generating new ones in [this document](about:/bots/features#botfather).

### [](#making-requests)Making requests

All queries to the Telegram Bot API must be served over HTTPS and need to be presented in this form: `https://api.telegram.org/bot<token>/METHOD_NAME`. Like this for example:

```
https://api.telegram.org/bot123456:ABC-DEF1234ghIkl-zyx57W2v1u123ew11/getMe
```


We support **GET** and **POST** HTTP methods. We support four ways of passing parameters in Bot API requests:

*   [URL query string](https://en.wikipedia.org/wiki/Query_string)
*   application/x-www-form-urlencoded
*   application/json (except for uploading files)
*   multipart/form-data (use to upload files)

The response contains a JSON object, which always has a Boolean field 'ok' and may have an optional String field 'description' with a human-readable description of the result. If 'ok' equals _True_, the request was successful and the result of the query can be found in the 'result' field. In case of an unsuccessful request, 'ok' equals false and the error is explained in the 'description'. An Integer 'error\_code' field is also returned, but its contents are subject to change in the future. Some errors may also have an optional field 'parameters' of the type [ResponseParameters](#responseparameters), which can help to automatically handle the error.

*   All methods in the Bot API are case-insensitive.
*   All queries must be made using UTF-8.

#### [](#making-requests-when-getting-updates)Making requests when getting updates

If you're using [**webhooks**](#getting-updates), you can perform a request to the Bot API while sending an answer to the webhook. Use either _application/json_ or _application/x-www-form-urlencoded_ or _multipart/form-data_ response content type for passing parameters. Specify the method to be invoked in the _method_ parameter of the request. It's not possible to know that such a request was successful or get its result.

> Please see our [FAQ](about:/bots/faq#how-can-i-make-requests-in-response-to-updates) for examples.

### [](#using-a-local-bot-api-server)Using a Local Bot API Server

The Bot API server source code is available at [telegram-bot-api](https://github.com/tdlib/telegram-bot-api). You can run it locally and send the requests to your own server instead of `https://api.telegram.org`. If you switch to a local Bot API server, your bot will be able to:

*   Download files without a size limit.
*   Upload files up to 2000 MB.
*   Upload files using their local path and [the file URI scheme](https://en.wikipedia.org/wiki/File_URI_scheme).
*   Use an HTTP URL for the webhook.
*   Use any local IP address for the webhook.
*   Use any port for the webhook.
*   Set _max\_webhook\_connections_ up to 100000.
*   Receive the absolute local path as a value of the _file\_path_ field without the need to download the file after a [getFile](#getfile) request.

#### [](#do-i-need-a-local-bot-api-server)Do I need a Local Bot API Server

The majority of bots will be OK with the default configuration, running on our servers. But if you feel that you need one of [these features](#using-a-local-bot-api-server), you're welcome to switch to your own at any time.

### [](#getting-updates)Getting updates

There are two mutually exclusive ways of receiving updates for your bot - the [getUpdates](#getupdates) method on one hand and [webhooks](#setwebhook) on the other. Incoming updates are stored on the server until the bot receives them either way, but they will not be kept longer than 24 hours.

Regardless of which option you choose, you will receive JSON-serialized [Update](#update) objects as a result.

#### [](#update)Update

This [object](#available-types) represents an incoming update.  
At most **one** of the optional fields can be present in any given update.



* Field: update_id
  * Type: Integer
  * Description: The update's unique identifier. Update identifiers start from a certain positive number and increase sequentially. This identifier becomes especially handy if you're using webhooks, since it allows you to ignore repeated updates or to restore the correct update sequence, should they get out of order. If there are no new updates for at least a week, then identifier of the next update will be chosen randomly instead of sequentially.
* Field: message
  * Type: Message
  * Description: Optional. New incoming message of any kind - text, photo, sticker, etc.
* Field: edited_message
  * Type: Message
  * Description: Optional. New version of a message that is known to the bot and was edited. This update may at times be triggered by changes to message fields that are either unavailable or not actively used by your bot.
* Field: channel_post
  * Type: Message
  * Description: Optional. New incoming channel post of any kind - text, photo, sticker, etc.
* Field: edited_channel_post
  * Type: Message
  * Description: Optional. New version of a channel post that is known to the bot and was edited. This update may at times be triggered by changes to message fields that are either unavailable or not actively used by your bot.
* Field: business_connection
  * Type: BusinessConnection
  * Description: Optional. The bot was connected to or disconnected from a business account, or a user edited an existing connection with the bot
* Field: business_message
  * Type: Message
  * Description: Optional. New message from a connected business account
* Field: edited_business_message
  * Type: Message
  * Description: Optional. New version of a message from a connected business account
* Field: deleted_business_messages
  * Type: BusinessMessagesDeleted
  * Description: Optional. Messages were deleted from a connected business account
* Field: guest_message
  * Type: Message
  * Description: Optional. New guest message. The bot can use the field Message.guest_query_id and the method answerGuestQuery to send a message in response.
* Field: message_reaction
  * Type: MessageReactionUpdated
  * Description: Optional. A reaction to a message was changed by a user. The bot must be an administrator in the chat and must explicitly specify "message_reaction" in the list of allowed_updates to receive these updates. The update isn't received for reactions set by bots.
* Field: message_reaction_count
  * Type: MessageReactionCountUpdated
  * Description: Optional. Reactions to a message with anonymous reactions were changed. The bot must be an administrator in the chat and must explicitly specify "message_reaction_count" in the list of allowed_updates to receive these updates. The updates are grouped and can be sent with delay up to a few minutes.
* Field: inline_query
  * Type: InlineQuery
  * Description: Optional. New incoming inline query
* Field: chosen_inline_result
  * Type: ChosenInlineResult
  * Description: Optional. The result of an inline query that was chosen by a user and sent to their chat partner. Please see our documentation on the feedback collecting for details on how to enable these updates for your bot.
* Field: callback_query
  * Type: CallbackQuery
  * Description: Optional. New incoming callback query
* Field: shipping_query
  * Type: ShippingQuery
  * Description: Optional. New incoming shipping query. Only for invoices with flexible price.
* Field: pre_checkout_query
  * Type: PreCheckoutQuery
  * Description: Optional. New incoming pre-checkout query. Contains full information about checkout.
* Field: purchased_paid_media
  * Type: PaidMediaPurchased
  * Description: Optional. A user purchased paid media with a non-empty payload sent by the bot in a non-channel chat
* Field: poll
  * Type: Poll
  * Description: Optional. New poll state. Bots receive only updates about manually stopped polls and polls, which are sent by the bot.
* Field: poll_answer
  * Type: PollAnswer
  * Description: Optional. A user changed their answer in a non-anonymous poll. Bots receive new votes only in polls that were sent by the bot itself.
* Field: my_chat_member
  * Type: ChatMemberUpdated
  * Description: Optional. The bot's chat member status was updated in a chat. For private chats, this update is received only when the bot is blocked or unblocked by the user.
* Field: chat_member
  * Type: ChatMemberUpdated
  * Description: Optional. A chat member's status was updated in a chat. The bot must be an administrator in the chat and must explicitly specify "chat_member" in the list of allowed_updates to receive these updates.
* Field: chat_join_request
  * Type: ChatJoinRequest
  * Description: Optional. A request to join the chat has been sent. The bot must have the can_invite_users administrator right in the chat to receive these updates.
* Field: chat_boost
  * Type: ChatBoostUpdated
  * Description: Optional. A chat boost was added or changed. The bot must be an administrator in the chat to receive these updates.
* Field: removed_chat_boost
  * Type: ChatBoostRemoved
  * Description: Optional. A boost was removed from a chat. The bot must be an administrator in the chat to receive these updates.
* Field: managed_bot
  * Type: ManagedBotUpdated
  * Description: Optional. A new bot was created to be managed by the bot, or token or owner of a managed bot was changed


#### [](#getupdates)getUpdates

Use this method to receive incoming updates using long polling ([wiki](https://en.wikipedia.org/wiki/Push_technology#Long_polling)). Returns an Array of [Update](#update) objects.



* Parameter: offset
  * Type: Integer
  * Required: Optional
  * Description: Identifier of the first update to be returned. Must be greater by one than the highest among the identifiers of previously received updates. By default, updates starting with the earliest unconfirmed update are returned. An update is considered confirmed as soon as getUpdates is called with an offset higher than its update_id. The negative offset can be specified to retrieve updates starting from -offset update from the end of the updates queue. All previous updates will be forgotten.
* Parameter: limit
  * Type: Integer
  * Required: Optional
  * Description: Limits the number of updates to be retrieved. Values between 1-100 are accepted. Defaults to 100.
* Parameter: timeout
  * Type: Integer
  * Required: Optional
  * Description: Timeout in seconds for long polling. Defaults to 0, i.e. usual short polling. Should be positive, short polling should be used for testing purposes only.
* Parameter: allowed_updates
  * Type: Array of String
  * Required: Optional
  * Description: A JSON-serialized list of the update types you want your bot to receive. For example, specify ["message", "edited_channel_post", "callback_query"] to only receive updates of these types. See Update for a complete list of available update types. Specify an empty list to receive all update types except chat_member, message_reaction, and message_reaction_count (default). If not specified, the previous setting will be used.Please note that this parameter doesn't affect updates created before the call to getUpdates, so unwanted updates may be received for a short period of time.


> **Notes**  
> **1.** This method will not work if an outgoing webhook is set up.  
> **2.** In order to avoid getting duplicate updates, recalculate _offset_ after each server response.

#### [](#setwebhook)setWebhook

Use this method to specify a URL and receive incoming updates via an outgoing webhook. Whenever there is an update for the bot, we will send an HTTPS POST request to the specified URL, containing a JSON-serialized [Update](#update). In case of an unsuccessful request (a request with response [HTTP status code](https://en.wikipedia.org/wiki/List_of_HTTP_status_codes) different from `2XY`), we will repeat the request and give up after a reasonable amount of attempts. Returns _True_ on success.

If you'd like to make sure that the webhook was set by you, you can specify secret data in the parameter _secret\_token_. If specified, the request will contain a header “X-Telegram-Bot-Api-Secret-Token” with the secret token as content.



* Parameter: url
  * Type: String
  * Required: Yes
  * Description: HTTPS URL to send updates to. Use an empty string to remove webhook integration.
* Parameter: certificate
  * Type: InputFile
  * Required: Optional
  * Description: Upload your public key certificate so that the root certificate in use can be checked. See our self-signed guide for details.
* Parameter: ip_address
  * Type: String
  * Required: Optional
  * Description: The fixed IP address which will be used to send webhook requests instead of the IP address resolved through DNS
* Parameter: max_connections
  * Type: Integer
  * Required: Optional
  * Description: The maximum allowed number of simultaneous HTTPS connections to the webhook for update delivery, 1-100. Defaults to 40. Use lower values to limit the load on your bot's server, and higher values to increase your bot's throughput.
* Parameter: allowed_updates
  * Type: Array of String
  * Required: Optional
  * Description: A JSON-serialized list of the update types you want your bot to receive. For example, specify ["message", "edited_channel_post", "callback_query"] to only receive updates of these types. See Update for a complete list of available update types. Specify an empty list to receive all update types except chat_member, message_reaction, and message_reaction_count (default). If not specified, the previous setting will be used.Please note that this parameter doesn't affect updates created before the call to the setWebhook, so unwanted updates may be received for a short period of time.
* Parameter: drop_pending_updates
  * Type: Boolean
  * Required: Optional
  * Description: Pass True to drop all pending updates
* Parameter: secret_token
  * Type: String
  * Required: Optional
  * Description: A secret token to be sent in a header “X-Telegram-Bot-Api-Secret-Token” in every webhook request, 1-256 characters. Only characters A-Z, a-z, 0-9, _ and - are allowed. The header is useful to ensure that the request comes from a webhook set by you.


> **Notes**  
> **1.** You will not be able to receive updates using [getUpdates](#getupdates) for as long as an outgoing webhook is set up.  
> **2.** To use a self-signed certificate, you need to upload your [public key certificate](https://core.telegram.org/bots/self-signed) using _certificate_ parameter. Please upload as InputFile, sending a String will not work.  
> **3.** Ports currently supported _for webhooks_: **443, 80, 88, 8443**.
> 
> If you're having any trouble setting up webhooks, please check out this [amazing guide to webhooks](https://core.telegram.org/bots/webhooks).

#### [](#deletewebhook)deleteWebhook

Use this method to remove webhook integration if you decide to switch back to [getUpdates](#getupdates). Returns _True_ on success.


|Parameter           |Type   |Required|Description                          |
|--------------------|-------|--------|-------------------------------------|
|drop_pending_updates|Boolean|Optional|Pass True to drop all pending updates|


#### [](#getwebhookinfo)getWebhookInfo

Use this method to get current webhook status. Requires no parameters. On success, returns a [WebhookInfo](#webhookinfo) object. If the bot is using [getUpdates](#getupdates), will return an object with the _url_ field empty.

#### [](#webhookinfo)WebhookInfo

Describes the current status of a webhook.



* Field: url
  * Type: String
  * Description: Webhook URL, may be empty if webhook is not set up
* Field: has_custom_certificate
  * Type: Boolean
  * Description: True, if a custom certificate was provided for webhook certificate checks
* Field: pending_update_count
  * Type: Integer
  * Description: Number of updates awaiting delivery
* Field: ip_address
  * Type: String
  * Description: Optional. Currently used webhook IP address
* Field: last_error_date
  * Type: Integer
  * Description: Optional. Unix time for the most recent error that happened when trying to deliver an update via webhook
* Field: last_error_message
  * Type: String
  * Description: Optional. Error message in human-readable format for the most recent error that happened when trying to deliver an update via webhook
* Field: last_synchronization_error_date
  * Type: Integer
  * Description: Optional. Unix time of the most recent error that happened when trying to synchronize available updates with Telegram datacenters
* Field: max_connections
  * Type: Integer
  * Description: Optional. The maximum allowed number of simultaneous HTTPS connections to the webhook for update delivery
* Field: allowed_updates
  * Type: Array of String
  * Description: Optional. A list of update types the bot is subscribed to. Defaults to all update types except chat_member, message_reaction, and message_reaction_count.


### [](#available-types)Available types

All types used in the Bot API responses are represented as JSON-objects.

It is safe to use 32-bit signed integers for storing all **Integer** fields unless otherwise noted.

> **Optional** fields may be not returned when irrelevant.

#### [](#user)User

This object represents a Telegram user or bot.



* Field: id
  * Type: Integer
  * Description: Unique identifier for this user or bot. This number may have more than 32 significant bits and some programming languages may have difficulty/silent defects in interpreting it. But it has at most 52 significant bits, so a 64-bit integer or double-precision float type are safe for storing this identifier.
* Field: is_bot
  * Type: Boolean
  * Description: True, if this user is a bot
* Field: first_name
  * Type: String
  * Description: User's or bot's first name
* Field: last_name
  * Type: String
  * Description: Optional. User's or bot's last name
* Field: username
  * Type: String
  * Description: Optional. User's or bot's username
* Field: language_code
  * Type: String
  * Description: Optional. IETF language tag of the user's language
* Field: is_premium
  * Type: True
  * Description: Optional. True, if this user is a Telegram Premium user
* Field: added_to_attachment_menu
  * Type: True
  * Description: Optional. True, if this user added the bot to the attachment menu
* Field: can_join_groups
  * Type: Boolean
  * Description: Optional. True, if the bot can be invited to groups. Returned only in getMe.
* Field: can_read_all_group_messages
  * Type: Boolean
  * Description: Optional. True, if privacy mode is disabled for the bot. Returned only in getMe.
* Field: supports_guest_queries
  * Type: Boolean
  * Description: Optional. True, if the bot supports guest queries from chats it is not a member of. Returned only in getMe.
* Field: supports_inline_queries
  * Type: Boolean
  * Description: Optional. True, if the bot supports inline queries. Returned only in getMe.
* Field: can_connect_to_business
  * Type: Boolean
  * Description: Optional. True, if the bot can be connected to a user account to manage it. Returned only in getMe.
* Field: has_main_web_app
  * Type: Boolean
  * Description: Optional. True, if the bot has a main Web App. Returned only in getMe.
* Field: has_topics_enabled
  * Type: Boolean
  * Description: Optional. True, if the bot has forum topic mode enabled in private chats. Returned only in getMe.
* Field: allows_users_to_create_topics
  * Type: Boolean
  * Description: Optional. True, if the bot allows users to create and delete topics in private chats. Returned only in getMe.
* Field: can_manage_bots
  * Type: Boolean
  * Description: Optional. True, if other bots can be created to be controlled by the bot. Returned only in getMe.


#### [](#chat)Chat

This object represents a chat.



* Field: id
  * Type: Integer
  * Description: Unique identifier for this chat. This number may have more than 32 significant bits and some programming languages may have difficulty/silent defects in interpreting it. But it has at most 52 significant bits, so a signed 64-bit integer or double-precision float type are safe for storing this identifier.
* Field: type
  * Type: String
  * Description: Type of the chat, can be either “private”, “group”, “supergroup” or “channel”
* Field: title
  * Type: String
  * Description: Optional. Title, for supergroups, channels and group chats
* Field: username
  * Type: String
  * Description: Optional. Username, for private chats, supergroups and channels if available
* Field: first_name
  * Type: String
  * Description: Optional. First name of the other party in a private chat
* Field: last_name
  * Type: String
  * Description: Optional. Last name of the other party in a private chat
* Field: is_forum
  * Type: True
  * Description: Optional. True, if the supergroup chat is a forum (has topics enabled)
* Field: is_direct_messages
  * Type: True
  * Description: Optional. True, if the chat is the direct messages chat of a channel


#### [](#chatfullinfo)ChatFullInfo

This object contains full information about a chat.



* Field: id
  * Type: Integer
  * Description: Unique identifier for this chat. This number may have more than 32 significant bits and some programming languages may have difficulty/silent defects in interpreting it. But it has at most 52 significant bits, so a signed 64-bit integer or double-precision float type are safe for storing this identifier.
* Field: type
  * Type: String
  * Description: Type of the chat, can be either “private”, “group”, “supergroup” or “channel”
* Field: title
  * Type: String
  * Description: Optional. Title, for supergroups, channels and group chats
* Field: username
  * Type: String
  * Description: Optional. Username, for private chats, supergroups and channels if available
* Field: first_name
  * Type: String
  * Description: Optional. First name of the other party in a private chat
* Field: last_name
  * Type: String
  * Description: Optional. Last name of the other party in a private chat
* Field: is_forum
  * Type: True
  * Description: Optional. True, if the supergroup chat is a forum (has topics enabled)
* Field: is_direct_messages
  * Type: True
  * Description: Optional. True, if the chat is the direct messages chat of a channel
* Field: accent_color_id
  * Type: Integer
  * Description: Identifier of the accent color for the chat name and backgrounds of the chat photo, reply header, and link preview. See accent colors for more details.
* Field: max_reaction_count
  * Type: Integer
  * Description: The maximum number of reactions that can be set on a message in the chat
* Field: photo
  * Type: ChatPhoto
  * Description: Optional. Chat photo
* Field: active_usernames
  * Type: Array of String
  * Description: Optional. If non-empty, the list of all active chat usernames; for private chats, supergroups and channels
* Field: birthdate
  * Type: Birthdate
  * Description: Optional. For private chats, the date of birth of the user
* Field: business_intro
  * Type: BusinessIntro
  * Description: Optional. For private chats with business accounts, the intro of the business
* Field: business_location
  * Type: BusinessLocation
  * Description: Optional. For private chats with business accounts, the location of the business
* Field: business_opening_hours
  * Type: BusinessOpeningHours
  * Description: Optional. For private chats with business accounts, the opening hours of the business
* Field: personal_chat
  * Type: Chat
  * Description: Optional. For private chats, the personal channel of the user
* Field: parent_chat
  * Type: Chat
  * Description: Optional. Information about the corresponding channel chat; for direct messages chats only
* Field: available_reactions
  * Type: Array of ReactionType
  * Description: Optional. List of available reactions allowed in the chat. If omitted, then all emoji reactions are allowed.
* Field: background_custom_emoji_id
  * Type: String
  * Description: Optional. Custom emoji identifier of the emoji chosen by the chat for the reply header and link preview background
* Field: profile_accent_color_id
  * Type: Integer
  * Description: Optional. Identifier of the accent color for the chat's profile background. See profile accent colors for more details.
* Field: profile_background_custom_emoji_id
  * Type: String
  * Description: Optional. Custom emoji identifier of the emoji chosen by the chat for its profile background
* Field: emoji_status_custom_emoji_id
  * Type: String
  * Description: Optional. Custom emoji identifier of the emoji status of the chat or the other party in a private chat
* Field: emoji_status_expiration_date
  * Type: Integer
  * Description: Optional. Expiration date of the emoji status of the chat or the other party in a private chat, in Unix time, if any
* Field: bio
  * Type: String
  * Description: Optional. Bio of the other party in a private chat
* Field: has_private_forwards
  * Type: True
  * Description: Optional. True, if privacy settings of the other party in the private chat allows to use tg://user?id=<user_id> links only in chats with the user
* Field: has_restricted_voice_and_video_messages
  * Type: True
  * Description: Optional. True, if the privacy settings of the other party restrict sending voice and video note messages in the private chat
* Field: join_to_send_messages
  * Type: True
  * Description: Optional. True, if users need to join the supergroup before they can send messages
* Field: join_by_request
  * Type: True
  * Description: Optional. True, if all users directly joining the supergroup without using an invite link need to be approved by supergroup administrators
* Field: description
  * Type: String
  * Description: Optional. Description, for groups, supergroups and channel chats
* Field: invite_link
  * Type: String
  * Description: Optional. Primary invite link, for groups, supergroups and channel chats
* Field: pinned_message
  * Type: Message
  * Description: Optional. The most recent pinned message (by sending date)
* Field: permissions
  * Type: ChatPermissions
  * Description: Optional. Default chat member permissions, for groups and supergroups
* Field: accepted_gift_types
  * Type: AcceptedGiftTypes
  * Description: Information about types of gifts that are accepted by the chat or by the corresponding user for private chats
* Field: can_send_paid_media
  * Type: True
  * Description: Optional. True, if paid media messages can be sent or forwarded to the channel chat. The field is available only for channel chats.
* Field: slow_mode_delay
  * Type: Integer
  * Description: Optional. For supergroups, the minimum allowed delay between consecutive messages sent by each unprivileged user; in seconds
* Field: unrestrict_boost_count
  * Type: Integer
  * Description: Optional. For supergroups, the minimum number of boosts that a non-administrator user needs to add in order to ignore slow mode and chat permissions
* Field: message_auto_delete_time
  * Type: Integer
  * Description: Optional. The time after which all messages sent to the chat will be automatically deleted; in seconds
* Field: has_aggressive_anti_spam_enabled
  * Type: True
  * Description: Optional. True, if aggressive anti-spam checks are enabled in the supergroup. The field is only available to chat administrators.
* Field: has_hidden_members
  * Type: True
  * Description: Optional. True, if non-administrators can only get the list of bots and administrators in the chat
* Field: has_protected_content
  * Type: True
  * Description: Optional. True, if messages from the chat can't be forwarded to other chats
* Field: has_visible_history
  * Type: True
  * Description: Optional. True, if new chat members will have access to old messages; available only to chat administrators
* Field: sticker_set_name
  * Type: String
  * Description: Optional. For supergroups, name of the group sticker set
* Field: can_set_sticker_set
  * Type: True
  * Description: Optional. True, if the bot can change the group sticker set
* Field: custom_emoji_sticker_set_name
  * Type: String
  * Description: Optional. For supergroups, the name of the group's custom emoji sticker set. Custom emoji from this set can be used by all users and bots in the group.
* Field: linked_chat_id
  * Type: Integer
  * Description: Optional. Unique identifier for the linked chat, i.e. the discussion group identifier for a channel and vice versa; for supergroups and channel chats. This identifier may be greater than 32 bits and some programming languages may have difficulty/silent defects in interpreting it. But it is smaller than 52 bits, so a signed 64 bit integer or double-precision float type are safe for storing this identifier.
* Field: location
  * Type: ChatLocation
  * Description: Optional. For supergroups, the location to which the supergroup is connected
* Field: rating
  * Type: UserRating
  * Description: Optional. For private chats, the rating of the user if any
* Field: first_profile_audio
  * Type: Audio
  * Description: Optional. For private chats, the first audio added to the profile of the user
* Field: unique_gift_colors
  * Type: UniqueGiftColors
  * Description: Optional. The color scheme based on a unique gift that must be used for the chat's name, message replies and link previews
* Field: paid_message_star_count
  * Type: Integer
  * Description: Optional. The number of Telegram Stars a general user has to pay to send a message to the chat


#### [](#message)Message

This object represents a message.



* Field: message_id
  * Type: Integer
  * Description: Unique message identifier inside this chat. In specific instances (e.g., message containing a video sent to a big chat), the server might automatically schedule a message instead of sending it immediately. In such cases, this field will be 0 and the relevant message will be unusable until it is actually sent.
* Field: message_thread_id
  * Type: Integer
  * Description: Optional. Unique identifier of a message thread or forum topic to which the message belongs; for supergroups and private chats only
* Field: direct_messages_topic
  * Type: DirectMessagesTopic
  * Description: Optional. Information about the direct messages chat topic that contains the message
* Field: from
  * Type: User
  * Description: Optional. Sender of the message; may be empty for messages sent to channels. For backward compatibility, if the message was sent on behalf of a chat, the field contains a fake sender user in non-channel chats.
* Field: sender_chat
  * Type: Chat
  * Description: Optional. Sender of the message when sent on behalf of a chat. For example, the supergroup itself for messages sent by its anonymous administrators or a linked channel for messages automatically forwarded to the channel's discussion group. For backward compatibility, if the message was sent on behalf of a chat, the field from contains a fake sender user in non-channel chats.
* Field: sender_boost_count
  * Type: Integer
  * Description: Optional. If the sender of the message boosted the chat, the number of boosts added by the user
* Field: sender_business_bot
  * Type: User
  * Description: Optional. The bot that actually sent the message on behalf of the business account. Available only for outgoing messages sent on behalf of the connected business account.
* Field: sender_tag
  * Type: String
  * Description: Optional. Tag or custom title of the sender of the message; for supergroups only
* Field: date
  * Type: Integer
  * Description: Date the message was sent in Unix time. It is always a positive number, representing a valid date.
* Field: guest_query_id
  * Type: String
  * Description: Optional. The unique identifier for the guest query. Use this identifier with the method answerGuestQuery to send a response message. If non-empty, the message belongs to the chat where the guest bot was summoned, which may not coincide with other existing bot chats sharing the same identifier.
* Field: business_connection_id
  * Type: String
  * Description: Optional. Unique identifier of the business connection from which the message was received. If non-empty, the message belongs to a chat of the corresponding business account that is independent from any potential bot chat which might share the same identifier.
* Field: chat
  * Type: Chat
  * Description: Chat the message belongs to
* Field: forward_origin
  * Type: MessageOrigin
  * Description: Optional. Information about the original message for forwarded messages
* Field: is_topic_message
  * Type: True
  * Description: Optional. True, if the message is sent to a topic in a forum supergroup or a private chat with the bot
* Field: is_automatic_forward
  * Type: True
  * Description: Optional. True, if the message is a channel post that was automatically forwarded to the connected discussion group
* Field: reply_to_message
  * Type: Message
  * Description: Optional. For replies in the same chat and message thread, the original message. Note that the Message object in this field will not contain further reply_to_message fields even if it itself is a reply.
* Field: external_reply
  * Type: ExternalReplyInfo
  * Description: Optional. Information about the message that is being replied to, which may come from another chat or forum topic
* Field: quote
  * Type: TextQuote
  * Description: Optional. For replies that quote part of the original message, the quoted part of the message
* Field: reply_to_story
  * Type: Story
  * Description: Optional. For replies to a story, the original story
* Field: reply_to_checklist_task_id
  * Type: Integer
  * Description: Optional. Identifier of the specific checklist task that is being replied to
* Field: reply_to_poll_option_id
  * Type: String
  * Description: Optional. Persistent identifier of the specific poll option that is being replied to
* Field: via_bot
  * Type: User
  * Description: Optional. Bot through which the message was sent
* Field: guest_bot_caller_user
  * Type: User
  * Description: Optional. For a message sent by a guest bot, this is the user whose original message triggered the bot's response
* Field: guest_bot_caller_chat
  * Type: Chat
  * Description: Optional. For a message sent by a guest bot, this is the chat whose original message triggered the bot's response
* Field: edit_date
  * Type: Integer
  * Description: Optional. Date the message was last edited in Unix time
* Field: has_protected_content
  * Type: True
  * Description: Optional. True, if the message can't be forwarded
* Field: is_from_offline
  * Type: True
  * Description: Optional. True, if the message was sent by an implicit action, for example, as an away or a greeting business message, or as a scheduled message
* Field: is_paid_post
  * Type: True
  * Description: Optional. True, if the message is a paid post. Note that such posts must not be deleted for 24 hours to receive the payment and can't be edited.
* Field: media_group_id
  * Type: String
  * Description: Optional. The unique identifier inside this chat of a media message group this message belongs to
* Field: author_signature
  * Type: String
  * Description: Optional. Signature of the post author for messages in channels, or the custom title of an anonymous group administrator
* Field: paid_star_count
  * Type: Integer
  * Description: Optional. The number of Telegram Stars that were paid by the sender of the message to send it
* Field: text
  * Type: String
  * Description: Optional. For text messages, the actual UTF-8 text of the message
* Field: entities
  * Type: Array of MessageEntity
  * Description: Optional. For text messages, special entities like usernames, URLs, bot commands, etc. that appear in the text
* Field: link_preview_options
  * Type: LinkPreviewOptions
  * Description: Optional. Options used for link preview generation for the message, if it is a text message and link preview options were changed
* Field: suggested_post_info
  * Type: SuggestedPostInfo
  * Description: Optional. Information about suggested post parameters if the message is a suggested post in a channel direct messages chat. If the message is an approved or declined suggested post, then it can't be edited.
* Field: effect_id
  * Type: String
  * Description: Optional. Unique identifier of the message effect added to the message
* Field: animation
  * Type: Animation
  * Description: Optional. Message is an animation, information about the animation. For backward compatibility, when this field is set, the document field will also be set.
* Field: audio
  * Type: Audio
  * Description: Optional. Message is an audio file, information about the file
* Field: document
  * Type: Document
  * Description: Optional. Message is a general file, information about the file
* Field: live_photo
  * Type: LivePhoto
  * Description: Optional. Message is a live photo, information about the live photo. For backward compatibility, when this field is set, the photo field will also be set.
* Field: paid_media
  * Type: PaidMediaInfo
  * Description: Optional. Message contains paid media; information about the paid media
* Field: photo
  * Type: Array of PhotoSize
  * Description: Optional. Message is a photo, available sizes of the photo
* Field: sticker
  * Type: Sticker
  * Description: Optional. Message is a sticker, information about the sticker
* Field: story
  * Type: Story
  * Description: Optional. Message is a forwarded story
* Field: video
  * Type: Video
  * Description: Optional. Message is a video, information about the video
* Field: video_note
  * Type: VideoNote
  * Description: Optional. Message is a video note, information about the video message
* Field: voice
  * Type: Voice
  * Description: Optional. Message is a voice message, information about the file
* Field: caption
  * Type: String
  * Description: Optional. Caption for the animation, audio, document, paid media, photo, video or voice
* Field: caption_entities
  * Type: Array of MessageEntity
  * Description: Optional. For messages with a caption, special entities like usernames, URLs, bot commands, etc. that appear in the caption
* Field: show_caption_above_media
  * Type: True
  * Description: Optional. True, if the caption must be shown above the message media
* Field: has_media_spoiler
  * Type: True
  * Description: Optional. True, if the message media is covered by a spoiler animation
* Field: checklist
  * Type: Checklist
  * Description: Optional. Message is a checklist
* Field: contact
  * Type: Contact
  * Description: Optional. Message is a shared contact, information about the contact
* Field: dice
  * Type: Dice
  * Description: Optional. Message is a dice with random value
* Field: game
  * Type: Game
  * Description: Optional. Message is a game, information about the game. More about games »
* Field: poll
  * Type: Poll
  * Description: Optional. Message is a native poll, information about the poll
* Field: venue
  * Type: Venue
  * Description: Optional. Message is a venue, information about the venue. For backward compatibility, when this field is set, the location field will also be set.
* Field: location
  * Type: Location
  * Description: Optional. Message is a shared location, information about the location
* Field: new_chat_members
  * Type: Array of User
  * Description: Optional. New members that were added to the group or supergroup and information about them (the bot itself may be one of these members)
* Field: left_chat_member
  * Type: User
  * Description: Optional. A member was removed from the group, information about them (this member may be the bot itself)
* Field: chat_owner_left
  * Type: ChatOwnerLeft
  * Description: Optional. Service message: chat owner has left
* Field: chat_owner_changed
  * Type: ChatOwnerChanged
  * Description: Optional. Service message: chat owner has changed
* Field: new_chat_title
  * Type: String
  * Description: Optional. A chat title was changed to this value
* Field: new_chat_photo
  * Type: Array of PhotoSize
  * Description: Optional. A chat photo was change to this value
* Field: delete_chat_photo
  * Type: True
  * Description: Optional. Service message: the chat photo was deleted
* Field: group_chat_created
  * Type: True
  * Description: Optional. Service message: the group has been created
* Field: supergroup_chat_created
  * Type: True
  * Description: Optional. Service message: the supergroup has been created. This field can't be received in a message coming through updates, because bot can't be a member of a supergroup when it is created. It can only be found in reply_to_message if someone replies to a very first message in a directly created supergroup.
* Field: channel_chat_created
  * Type: True
  * Description: Optional. Service message: the channel has been created. This field can't be received in a message coming through updates, because bot can't be a member of a channel when it is created. It can only be found in reply_to_message if someone replies to a very first message in a channel.
* Field: message_auto_delete_timer_changed
  * Type: MessageAutoDeleteTimerChanged
  * Description: Optional. Service message: auto-delete timer settings changed in the chat
* Field: migrate_to_chat_id
  * Type: Integer
  * Description: Optional. The group has been migrated to a supergroup with the specified identifier. This number may have more than 32 significant bits and some programming languages may have difficulty/silent defects in interpreting it. But it has at most 52 significant bits, so a signed 64-bit integer or double-precision float type are safe for storing this identifier.
* Field: migrate_from_chat_id
  * Type: Integer
  * Description: Optional. The supergroup has been migrated from a group with the specified identifier. This number may have more than 32 significant bits and some programming languages may have difficulty/silent defects in interpreting it. But it has at most 52 significant bits, so a signed 64-bit integer or double-precision float type are safe for storing this identifier.
* Field: pinned_message
  * Type: MaybeInaccessibleMessage
  * Description: Optional. Specified message was pinned. Note that the Message object in this field will not contain further reply_to_message fields even if it itself is a reply.
* Field: invoice
  * Type: Invoice
  * Description: Optional. Message is an invoice for a payment, information about the invoice. More about payments »
* Field: successful_payment
  * Type: SuccessfulPayment
  * Description: Optional. Message is a service message about a successful payment, information about the payment. More about payments »
* Field: refunded_payment
  * Type: RefundedPayment
  * Description: Optional. Message is a service message about a refunded payment, information about the payment. More about payments »
* Field: users_shared
  * Type: UsersShared
  * Description: Optional. Service message: users were shared with the bot
* Field: chat_shared
  * Type: ChatShared
  * Description: Optional. Service message: a chat was shared with the bot
* Field: gift
  * Type: GiftInfo
  * Description: Optional. Service message: a regular gift was sent or received
* Field: unique_gift
  * Type: UniqueGiftInfo
  * Description: Optional. Service message: a unique gift was sent or received
* Field: gift_upgrade_sent
  * Type: GiftInfo
  * Description: Optional. Service message: upgrade of a gift was purchased after the gift was sent
* Field: connected_website
  * Type: String
  * Description: Optional. The domain name of the website on which the user has logged in. More about Telegram Login »
* Field: write_access_allowed
  * Type: WriteAccessAllowed
  * Description: Optional. Service message: the user allowed the bot to write messages after adding it to the attachment or side menu, launching a Web App from a link, or accepting an explicit request from a Web App sent by the method requestWriteAccess
* Field: passport_data
  * Type: PassportData
  * Description: Optional. Telegram Passport data
* Field: proximity_alert_triggered
  * Type: ProximityAlertTriggered
  * Description: Optional. Service message. A user in the chat triggered another user's proximity alert while sharing Live Location.
* Field: boost_added
  * Type: ChatBoostAdded
  * Description: Optional. Service message: user boosted the chat
* Field: chat_background_set
  * Type: ChatBackground
  * Description: Optional. Service message: chat background set
* Field: checklist_tasks_done
  * Type: ChecklistTasksDone
  * Description: Optional. Service message: some tasks in a checklist were marked as done or not done
* Field: checklist_tasks_added
  * Type: ChecklistTasksAdded
  * Description: Optional. Service message: tasks were added to a checklist
* Field: direct_message_price_changed
  * Type: DirectMessagePriceChanged
  * Description: Optional. Service message: the price for paid messages in the corresponding direct messages chat of a channel has changed
* Field: forum_topic_created
  * Type: ForumTopicCreated
  * Description: Optional. Service message: forum topic created
* Field: forum_topic_edited
  * Type: ForumTopicEdited
  * Description: Optional. Service message: forum topic edited
* Field: forum_topic_closed
  * Type: ForumTopicClosed
  * Description: Optional. Service message: forum topic closed
* Field: forum_topic_reopened
  * Type: ForumTopicReopened
  * Description: Optional. Service message: forum topic reopened
* Field: general_forum_topic_hidden
  * Type: GeneralForumTopicHidden
  * Description: Optional. Service message: the 'General' forum topic hidden
* Field: general_forum_topic_unhidden
  * Type: GeneralForumTopicUnhidden
  * Description: Optional. Service message: the 'General' forum topic unhidden
* Field: giveaway_created
  * Type: GiveawayCreated
  * Description: Optional. Service message: a scheduled giveaway was created
* Field: giveaway
  * Type: Giveaway
  * Description: Optional. The message is a scheduled giveaway message
* Field: giveaway_winners
  * Type: GiveawayWinners
  * Description: Optional. A giveaway with public winners was completed
* Field: giveaway_completed
  * Type: GiveawayCompleted
  * Description: Optional. Service message: a giveaway without public winners was completed
* Field: managed_bot_created
  * Type: ManagedBotCreated
  * Description: Optional. Service message: user created a bot that will be managed by the current bot
* Field: paid_message_price_changed
  * Type: PaidMessagePriceChanged
  * Description: Optional. Service message: the price for paid messages has changed in the chat
* Field: poll_option_added
  * Type: PollOptionAdded
  * Description: Optional. Service message: answer option was added to a poll
* Field: poll_option_deleted
  * Type: PollOptionDeleted
  * Description: Optional. Service message: answer option was deleted from a poll
* Field: suggested_post_approved
  * Type: SuggestedPostApproved
  * Description: Optional. Service message: a suggested post was approved
* Field: suggested_post_approval_failed
  * Type: SuggestedPostApprovalFailed
  * Description: Optional. Service message: approval of a suggested post has failed
* Field: suggested_post_declined
  * Type: SuggestedPostDeclined
  * Description: Optional. Service message: a suggested post was declined
* Field: suggested_post_paid
  * Type: SuggestedPostPaid
  * Description: Optional. Service message: payment for a suggested post was received
* Field: suggested_post_refunded
  * Type: SuggestedPostRefunded
  * Description: Optional. Service message: payment for a suggested post was refunded
* Field: video_chat_scheduled
  * Type: VideoChatScheduled
  * Description: Optional. Service message: video chat scheduled
* Field: video_chat_started
  * Type: VideoChatStarted
  * Description: Optional. Service message: video chat started
* Field: video_chat_ended
  * Type: VideoChatEnded
  * Description: Optional. Service message: video chat ended
* Field: video_chat_participants_invited
  * Type: VideoChatParticipantsInvited
  * Description: Optional. Service message: new participants invited to a video chat
* Field: web_app_data
  * Type: WebAppData
  * Description: Optional. Service message: data sent by a Web App
* Field: reply_markup
  * Type: InlineKeyboardMarkup
  * Description: Optional. Inline keyboard attached to the message. login_url buttons are represented as ordinary url buttons.


#### [](#messageid)MessageId

This object represents a unique message identifier.



* Field: message_id
  * Type: Integer
  * Description: Unique message identifier. In specific instances (e.g., message containing a video sent to a big chat), the server might automatically schedule a message instead of sending it immediately. In such cases, this field will be 0 and the relevant message will be unusable until it is actually sent.


#### [](#inaccessiblemessage)InaccessibleMessage

This object describes a message that was deleted or is otherwise inaccessible to the bot.



* Field: chat
  * Type: Chat
  * Description: Chat the message belonged to
* Field: message_id
  * Type: Integer
  * Description: Unique message identifier inside the chat
* Field: date
  * Type: Integer
  * Description: Always 0. The field can be used to differentiate regular and inaccessible messages.


#### [](#maybeinaccessiblemessage)MaybeInaccessibleMessage

This object describes a message that can be inaccessible to the bot. It can be one of

*   [Message](#message)
*   [InaccessibleMessage](#inaccessiblemessage)

#### [](#messageentity)MessageEntity

This object represents one special entity in a text message. For example, hashtags, usernames, URLs, etc.



* Field: type
  * Type: String
  * Description: Type of the entity. Currently, can be “mention” (@username), “hashtag” (#hashtag or #hashtag@chatusername), “cashtag” ($USD or $USD@chatusername), “bot_command” (/start@jobs_bot), “url” (https://telegram.org), “email” (do-not-reply@telegram.org), “phone_number” (+1-212-555-0123), “bold” (bold text), “italic” (italic text), “underline” (underlined text), “strikethrough” (strikethrough text), “spoiler” (spoiler message), “blockquote” (block quotation), “expandable_blockquote” (collapsed-by-default block quotation), “code” (monowidth string), “pre” (monowidth block), “text_link” (for clickable text URLs), “text_mention” (for users without usernames), “custom_emoji” (for inline custom emoji stickers), or “date_time” (for formatted date and time).
* Field: offset
  * Type: Integer
  * Description: Offset in UTF-16 code units to the start of the entity
* Field: length
  * Type: Integer
  * Description: Length of the entity in UTF-16 code units
* Field: url
  * Type: String
  * Description: Optional. For “text_link” only, URL that will be opened after user taps on the text
* Field: user
  * Type: User
  * Description: Optional. For “text_mention” only, the mentioned user
* Field: language
  * Type: String
  * Description: Optional. For “pre” only, the programming language of the entity text
* Field: custom_emoji_id
  * Type: String
  * Description: Optional. For “custom_emoji” only, unique identifier of the custom emoji. Use getCustomEmojiStickers to get full information about the sticker.
* Field: unix_time
  * Type: Integer
  * Description: Optional. For “date_time” only, the Unix time associated with the entity
* Field: date_time_format
  * Type: String
  * Description: Optional. For “date_time” only, the string that defines the formatting of the date and time. See date-time entity formatting for more details.


#### [](#textquote)TextQuote

This object contains information about the quoted part of a message that is replied to by the given message.



* Field: text
  * Type: String
  * Description: Text of the quoted part of a message that is replied to by the given message
* Field: entities
  * Type: Array of MessageEntity
  * Description: Optional. Special entities that appear in the quote. Currently, only bold, italic, underline, strikethrough, spoiler, custom_emoji, and date_time entities are kept in quotes.
* Field: position
  * Type: Integer
  * Description: Approximate quote position in the original message in UTF-16 code units as specified by the sender
* Field: is_manual
  * Type: True
  * Description: Optional. True, if the quote was chosen manually by the message sender. Otherwise, the quote was added automatically by the server.


#### [](#externalreplyinfo)ExternalReplyInfo

This object contains information about a message that is being replied to, which may come from another chat or forum topic.



* Field: origin
  * Type: MessageOrigin
  * Description: Origin of the message replied to by the given message
* Field: chat
  * Type: Chat
  * Description: Optional. Chat the original message belongs to. Available only if the chat is a supergroup or a channel.
* Field: message_id
  * Type: Integer
  * Description: Optional. Unique message identifier inside the original chat. Available only if the original chat is a supergroup or a channel.
* Field: link_preview_options
  * Type: LinkPreviewOptions
  * Description: Optional. Options used for link preview generation for the original message, if it is a text message
* Field: animation
  * Type: Animation
  * Description: Optional. Message is an animation, information about the animation
* Field: audio
  * Type: Audio
  * Description: Optional. Message is an audio file, information about the file
* Field: document
  * Type: Document
  * Description: Optional. Message is a general file, information about the file
* Field: live_photo
  * Type: LivePhoto
  * Description: Optional. Message is a live photo, information about the live photo
* Field: paid_media
  * Type: PaidMediaInfo
  * Description: Optional. Message contains paid media; information about the paid media
* Field: photo
  * Type: Array of PhotoSize
  * Description: Optional. Message is a photo, available sizes of the photo
* Field: sticker
  * Type: Sticker
  * Description: Optional. Message is a sticker, information about the sticker
* Field: story
  * Type: Story
  * Description: Optional. Message is a forwarded story
* Field: video
  * Type: Video
  * Description: Optional. Message is a video, information about the video
* Field: video_note
  * Type: VideoNote
  * Description: Optional. Message is a video note, information about the video message
* Field: voice
  * Type: Voice
  * Description: Optional. Message is a voice message, information about the file
* Field: has_media_spoiler
  * Type: True
  * Description: Optional. True, if the message media is covered by a spoiler animation
* Field: checklist
  * Type: Checklist
  * Description: Optional. Message is a checklist
* Field: contact
  * Type: Contact
  * Description: Optional. Message is a shared contact, information about the contact
* Field: dice
  * Type: Dice
  * Description: Optional. Message is a dice with random value
* Field: game
  * Type: Game
  * Description: Optional. Message is a game, information about the game. More about games »
* Field: giveaway
  * Type: Giveaway
  * Description: Optional. Message is a scheduled giveaway, information about the giveaway
* Field: giveaway_winners
  * Type: GiveawayWinners
  * Description: Optional. A giveaway with public winners was completed
* Field: invoice
  * Type: Invoice
  * Description: Optional. Message is an invoice for a payment, information about the invoice. More about payments »
* Field: location
  * Type: Location
  * Description: Optional. Message is a shared location, information about the location
* Field: poll
  * Type: Poll
  * Description: Optional. Message is a native poll, information about the poll
* Field: venue
  * Type: Venue
  * Description: Optional. Message is a venue, information about the venue


#### [](#replyparameters)ReplyParameters

Describes reply parameters for the message that is being sent.



* Field: message_id
  * Type: Integer
  * Description: Identifier of the message that will be replied to in the current chat, or in the chat chat_id if it is specified
* Field: chat_id
  * Type: Integer or String
  * Description: Optional. If the message to be replied to is from a different chat, unique identifier for the chat or username of the bot, supergroup or channel in the format @username. Not supported for messages sent on behalf of a business account and messages from channel direct messages chats.
* Field: allow_sending_without_reply
  * Type: Boolean
  * Description: Optional. Pass True if the message should be sent even if the specified message to be replied to is not found. Always False for replies in another chat or forum topic. Always True for messages sent on behalf of a business account.
* Field: quote
  * Type: String
  * Description: Optional. Quoted part of the message to be replied to; 0-1024 characters after entities parsing. The quote must be an exact substring of the message to be replied to, including bold, italic, underline, strikethrough, spoiler, custom_emoji, and date_time entities. The message will fail to send if the quote isn't found in the original message.
* Field: quote_parse_mode
  * Type: String
  * Description: Optional. Mode for parsing entities in the quote. See formatting options for more details.
* Field: quote_entities
  * Type: Array of MessageEntity
  * Description: Optional. A JSON-serialized list of special entities that appear in the quote. It can be specified instead of quote_parse_mode.
* Field: quote_position
  * Type: Integer
  * Description: Optional. Position of the quote in the original message in UTF-16 code units
* Field: checklist_task_id
  * Type: Integer
  * Description: Optional. Identifier of the specific checklist task to be replied to
* Field: poll_option_id
  * Type: String
  * Description: Optional. Persistent identifier of the specific poll option to be replied to


#### [](#messageorigin)MessageOrigin

This object describes the origin of a message. It can be one of

*   [MessageOriginUser](#messageoriginuser)
*   [MessageOriginHiddenUser](#messageoriginhiddenuser)
*   [MessageOriginChat](#messageoriginchat)
*   [MessageOriginChannel](#messageoriginchannel)

#### [](#messageoriginuser)MessageOriginUser

The message was originally sent by a known user.


|Field      |Type   |Description                                      |
|-----------|-------|-------------------------------------------------|
|type       |String |Type of the message origin, always “user”        |
|date       |Integer|Date the message was sent originally in Unix time|
|sender_user|User   |User that sent the message originally            |


#### [](#messageoriginhiddenuser)MessageOriginHiddenUser

The message was originally sent by an unknown user.


|Field           |Type   |Description                                      |
|----------------|-------|-------------------------------------------------|
|type            |String |Type of the message origin, always “hidden_user” |
|date            |Integer|Date the message was sent originally in Unix time|
|sender_user_name|String |Name of the user that sent the message originally|


#### [](#messageoriginchat)MessageOriginChat

The message was originally sent on behalf of a chat to a group chat.



* Field: type
  * Type: String
  * Description: Type of the message origin, always “chat”
* Field: date
  * Type: Integer
  * Description: Date the message was sent originally in Unix time
* Field: sender_chat
  * Type: Chat
  * Description: Chat that sent the message originally
* Field: author_signature
  * Type: String
  * Description: Optional. For messages originally sent by an anonymous chat administrator, original message author signature


#### [](#messageoriginchannel)MessageOriginChannel

The message was originally sent to a channel chat.


|Field           |Type   |Description                                          |
|----------------|-------|-----------------------------------------------------|
|type            |String |Type of the message origin, always “channel”         |
|date            |Integer|Date the message was sent originally in Unix time    |
|chat            |Chat   |Channel chat to which the message was originally sent|
|message_id      |Integer|Unique message identifier inside the chat            |
|author_signature|String |Optional. Signature of the original post author      |


#### [](#photosize)PhotoSize

This object represents one size of a photo or a [file](#document) / [sticker](#sticker) thumbnail.



* Field: file_id
  * Type: String
  * Description: Identifier for this file, which can be used to download or reuse the file
* Field: file_unique_id
  * Type: String
  * Description: Unique identifier for this file, which is supposed to be the same over time and for different bots. Can't be used to download or reuse the file.
* Field: width
  * Type: Integer
  * Description: Photo width
* Field: height
  * Type: Integer
  * Description: Photo height
* Field: file_size
  * Type: Integer
  * Description: Optional. File size in bytes


#### [](#animation)Animation

This object represents an animation file (GIF or H.264/MPEG-4 AVC video without sound).



* Field: file_id
  * Type: String
  * Description: Identifier for this file, which can be used to download or reuse the file
* Field: file_unique_id
  * Type: String
  * Description: Unique identifier for this file, which is supposed to be the same over time and for different bots. Can't be used to download or reuse the file.
* Field: width
  * Type: Integer
  * Description: Video width as defined by the sender
* Field: height
  * Type: Integer
  * Description: Video height as defined by the sender
* Field: duration
  * Type: Integer
  * Description: Duration of the video in seconds as defined by the sender
* Field: thumbnail
  * Type: PhotoSize
  * Description: Optional. Animation thumbnail as defined by the sender
* Field: file_name
  * Type: String
  * Description: Optional. Original animation filename as defined by the sender
* Field: mime_type
  * Type: String
  * Description: Optional. MIME type of the file as defined by the sender
* Field: file_size
  * Type: Integer
  * Description: Optional. File size in bytes. It can be bigger than 2^31 and some programming languages may have difficulty/silent defects in interpreting it. But it has at most 52 significant bits, so a signed 64-bit integer or double-precision float type are safe for storing this value.


#### [](#audio)Audio

This object represents an audio file to be treated as music by the Telegram clients.



* Field: file_id
  * Type: String
  * Description: Identifier for this file, which can be used to download or reuse the file
* Field: file_unique_id
  * Type: String
  * Description: Unique identifier for this file, which is supposed to be the same over time and for different bots. Can't be used to download or reuse the file.
* Field: duration
  * Type: Integer
  * Description: Duration of the audio in seconds as defined by the sender
* Field: performer
  * Type: String
  * Description: Optional. Performer of the audio as defined by the sender or by audio tags
* Field: title
  * Type: String
  * Description: Optional. Title of the audio as defined by the sender or by audio tags
* Field: file_name
  * Type: String
  * Description: Optional. Original filename as defined by the sender
* Field: mime_type
  * Type: String
  * Description: Optional. MIME type of the file as defined by the sender
* Field: file_size
  * Type: Integer
  * Description: Optional. File size in bytes. It can be bigger than 2^31 and some programming languages may have difficulty/silent defects in interpreting it. But it has at most 52 significant bits, so a signed 64-bit integer or double-precision float type are safe for storing this value.
* Field: thumbnail
  * Type: PhotoSize
  * Description: Optional. Thumbnail of the album cover to which the music file belongs


#### [](#document)Document

This object represents a general file (as opposed to [photos](#photosize), [voice messages](#voice) and [audio files](#audio)).



* Field: file_id
  * Type: String
  * Description: Identifier for this file, which can be used to download or reuse the file
* Field: file_unique_id
  * Type: String
  * Description: Unique identifier for this file, which is supposed to be the same over time and for different bots. Can't be used to download or reuse the file.
* Field: thumbnail
  * Type: PhotoSize
  * Description: Optional. Document thumbnail as defined by the sender
* Field: file_name
  * Type: String
  * Description: Optional. Original filename as defined by the sender
* Field: mime_type
  * Type: String
  * Description: Optional. MIME type of the file as defined by the sender
* Field: file_size
  * Type: Integer
  * Description: Optional. File size in bytes. It can be bigger than 2^31 and some programming languages may have difficulty/silent defects in interpreting it. But it has at most 52 significant bits, so a signed 64-bit integer or double-precision float type are safe for storing this value.


#### [](#livephoto)LivePhoto

This object represents a live photo.



* Field: photo
  * Type: Array of PhotoSize
  * Description: Optional. Available sizes of the corresponding static photo
* Field: file_id
  * Type: String
  * Description: Identifier for the video file which can be used to download or reuse the file
* Field: file_unique_id
  * Type: String
  * Description: Unique identifier for the video file which is supposed to be the same over time and for different bots. Can't be used to download or reuse the file.
* Field: width
  * Type: Integer
  * Description: Video width as defined by the sender
* Field: height
  * Type: Integer
  * Description: Video height as defined by the sender
* Field: duration
  * Type: Integer
  * Description: Duration of the video in seconds as defined by the sender
* Field: mime_type
  * Type: String
  * Description: Optional. MIME type of the file as defined by the sender
* Field: file_size
  * Type: Integer
  * Description: Optional. File size in bytes. It can be bigger than 2^31 and some programming languages may have difficulty/silent defects in interpreting it. But it has at most 52 significant bits, so a signed 64-bit integer or double-precision float type are safe for storing this value.


#### [](#story)Story

This object represents a story.


|Field|Type   |Description                                |
|-----|-------|-------------------------------------------|
|chat |Chat   |Chat that posted the story                 |
|id   |Integer|Unique identifier for the story in the chat|


#### [](#videoquality)VideoQuality

This object represents a video file of a specific quality.



* Field: file_id
  * Type: String
  * Description: Identifier for this file, which can be used to download or reuse the file
* Field: file_unique_id
  * Type: String
  * Description: Unique identifier for this file, which is supposed to be the same over time and for different bots. Can't be used to download or reuse the file.
* Field: width
  * Type: Integer
  * Description: Video width
* Field: height
  * Type: Integer
  * Description: Video height
* Field: codec
  * Type: String
  * Description: Codec that was used to encode the video, for example, “h264”, “h265”, or “av01”
* Field: file_size
  * Type: Integer
  * Description: Optional. File size in bytes. It can be bigger than 2^31 and some programming languages may have difficulty/silent defects in interpreting it. But it has at most 52 significant bits, so a signed 64-bit integer or double-precision float type are safe for storing this value.


#### [](#video)Video

This object represents a video file.



* Field: file_id
  * Type: String
  * Description: Identifier for this file, which can be used to download or reuse the file
* Field: file_unique_id
  * Type: String
  * Description: Unique identifier for this file, which is supposed to be the same over time and for different bots. Can't be used to download or reuse the file.
* Field: width
  * Type: Integer
  * Description: Video width as defined by the sender
* Field: height
  * Type: Integer
  * Description: Video height as defined by the sender
* Field: duration
  * Type: Integer
  * Description: Duration of the video in seconds as defined by the sender
* Field: thumbnail
  * Type: PhotoSize
  * Description: Optional. Video thumbnail
* Field: cover
  * Type: Array of PhotoSize
  * Description: Optional. Available sizes of the cover of the video in the message
* Field: start_timestamp
  * Type: Integer
  * Description: Optional. Timestamp in seconds from which the video will play in the message
* Field: qualities
  * Type: Array of VideoQuality
  * Description: Optional. List of available qualities of the video
* Field: file_name
  * Type: String
  * Description: Optional. Original filename as defined by the sender
* Field: mime_type
  * Type: String
  * Description: Optional. MIME type of the file as defined by the sender
* Field: file_size
  * Type: Integer
  * Description: Optional. File size in bytes. It can be bigger than 2^31 and some programming languages may have difficulty/silent defects in interpreting it. But it has at most 52 significant bits, so a signed 64-bit integer or double-precision float type are safe for storing this value.


#### [](#videonote)VideoNote

This object represents a [video message](https://telegram.org/blog/video-messages-and-telescope) (available in Telegram apps as of [v.4.0](https://telegram.org/blog/video-messages-and-telescope)).



* Field: file_id
  * Type: String
  * Description: Identifier for this file, which can be used to download or reuse the file
* Field: file_unique_id
  * Type: String
  * Description: Unique identifier for this file, which is supposed to be the same over time and for different bots. Can't be used to download or reuse the file.
* Field: length
  * Type: Integer
  * Description: Video width and height (diameter of the video message) as defined by the sender
* Field: duration
  * Type: Integer
  * Description: Duration of the video in seconds as defined by the sender
* Field: thumbnail
  * Type: PhotoSize
  * Description: Optional. Video thumbnail
* Field: file_size
  * Type: Integer
  * Description: Optional. File size in bytes


#### [](#voice)Voice

This object represents a voice note.



* Field: file_id
  * Type: String
  * Description: Identifier for this file, which can be used to download or reuse the file
* Field: file_unique_id
  * Type: String
  * Description: Unique identifier for this file, which is supposed to be the same over time and for different bots. Can't be used to download or reuse the file.
* Field: duration
  * Type: Integer
  * Description: Duration of the audio in seconds as defined by the sender
* Field: mime_type
  * Type: String
  * Description: Optional. MIME type of the file as defined by the sender
* Field: file_size
  * Type: Integer
  * Description: Optional. File size in bytes. It can be bigger than 2^31 and some programming languages may have difficulty/silent defects in interpreting it. But it has at most 52 significant bits, so a signed 64-bit integer or double-precision float type are safe for storing this value.


#### [](#paidmediainfo)PaidMediaInfo

Describes the paid media added to a message.



* Field: star_count
  * Type: Integer
  * Description: The number of Telegram Stars that must be paid to buy access to the media
* Field: paid_media
  * Type: Array of PaidMedia
  * Description: Information about the paid media


#### [](#paidmedia)PaidMedia

This object describes paid media. Currently, it can be one of

*   [PaidMediaLivePhoto](#paidmedialivephoto)
*   [PaidMediaPhoto](#paidmediaphoto)
*   [PaidMediaPreview](#paidmediapreview)
*   [PaidMediaVideo](#paidmediavideo)

#### [](#paidmedialivephoto)PaidMediaLivePhoto

The paid media is a [live photo](#livephoto).


|Field     |Type     |Description                                |
|----------|---------|-------------------------------------------|
|type      |String   |Type of the paid media, always “live_photo”|
|live_photo|LivePhoto|The photo                                  |


#### [](#paidmediaphoto)PaidMediaPhoto

The paid media is a photo.


|Field|Type              |Description                           |
|-----|------------------|--------------------------------------|
|type |String            |Type of the paid media, always “photo”|
|photo|Array of PhotoSize|The photo                             |


#### [](#paidmediapreview)PaidMediaPreview

The paid media isn't available before the payment.


|Field   |Type   |Description                                                        |
|--------|-------|-------------------------------------------------------------------|
|type    |String |Type of the paid media, always “preview”                           |
|width   |Integer|Optional. Media width as defined by the sender                     |
|height  |Integer|Optional. Media height as defined by the sender                    |
|duration|Integer|Optional. Duration of the media in seconds as defined by the sender|


#### [](#paidmediavideo)PaidMediaVideo

The paid media is a video.


|Field|Type  |Description                           |
|-----|------|--------------------------------------|
|type |String|Type of the paid media, always “video”|
|video|Video |The video                             |


#### [](#contact)Contact

This object represents a phone contact.



* Field: phone_number
  * Type: String
  * Description: Contact's phone number
* Field: first_name
  * Type: String
  * Description: Contact's first name
* Field: last_name
  * Type: String
  * Description: Optional. Contact's last name
* Field: user_id
  * Type: Integer
  * Description: Optional. Contact's user identifier in Telegram. This number may have more than 32 significant bits and some programming languages may have difficulty/silent defects in interpreting it. But it has at most 52 significant bits, so a 64-bit integer or double-precision float type are safe for storing this identifier.
* Field: vcard
  * Type: String
  * Description: Optional. Additional data about the contact in the form of a vCard


#### [](#dice)Dice

This object represents an animated emoji that displays a random value.



* Field: emoji
  * Type: String
  * Description: Emoji on which the dice throw animation is based
* Field: value
  * Type: Integer
  * Description: Value of the dice, 1-6 for “”, “” and “” base emoji, 1-5 for “” and “” base emoji, 1-64 for “” base emoji


#### [](#pollmedia)PollMedia

At most **one** of the optional fields can be present in any given object.



* Field: animation
  * Type: Animation
  * Description: Optional. Media is an animation, information about the animation
* Field: audio
  * Type: Audio
  * Description: Optional. Media is an audio file, information about the file; currently, can't be received in a poll option
* Field: document
  * Type: Document
  * Description: Optional. Media is a general file, information about the file; currently, can't be received in a poll option
* Field: live_photo
  * Type: LivePhoto
  * Description: Optional. Media is a live photo, information about the live photo
* Field: location
  * Type: Location
  * Description: Optional. Media is a shared location, information about the location
* Field: photo
  * Type: Array of PhotoSize
  * Description: Optional. Media is a photo, available sizes of the photo
* Field: sticker
  * Type: Sticker
  * Description: Optional. Media is a sticker, information about the sticker; currently, for poll options only
* Field: venue
  * Type: Venue
  * Description: Optional. Media is a venue, information about the venue
* Field: video
  * Type: Video
  * Description: Optional. Media is a video, information about the video


#### [](#inputpollmedia)InputPollMedia

This object represents the content of a poll description or a quiz explanation to be sent. It should be one of

*   [InputMediaAnimation](#inputmediaanimation)
*   [InputMediaAudio](#inputmediaaudio)
*   [InputMediaDocument](#inputmediadocument)
*   [InputMediaLivePhoto](#inputmedialivephoto)
*   [InputMediaLocation](#inputmedialocation)
*   [InputMediaPhoto](#inputmediaphoto)
*   [InputMediaVenue](#inputmediavenue)
*   [InputMediaVideo](#inputmediavideo)

#### [](#inputpolloptionmedia)InputPollOptionMedia

This object represents the content of a poll option to be sent. It should be one of

*   [InputMediaAnimation](#inputmediaanimation)
*   [InputMediaLivePhoto](#inputmedialivephoto)
*   [InputMediaLocation](#inputmedialocation)
*   [InputMediaPhoto](#inputmediaphoto)
*   [InputMediaSticker](#inputmediasticker)
*   [InputMediaVenue](#inputmediavenue)
*   [InputMediaVideo](#inputmediavideo)

#### [](#polloption)PollOption

This object contains information about one answer option in a poll.



* Field: persistent_id
  * Type: String
  * Description: Unique identifier of the option, persistent on option addition and deletion
* Field: text
  * Type: String
  * Description: Option text, 1-100 characters
* Field: text_entities
  * Type: Array of MessageEntity
  * Description: Optional. Special entities that appear in the option text. Currently, only custom emoji entities are allowed in poll option texts
* Field: media
  * Type: PollMedia
  * Description: Optional. Media added to the poll option
* Field: voter_count
  * Type: Integer
  * Description: Number of users who voted for this option; may be 0 if unknown
* Field: added_by_user
  * Type: User
  * Description: Optional. User who added the option; omitted if the option wasn't added by a user after poll creation
* Field: added_by_chat
  * Type: Chat
  * Description: Optional. Chat that added the option; omitted if the option wasn't added by a chat after poll creation
* Field: addition_date
  * Type: Integer
  * Description: Optional. Point in time (Unix timestamp) when the option was added; omitted if the option existed in the original poll


#### [](#inputpolloption)InputPollOption

This object contains information about one answer option in a poll to be sent.



* Field: text
  * Type: String
  * Description: Option text, 1-100 characters
* Field: text_parse_mode
  * Type: String
  * Description: Optional. Mode for parsing entities in the text. See formatting options for more details. Currently, only custom emoji entities are allowed.
* Field: text_entities
  * Type: Array of MessageEntity
  * Description: Optional. A JSON-serialized list of special entities that appear in the poll option text. It can be specified instead of text_parse_mode.
* Field: media
  * Type: InputPollOptionMedia
  * Description: Optional. Media added to the poll option


#### [](#pollanswer)PollAnswer

This object represents an answer of a user in a non-anonymous poll.



* Field: poll_id
  * Type: String
  * Description: Unique poll identifier
* Field: voter_chat
  * Type: Chat
  * Description: Optional. The chat that changed the answer to the poll, if the voter is anonymous
* Field: user
  * Type: User
  * Description: Optional. The user that changed the answer to the poll, if the voter isn't anonymous
* Field: option_ids
  * Type: Array of Integer
  * Description: 0-based identifiers of chosen answer options. May be empty if the vote was retracted.
* Field: option_persistent_ids
  * Type: Array of String
  * Description: Persistent identifiers of the chosen answer options. May be empty if the vote was retracted.


#### [](#poll)Poll

This object contains information about a poll.



* Field: id
  * Type: String
  * Description: Unique poll identifier
* Field: question
  * Type: String
  * Description: Poll question, 1-300 characters
* Field: question_entities
  * Type: Array of MessageEntity
  * Description: Optional. Special entities that appear in the question. Currently, only custom emoji entities are allowed in poll questions
* Field: options
  * Type: Array of PollOption
  * Description: List of poll options
* Field: total_voter_count
  * Type: Integer
  * Description: Total number of users that voted in the poll
* Field: is_closed
  * Type: Boolean
  * Description: True, if the poll is closed
* Field: is_anonymous
  * Type: Boolean
  * Description: True, if the poll is anonymous
* Field: type
  * Type: String
  * Description: Poll type, currently can be “regular” or “quiz”
* Field: allows_multiple_answers
  * Type: Boolean
  * Description: True, if the poll allows multiple answers
* Field: allows_revoting
  * Type: Boolean
  * Description: True, if the poll allows to change the chosen answer options
* Field: members_only
  * Type: Boolean
  * Description: True if voting is limited to users who have been members of the chat where the poll was originally sent for more than 24 hours
* Field: country_codes
  * Type: Array of String
  * Description: Optional. A list of two-letter ISO 3166-1 alpha-2 country codes indicating the countries from which users can vote in the poll. The country code “FT” is used for users with anonymous numbers. If omitted, then users from any country can participate in the poll.
* Field: correct_option_ids
  * Type: Array of Integer
  * Description: Optional. Array of 0-based identifiers of the correct answer options. Available only for polls in quiz mode which are closed or were sent (not forwarded) by the bot or to the private chat with the bot.
* Field: explanation
  * Type: String
  * Description: Optional. Text that is shown when a user chooses an incorrect answer or taps on the lamp icon in a quiz-style poll, 0-200 characters
* Field: explanation_entities
  * Type: Array of MessageEntity
  * Description: Optional. Special entities like usernames, URLs, bot commands, etc. that appear in the explanation
* Field: explanation_media
  * Type: PollMedia
  * Description: Optional. Media added to the quiz explanation
* Field: open_period
  * Type: Integer
  * Description: Optional. Amount of time in seconds the poll will be active after creation
* Field: close_date
  * Type: Integer
  * Description: Optional. Point in time (Unix timestamp) when the poll will be automatically closed
* Field: description
  * Type: String
  * Description: Optional. Description of the poll; for polls inside the Message object only
* Field: description_entities
  * Type: Array of MessageEntity
  * Description: Optional. Special entities like usernames, URLs, bot commands, etc. that appear in the description
* Field: media
  * Type: PollMedia
  * Description: Optional. Media added to the poll description; for polls inside the Message object only


#### [](#checklisttask)ChecklistTask

Describes a task in a checklist.



* Field: id
  * Type: Integer
  * Description: Unique identifier of the task
* Field: text
  * Type: String
  * Description: Text of the task
* Field: text_entities
  * Type: Array of MessageEntity
  * Description: Optional. Special entities that appear in the task text
* Field: completed_by_user
  * Type: User
  * Description: Optional. User that completed the task; omitted if the task wasn't completed by a user
* Field: completed_by_chat
  * Type: Chat
  * Description: Optional. Chat that completed the task; omitted if the task wasn't completed by a chat
* Field: completion_date
  * Type: Integer
  * Description: Optional. Point in time (Unix timestamp) when the task was completed; 0 if the task wasn't completed


#### [](#checklist)Checklist

Describes a checklist.



* Field: title
  * Type: String
  * Description: Title of the checklist
* Field: title_entities
  * Type: Array of MessageEntity
  * Description: Optional. Special entities that appear in the checklist title
* Field: tasks
  * Type: Array of ChecklistTask
  * Description: List of tasks in the checklist
* Field: others_can_add_tasks
  * Type: True
  * Description: Optional. True, if users other than the creator of the list can add tasks to the list
* Field: others_can_mark_tasks_as_done
  * Type: True
  * Description: Optional. True, if users other than the creator of the list can mark tasks as done or not done


#### [](#inputchecklisttask)InputChecklistTask

Describes a task to add to a checklist.



* Field: id
  * Type: Integer
  * Description: Unique identifier of the task; must be positive and unique among all task identifiers currently present in the checklist
* Field: text
  * Type: String
  * Description: Text of the task; 1-100 characters after entities parsing
* Field: parse_mode
  * Type: String
  * Description: Optional. Mode for parsing entities in the text. See formatting options for more details.
* Field: text_entities
  * Type: Array of MessageEntity
  * Description: Optional. List of special entities that appear in the text, which can be specified instead of parse_mode. Currently, only bold, italic, underline, strikethrough, spoiler, custom_emoji, and date_time entities are allowed.


#### [](#inputchecklist)InputChecklist

Describes a checklist to create.



* Field: title
  * Type: String
  * Description: Title of the checklist; 1-255 characters after entities parsing
* Field: parse_mode
  * Type: String
  * Description: Optional. Mode for parsing entities in the title. See formatting options for more details.
* Field: title_entities
  * Type: Array of MessageEntity
  * Description: Optional. List of special entities that appear in the title, which can be specified instead of parse_mode. Currently, only bold, italic, underline, strikethrough, spoiler, custom_emoji, and date_time entities are allowed.
* Field: tasks
  * Type: Array of InputChecklistTask
  * Description: List of 1-30 tasks in the checklist
* Field: others_can_add_tasks
  * Type: Boolean
  * Description: Optional. Pass True if other users can add tasks to the checklist
* Field: others_can_mark_tasks_as_done
  * Type: Boolean
  * Description: Optional. Pass True if other users can mark tasks as done or not done in the checklist


#### [](#checklisttasksdone)ChecklistTasksDone

Describes a service message about checklist tasks marked as done or not done.



* Field: checklist_message
  * Type: Message
  * Description: Optional. Message containing the checklist whose tasks were marked as done or not done. Note that the Message object in this field will not contain the reply_to_message field even if it itself is a reply.
* Field: marked_as_done_task_ids
  * Type: Array of Integer
  * Description: Optional. Identifiers of the tasks that were marked as done
* Field: marked_as_not_done_task_ids
  * Type: Array of Integer
  * Description: Optional. Identifiers of the tasks that were marked as not done


#### [](#checklisttasksadded)ChecklistTasksAdded

Describes a service message about tasks added to a checklist.



* Field: checklist_message
  * Type: Message
  * Description: Optional. Message containing the checklist to which the tasks were added. Note that the Message object in this field will not contain the reply_to_message field even if it itself is a reply.
* Field: tasks
  * Type: Array of ChecklistTask
  * Description: List of tasks added to the checklist


#### [](#location)Location

This object represents a point on the map.



* Field: latitude
  * Type: Float
  * Description: Latitude as defined by the sender
* Field: longitude
  * Type: Float
  * Description: Longitude as defined by the sender
* Field: horizontal_accuracy
  * Type: Float
  * Description: Optional. The radius of uncertainty for the location, measured in meters; 0-1500
* Field: live_period
  * Type: Integer
  * Description: Optional. Time relative to the message sending date, during which the location can be updated; in seconds. For active live locations only.
* Field: heading
  * Type: Integer
  * Description: Optional. The direction in which user is moving, in degrees; 1-360. For active live locations only.
* Field: proximity_alert_radius
  * Type: Integer
  * Description: Optional. The maximum distance for proximity alerts about approaching another chat member, in meters. For sent live locations only.


#### [](#venue)Venue

This object represents a venue.



* Field: location
  * Type: Location
  * Description: Venue location. Can't be a live location.
* Field: title
  * Type: String
  * Description: Name of the venue
* Field: address
  * Type: String
  * Description: Address of the venue
* Field: foursquare_id
  * Type: String
  * Description: Optional. Foursquare identifier of the venue
* Field: foursquare_type
  * Type: String
  * Description: Optional. Foursquare type of the venue. (For example, “arts_entertainment/default”, “arts_entertainment/aquarium” or “food/icecream”.)
* Field: google_place_id
  * Type: String
  * Description: Optional. Google Places identifier of the venue
* Field: google_place_type
  * Type: String
  * Description: Optional. Google Places type of the venue. (See supported types.)


#### [](#webappdata)WebAppData

Describes data sent from a [Web App](https://core.telegram.org/bots/webapps) to the bot.



* Field: data
  * Type: String
  * Description: The data. Be aware that a bad client can send arbitrary data in this field.
* Field: button_text
  * Type: String
  * Description: Text of the web_app keyboard button from which the Web App was opened. Be aware that a bad client can send arbitrary data in this field.


#### [](#proximityalerttriggered)ProximityAlertTriggered

This object represents the content of a service message, sent whenever a user in the chat triggers a proximity alert set by another user.


|Field   |Type   |Description                   |
|--------|-------|------------------------------|
|traveler|User   |User that triggered the alert |
|watcher |User   |User that set the alert       |
|distance|Integer|The distance between the users|


#### [](#messageautodeletetimerchanged)MessageAutoDeleteTimerChanged

This object represents a service message about a change in auto-delete timer settings.


|Field                   |Type   |Description                                              |
|------------------------|-------|---------------------------------------------------------|
|message_auto_delete_time|Integer|New auto-delete time for messages in the chat; in seconds|


#### [](#managedbotcreated)ManagedBotCreated

This object contains information about the bot that was created to be managed by the current bot.



* Field: bot
  * Type: User
  * Description: Information about the bot. The bot's token can be fetched using the method getManagedBotToken.


#### [](#managedbotupdated)ManagedBotUpdated

This object contains information about the creation, token update, or owner update of a bot that is managed by the current bot.



* Field: user
  * Type: User
  * Description: User that created the bot
* Field: bot
  * Type: User
  * Description: Information about the bot. Token of the bot can be fetched using the method getManagedBotToken.


#### [](#polloptionadded)PollOptionAdded

Describes a service message about an option added to a poll.



* Field: poll_message
  * Type: MaybeInaccessibleMessage
  * Description: Optional. Message containing the poll to which the option was added, if known. Note that the Message object in this field will not contain the reply_to_message field even if it itself is a reply.
* Field: option_persistent_id
  * Type: String
  * Description: Unique identifier of the added option
* Field: option_text
  * Type: String
  * Description: Option text
* Field: option_text_entities
  * Type: Array of MessageEntity
  * Description: Optional. Special entities that appear in the option_text


#### [](#polloptiondeleted)PollOptionDeleted

Describes a service message about an option deleted from a poll.



* Field: poll_message
  * Type: MaybeInaccessibleMessage
  * Description: Optional. Message containing the poll from which the option was deleted, if known. Note that the Message object in this field will not contain the reply_to_message field even if it itself is a reply.
* Field: option_persistent_id
  * Type: String
  * Description: Unique identifier of the deleted option
* Field: option_text
  * Type: String
  * Description: Option text
* Field: option_text_entities
  * Type: Array of MessageEntity
  * Description: Optional. Special entities that appear in the option_text


#### [](#chatboostadded)ChatBoostAdded

This object represents a service message about a user boosting a chat.


|Field      |Type   |Description                       |
|-----------|-------|----------------------------------|
|boost_count|Integer|Number of boosts added by the user|


#### [](#backgroundfill)BackgroundFill

This object describes the way a background is filled based on the selected colors. Currently, it can be one of

*   [BackgroundFillSolid](#backgroundfillsolid)
*   [BackgroundFillGradient](#backgroundfillgradient)
*   [BackgroundFillFreeformGradient](#backgroundfillfreeformgradient)

#### [](#backgroundfillsolid)BackgroundFillSolid

The background is filled using the selected color.


|Field|Type   |Description                                         |
|-----|-------|----------------------------------------------------|
|type |String |Type of the background fill, always “solid”         |
|color|Integer|The color of the background fill in the RGB24 format|


#### [](#backgroundfillgradient)BackgroundFillGradient

The background is a gradient fill.


|Field         |Type   |Description                                                      |
|--------------|-------|-----------------------------------------------------------------|
|type          |String |Type of the background fill, always “gradient”                   |
|top_color     |Integer|Top color of the gradient in the RGB24 format                    |
|bottom_color  |Integer|Bottom color of the gradient in the RGB24 format                 |
|rotation_angle|Integer|Clockwise rotation angle of the background fill in degrees; 0-359|


#### [](#backgroundfillfreeformgradient)BackgroundFillFreeformGradient

The background is a freeform gradient that rotates after every message in the chat.



* Field: type
  * Type: String
  * Description: Type of the background fill, always “freeform_gradient”
* Field: colors
  * Type: Array of Integer
  * Description: A list of the 3 or 4 base colors that are used to generate the freeform gradient in the RGB24 format


#### [](#backgroundtype)BackgroundType

This object describes the type of a background. Currently, it can be one of

*   [BackgroundTypeFill](#backgroundtypefill)
*   [BackgroundTypeWallpaper](#backgroundtypewallpaper)
*   [BackgroundTypePattern](#backgroundtypepattern)
*   [BackgroundTypeChatTheme](#backgroundtypechattheme)

#### [](#backgroundtypefill)BackgroundTypeFill

The background is automatically filled based on the selected colors.



* Field: type
  * Type: String
  * Description: Type of the background, always “fill”
* Field: fill
  * Type: BackgroundFill
  * Description: The background fill
* Field: dark_theme_dimming
  * Type: Integer
  * Description: Dimming of the background in dark themes, as a percentage; 0-100


#### [](#backgroundtypewallpaper)BackgroundTypeWallpaper

The background is a wallpaper in the JPEG format.



* Field: type
  * Type: String
  * Description: Type of the background, always “wallpaper”
* Field: document
  * Type: Document
  * Description: Document with the wallpaper
* Field: dark_theme_dimming
  * Type: Integer
  * Description: Dimming of the background in dark themes, as a percentage; 0-100
* Field: is_blurred
  * Type: True
  * Description: Optional. True, if the wallpaper is downscaled to fit in a 450x450 square and then box-blurred with radius 12
* Field: is_moving
  * Type: True
  * Description: Optional. True, if the background moves slightly when the device is tilted


#### [](#backgroundtypepattern)BackgroundTypePattern

The background is a .PNG or .TGV (gzipped subset of SVG with MIME type “application/x-tgwallpattern”) pattern to be combined with the background fill chosen by the user.



* Field: type
  * Type: String
  * Description: Type of the background, always “pattern”
* Field: document
  * Type: Document
  * Description: Document with the pattern
* Field: fill
  * Type: BackgroundFill
  * Description: The background fill that is combined with the pattern
* Field: intensity
  * Type: Integer
  * Description: Intensity of the pattern when it is shown above the filled background; 0-100
* Field: is_inverted
  * Type: True
  * Description: Optional. True, if the background fill must be applied only to the pattern itself. All other pixels are black in this case. For dark themes only.
* Field: is_moving
  * Type: True
  * Description: Optional. True, if the background moves slightly when the device is tilted


#### [](#backgroundtypechattheme)BackgroundTypeChatTheme

The background is taken directly from a built-in chat theme.


|Field     |Type  |Description                                      |
|----------|------|-------------------------------------------------|
|type      |String|Type of the background, always “chat_theme”      |
|theme_name|String|Name of the chat theme, which is usually an emoji|


#### [](#chatbackground)ChatBackground

This object represents a chat background.


|Field|Type          |Description           |
|-----|--------------|----------------------|
|type |BackgroundType|Type of the background|


#### [](#forumtopiccreated)ForumTopicCreated

This object represents a service message about a new forum topic created in the chat.



* Field: name
  * Type: String
  * Description: Name of the topic
* Field: icon_color
  * Type: Integer
  * Description: Color of the topic icon in RGB format
* Field: icon_custom_emoji_id
  * Type: String
  * Description: Optional. Unique identifier of the custom emoji shown as the topic icon
* Field: is_name_implicit
  * Type: True
  * Description: Optional. True, if the name of the topic wasn't specified explicitly by its creator and likely needs to be changed by the bot


#### [](#forumtopicclosed)ForumTopicClosed

This object represents a service message about a forum topic closed in the chat. Currently holds no information.

#### [](#forumtopicedited)ForumTopicEdited

This object represents a service message about an edited forum topic.



* Field: name
  * Type: String
  * Description: Optional. New name of the topic, if it was edited
* Field: icon_custom_emoji_id
  * Type: String
  * Description: Optional. New identifier of the custom emoji shown as the topic icon, if it was edited; an empty string if the icon was removed


#### [](#forumtopicreopened)ForumTopicReopened

This object represents a service message about a forum topic reopened in the chat. Currently holds no information.

#### [](#generalforumtopichidden)GeneralForumTopicHidden

This object represents a service message about General forum topic hidden in the chat. Currently holds no information.

#### [](#generalforumtopicunhidden)GeneralForumTopicUnhidden

This object represents a service message about General forum topic unhidden in the chat. Currently holds no information.

#### [](#shareduser)SharedUser

This object contains information about a user that was shared with the bot using a [KeyboardButtonRequestUsers](#keyboardbuttonrequestusers) button.



* Field: user_id
  * Type: Integer
  * Description: Identifier of the shared user. This number may have more than 32 significant bits and some programming languages may have difficulty/silent defects in interpreting it. But it has at most 52 significant bits, so 64-bit integers or double-precision float types are safe for storing these identifiers. The bot may not have access to the user and could be unable to use this identifier, unless the user is already known to the bot by some other means.
* Field: first_name
  * Type: String
  * Description: Optional. First name of the user, if the name was requested by the bot
* Field: last_name
  * Type: String
  * Description: Optional. Last name of the user, if the name was requested by the bot
* Field: username
  * Type: String
  * Description: Optional. Username of the user, if the username was requested by the bot
* Field: photo
  * Type: Array of PhotoSize
  * Description: Optional. Available sizes of the chat photo, if the photo was requested by the bot


#### [](#usersshared)UsersShared

This object contains information about the users whose identifiers were shared with the bot using a [KeyboardButtonRequestUsers](#keyboardbuttonrequestusers) button.


|Field     |Type               |Description                                |
|----------|-------------------|-------------------------------------------|
|request_id|Integer            |Identifier of the request                  |
|users     |Array of SharedUser|Information about users shared with the bot|


#### [](#chatshared)ChatShared

This object contains information about a chat that was shared with the bot using a [KeyboardButtonRequestChat](#keyboardbuttonrequestchat) button.



* Field: request_id
  * Type: Integer
  * Description: Identifier of the request
* Field: chat_id
  * Type: Integer
  * Description: Identifier of the shared chat. This number may have more than 32 significant bits and some programming languages may have difficulty/silent defects in interpreting it. But it has at most 52 significant bits, so a 64-bit integer or double-precision float type are safe for storing this identifier. The bot may not have access to the chat and could be unable to use this identifier, unless the chat is already known to the bot by some other means.
* Field: title
  * Type: String
  * Description: Optional. Title of the chat, if the title was requested by the bot
* Field: username
  * Type: String
  * Description: Optional. Username of the chat, if the username was requested by the bot and available
* Field: photo
  * Type: Array of PhotoSize
  * Description: Optional. Available sizes of the chat photo, if the photo was requested by the bot


#### [](#writeaccessallowed)WriteAccessAllowed

This object represents a service message about a user allowing a bot to write messages after adding it to the attachment menu, launching a Web App from a link, or accepting an explicit request from a Web App sent by the method [requestWriteAccess](about:/bots/webapps#initializing-mini-apps).



* Field: from_request
  * Type: Boolean
  * Description: Optional. True, if the access was granted after the user accepted an explicit request from a Web App sent by the method requestWriteAccess
* Field: web_app_name
  * Type: String
  * Description: Optional. Name of the Web App, if the access was granted when the Web App was launched from a link
* Field: from_attachment_menu
  * Type: Boolean
  * Description: Optional. True, if the access was granted when the bot was added to the attachment or side menu


#### [](#videochatscheduled)VideoChatScheduled

This object represents a service message about a video chat scheduled in the chat.



* Field: start_date
  * Type: Integer
  * Description: Point in time (Unix timestamp) when the video chat is supposed to be started by a chat administrator


#### [](#videochatstarted)VideoChatStarted

This object represents a service message about a video chat started in the chat. Currently holds no information.

#### [](#videochatended)VideoChatEnded

This object represents a service message about a video chat ended in the chat.


|Field   |Type   |Description                   |
|--------|-------|------------------------------|
|duration|Integer|Video chat duration in seconds|


#### [](#videochatparticipantsinvited)VideoChatParticipantsInvited

This object represents a service message about new members invited to a video chat.


|Field|Type         |Description                                    |
|-----|-------------|-----------------------------------------------|
|users|Array of User|New members that were invited to the video chat|


#### [](#paidmessagepricechanged)PaidMessagePriceChanged

Describes a service message about a change in the price of paid messages within a chat.



* Field: paid_message_star_count
  * Type: Integer
  * Description: The new number of Telegram Stars that must be paid by non-administrator users of the supergroup chat for each sent message


#### [](#directmessagepricechanged)DirectMessagePriceChanged

Describes a service message about a change in the price of direct messages sent to a channel chat.



* Field: are_direct_messages_enabled
  * Type: Boolean
  * Description: True, if direct messages are enabled for the channel chat; false otherwise
* Field: direct_message_star_count
  * Type: Integer
  * Description: Optional. The new number of Telegram Stars that must be paid by users for each direct message sent to the channel. Does not apply to users who have been exempted by administrators. Defaults to 0.


#### [](#suggestedpostapproved)SuggestedPostApproved

Describes a service message about the approval of a suggested post.



* Field: suggested_post_message
  * Type: Message
  * Description: Optional. Message containing the suggested post. Note that the Message object in this field will not contain the reply_to_message field even if it itself is a reply.
* Field: price
  * Type: SuggestedPostPrice
  * Description: Optional. Amount paid for the post
* Field: send_date
  * Type: Integer
  * Description: Date when the post will be published


#### [](#suggestedpostapprovalfailed)SuggestedPostApprovalFailed

Describes a service message about the failed approval of a suggested post. Currently, only caused by insufficient user funds at the time of approval.



* Field: suggested_post_message
  * Type: Message
  * Description: Optional. Message containing the suggested post whose approval has failed. Note that the Message object in this field will not contain the reply_to_message field even if it itself is a reply.
* Field: price
  * Type: SuggestedPostPrice
  * Description: Expected price of the post


#### [](#suggestedpostdeclined)SuggestedPostDeclined

Describes a service message about the rejection of a suggested post.



* Field: suggested_post_message
  * Type: Message
  * Description: Optional. Message containing the suggested post. Note that the Message object in this field will not contain the reply_to_message field even if it itself is a reply.
* Field: comment
  * Type: String
  * Description: Optional. Comment with which the post was declined


#### [](#suggestedpostpaid)SuggestedPostPaid

Describes a service message about a successful payment for a suggested post.



* Field: suggested_post_message
  * Type: Message
  * Description: Optional. Message containing the suggested post. Note that the Message object in this field will not contain the reply_to_message field even if it itself is a reply.
* Field: currency
  * Type: String
  * Description: Currency in which the payment was made. Currently, one of “XTR” for Telegram Stars or “TON” for toncoins.
* Field: amount
  * Type: Integer
  * Description: Optional. The amount of the currency that was received by the channel in nanotoncoins; for payments in toncoins only
* Field: star_amount
  * Type: StarAmount
  * Description: Optional. The amount of Telegram Stars that was received by the channel; for payments in Telegram Stars only


#### [](#suggestedpostrefunded)SuggestedPostRefunded

Describes a service message about a payment refund for a suggested post.



* Field: suggested_post_message
  * Type: Message
  * Description: Optional. Message containing the suggested post. Note that the Message object in this field will not contain the reply_to_message field even if it itself is a reply.
* Field: reason
  * Type: String
  * Description: Reason for the refund. Currently, one of “post_deleted” if the post was deleted within 24 hours of being posted or removed from scheduled messages without being posted, or “payment_refunded” if the payer refunded their payment.


#### [](#giveawaycreated)GiveawayCreated

This object represents a service message about the creation of a scheduled giveaway.



* Field: prize_star_count
  * Type: Integer
  * Description: Optional. The number of Telegram Stars to be split between giveaway winners; for Telegram Star giveaways only


#### [](#giveaway)Giveaway

This object represents a message about a scheduled giveaway.



* Field: chats
  * Type: Array of Chat
  * Description: The list of chats which the user must join to participate in the giveaway
* Field: winners_selection_date
  * Type: Integer
  * Description: Point in time (Unix timestamp) when winners of the giveaway will be selected
* Field: winner_count
  * Type: Integer
  * Description: The number of users which are supposed to be selected as winners of the giveaway
* Field: only_new_members
  * Type: True
  * Description: Optional. True, if only users who join the chats after the giveaway started should be eligible to win
* Field: has_public_winners
  * Type: True
  * Description: Optional. True, if the list of giveaway winners will be visible to everyone
* Field: prize_description
  * Type: String
  * Description: Optional. Description of additional giveaway prize
* Field: country_codes
  * Type: Array of String
  * Description: Optional. A list of two-letter ISO 3166-1 alpha-2 country codes indicating the countries from which eligible users for the giveaway must come. If empty, then all users can participate in the giveaway. Users with a phone number that was bought on Fragment can always participate in giveaways.
* Field: prize_star_count
  * Type: Integer
  * Description: Optional. The number of Telegram Stars to be split between giveaway winners; for Telegram Star giveaways only
* Field: premium_subscription_month_count
  * Type: Integer
  * Description: Optional. The number of months the Telegram Premium subscription won from the giveaway will be active for; for Telegram Premium giveaways only


#### [](#giveawaywinners)GiveawayWinners

This object represents a message about the completion of a giveaway with public winners.



* Field: chat
  * Type: Chat
  * Description: The chat that created the giveaway
* Field: giveaway_message_id
  * Type: Integer
  * Description: Identifier of the message with the giveaway in the chat
* Field: winners_selection_date
  * Type: Integer
  * Description: Point in time (Unix timestamp) when winners of the giveaway were selected
* Field: winner_count
  * Type: Integer
  * Description: Total number of winners in the giveaway
* Field: winners
  * Type: Array of User
  * Description: List of up to 100 winners of the giveaway
* Field: additional_chat_count
  * Type: Integer
  * Description: Optional. The number of other chats the user had to join in order to be eligible for the giveaway
* Field: prize_star_count
  * Type: Integer
  * Description: Optional. The number of Telegram Stars that were split between giveaway winners; for Telegram Star giveaways only
* Field: premium_subscription_month_count
  * Type: Integer
  * Description: Optional. The number of months the Telegram Premium subscription won from the giveaway will be active for; for Telegram Premium giveaways only
* Field: unclaimed_prize_count
  * Type: Integer
  * Description: Optional. Number of undistributed prizes
* Field: only_new_members
  * Type: True
  * Description: Optional. True, if only users who had joined the chats after the giveaway started were eligible to win
* Field: was_refunded
  * Type: True
  * Description: Optional. True, if the giveaway was canceled because the payment for it was refunded
* Field: prize_description
  * Type: String
  * Description: Optional. Description of additional giveaway prize


#### [](#giveawaycompleted)GiveawayCompleted

This object represents a service message about the completion of a giveaway without public winners.



* Field: winner_count
  * Type: Integer
  * Description: Number of winners in the giveaway
* Field: unclaimed_prize_count
  * Type: Integer
  * Description: Optional. Number of undistributed prizes
* Field: giveaway_message
  * Type: Message
  * Description: Optional. Message with the giveaway that was completed, if it wasn't deleted
* Field: is_star_giveaway
  * Type: True
  * Description: Optional. True, if the giveaway is a Telegram Star giveaway. Otherwise, currently, the giveaway is a Telegram Premium giveaway.


#### [](#linkpreviewoptions)LinkPreviewOptions

Describes the options used for link preview generation.



* Field: is_disabled
  * Type: Boolean
  * Description: Optional. True, if the link preview is disabled
* Field: url
  * Type: String
  * Description: Optional. URL to use for the link preview. If empty, then the first URL found in the message text will be used.
* Field: prefer_small_media
  * Type: Boolean
  * Description: Optional. True, if the media in the link preview is supposed to be shrunk; ignored if the URL isn't explicitly specified or media size change isn't supported for the preview
* Field: prefer_large_media
  * Type: Boolean
  * Description: Optional. True, if the media in the link preview is supposed to be enlarged; ignored if the URL isn't explicitly specified or media size change isn't supported for the preview
* Field: show_above_text
  * Type: Boolean
  * Description: Optional. True, if the link preview must be shown above the message text; otherwise, the link preview will be shown below the message text


#### [](#suggestedpostprice)SuggestedPostPrice

Describes the price of a suggested post.



* Field: currency
  * Type: String
  * Description: Currency in which the post will be paid. Currently, must be one of “XTR” for Telegram Stars or “TON” for toncoins.
* Field: amount
  * Type: Integer
  * Description: The amount of the currency that will be paid for the post in the smallest units of the currency, i.e. Telegram Stars or nanotoncoins. Currently, price in Telegram Stars must be between 5 and 100000, and price in nanotoncoins must be between 10000000 and 10000000000000.


#### [](#suggestedpostinfo)SuggestedPostInfo

Contains information about a suggested post.



* Field: state
  * Type: String
  * Description: State of the suggested post. Currently, it can be one of “pending”, “approved”, “declined”.
* Field: price
  * Type: SuggestedPostPrice
  * Description: Optional. Proposed price of the post. If the field is omitted, then the post is unpaid.
* Field: send_date
  * Type: Integer
  * Description: Optional. Proposed send date of the post. If the field is omitted, then the post can be published at any time within 30 days at the sole discretion of the user or administrator who approves it.


#### [](#suggestedpostparameters)SuggestedPostParameters

Contains parameters of a post that is being suggested by the bot.



* Field: price
  * Type: SuggestedPostPrice
  * Description: Optional. Proposed price for the post. If the field is omitted, then the post is unpaid.
* Field: send_date
  * Type: Integer
  * Description: Optional. Proposed send date of the post. If specified, then the date must be between 300 second and 2678400 seconds (30 days) in the future. If the field is omitted, then the post can be published at any time within 30 days at the sole discretion of the user who approves it.


#### [](#directmessagestopic)DirectMessagesTopic

Describes a topic of a direct messages chat.



* Field: topic_id
  * Type: Integer
  * Description: Unique identifier of the topic. This number may have more than 32 significant bits and some programming languages may have difficulty/silent defects in interpreting it. But it has at most 52 significant bits, so a 64-bit integer or double-precision float type are safe for storing this identifier.
* Field: user
  * Type: User
  * Description: Optional. Information about the user that created the topic. Currently, it is always present.


#### [](#userprofilephotos)UserProfilePhotos

This object represent a user's profile pictures.


|Field      |Type                       |Description                                         |
|-----------|---------------------------|----------------------------------------------------|
|total_count|Integer                    |Total number of profile pictures the target user has|
|photos     |Array of Array of PhotoSize|Requested profile pictures (in up to 4 sizes each)  |


#### [](#userprofileaudios)UserProfileAudios

This object represents the audios displayed on a user's profile.


|Field      |Type          |Description                                       |
|-----------|--------------|--------------------------------------------------|
|total_count|Integer       |Total number of profile audios for the target user|
|audios     |Array of Audio|Requested profile audios                          |


#### [](#file)File

This object represents a file ready to be downloaded. The file can be downloaded via the link `https://api.telegram.org/file/bot<token>/<file_path>`. It is guaranteed that the link will be valid for at least 1 hour. When the link expires, a new one can be requested by calling [getFile](#getfile).

> The maximum file size to download is 20 MB



* Field: file_id
  * Type: String
  * Description: Identifier for this file, which can be used to download or reuse the file
* Field: file_unique_id
  * Type: String
  * Description: Unique identifier for this file, which is supposed to be the same over time and for different bots. Can't be used to download or reuse the file.
* Field: file_size
  * Type: Integer
  * Description: Optional. File size in bytes. It can be bigger than 2^31 and some programming languages may have difficulty/silent defects in interpreting it. But it has at most 52 significant bits, so a signed 64-bit integer or double-precision float type are safe for storing this value.
* Field: file_path
  * Type: String
  * Description: Optional. File path. Use https://api.telegram.org/file/bot<token>/<file_path> to get the file.


#### [](#webappinfo)WebAppInfo

Describes a [Web App](https://core.telegram.org/bots/webapps).



* Field: url
  * Type: String
  * Description: An HTTPS URL of a Web App to be opened with additional data as specified in Initializing Web Apps


#### [](#replykeyboardmarkup)ReplyKeyboardMarkup

This object represents a [custom keyboard](about:/bots/features#keyboards) with reply options (see [Introduction to bots](about:/bots/features#keyboards) for details and examples). Not supported in channels and for messages sent on behalf of a business account.



* Field: keyboard
  * Type: Array of Array of KeyboardButton
  * Description: Array of button rows, each represented by an Array of KeyboardButton objects
* Field: is_persistent
  * Type: Boolean
  * Description: Optional. Requests clients to always show the keyboard when the regular keyboard is hidden. Defaults to false, in which case the custom keyboard can be hidden and opened with a keyboard icon.
* Field: resize_keyboard
  * Type: Boolean
  * Description: Optional. Requests clients to resize the keyboard vertically for optimal fit (e.g., make the keyboard smaller if there are just two rows of buttons). Defaults to false, in which case the custom keyboard is always of the same height as the app's standard keyboard.
* Field: one_time_keyboard
  * Type: Boolean
  * Description: Optional. Requests clients to hide the keyboard as soon as it's been used. The keyboard will still be available, but clients will automatically display the usual letter-keyboard in the chat - the user can press a special button in the input field to see the custom keyboard again. Defaults to false.
* Field: input_field_placeholder
  * Type: String
  * Description: Optional. The placeholder to be shown in the input field when the keyboard is active; 1-64 characters
* Field: selective
  * Type: Boolean
  * Description: Optional. Use this parameter if you want to show the keyboard to specific users only. Targets: 1) users that are @mentioned in the text of the Message object; 2) if the bot's message is a reply to a message in the same chat and forum topic, sender of the original message.Example: A user requests to change the bot's language, bot replies to the request with a keyboard to select the new language. Other users in the group don't see the keyboard.


#### [](#keyboardbutton)KeyboardButton

This object represents one button of the reply keyboard. At most one of the fields other than _text_, _icon\_custom\_emoji\_id_, and _style_ must be used to specify the type of the button. For simple text buttons, _String_ can be used instead of this object to specify the button text.



* Field: text
  * Type: String
  * Description: Text of the button. If none of the fields other than text, icon_custom_emoji_id, and style are used, it will be sent as a message when the button is pressed.
* Field: icon_custom_emoji_id
  * Type: String
  * Description: Optional. Unique identifier of the custom emoji shown before the text of the button. Can only be used by bots that purchased additional usernames on Fragment or in the messages directly sent by the bot to private, group and supergroup chats if the owner of the bot has a Telegram Premium subscription.
* Field: style
  * Type: String
  * Description: Optional. Style of the button. Must be one of “danger” (red), “success” (green) or “primary” (blue). If omitted, then an app-specific style is used.
* Field: request_users
  * Type: KeyboardButtonRequestUsers
  * Description: Optional. If specified, pressing the button will open a list of suitable users. Identifiers of selected users will be sent to the bot in a “users_shared” service message. Available in private chats only.
* Field: request_chat
  * Type: KeyboardButtonRequestChat
  * Description: Optional. If specified, pressing the button will open a list of suitable chats. Tapping on a chat will send its identifier to the bot in a “chat_shared” service message. Available in private chats only.
* Field: request_managed_bot
  * Type: KeyboardButtonRequestManagedBot
  * Description: Optional. If specified, pressing the button will ask the user to create and share a bot that will be managed by the current bot. Available for bots that enabled management of other bots in the @BotFather Mini App. Available in private chats only.
* Field: request_contact
  * Type: Boolean
  * Description: Optional. If True, the user's phone number will be sent as a contact when the button is pressed. Available in private chats only.
* Field: request_location
  * Type: Boolean
  * Description: Optional. If True, the user's current location will be sent when the button is pressed. Available in private chats only.
* Field: request_poll
  * Type: KeyboardButtonPollType
  * Description: Optional. If specified, the user will be asked to create a poll and send it to the bot when the button is pressed. Available in private chats only.
* Field: web_app
  * Type: WebAppInfo
  * Description: Optional. If specified, the described Web App will be launched when the button is pressed. The Web App will be able to send a “web_app_data” service message. Available in private chats only.


#### [](#keyboardbuttonrequestusers)KeyboardButtonRequestUsers

This object defines the criteria used to request suitable users. Information about the selected users will be shared with the bot when the corresponding button is pressed. [More about requesting users »](about:/bots/features#chat-and-user-selection)



* Field: request_id
  * Type: Integer
  * Description: Signed 32-bit identifier of the request that will be received back in the UsersShared object. Must be unique within the message.
* Field: user_is_bot
  * Type: Boolean
  * Description: Optional. Pass True to request bots, pass False to request regular users. If not specified, no additional restrictions are applied.
* Field: user_is_premium
  * Type: Boolean
  * Description: Optional. Pass True to request premium users, pass False to request non-premium users. If not specified, no additional restrictions are applied.
* Field: max_quantity
  * Type: Integer
  * Description: Optional. The maximum number of users to be selected; 1-10. Defaults to 1.
* Field: request_name
  * Type: Boolean
  * Description: Optional. Pass True to request the users' first and last names
* Field: request_username
  * Type: Boolean
  * Description: Optional. Pass True to request the users' usernames
* Field: request_photo
  * Type: Boolean
  * Description: Optional. Pass True to request the users' photos


#### [](#keyboardbuttonrequestchat)KeyboardButtonRequestChat

This object defines the criteria used to request a suitable chat. Information about the selected chat will be shared with the bot when the corresponding button is pressed. The bot will be granted requested rights in the chat if appropriate. [More about requesting chats »](about:/bots/features#chat-and-user-selection).



* Field: request_id
  * Type: Integer
  * Description: Signed 32-bit identifier of the request, which will be received back in the ChatShared object. Must be unique within the message.
* Field: chat_is_channel
  * Type: Boolean
  * Description: Pass True to request a channel chat, pass False to request a group or a supergroup chat
* Field: chat_is_forum
  * Type: Boolean
  * Description: Optional. Pass True to request a forum supergroup, pass False to request a non-forum chat. If not specified, no additional restrictions are applied.
* Field: chat_has_username
  * Type: Boolean
  * Description: Optional. Pass True to request a supergroup or a channel with a username, pass False to request a chat without a username. If not specified, no additional restrictions are applied.
* Field: chat_is_created
  * Type: Boolean
  * Description: Optional. Pass True to request a chat owned by the user. Otherwise, no additional restrictions are applied.
* Field: user_administrator_rights
  * Type: ChatAdministratorRights
  * Description: Optional. A JSON-serialized object listing the required administrator rights of the user in the chat. The rights must be a superset of bot_administrator_rights. If not specified, no additional restrictions are applied.
* Field: bot_administrator_rights
  * Type: ChatAdministratorRights
  * Description: Optional. A JSON-serialized object listing the required administrator rights of the bot in the chat. The rights must be a subset of user_administrator_rights. If not specified, no additional restrictions are applied.
* Field: bot_is_member
  * Type: Boolean
  * Description: Optional. Pass True to request a chat with the bot as a member. Otherwise, no additional restrictions are applied.
* Field: request_title
  * Type: Boolean
  * Description: Optional. Pass True to request the chat's title
* Field: request_username
  * Type: Boolean
  * Description: Optional. Pass True to request the chat's username
* Field: request_photo
  * Type: Boolean
  * Description: Optional. Pass True to request the chat's photo


#### [](#keyboardbuttonrequestmanagedbot)KeyboardButtonRequestManagedBot

This object defines the parameters for the creation of a managed bot. Information about the created bot will be shared with the bot using the update _managed\_bot_ and a [Message](#message) with the field _managed\_bot\_created_.



* Field: request_id
  * Type: Integer
  * Description: Signed 32-bit identifier of the request. Must be unique within the message.
* Field: suggested_name
  * Type: String
  * Description: Optional. Suggested name for the bot
* Field: suggested_username
  * Type: String
  * Description: Optional. Suggested username for the bot


#### [](#keyboardbuttonpolltype)KeyboardButtonPollType

This object represents type of a poll, which is allowed to be created and sent when the corresponding button is pressed.



* Field: type
  * Type: String
  * Description: Optional. If quiz is passed, the user will be allowed to create only polls in the quiz mode. If regular is passed, only regular polls will be allowed. Otherwise, the user will be allowed to create a poll of any type.


#### [](#replykeyboardremove)ReplyKeyboardRemove

Upon receiving a message with this object, Telegram clients will remove the current custom keyboard and display the default letter-keyboard. By default, custom keyboards are displayed until a new keyboard is sent by a bot. An exception is made for one-time keyboards that are hidden immediately after the user presses a button (see [ReplyKeyboardMarkup](#replykeyboardmarkup)). Not supported in channels and for messages sent on behalf of a business account.



* Field: remove_keyboard
  * Type: True
  * Description: Requests clients to remove the custom keyboard (user will not be able to summon this keyboard; if you want to hide the keyboard from sight but keep it accessible, use one_time_keyboard in ReplyKeyboardMarkup)
* Field: selective
  * Type: Boolean
  * Description: Optional. Use this parameter if you want to remove the keyboard for specific users only. Targets: 1) users that are @mentioned in the text of the Message object; 2) if the bot's message is a reply to a message in the same chat and forum topic, sender of the original message.Example: A user votes in a poll, bot returns confirmation message in reply to the vote and removes the keyboard for that user, while still showing the keyboard with poll options to users who haven't voted yet.


#### [](#inlinekeyboardmarkup)InlineKeyboardMarkup

This object represents an [inline keyboard](about:/bots/features#inline-keyboards) that appears right next to the message it belongs to.



* Field: inline_keyboard
  * Type: Array of Array of InlineKeyboardButton
  * Description: Array of button rows, each represented by an Array of InlineKeyboardButton objects


#### [](#inlinekeyboardbutton)InlineKeyboardButton

This object represents one button of an inline keyboard. Exactly one of the fields other than _text_, _icon\_custom\_emoji\_id_, and _style_ must be used to specify the type of the button.



* Field: text
  * Type: String
  * Description: Label text on the button
* Field: icon_custom_emoji_id
  * Type: String
  * Description: Optional. Unique identifier of the custom emoji shown before the text of the button. Can only be used by bots that purchased additional usernames on Fragment or in the messages directly sent by the bot to private, group and supergroup chats if the owner of the bot has a Telegram Premium subscription.
* Field: style
  * Type: String
  * Description: Optional. Style of the button. Must be one of “danger” (red), “success” (green) or “primary” (blue). If omitted, then an app-specific style is used.
* Field: url
  * Type: String
  * Description: Optional. HTTP or tg:// URL to be opened when the button is pressed. Links tg://user?id=<user_id> can be used to mention a user by their identifier without using a username, if this is allowed by their privacy settings.
* Field: callback_data
  * Type: String
  * Description: Optional. Data to be sent in a callback query to the bot when the button is pressed, 1-64 bytes
* Field: web_app
  * Type: WebAppInfo
  * Description: Optional. Description of the Web App that will be launched when the user presses the button. The Web App will be able to send an arbitrary message on behalf of the user using the method answerWebAppQuery. Available only in private chats between a user and the bot. Not supported for messages sent on behalf of a business account.
* Field: login_url
  * Type: LoginUrl
  * Description: Optional. An HTTPS URL used to automatically authorize the user. Can be used as a replacement for the Telegram Login Widget.
* Field: switch_inline_query
  * Type: String
  * Description: Optional. If set, pressing the button will prompt the user to select one of their chats, open that chat and insert the bot's username and the specified inline query in the input field. May be empty, in which case just the bot's username will be inserted. Not supported for messages sent in channel direct messages chats and on behalf of a business account.
* Field: switch_inline_query_current_chat
  * Type: String
  * Description: Optional. If set, pressing the button will insert the bot's username and the specified inline query in the current chat's input field. May be empty, in which case only the bot's username will be inserted.This offers a quick way for the user to open your bot in inline mode in the same chat - good for selecting something from multiple options. Not supported in channels and for messages sent in channel direct messages chats and on behalf of a business account.
* Field: switch_inline_query_chosen_chat
  * Type: SwitchInlineQueryChosenChat
  * Description: Optional. If set, pressing the button will prompt the user to select one of their chats of the specified type, open that chat and insert the bot's username and the specified inline query in the input field. Not supported for messages sent in channel direct messages chats and on behalf of a business account.
* Field: copy_text
  * Type: CopyTextButton
  * Description: Optional. Description of the button that copies the specified text to the clipboard
* Field: callback_game
  * Type: CallbackGame
  * Description: Optional. Description of the game that will be launched when the user presses the button.NOTE: This type of button must always be the first button in the first row.
* Field: pay
  * Type: Boolean
  * Description: Optional. Specify True, to send a Pay button. Substrings “” and “XTR” in the buttons's text will be replaced with a Telegram Star icon.NOTE: This type of button must always be the first button in the first row and can only be used in invoice messages.


#### [](#loginurl)LoginUrl

This object represents a parameter of the inline keyboard button used to automatically authorize a user. Serves as a great replacement for the [Telegram Login Widget](https://core.telegram.org/widgets/login) when the user is coming from Telegram. All the user needs to do is tap/click a button and confirm that they want to log in:

[![TITLE](https://core.telegram.org/file/811140909/1631/20k1Z53eiyY.23995/c541e89b74253623d9 "TITLE")](/file/811140015/1734/8VZFkwWXalM.97872/6127fa62d8a0bf2b3c)

Telegram apps support these buttons as of [version 5.7](https://telegram.org/blog/privacy-discussions-web-bots#meet-seamless-web-bots).

> Sample bot: [@discussbot](https://t.me/discussbot)



* Field: url
  * Type: String
  * Description: An HTTPS URL to be opened with user authorization data added to the query string when the button is pressed. If the user refuses to provide authorization data, the original URL without information about the user will be opened. The data added is the same as described in Receiving authorization data.NOTE: You must always check the hash of the received data to verify the authentication and the integrity of the data as described in Checking authorization.
* Field: forward_text
  * Type: String
  * Description: Optional. New text of the button in forwarded messages
* Field: bot_username
  * Type: String
  * Description: Optional. Username of a bot, which will be used for user authorization. See Setting up a bot for more details. If not specified, the current bot's username will be assumed. The url's domain must be the same as the domain linked with the bot. See Linking your domain to the bot for more details.
* Field: request_write_access
  * Type: Boolean
  * Description: Optional. Pass True to request the permission for your bot to send messages to the user


#### [](#switchinlinequerychosenchat)SwitchInlineQueryChosenChat

This object represents an inline button that switches the current user to inline mode in a chosen chat, with an optional default inline query.



* Field: query
  * Type: String
  * Description: Optional. The default inline query to be inserted in the input field. If left empty, only the bot's username will be inserted.
* Field: allow_user_chats
  * Type: Boolean
  * Description: Optional. True, if private chats with users can be chosen
* Field: allow_bot_chats
  * Type: Boolean
  * Description: Optional. True, if private chats with bots can be chosen
* Field: allow_group_chats
  * Type: Boolean
  * Description: Optional. True, if group and supergroup chats can be chosen
* Field: allow_channel_chats
  * Type: Boolean
  * Description: Optional. True, if channel chats can be chosen


#### [](#copytextbutton)CopyTextButton

This object represents an inline keyboard button that copies specified text to the clipboard.


|Field|Type  |Description                                             |
|-----|------|--------------------------------------------------------|
|text |String|The text to be copied to the clipboard; 1-256 characters|


#### [](#callbackquery)CallbackQuery

This object represents an incoming callback query from a callback button in an [inline keyboard](about:/bots/features#inline-keyboards). If the button that originated the query was attached to a message sent by the bot, the field _message_ will be present. If the button was attached to a message sent via the bot (in [inline mode](#inline-mode)), the field _inline\_message\_id_ will be present. Exactly one of the fields _data_ or _game\_short\_name_ will be present.



* Field: id
  * Type: String
  * Description: Unique identifier for this query
* Field: from
  * Type: User
  * Description: Sender
* Field: message
  * Type: MaybeInaccessibleMessage
  * Description: Optional. Message sent by the bot with the callback button that originated the query
* Field: inline_message_id
  * Type: String
  * Description: Optional. Identifier of the message sent via the bot in inline mode, that originated the query
* Field: chat_instance
  * Type: String
  * Description: Global identifier, uniquely corresponding to the chat to which the message with the callback button was sent. Useful for high scores in games.
* Field: data
  * Type: String
  * Description: Optional. Data associated with the callback button. Be aware that the message originated the query can contain no callback buttons with this data.
* Field: game_short_name
  * Type: String
  * Description: Optional. Short name of a Game to be returned, serves as the unique identifier for the game


> **NOTE:** After the user presses a callback button, Telegram clients will display a progress bar until you call [answerCallbackQuery](#answercallbackquery). It is, therefore, necessary to react by calling [answerCallbackQuery](#answercallbackquery) even if no notification to the user is needed (e.g., without specifying any of the optional parameters).

#### [](#forcereply)ForceReply

Upon receiving a message with this object, Telegram clients will display a reply interface to the user (act as if the user has selected the bot's message and tapped 'Reply'). This can be extremely useful if you want to create user-friendly step-by-step interfaces without having to sacrifice [privacy mode](about:/bots/features#privacy-mode). Not supported in channels and for messages sent on behalf of a user account.



* Field: force_reply
  * Type: True
  * Description: Shows reply interface to the user, as if they manually selected the bot's message and tapped 'Reply'
* Field: input_field_placeholder
  * Type: String
  * Description: Optional. The placeholder to be shown in the input field when the reply is active; 1-64 characters
* Field: selective
  * Type: Boolean
  * Description: Optional. Use this parameter if you want to force reply from specific users only. Targets: 1) users that are @mentioned in the text of the Message object; 2) if the bot's message is a reply to a message in the same chat and forum topic, sender of the original message.


> **Example:** A [poll bot](https://t.me/PollBot) for groups runs in privacy mode (only receives commands, replies to its messages and mentions). There could be two ways to create a new poll:
> 
> *   Explain the user how to send a command with parameters (e.g. /newpoll question answer1 answer2). May be appealing for hardcore users but lacks modern day polish.
> *   Guide the user through a step-by-step process. 'Please send me your question', 'Cool, now let's add the first answer option', 'Great. Keep adding answer options, then send /done when you're ready'.
> 
> The last option is definitely more attractive. And if you use [ForceReply](#forcereply) in your bot's questions, it will receive the user's answers even if it only receives replies, commands and mentions - without any extra work for the user.

#### [](#chatphoto)ChatPhoto

This object represents a chat photo.



* Field: small_file_id
  * Type: String
  * Description: File identifier of small (160x160) chat photo. This file_id can be used only for photo download and only for as long as the photo is not changed.
* Field: small_file_unique_id
  * Type: String
  * Description: Unique file identifier of small (160x160) chat photo, which is supposed to be the same over time and for different bots. Can't be used to download or reuse the file.
* Field: big_file_id
  * Type: String
  * Description: File identifier of big (640x640) chat photo. This file_id can be used only for photo download and only for as long as the photo is not changed.
* Field: big_file_unique_id
  * Type: String
  * Description: Unique file identifier of big (640x640) chat photo, which is supposed to be the same over time and for different bots. Can't be used to download or reuse the file.


#### [](#chatinvitelink)ChatInviteLink

Represents an invite link for a chat.



* Field: invite_link
  * Type: String
  * Description: The invite link. If the link was created by another chat administrator, then the second part of the link will be replaced with “…”.
* Field: creator
  * Type: User
  * Description: Creator of the link
* Field: creates_join_request
  * Type: Boolean
  * Description: True, if users joining the chat via the link need to be approved by chat administrators
* Field: is_primary
  * Type: Boolean
  * Description: True, if the link is primary
* Field: is_revoked
  * Type: Boolean
  * Description: True, if the link is revoked
* Field: name
  * Type: String
  * Description: Optional. Invite link name
* Field: expire_date
  * Type: Integer
  * Description: Optional. Point in time (Unix timestamp) when the link will expire or has been expired
* Field: member_limit
  * Type: Integer
  * Description: Optional. The maximum number of users that can be members of the chat simultaneously after joining the chat via this invite link; 1-99999
* Field: pending_join_request_count
  * Type: Integer
  * Description: Optional. Number of pending join requests created using this link
* Field: subscription_period
  * Type: Integer
  * Description: Optional. The number of seconds the subscription will be active for before the next payment
* Field: subscription_price
  * Type: Integer
  * Description: Optional. The amount of Telegram Stars a user must pay initially and after each subsequent subscription period to be a member of the chat using the link


#### [](#chatadministratorrights)ChatAdministratorRights

Represents the rights of an administrator in a chat.



* Field: is_anonymous
  * Type: Boolean
  * Description: True, if the user's presence in the chat is hidden
* Field: can_manage_chat
  * Type: Boolean
  * Description: True, if the administrator can access the chat event log, get boost list, see hidden supergroup and channel members, report spam messages, ignore slow mode, and send messages to the chat without paying Telegram Stars. Implied by any other administrator privilege.
* Field: can_delete_messages
  * Type: Boolean
  * Description: True, if the administrator can delete messages of other users
* Field: can_manage_video_chats
  * Type: Boolean
  * Description: True, if the administrator can manage video chats
* Field: can_restrict_members
  * Type: Boolean
  * Description: True, if the administrator can restrict, ban or unban chat members, or access supergroup statistics
* Field: can_promote_members
  * Type: Boolean
  * Description: True, if the administrator can add new administrators with a subset of their own privileges or demote administrators that they have promoted, directly or indirectly (promoted by administrators that were appointed by the user)
* Field: can_change_info
  * Type: Boolean
  * Description: True, if the user is allowed to change the chat title, photo and other settings
* Field: can_invite_users
  * Type: Boolean
  * Description: True, if the user is allowed to invite new users to the chat
* Field: can_post_stories
  * Type: Boolean
  * Description: True, if the administrator can post stories to the chat
* Field: can_edit_stories
  * Type: Boolean
  * Description: True, if the administrator can edit stories posted by other users, post stories to the chat page, pin chat stories, and access the chat's story archive
* Field: can_delete_stories
  * Type: Boolean
  * Description: True, if the administrator can delete stories posted by other users
* Field: can_post_messages
  * Type: Boolean
  * Description: Optional. True, if the administrator can post messages in the channel, approve suggested posts, or access channel statistics; for channels only
* Field: can_edit_messages
  * Type: Boolean
  * Description: Optional. True, if the administrator can edit messages of other users and can pin messages; for channels only
* Field: can_pin_messages
  * Type: Boolean
  * Description: Optional. True, if the user is allowed to pin messages; for groups and supergroups only
* Field: can_manage_topics
  * Type: Boolean
  * Description: Optional. True, if the user is allowed to create, rename, close, and reopen forum topics; for supergroups only
* Field: can_manage_direct_messages
  * Type: Boolean
  * Description: Optional. True, if the administrator can manage direct messages of the channel and decline suggested posts; for channels only
* Field: can_manage_tags
  * Type: Boolean
  * Description: Optional. True, if the administrator can edit the tags of regular members; for groups and supergroups only. If omitted defaults to the value of can_pin_messages.


#### [](#chatmemberupdated)ChatMemberUpdated

This object represents changes in the status of a chat member.



* Field: chat
  * Type: Chat
  * Description: Chat the user belongs to
* Field: from
  * Type: User
  * Description: Performer of the action, which resulted in the change
* Field: date
  * Type: Integer
  * Description: Date the change was done in Unix time
* Field: old_chat_member
  * Type: ChatMember
  * Description: Previous information about the chat member
* Field: new_chat_member
  * Type: ChatMember
  * Description: New information about the chat member
* Field: invite_link
  * Type: ChatInviteLink
  * Description: Optional. Chat invite link, which was used by the user to join the chat; for joining by invite link events only
* Field: via_join_request
  * Type: Boolean
  * Description: Optional. True, if the user joined the chat after sending a direct join request without using an invite link and being approved by an administrator
* Field: via_chat_folder_invite_link
  * Type: Boolean
  * Description: Optional. True, if the user joined the chat via a chat folder invite link


#### [](#chatmember)ChatMember

This object contains information about one member of a chat. Currently, the following 6 types of chat members are supported:

*   [ChatMemberOwner](#chatmemberowner)
*   [ChatMemberAdministrator](#chatmemberadministrator)
*   [ChatMemberMember](#chatmembermember)
*   [ChatMemberRestricted](#chatmemberrestricted)
*   [ChatMemberLeft](#chatmemberleft)
*   [ChatMemberBanned](#chatmemberbanned)

#### [](#chatmemberowner)ChatMemberOwner

Represents a [chat member](#chatmember) that owns the chat and has all administrator privileges.


|Field       |Type   |Description                                       |
|------------|-------|--------------------------------------------------|
|status      |String |The member's status in the chat, always “creator” |
|user        |User   |Information about the user                        |
|is_anonymous|Boolean|True, if the user's presence in the chat is hidden|
|custom_title|String |Optional. Custom title for this user              |


#### [](#chatmemberadministrator)ChatMemberAdministrator

Represents a [chat member](#chatmember) that has some additional privileges.



* Field: status
  * Type: String
  * Description: The member's status in the chat, always “administrator”
* Field: user
  * Type: User
  * Description: Information about the user
* Field: can_be_edited
  * Type: Boolean
  * Description: True, if the bot is allowed to edit administrator privileges of that user
* Field: is_anonymous
  * Type: Boolean
  * Description: True, if the user's presence in the chat is hidden
* Field: can_manage_chat
  * Type: Boolean
  * Description: True, if the administrator can access the chat event log, get boost list, see hidden supergroup and channel members, report spam messages, ignore slow mode, and send messages to the chat without paying Telegram Stars. Implied by any other administrator privilege.
* Field: can_delete_messages
  * Type: Boolean
  * Description: True, if the administrator can delete messages of other users
* Field: can_manage_video_chats
  * Type: Boolean
  * Description: True, if the administrator can manage video chats
* Field: can_restrict_members
  * Type: Boolean
  * Description: True, if the administrator can restrict, ban or unban chat members, or access supergroup statistics
* Field: can_promote_members
  * Type: Boolean
  * Description: True, if the administrator can add new administrators with a subset of their own privileges or demote administrators that they have promoted, directly or indirectly (promoted by administrators that were appointed by the user)
* Field: can_change_info
  * Type: Boolean
  * Description: True, if the user is allowed to change the chat title, photo and other settings
* Field: can_invite_users
  * Type: Boolean
  * Description: True, if the user is allowed to invite new users to the chat
* Field: can_post_stories
  * Type: Boolean
  * Description: True, if the administrator can post stories to the chat
* Field: can_edit_stories
  * Type: Boolean
  * Description: True, if the administrator can edit stories posted by other users, post stories to the chat page, pin chat stories, and access the chat's story archive
* Field: can_delete_stories
  * Type: Boolean
  * Description: True, if the administrator can delete stories posted by other users
* Field: can_post_messages
  * Type: Boolean
  * Description: Optional. True, if the administrator can post messages in the channel, approve suggested posts, or access channel statistics; for channels only
* Field: can_edit_messages
  * Type: Boolean
  * Description: Optional. True, if the administrator can edit messages of other users and can pin messages; for channels only
* Field: can_pin_messages
  * Type: Boolean
  * Description: Optional. True, if the user is allowed to pin messages; for groups and supergroups only
* Field: can_manage_topics
  * Type: Boolean
  * Description: Optional. True, if the user is allowed to create, rename, close, and reopen forum topics; for supergroups only
* Field: can_manage_direct_messages
  * Type: Boolean
  * Description: Optional. True, if the administrator can manage direct messages of the channel and decline suggested posts; for channels only
* Field: can_manage_tags
  * Type: Boolean
  * Description: Optional. True, if the administrator can edit the tags of regular members; for groups and supergroups only. If omitted defaults to the value of can_pin_messages.
* Field: custom_title
  * Type: String
  * Description: Optional. Custom title for this user


#### [](#chatmembermember)ChatMemberMember

Represents a [chat member](#chatmember) that has no additional privileges or restrictions.


|Field     |Type   |Description                                                       |
|----------|-------|------------------------------------------------------------------|
|status    |String |The member's status in the chat, always “member”                  |
|tag       |String |Optional. Tag of the member                                       |
|user      |User   |Information about the user                                        |
|until_date|Integer|Optional. Date when the user's subscription will expire; Unix time|


#### [](#chatmemberrestricted)ChatMemberRestricted

Represents a [chat member](#chatmember) that is under certain restrictions in the chat. Supergroups only.



* Field: status
  * Type: String
  * Description: The member's status in the chat, always “restricted”
* Field: tag
  * Type: String
  * Description: Optional. Tag of the member
* Field: user
  * Type: User
  * Description: Information about the user
* Field: is_member
  * Type: Boolean
  * Description: True, if the user is a member of the chat at the moment of the request
* Field: can_send_messages
  * Type: Boolean
  * Description: True, if the user is allowed to send text messages, contacts, giveaways, giveaway winners, invoices, locations and venues
* Field: can_send_audios
  * Type: Boolean
  * Description: True, if the user is allowed to send audios
* Field: can_send_documents
  * Type: Boolean
  * Description: True, if the user is allowed to send documents
* Field: can_send_photos
  * Type: Boolean
  * Description: True, if the user is allowed to send photos
* Field: can_send_videos
  * Type: Boolean
  * Description: True, if the user is allowed to send videos
* Field: can_send_video_notes
  * Type: Boolean
  * Description: True, if the user is allowed to send video notes
* Field: can_send_voice_notes
  * Type: Boolean
  * Description: True, if the user is allowed to send voice notes
* Field: can_send_polls
  * Type: Boolean
  * Description: True, if the user is allowed to send polls and checklists
* Field: can_send_other_messages
  * Type: Boolean
  * Description: True, if the user is allowed to send animations, games, stickers and use inline bots
* Field: can_add_web_page_previews
  * Type: Boolean
  * Description: True, if the user is allowed to add web page previews to their messages
* Field: can_react_to_messages
  * Type: Boolean
  * Description: True, if the user is allowed to react to messages
* Field: can_edit_tag
  * Type: Boolean
  * Description: True, if the user is allowed to edit their own tag
* Field: can_change_info
  * Type: Boolean
  * Description: True, if the user is allowed to change the chat title, photo and other settings
* Field: can_invite_users
  * Type: Boolean
  * Description: True, if the user is allowed to invite new users to the chat
* Field: can_pin_messages
  * Type: Boolean
  * Description: True, if the user is allowed to pin messages
* Field: can_manage_topics
  * Type: Boolean
  * Description: True, if the user is allowed to create forum topics
* Field: until_date
  * Type: Integer
  * Description: Date when restrictions will be lifted for this user; Unix time. If 0, then the user is restricted forever.


#### [](#chatmemberleft)ChatMemberLeft

Represents a [chat member](#chatmember) that isn't currently a member of the chat, but may join it themselves.


|Field |Type  |Description                                   |
|------|------|----------------------------------------------|
|status|String|The member's status in the chat, always “left”|
|user  |User  |Information about the user                    |


#### [](#chatmemberbanned)ChatMemberBanned

Represents a [chat member](#chatmember) that was banned in the chat and can't return to the chat or view chat messages.



* Field: status
  * Type: String
  * Description: The member's status in the chat, always “kicked”
* Field: user
  * Type: User
  * Description: Information about the user
* Field: until_date
  * Type: Integer
  * Description: Date when restrictions will be lifted for this user; Unix time. If 0, then the user is banned forever.


#### [](#chatjoinrequest)ChatJoinRequest

Represents a join request sent to a chat.



* Field: chat
  * Type: Chat
  * Description: Chat to which the request was sent
* Field: from
  * Type: User
  * Description: User that sent the join request
* Field: user_chat_id
  * Type: Integer
  * Description: Identifier of a private chat with the user who sent the join request. This number may have more than 32 significant bits and some programming languages may have difficulty/silent defects in interpreting it. But it has at most 52 significant bits, so a 64-bit integer or double-precision float type are safe for storing this identifier. The bot can use this identifier for 5 minutes to send messages until the join request is processed, assuming no other administrator contacted the user.
* Field: date
  * Type: Integer
  * Description: Date the request was sent in Unix time
* Field: bio
  * Type: String
  * Description: Optional. Bio of the user
* Field: invite_link
  * Type: ChatInviteLink
  * Description: Optional. Chat invite link that was used by the user to send the join request


#### [](#chatpermissions)ChatPermissions

Describes actions that a non-administrator user is allowed to take in a chat.



* Field: can_send_messages
  * Type: Boolean
  * Description: Optional. True, if the user is allowed to send text messages, contacts, giveaways, giveaway winners, invoices, locations and venues
* Field: can_send_audios
  * Type: Boolean
  * Description: Optional. True, if the user is allowed to send audios
* Field: can_send_documents
  * Type: Boolean
  * Description: Optional. True, if the user is allowed to send documents
* Field: can_send_photos
  * Type: Boolean
  * Description: Optional. True, if the user is allowed to send photos
* Field: can_send_videos
  * Type: Boolean
  * Description: Optional. True, if the user is allowed to send videos
* Field: can_send_video_notes
  * Type: Boolean
  * Description: Optional. True, if the user is allowed to send video notes
* Field: can_send_voice_notes
  * Type: Boolean
  * Description: Optional. True, if the user is allowed to send voice notes
* Field: can_send_polls
  * Type: Boolean
  * Description: Optional. True, if the user is allowed to send polls and checklists
* Field: can_send_other_messages
  * Type: Boolean
  * Description: Optional. True, if the user is allowed to send animations, games, stickers and use inline bots
* Field: can_add_web_page_previews
  * Type: Boolean
  * Description: Optional. True, if the user is allowed to add web page previews to their messages
* Field: can_react_to_messages
  * Type: Boolean
  * Description: Optional. True, if the user is allowed to react to messages. If omitted, defaults to the value of can_send_messages.
* Field: can_edit_tag
  * Type: Boolean
  * Description: Optional. True, if the user is allowed to edit their own tag. If omitted, defaults to the value of can_pin_messages.
* Field: can_change_info
  * Type: Boolean
  * Description: Optional. True, if the user is allowed to change the chat title, photo and other settings. Ignored in public supergroups.
* Field: can_invite_users
  * Type: Boolean
  * Description: Optional. True, if the user is allowed to invite new users to the chat
* Field: can_pin_messages
  * Type: Boolean
  * Description: Optional. True, if the user is allowed to pin messages. Ignored in public supergroups.
* Field: can_manage_topics
  * Type: Boolean
  * Description: Optional. True, if the user is allowed to create forum topics. If omitted defaults to the value of can_pin_messages.


#### [](#birthdate)Birthdate

Describes the birthdate of a user.


|Field|Type   |Description                       |
|-----|-------|----------------------------------|
|day  |Integer|Day of the user's birth; 1-31     |
|month|Integer|Month of the user's birth; 1-12   |
|year |Integer|Optional. Year of the user's birth|


#### [](#businessintro)BusinessIntro

Contains information about the start page settings of a Telegram Business account.


|Field  |Type   |Description                                 |
|-------|-------|--------------------------------------------|
|title  |String |Optional. Title text of the business intro  |
|message|String |Optional. Message text of the business intro|
|sticker|Sticker|Optional. Sticker of the business intro     |


#### [](#businesslocation)BusinessLocation

Contains information about the location of a Telegram Business account.


|Field   |Type    |Description                       |
|--------|--------|----------------------------------|
|address |String  |Address of the business           |
|location|Location|Optional. Location of the business|


#### [](#businessopeninghoursinterval)BusinessOpeningHoursInterval

Describes an interval of time during which a business is open.



* Field: opening_minute
  * Type: Integer
  * Description: The minute's sequence number in a week, starting on Monday, marking the start of the time interval during which the business is open; 0 - 7 * 24 * 60
* Field: closing_minute
  * Type: Integer
  * Description: The minute's sequence number in a week, starting on Monday, marking the end of the time interval during which the business is open; 0 - 8 * 24 * 60


#### [](#businessopeninghours)BusinessOpeningHours

Describes the opening hours of a business.



* Field: time_zone_name
  * Type: String
  * Description: Unique name of the time zone for which the opening hours are defined
* Field: opening_hours
  * Type: Array of BusinessOpeningHoursInterval
  * Description: List of time intervals describing business opening hours


#### [](#userrating)UserRating

This object describes the rating of a user based on their Telegram Star spendings.



* Field: level
  * Type: Integer
  * Description: Current level of the user, indicating their reliability when purchasing digital goods and services. A higher level suggests a more trustworthy customer; a negative level is likely reason for concern.
* Field: rating
  * Type: Integer
  * Description: Numerical value of the user's rating; the higher the rating, the better
* Field: current_level_rating
  * Type: Integer
  * Description: The rating value required to get the current level
* Field: next_level_rating
  * Type: Integer
  * Description: Optional. The rating value required to get to the next level; omitted if the maximum level was reached


#### [](#storyareaposition)StoryAreaPosition

Describes the position of a clickable area within a story.



* Field: x_percentage
  * Type: Float
  * Description: The abscissa of the area's center, as a percentage of the media width
* Field: y_percentage
  * Type: Float
  * Description: The ordinate of the area's center, as a percentage of the media height
* Field: width_percentage
  * Type: Float
  * Description: The width of the area's rectangle, as a percentage of the media width
* Field: height_percentage
  * Type: Float
  * Description: The height of the area's rectangle, as a percentage of the media height
* Field: rotation_angle
  * Type: Float
  * Description: The clockwise rotation angle of the rectangle, in degrees; 0-360
* Field: corner_radius_percentage
  * Type: Float
  * Description: The radius of the rectangle corner rounding, as a percentage of the media width


#### [](#locationaddress)LocationAddress

Describes the physical address of a location.



* Field: country_code
  * Type: String
  * Description: The two-letter ISO 3166-1 alpha-2 country code of the country where the location is located
* Field: state
  * Type: String
  * Description: Optional. State of the location
* Field: city
  * Type: String
  * Description: Optional. City of the location
* Field: street
  * Type: String
  * Description: Optional. Street address of the location


#### [](#storyareatype)StoryAreaType

Describes the type of a clickable area on a story. Currently, it can be one of

*   [StoryAreaTypeLocation](#storyareatypelocation)
*   [StoryAreaTypeSuggestedReaction](#storyareatypesuggestedreaction)
*   [StoryAreaTypeLink](#storyareatypelink)
*   [StoryAreaTypeWeather](#storyareatypeweather)
*   [StoryAreaTypeUniqueGift](#storyareatypeuniquegift)

#### [](#storyareatypelocation)StoryAreaTypeLocation

Describes a story area pointing to a location. Currently, a story can have up to 10 location areas.


|Field    |Type           |Description                        |
|---------|---------------|-----------------------------------|
|type     |String         |Type of the area, always “location”|
|latitude |Float          |Location latitude in degrees       |
|longitude|Float          |Location longitude in degrees      |
|address  |LocationAddress|Optional. Address of the location  |


#### [](#storyareatypesuggestedreaction)StoryAreaTypeSuggestedReaction

Describes a story area pointing to a suggested reaction. Currently, a story can have up to 5 suggested reaction areas.


|Field        |Type        |Description                                                   |
|-------------|------------|--------------------------------------------------------------|
|type         |String      |Type of the area, always “suggested_reaction”                 |
|reaction_type|ReactionType|Type of the reaction                                          |
|is_dark      |Boolean     |Optional. Pass True if the reaction area has a dark background|
|is_flipped   |Boolean     |Optional. Pass True if reaction area corner is flipped        |


#### [](#storyareatypelink)StoryAreaTypeLink

Describes a story area pointing to an HTTP or tg:// link. Currently, a story can have up to 3 link areas.


|Field|Type  |Description                                            |
|-----|------|-------------------------------------------------------|
|type |String|Type of the area, always “link”                        |
|url  |String|HTTP or tg:// URL to be opened when the area is clicked|


#### [](#storyareatypeweather)StoryAreaTypeWeather

Describes a story area containing weather information. Currently, a story can have up to 3 weather areas.


|Field           |Type   |Description                                      |
|----------------|-------|-------------------------------------------------|
|type            |String |Type of the area, always “weather”               |
|temperature     |Float  |Temperature, in degree Celsius                   |
|emoji           |String |Emoji representing the weather                   |
|background_color|Integer|A color of the area background in the ARGB format|


#### [](#storyareatypeuniquegift)StoryAreaTypeUniqueGift

Describes a story area pointing to a unique gift. Currently, a story can have at most 1 unique gift area.


|Field|Type  |Description                           |
|-----|------|--------------------------------------|
|type |String|Type of the area, always “unique_gift”|
|name |String|Unique name of the gift               |


#### [](#storyarea)StoryArea

Describes a clickable area on a story media.


|Field   |Type             |Description         |
|--------|-----------------|--------------------|
|position|StoryAreaPosition|Position of the area|
|type    |StoryAreaType    |Type of the area    |


#### [](#chatlocation)ChatLocation

Represents a location to which a chat is connected.


|Field   |Type    |Description                                                                 |
|--------|--------|----------------------------------------------------------------------------|
|location|Location|The location to which the supergroup is connected. Can't be a live location.|
|address |String  |Location address; 1-64 characters, as defined by the chat owner             |


#### [](#reactiontype)ReactionType

This object describes the type of a reaction. Currently, it can be one of

*   [ReactionTypeEmoji](#reactiontypeemoji)
*   [ReactionTypeCustomEmoji](#reactiontypecustomemoji)
*   [ReactionTypePaid](#reactiontypepaid)

#### [](#reactiontypeemoji)ReactionTypeEmoji

The reaction is based on an emoji.



* Field: type
  * Type: String
  * Description: Type of the reaction, always “emoji”
* Field: emoji
  * Type: String
  * Description: Reaction emoji. Currently, it can be one of "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "".


#### [](#reactiontypecustomemoji)ReactionTypeCustomEmoji

The reaction is based on a custom emoji.


|Field          |Type  |Description                                |
|---------------|------|-------------------------------------------|
|type           |String|Type of the reaction, always “custom_emoji”|
|custom_emoji_id|String|Custom emoji identifier                    |


#### [](#reactiontypepaid)ReactionTypePaid

The reaction is paid.


|Field|Type  |Description                        |
|-----|------|-----------------------------------|
|type |String|Type of the reaction, always “paid”|


#### [](#reactioncount)ReactionCount

Represents a reaction added to a message along with the number of times it was added.


|Field      |Type        |Description                           |
|-----------|------------|--------------------------------------|
|type       |ReactionType|Type of the reaction                  |
|total_count|Integer     |Number of times the reaction was added|


#### [](#messagereactionupdated)MessageReactionUpdated

This object represents a change of a reaction on a message performed by a user.



* Field: chat
  * Type: Chat
  * Description: The chat containing the message the user reacted to
* Field: message_id
  * Type: Integer
  * Description: Unique identifier of the message inside the chat
* Field: user
  * Type: User
  * Description: Optional. The user that changed the reaction, if the user isn't anonymous
* Field: actor_chat
  * Type: Chat
  * Description: Optional. The chat on behalf of which the reaction was changed, if the user is anonymous
* Field: date
  * Type: Integer
  * Description: Date of the change in Unix time
* Field: old_reaction
  * Type: Array of ReactionType
  * Description: Previous list of reaction types that were set by the user
* Field: new_reaction
  * Type: Array of ReactionType
  * Description: New list of reaction types that have been set by the user


#### [](#messagereactioncountupdated)MessageReactionCountUpdated

This object represents reaction changes on a message with anonymous reactions.


|Field     |Type                  |Description                                      |
|----------|----------------------|-------------------------------------------------|
|chat      |Chat                  |The chat containing the message                  |
|message_id|Integer               |Unique message identifier inside the chat        |
|date      |Integer               |Date of the change in Unix time                  |
|reactions |Array of ReactionCount|List of reactions that are present on the message|


#### [](#forumtopic)ForumTopic

This object represents a forum topic.



* Field: message_thread_id
  * Type: Integer
  * Description: Unique identifier of the forum topic
* Field: name
  * Type: String
  * Description: Name of the topic
* Field: icon_color
  * Type: Integer
  * Description: Color of the topic icon in RGB format
* Field: icon_custom_emoji_id
  * Type: String
  * Description: Optional. Unique identifier of the custom emoji shown as the topic icon
* Field: is_name_implicit
  * Type: True
  * Description: Optional. True, if the name of the topic wasn't specified explicitly by its creator and likely needs to be changed by the bot


#### [](#giftbackground)GiftBackground

This object describes the background of a gift.


|Field       |Type   |Description                                 |
|------------|-------|--------------------------------------------|
|center_color|Integer|Center color of the background in RGB format|
|edge_color  |Integer|Edge color of the background in RGB format  |
|text_color  |Integer|Text color of the background in RGB format  |


#### [](#gift)Gift

This object represents a gift that can be sent by the bot.



* Field: id
  * Type: String
  * Description: Unique identifier of the gift
* Field: sticker
  * Type: Sticker
  * Description: The sticker that represents the gift
* Field: star_count
  * Type: Integer
  * Description: The number of Telegram Stars that must be paid to send the sticker
* Field: upgrade_star_count
  * Type: Integer
  * Description: Optional. The number of Telegram Stars that must be paid to upgrade the gift to a unique one
* Field: is_premium
  * Type: True
  * Description: Optional. True, if the gift can only be purchased by Telegram Premium subscribers
* Field: has_colors
  * Type: True
  * Description: Optional. True, if the gift can be used (after being upgraded) to customize a user's appearance
* Field: total_count
  * Type: Integer
  * Description: Optional. The total number of gifts of this type that can be sent by all users; for limited gifts only
* Field: remaining_count
  * Type: Integer
  * Description: Optional. The number of remaining gifts of this type that can be sent by all users; for limited gifts only
* Field: personal_total_count
  * Type: Integer
  * Description: Optional. The total number of gifts of this type that can be sent by the bot; for limited gifts only
* Field: personal_remaining_count
  * Type: Integer
  * Description: Optional. The number of remaining gifts of this type that can be sent by the bot; for limited gifts only
* Field: background
  * Type: GiftBackground
  * Description: Optional. Background of the gift
* Field: unique_gift_variant_count
  * Type: Integer
  * Description: Optional. The total number of different unique gifts that can be obtained by upgrading the gift
* Field: publisher_chat
  * Type: Chat
  * Description: Optional. Information about the chat that published the gift


#### [](#gifts)Gifts

This object represent a list of gifts.


|Field|Type         |Description      |
|-----|-------------|-----------------|
|gifts|Array of Gift|The list of gifts|


#### [](#uniquegiftmodel)UniqueGiftModel

This object describes the model of a unique gift.



* Field: name
  * Type: String
  * Description: Name of the model
* Field: sticker
  * Type: Sticker
  * Description: The sticker that represents the unique gift
* Field: rarity_per_mille
  * Type: Integer
  * Description: The number of unique gifts that receive this model for every 1000 gift upgrades. Always 0 for crafted gifts.
* Field: rarity
  * Type: String
  * Description: Optional. Rarity of the model if it is a crafted model. Currently, can be “uncommon”, “rare”, “epic”, or “legendary”.


#### [](#uniquegiftsymbol)UniqueGiftSymbol

This object describes the symbol shown on the pattern of a unique gift.



* Field: name
  * Type: String
  * Description: Name of the symbol
* Field: sticker
  * Type: Sticker
  * Description: The sticker that represents the unique gift
* Field: rarity_per_mille
  * Type: Integer
  * Description: The number of unique gifts that receive this model for every 1000 gifts upgraded


#### [](#uniquegiftbackdropcolors)UniqueGiftBackdropColors

This object describes the colors of the backdrop of a unique gift.


|Field       |Type   |Description                                          |
|------------|-------|-----------------------------------------------------|
|center_color|Integer|The color in the center of the backdrop in RGB format|
|edge_color  |Integer|The color on the edges of the backdrop in RGB format |
|symbol_color|Integer|The color to be applied to the symbol in RGB format  |
|text_color  |Integer|The color for the text on the backdrop in RGB format |


#### [](#uniquegiftbackdrop)UniqueGiftBackdrop

This object describes the backdrop of a unique gift.



* Field: name
  * Type: String
  * Description: Name of the backdrop
* Field: colors
  * Type: UniqueGiftBackdropColors
  * Description: Colors of the backdrop
* Field: rarity_per_mille
  * Type: Integer
  * Description: The number of unique gifts that receive this backdrop for every 1000 gifts upgraded


#### [](#uniquegiftcolors)UniqueGiftColors

This object contains information about the color scheme for a user's name, message replies and link previews based on a unique gift.



* Field: model_custom_emoji_id
  * Type: String
  * Description: Custom emoji identifier of the unique gift's model
* Field: symbol_custom_emoji_id
  * Type: String
  * Description: Custom emoji identifier of the unique gift's symbol
* Field: light_theme_main_color
  * Type: Integer
  * Description: Main color used in light themes; RGB format
* Field: light_theme_other_colors
  * Type: Array of Integer
  * Description: List of 1-3 additional colors used in light themes; RGB format
* Field: dark_theme_main_color
  * Type: Integer
  * Description: Main color used in dark themes; RGB format
* Field: dark_theme_other_colors
  * Type: Array of Integer
  * Description: List of 1-3 additional colors used in dark themes; RGB format


#### [](#uniquegift)UniqueGift

This object describes a unique gift that was upgraded from a regular gift.



* Field: gift_id
  * Type: String
  * Description: Identifier of the regular gift from which the gift was upgraded
* Field: base_name
  * Type: String
  * Description: Human-readable name of the regular gift from which this unique gift was upgraded
* Field: name
  * Type: String
  * Description: Unique name of the gift. This name can be used in https://t.me/nft/... links and story areas.
* Field: number
  * Type: Integer
  * Description: Unique number of the upgraded gift among gifts upgraded from the same regular gift
* Field: model
  * Type: UniqueGiftModel
  * Description: Model of the gift
* Field: symbol
  * Type: UniqueGiftSymbol
  * Description: Symbol of the gift
* Field: backdrop
  * Type: UniqueGiftBackdrop
  * Description: Backdrop of the gift
* Field: is_premium
  * Type: True
  * Description: Optional. True, if the original regular gift was exclusively purchaseable by Telegram Premium subscribers
* Field: is_burned
  * Type: True
  * Description: Optional. True, if the gift was used to craft another gift and isn't available anymore
* Field: is_from_blockchain
  * Type: True
  * Description: Optional. True, if the gift is assigned from the TON blockchain and can't be resold or transferred in Telegram
* Field: colors
  * Type: UniqueGiftColors
  * Description: Optional. The color scheme that can be used by the gift's owner for the chat's name, replies to messages and link previews; for business account gifts and gifts that are currently on sale only
* Field: publisher_chat
  * Type: Chat
  * Description: Optional. Information about the chat that published the gift


#### [](#giftinfo)GiftInfo

Describes a service message about a regular gift that was sent or received.



* Field: gift
  * Type: Gift
  * Description: Information about the gift
* Field: owned_gift_id
  * Type: String
  * Description: Optional. Unique identifier of the received gift for the bot; only present for gifts received on behalf of business accounts
* Field: convert_star_count
  * Type: Integer
  * Description: Optional. Number of Telegram Stars that can be claimed by the receiver by converting the gift; omitted if conversion to Telegram Stars is impossible
* Field: prepaid_upgrade_star_count
  * Type: Integer
  * Description: Optional. Number of Telegram Stars that were prepaid for the ability to upgrade the gift
* Field: is_upgrade_separate
  * Type: True
  * Description: Optional. True, if the gift's upgrade was purchased after the gift was sent
* Field: can_be_upgraded
  * Type: True
  * Description: Optional. True, if the gift can be upgraded to a unique gift
* Field: text
  * Type: String
  * Description: Optional. Text of the message that was added to the gift
* Field: entities
  * Type: Array of MessageEntity
  * Description: Optional. Special entities that appear in the text
* Field: is_private
  * Type: True
  * Description: Optional. True, if the sender and gift text are shown only to the gift receiver; otherwise, everyone will be able to see them
* Field: unique_gift_number
  * Type: Integer
  * Description: Optional. Unique number reserved for this gift when upgraded. See the number field in UniqueGift.


#### [](#uniquegiftinfo)UniqueGiftInfo

Describes a service message about a unique gift that was sent or received.



* Field: gift
  * Type: UniqueGift
  * Description: Information about the gift
* Field: origin
  * Type: String
  * Description: Origin of the gift. Currently, either “upgrade” for gifts upgraded from regular gifts, “transfer” for gifts transferred from other users or channels, “resale” for gifts bought from other users, “gifted_upgrade” for upgrades purchased after the gift was sent, or “offer” for gifts bought or sold through gift purchase offers.
* Field: last_resale_currency
  * Type: String
  * Description: Optional. For gifts bought from other users, the currency in which the payment for the gift was done. Currently, one of “XTR” for Telegram Stars or “TON” for toncoins.
* Field: last_resale_amount
  * Type: Integer
  * Description: Optional. For gifts bought from other users, the price paid for the gift in either Telegram Stars or nanotoncoins
* Field: owned_gift_id
  * Type: String
  * Description: Optional. Unique identifier of the received gift for the bot; only present for gifts received on behalf of business accounts
* Field: transfer_star_count
  * Type: Integer
  * Description: Optional. Number of Telegram Stars that must be paid to transfer the gift; omitted if the bot cannot transfer the gift
* Field: next_transfer_date
  * Type: Integer
  * Description: Optional. Point in time (Unix timestamp) when the gift can be transferred. If it is in the past, then the gift can be transferred now.


#### [](#ownedgift)OwnedGift

This object describes a gift received and owned by a user or a chat. Currently, it can be one of

*   [OwnedGiftRegular](#ownedgiftregular)
*   [OwnedGiftUnique](#ownedgiftunique)

#### [](#ownedgiftregular)OwnedGiftRegular

Describes a regular gift owned by a user or a chat.



* Field: type
  * Type: String
  * Description: Type of the gift, always “regular”
* Field: gift
  * Type: Gift
  * Description: Information about the regular gift
* Field: owned_gift_id
  * Type: String
  * Description: Optional. Unique identifier of the gift for the bot; for gifts received on behalf of business accounts only
* Field: sender_user
  * Type: User
  * Description: Optional. Sender of the gift if it is a known user
* Field: send_date
  * Type: Integer
  * Description: Date the gift was sent in Unix time
* Field: text
  * Type: String
  * Description: Optional. Text of the message that was added to the gift
* Field: entities
  * Type: Array of MessageEntity
  * Description: Optional. Special entities that appear in the text
* Field: is_private
  * Type: True
  * Description: Optional. True, if the sender and gift text are shown only to the gift receiver; otherwise, everyone will be able to see them
* Field: is_saved
  * Type: True
  * Description: Optional. True, if the gift is displayed on the account's profile page; for gifts received on behalf of business accounts only
* Field: can_be_upgraded
  * Type: True
  * Description: Optional. True, if the gift can be upgraded to a unique gift; for gifts received on behalf of business accounts only
* Field: was_refunded
  * Type: True
  * Description: Optional. True, if the gift was refunded and isn't available anymore
* Field: convert_star_count
  * Type: Integer
  * Description: Optional. Number of Telegram Stars that can be claimed by the receiver instead of the gift; omitted if the gift cannot be converted to Telegram Stars; for gifts received on behalf of business accounts only
* Field: prepaid_upgrade_star_count
  * Type: Integer
  * Description: Optional. Number of Telegram Stars that were paid for the ability to upgrade the gift
* Field: is_upgrade_separate
  * Type: True
  * Description: Optional. True, if the gift's upgrade was purchased after the gift was sent; for gifts received on behalf of business accounts only
* Field: unique_gift_number
  * Type: Integer
  * Description: Optional. Unique number reserved for this gift when upgraded. See the number field in UniqueGift.


#### [](#ownedgiftunique)OwnedGiftUnique

Describes a unique gift received and owned by a user or a chat.



* Field: type
  * Type: String
  * Description: Type of the gift, always “unique”
* Field: gift
  * Type: UniqueGift
  * Description: Information about the unique gift
* Field: owned_gift_id
  * Type: String
  * Description: Optional. Unique identifier of the received gift for the bot; for gifts received on behalf of business accounts only
* Field: sender_user
  * Type: User
  * Description: Optional. Sender of the gift if it is a known user
* Field: send_date
  * Type: Integer
  * Description: Date the gift was sent in Unix time
* Field: is_saved
  * Type: True
  * Description: Optional. True, if the gift is displayed on the account's profile page; for gifts received on behalf of business accounts only
* Field: can_be_transferred
  * Type: True
  * Description: Optional. True, if the gift can be transferred to another owner; for gifts received on behalf of business accounts only
* Field: transfer_star_count
  * Type: Integer
  * Description: Optional. Number of Telegram Stars that must be paid to transfer the gift; omitted if the bot cannot transfer the gift
* Field: next_transfer_date
  * Type: Integer
  * Description: Optional. Point in time (Unix timestamp) when the gift can be transferred. If it is in the past, then the gift can be transferred now.


#### [](#ownedgifts)OwnedGifts

Contains the list of gifts received and owned by a user or a chat.



* Field: total_count
  * Type: Integer
  * Description: The total number of gifts owned by the user or the chat
* Field: gifts
  * Type: Array of OwnedGift
  * Description: The list of gifts
* Field: next_offset
  * Type: String
  * Description: Optional. Offset for the next request. If empty, then there are no more results.


#### [](#botaccesssettings)BotAccessSettings

This object describes the access settings of a bot.



* Field: is_access_restricted
  * Type: Boolean
  * Description: True, if only selected users can access the bot. The bot's owner can always access it.
* Field: added_users
  * Type: Array of User
  * Description: Optional. The list of other users who have access to the bot if the access is restricted


#### [](#acceptedgifttypes)AcceptedGiftTypes

This object describes the types of gifts that can be gifted to a user or a chat.



* Field: unlimited_gifts
  * Type: Boolean
  * Description: True, if unlimited regular gifts are accepted
* Field: limited_gifts
  * Type: Boolean
  * Description: True, if limited regular gifts are accepted
* Field: unique_gifts
  * Type: Boolean
  * Description: True, if unique gifts or gifts that can be upgraded to unique for free are accepted
* Field: premium_subscription
  * Type: Boolean
  * Description: True, if a Telegram Premium subscription is accepted
* Field: gifts_from_channels
  * Type: Boolean
  * Description: True, if transfers of unique gifts from channels are accepted


#### [](#staramount)StarAmount

Describes an amount of Telegram Stars.



* Field: amount
  * Type: Integer
  * Description: Integer amount of Telegram Stars, rounded to 0; can be negative
* Field: nanostar_amount
  * Type: Integer
  * Description: Optional. The number of 1/1000000000 shares of Telegram Stars; from -999999999 to 999999999; can be negative if and only if amount is non-positive


#### [](#botcommand)BotCommand

This object represents a bot command.



* Field: command
  * Type: String
  * Description: Text of the command; 1-32 characters. Can contain only lowercase English letters, digits and underscores.
* Field: description
  * Type: String
  * Description: Description of the command; 1-256 characters


#### [](#botcommandscope)BotCommandScope

This object represents the scope to which bot commands are applied. Currently, the following 7 scopes are supported:

*   [BotCommandScopeDefault](#botcommandscopedefault)
*   [BotCommandScopeAllPrivateChats](#botcommandscopeallprivatechats)
*   [BotCommandScopeAllGroupChats](#botcommandscopeallgroupchats)
*   [BotCommandScopeAllChatAdministrators](#botcommandscopeallchatadministrators)
*   [BotCommandScopeChat](#botcommandscopechat)
*   [BotCommandScopeChatAdministrators](#botcommandscopechatadministrators)
*   [BotCommandScopeChatMember](#botcommandscopechatmember)

#### [](#determining-list-of-commands)Determining list of commands

The following algorithm is used to determine the list of commands for a particular user viewing the bot menu. The first list of commands which is set is returned:

**Commands in the chat with the bot**

*   botCommandScopeChat + language\_code
*   botCommandScopeChat
*   botCommandScopeAllPrivateChats + language\_code
*   botCommandScopeAllPrivateChats
*   botCommandScopeDefault + language\_code
*   botCommandScopeDefault

**Commands in group and supergroup chats**

*   botCommandScopeChatMember + language\_code
*   botCommandScopeChatMember
*   botCommandScopeChatAdministrators + language\_code (administrators only)
*   botCommandScopeChatAdministrators (administrators only)
*   botCommandScopeChat + language\_code
*   botCommandScopeChat
*   botCommandScopeAllChatAdministrators + language\_code (administrators only)
*   botCommandScopeAllChatAdministrators (administrators only)
*   botCommandScopeAllGroupChats + language\_code
*   botCommandScopeAllGroupChats
*   botCommandScopeDefault + language\_code
*   botCommandScopeDefault

#### [](#botcommandscopedefault)BotCommandScopeDefault

Represents the default [scope](#botcommandscope) of bot commands. Default commands are used if no commands with a [narrower scope](#determining-list-of-commands) are specified for the user.


|Field|Type  |Description                |
|-----|------|---------------------------|
|type |String|Scope type, must be default|


#### [](#botcommandscopeallprivatechats)BotCommandScopeAllPrivateChats

Represents the [scope](#botcommandscope) of bot commands, covering all private chats.


|Field|Type  |Description                          |
|-----|------|-------------------------------------|
|type |String|Scope type, must be all_private_chats|


#### [](#botcommandscopeallgroupchats)BotCommandScopeAllGroupChats

Represents the [scope](#botcommandscope) of bot commands, covering all group and supergroup chats.


|Field|Type  |Description                        |
|-----|------|-----------------------------------|
|type |String|Scope type, must be all_group_chats|


#### [](#botcommandscopeallchatadministrators)BotCommandScopeAllChatAdministrators

Represents the [scope](#botcommandscope) of bot commands, covering all group and supergroup chat administrators.


|Field|Type  |Description                                |
|-----|------|-------------------------------------------|
|type |String|Scope type, must be all_chat_administrators|


#### [](#botcommandscopechat)BotCommandScopeChat

Represents the [scope](#botcommandscope) of bot commands, covering a specific chat.



* Field: type
  * Type: String
  * Description: Scope type, must be chat
* Field: chat_id
  * Type: Integer or String
  * Description: Unique identifier for the target chat or username of the target supergroup in the format @username. Channel direct messages chats and channel chats aren't supported.


#### [](#botcommandscopechatadministrators)BotCommandScopeChatAdministrators

Represents the [scope](#botcommandscope) of bot commands, covering all administrators of a specific group or supergroup chat.



* Field: type
  * Type: String
  * Description: Scope type, must be chat_administrators
* Field: chat_id
  * Type: Integer or String
  * Description: Unique identifier for the target chat or username of the target supergroup in the format @username. Channel direct messages chats and channel chats aren't supported.


#### [](#botcommandscopechatmember)BotCommandScopeChatMember

Represents the [scope](#botcommandscope) of bot commands, covering a specific member of a group or supergroup chat.



* Field: type
  * Type: String
  * Description: Scope type, must be chat_member
* Field: chat_id
  * Type: Integer or String
  * Description: Unique identifier for the target chat or username of the target supergroup in the format @username. Channel direct messages chats and channel chats aren't supported.
* Field: user_id
  * Type: Integer
  * Description: Unique identifier of the target user


#### [](#botname)BotName

This object represents the bot's name.


|Field|Type  |Description   |
|-----|------|--------------|
|name |String|The bot's name|


#### [](#botdescription)BotDescription

This object represents the bot's description.


|Field      |Type  |Description          |
|-----------|------|---------------------|
|description|String|The bot's description|


#### [](#botshortdescription)BotShortDescription

This object represents the bot's short description.


|Field            |Type  |Description                |
|-----------------|------|---------------------------|
|short_description|String|The bot's short description|


#### [](#menubutton)MenuButton

This object describes the bot's menu button in a private chat. It should be one of

*   [MenuButtonCommands](#menubuttoncommands)
*   [MenuButtonWebApp](#menubuttonwebapp)
*   [MenuButtonDefault](#menubuttondefault)

If a menu button other than [MenuButtonDefault](#menubuttondefault) is set for a private chat, then it is applied in the chat. Otherwise the default menu button is applied. By default, the menu button opens the list of bot commands.

#### [](#menubuttoncommands)MenuButtonCommands

Represents a menu button, which opens the bot's list of commands.


|Field|Type  |Description                         |
|-----|------|------------------------------------|
|type |String|Type of the button, must be commands|


#### [](#menubuttonwebapp)MenuButtonWebApp

Represents a menu button, which launches a [Web App](https://core.telegram.org/bots/webapps).



* Field: type
  * Type: String
  * Description: Type of the button, must be web_app
* Field: text
  * Type: String
  * Description: Text on the button
* Field: web_app
  * Type: WebAppInfo
  * Description: Description of the Web App that will be launched when the user presses the button. The Web App will be able to send an arbitrary message on behalf of the user using the method answerWebAppQuery. Alternatively, a t.me link to a Web App of the bot can be specified in the object instead of the Web App's URL, in which case the Web App will be opened as if the user pressed the link.


#### [](#menubuttondefault)MenuButtonDefault

Describes that no specific value for the menu button was set.


|Field|Type  |Description                        |
|-----|------|-----------------------------------|
|type |String|Type of the button, must be default|


#### [](#chatboostsource)ChatBoostSource

This object describes the source of a chat boost. It can be one of

*   [ChatBoostSourcePremium](#chatboostsourcepremium)
*   [ChatBoostSourceGiftCode](#chatboostsourcegiftcode)
*   [ChatBoostSourceGiveaway](#chatboostsourcegiveaway)

#### [](#chatboostsourcepremium)ChatBoostSourcePremium

The boost was obtained by subscribing to Telegram Premium or by gifting a Telegram Premium subscription to another user.


|Field |Type  |Description                          |
|------|------|-------------------------------------|
|source|String|Source of the boost, always “premium”|
|user  |User  |User that boosted the chat           |


#### [](#chatboostsourcegiftcode)ChatBoostSourceGiftCode

The boost was obtained by the creation of Telegram Premium gift codes to boost a chat. Each such code boosts the chat 4 times for the duration of the corresponding Telegram Premium subscription.


|Field |Type  |Description                             |
|------|------|----------------------------------------|
|source|String|Source of the boost, always “gift_code” |
|user  |User  |User for which the gift code was created|


#### [](#chatboostsourcegiveaway)ChatBoostSourceGiveaway

The boost was obtained by the creation of a Telegram Premium or a Telegram Star giveaway. This boosts the chat 4 times for the duration of the corresponding Telegram Premium subscription for Telegram Premium giveaways and _prize\_star\_count_ / 500 times for one year for Telegram Star giveaways.



* Field: source
  * Type: String
  * Description: Source of the boost, always “giveaway”
* Field: giveaway_message_id
  * Type: Integer
  * Description: Identifier of a message in the chat with the giveaway; the message could have been deleted already. May be 0 if the message isn't sent yet.
* Field: user
  * Type: User
  * Description: Optional. User that won the prize in the giveaway if any; for Telegram Premium giveaways only
* Field: prize_star_count
  * Type: Integer
  * Description: Optional. The number of Telegram Stars to be split between giveaway winners; for Telegram Star giveaways only
* Field: is_unclaimed
  * Type: True
  * Description: Optional. True, if the giveaway was completed, but there was no user to win the prize


#### [](#chatboost)ChatBoost

This object contains information about a chat boost.



* Field: boost_id
  * Type: String
  * Description: Unique identifier of the boost
* Field: add_date
  * Type: Integer
  * Description: Point in time (Unix timestamp) when the chat was boosted
* Field: expiration_date
  * Type: Integer
  * Description: Point in time (Unix timestamp) when the boost will automatically expire, unless the booster's Telegram Premium subscription is prolonged
* Field: source
  * Type: ChatBoostSource
  * Description: Source of the added boost


#### [](#chatboostupdated)ChatBoostUpdated

This object represents a boost added to a chat or changed.


|Field|Type     |Description                     |
|-----|---------|--------------------------------|
|chat |Chat     |Chat which was boosted          |
|boost|ChatBoost|Information about the chat boost|


#### [](#chatboostremoved)ChatBoostRemoved

This object represents a boost removed from a chat.


|Field      |Type           |Description                                              |
|-----------|---------------|---------------------------------------------------------|
|chat       |Chat           |Chat which was boosted                                   |
|boost_id   |String         |Unique identifier of the boost                           |
|remove_date|Integer        |Point in time (Unix timestamp) when the boost was removed|
|source     |ChatBoostSource|Source of the removed boost                              |


#### [](#chatownerleft)ChatOwnerLeft

Describes a service message about the chat owner leaving the chat.



* Field: new_owner
  * Type: User
  * Description: Optional. The user who will become the new owner of the chat if the previous owner does not return to the chat


#### [](#chatownerchanged)ChatOwnerChanged

Describes a service message about an ownership change in the chat.


|Field    |Type|Description              |
|---------|----|-------------------------|
|new_owner|User|The new owner of the chat|


#### [](#userchatboosts)UserChatBoosts

This object represents a list of boosts added to a chat by a user.


|Field |Type              |Description                                     |
|------|------------------|------------------------------------------------|
|boosts|Array of ChatBoost|The list of boosts added to the chat by the user|


#### [](#businessbotrights)BusinessBotRights

Represents the rights of a business bot.



* Field: can_reply
  * Type: True
  * Description: Optional. True, if the bot can send and edit messages in the private chats that had incoming messages in the last 24 hours
* Field: can_read_messages
  * Type: True
  * Description: Optional. True, if the bot can mark incoming private messages as read
* Field: can_delete_sent_messages
  * Type: True
  * Description: Optional. True, if the bot can delete messages sent by the bot
* Field: can_delete_all_messages
  * Type: True
  * Description: Optional. True, if the bot can delete all private messages in managed chats
* Field: can_edit_name
  * Type: True
  * Description: Optional. True, if the bot can edit the first and last name of the business account
* Field: can_edit_bio
  * Type: True
  * Description: Optional. True, if the bot can edit the bio of the business account
* Field: can_edit_profile_photo
  * Type: True
  * Description: Optional. True, if the bot can edit the profile photo of the business account
* Field: can_edit_username
  * Type: True
  * Description: Optional. True, if the bot can edit the username of the business account
* Field: can_change_gift_settings
  * Type: True
  * Description: Optional. True, if the bot can change the privacy settings pertaining to gifts for the business account
* Field: can_view_gifts_and_stars
  * Type: True
  * Description: Optional. True, if the bot can view gifts and the amount of Telegram Stars owned by the business account
* Field: can_convert_gifts_to_stars
  * Type: True
  * Description: Optional. True, if the bot can convert regular gifts owned by the business account to Telegram Stars
* Field: can_transfer_and_upgrade_gifts
  * Type: True
  * Description: Optional. True, if the bot can transfer and upgrade gifts owned by the business account
* Field: can_transfer_stars
  * Type: True
  * Description: Optional. True, if the bot can transfer Telegram Stars received by the business account to its own account, or use them to upgrade and transfer gifts
* Field: can_manage_stories
  * Type: True
  * Description: Optional. True, if the bot can post, edit and delete stories on behalf of the business account


#### [](#businessconnection)BusinessConnection

Describes the connection of the bot with a business account.



* Field: id
  * Type: String
  * Description: Unique identifier of the business connection
* Field: user
  * Type: User
  * Description: Business account user that created the business connection
* Field: user_chat_id
  * Type: Integer
  * Description: Identifier of a private chat with the user who created the business connection. This number may have more than 32 significant bits and some programming languages may have difficulty/silent defects in interpreting it. But it has at most 52 significant bits, so a 64-bit integer or double-precision float type are safe for storing this identifier.
* Field: date
  * Type: Integer
  * Description: Date the connection was established in Unix time
* Field: rights
  * Type: BusinessBotRights
  * Description: Optional. Rights of the business bot
* Field: is_enabled
  * Type: Boolean
  * Description: True, if the connection is active


#### [](#businessmessagesdeleted)BusinessMessagesDeleted

This object is received when messages are deleted from a connected business account.



* Field: business_connection_id
  * Type: String
  * Description: Unique identifier of the business connection
* Field: chat
  * Type: Chat
  * Description: Information about a chat in the business account. The bot may not have access to the chat or the corresponding user.
* Field: message_ids
  * Type: Array of Integer
  * Description: The list of identifiers of deleted messages in the chat of the business account


#### [](#sentwebappmessage)SentWebAppMessage

Describes an inline message sent by a [Web App](https://core.telegram.org/bots/webapps) on behalf of a user.



* Field: inline_message_id
  * Type: String
  * Description: Optional. Identifier of the sent inline message. Available only if there is an inline keyboard attached to the message.


#### [](#sentguestmessage)SentGuestMessage

Describes an inline message sent by a guest bot.


|Field            |Type  |Description                          |
|-----------------|------|-------------------------------------|
|inline_message_id|String|Identifier of the sent inline message|


#### [](#preparedinlinemessage)PreparedInlineMessage

Describes an inline message to be sent by a user of a Mini App.



* Field: id
  * Type: String
  * Description: Unique identifier of the prepared message
* Field: expiration_date
  * Type: Integer
  * Description: Expiration date of the prepared message, in Unix time. Expired prepared messages can no longer be used.


#### [](#preparedkeyboardbutton)PreparedKeyboardButton

Describes a keyboard button to be used by a user of a Mini App.


|Field|Type  |Description                             |
|-----|------|----------------------------------------|
|id   |String|Unique identifier of the keyboard button|


#### [](#responseparameters)ResponseParameters

Describes why a request was unsuccessful.



* Field: migrate_to_chat_id
  * Type: Integer
  * Description: Optional. The group has been migrated to a supergroup with the specified identifier. This number may have more than 32 significant bits and some programming languages may have difficulty/silent defects in interpreting it. But it has at most 52 significant bits, so a signed 64-bit integer or double-precision float type are safe for storing this identifier.
* Field: retry_after
  * Type: Integer
  * Description: Optional. In case of exceeding flood control, the number of seconds left to wait before the request can be repeated


#### [](#inputmedia)InputMedia

This object represents the content of a media message to be sent. It should be one of

*   [InputMediaAnimation](#inputmediaanimation)
*   [InputMediaAudio](#inputmediaaudio)
*   [InputMediaDocument](#inputmediadocument)
*   [InputMediaLivePhoto](#inputmedialivephoto)
*   [InputMediaPhoto](#inputmediaphoto)
*   [InputMediaVideo](#inputmediavideo)

#### [](#inputmediaanimation)InputMediaAnimation

Represents an animation file (GIF or H.264/MPEG-4 AVC video without sound) to be sent.



* Field: type
  * Type: String
  * Description: Type of the result, must be animation
* Field: media
  * Type: String
  * Description: File to send. Pass a file_id to send a file that exists on the Telegram servers (recommended), pass an HTTP URL for Telegram to get a file from the Internet, or pass “attach://<file_attach_name>” to upload a new one using multipart/form-data under <file_attach_name> name. More information on Sending Files »
* Field: thumbnail
  * Type: String
  * Description: Optional. Thumbnail of the file sent; can be ignored if thumbnail generation for the file is supported server-side. The thumbnail should be in JPEG format and less than 200 kB in size. A thumbnail's width and height should not exceed 320. Ignored if the file is not uploaded using multipart/form-data. Thumbnails can't be reused and can be only uploaded as a new file, so you can pass “attach://<file_attach_name>” if the thumbnail was uploaded using multipart/form-data under <file_attach_name>. More information on Sending Files »
* Field: caption
  * Type: String
  * Description: Optional. Caption of the animation to be sent, 0-1024 characters after entities parsing
* Field: parse_mode
  * Type: String
  * Description: Optional. Mode for parsing entities in the animation caption. See formatting options for more details.
* Field: caption_entities
  * Type: Array of MessageEntity
  * Description: Optional. List of special entities that appear in the caption, which can be specified instead of parse_mode
* Field: show_caption_above_media
  * Type: Boolean
  * Description: Optional. Pass True, if the caption must be shown above the message media
* Field: width
  * Type: Integer
  * Description: Optional. Animation width
* Field: height
  * Type: Integer
  * Description: Optional. Animation height
* Field: duration
  * Type: Integer
  * Description: Optional. Animation duration in seconds
* Field: has_spoiler
  * Type: Boolean
  * Description: Optional. Pass True if the animation needs to be covered with a spoiler animation


#### [](#inputmediaaudio)InputMediaAudio

Represents an audio file to be treated as music to be sent.



* Field: type
  * Type: String
  * Description: Type of the result, must be audio
* Field: media
  * Type: String
  * Description: File to send. Pass a file_id to send a file that exists on the Telegram servers (recommended), pass an HTTP URL for Telegram to get a file from the Internet, or pass “attach://<file_attach_name>” to upload a new one using multipart/form-data under <file_attach_name> name. More information on Sending Files »
* Field: thumbnail
  * Type: String
  * Description: Optional. Thumbnail of the file sent; can be ignored if thumbnail generation for the file is supported server-side. The thumbnail should be in JPEG format and less than 200 kB in size. A thumbnail's width and height should not exceed 320. Ignored if the file is not uploaded using multipart/form-data. Thumbnails can't be reused and can be only uploaded as a new file, so you can pass “attach://<file_attach_name>” if the thumbnail was uploaded using multipart/form-data under <file_attach_name>. More information on Sending Files »
* Field: caption
  * Type: String
  * Description: Optional. Caption of the audio to be sent, 0-1024 characters after entities parsing
* Field: parse_mode
  * Type: String
  * Description: Optional. Mode for parsing entities in the audio caption. See formatting options for more details.
* Field: caption_entities
  * Type: Array of MessageEntity
  * Description: Optional. List of special entities that appear in the caption, which can be specified instead of parse_mode
* Field: duration
  * Type: Integer
  * Description: Optional. Duration of the audio in seconds
* Field: performer
  * Type: String
  * Description: Optional. Performer of the audio
* Field: title
  * Type: String
  * Description: Optional. Title of the audio


#### [](#inputmediadocument)InputMediaDocument

Represents a general file to be sent.



* Field: type
  * Type: String
  * Description: Type of the result, must be document
* Field: media
  * Type: String
  * Description: File to send. Pass a file_id to send a file that exists on the Telegram servers (recommended), pass an HTTP URL for Telegram to get a file from the Internet, or pass “attach://<file_attach_name>” to upload a new one using multipart/form-data under <file_attach_name> name. More information on Sending Files »
* Field: thumbnail
  * Type: String
  * Description: Optional. Thumbnail of the file sent; can be ignored if thumbnail generation for the file is supported server-side. The thumbnail should be in JPEG format and less than 200 kB in size. A thumbnail's width and height should not exceed 320. Ignored if the file is not uploaded using multipart/form-data. Thumbnails can't be reused and can be only uploaded as a new file, so you can pass “attach://<file_attach_name>” if the thumbnail was uploaded using multipart/form-data under <file_attach_name>. More information on Sending Files »
* Field: caption
  * Type: String
  * Description: Optional. Caption of the document to be sent, 0-1024 characters after entities parsing
* Field: parse_mode
  * Type: String
  * Description: Optional. Mode for parsing entities in the document caption. See formatting options for more details.
* Field: caption_entities
  * Type: Array of MessageEntity
  * Description: Optional. List of special entities that appear in the caption, which can be specified instead of parse_mode
* Field: disable_content_type_detection
  * Type: Boolean
  * Description: Optional. Disables automatic server-side content type detection for files uploaded using multipart/form-data. Always True, if the document is sent as part of an album.


#### [](#inputmedialivephoto)InputMediaLivePhoto

Represents a live photo to be sent.



* Field: type
  * Type: String
  * Description: Type of the result, must be live_photo
* Field: media
  * Type: String
  * Description: Video of the live photo to send. Pass a file_id to send a file that exists on the Telegram servers (recommended) or pass “attach://<file_attach_name>” to upload a new one using multipart/form-data under <file_attach_name> name. More information on Sending Files ». Sending live photos by a URL is currently unsupported.
* Field: photo
  * Type: String
  * Description: The static photo to send. Pass a file_id to send a file that exists on the Telegram servers (recommended) or pass “attach://<file_attach_name>” to upload a new one using multipart/form-data under <file_attach_name> name. More information on Sending Files ». Sending live photos by a URL is currently unsupported.
* Field: caption
  * Type: String
  * Description: Optional. Caption of the live photo to be sent, 0-1024 characters after entities parsing
* Field: parse_mode
  * Type: String
  * Description: Optional. Mode for parsing entities in the live photo caption. See formatting options for more details.
* Field: caption_entities
  * Type: Array of MessageEntity
  * Description: Optional. List of special entities that appear in the caption, which can be specified instead of parse_mode
* Field: show_caption_above_media
  * Type: Boolean
  * Description: Optional. Pass True, if the caption must be shown above the message media
* Field: has_spoiler
  * Type: Boolean
  * Description: Optional. Pass True if the live photo needs to be covered with a spoiler animation


#### [](#inputmedialocation)InputMediaLocation

Represents a location to be sent.



* Field: type
  * Type: String
  * Description: Type of the result, must be location
* Field: latitude
  * Type: Float
  * Description: Latitude of the location
* Field: longitude
  * Type: Float
  * Description: Longitude of the location
* Field: horizontal_accuracy
  * Type: Float
  * Description: Optional. The radius of uncertainty for the location, measured in meters; 0-1500


#### [](#inputmediaphoto)InputMediaPhoto

Represents a photo to be sent.



* Field: type
  * Type: String
  * Description: Type of the result, must be photo
* Field: media
  * Type: String
  * Description: File to send. Pass a file_id to send a file that exists on the Telegram servers (recommended), pass an HTTP URL for Telegram to get a file from the Internet, or pass “attach://<file_attach_name>” to upload a new one using multipart/form-data under <file_attach_name> name. More information on Sending Files »
* Field: caption
  * Type: String
  * Description: Optional. Caption of the photo to be sent, 0-1024 characters after entities parsing
* Field: parse_mode
  * Type: String
  * Description: Optional. Mode for parsing entities in the photo caption. See formatting options for more details.
* Field: caption_entities
  * Type: Array of MessageEntity
  * Description: Optional. List of special entities that appear in the caption, which can be specified instead of parse_mode
* Field: show_caption_above_media
  * Type: Boolean
  * Description: Optional. Pass True, if the caption must be shown above the message media
* Field: has_spoiler
  * Type: Boolean
  * Description: Optional. Pass True if the photo needs to be covered with a spoiler animation


#### [](#inputmediasticker)InputMediaSticker

Represents a sticker file to be sent.



* Field: type
  * Type: String
  * Description: Type of the result, must be sticker
* Field: media
  * Type: String
  * Description: File to send. Pass a file_id to send a file that exists on the Telegram servers (recommended), pass an HTTP URL for Telegram to get a .WEBP sticker from the Internet, or pass “attach://<file_attach_name>” to upload a new .WEBP, .TGS, or .WEBM sticker using multipart/form-data under <file_attach_name> name. More information on Sending Files »
* Field: emoji
  * Type: String
  * Description: Optional. Emoji associated with the sticker; only for just uploaded stickers


#### [](#inputmediavenue)InputMediaVenue

Represents a venue to be sent.



* Field: type
  * Type: String
  * Description: Type of the result, must be venue
* Field: latitude
  * Type: Float
  * Description: Latitude of the location
* Field: longitude
  * Type: Float
  * Description: Longitude of the location
* Field: title
  * Type: String
  * Description: Name of the venue
* Field: address
  * Type: String
  * Description: Address of the venue
* Field: foursquare_id
  * Type: String
  * Description: Optional. Foursquare identifier of the venue
* Field: foursquare_type
  * Type: String
  * Description: Optional. Foursquare type of the venue, if known. (For example, “arts_entertainment/default”, “arts_entertainment/aquarium” or “food/icecream”.)
* Field: google_place_id
  * Type: String
  * Description: Optional. Google Places identifier of the venue
* Field: google_place_type
  * Type: String
  * Description: Optional. Google Places type of the venue. (See supported types.)


#### [](#inputmediavideo)InputMediaVideo

Represents a video to be sent.



* Field: type
  * Type: String
  * Description: Type of the result, must be video
* Field: media
  * Type: String
  * Description: File to send. Pass a file_id to send a file that exists on the Telegram servers (recommended), pass an HTTP URL for Telegram to get a file from the Internet, or pass “attach://<file_attach_name>” to upload a new one using multipart/form-data under <file_attach_name> name. More information on Sending Files »
* Field: thumbnail
  * Type: String
  * Description: Optional. Thumbnail of the file sent; can be ignored if thumbnail generation for the file is supported server-side. The thumbnail should be in JPEG format and less than 200 kB in size. A thumbnail's width and height should not exceed 320. Ignored if the file is not uploaded using multipart/form-data. Thumbnails can't be reused and can be only uploaded as a new file, so you can pass “attach://<file_attach_name>” if the thumbnail was uploaded using multipart/form-data under <file_attach_name>. More information on Sending Files »
* Field: cover
  * Type: String
  * Description: Optional. Cover for the video in the message. Pass a file_id to send a file that exists on the Telegram servers (recommended), pass an HTTP URL for Telegram to get a file from the Internet, or pass “attach://<file_attach_name>” to upload a new one using multipart/form-data under <file_attach_name> name. More information on Sending Files »
* Field: start_timestamp
  * Type: Integer
  * Description: Optional. Start timestamp for the video in the message
* Field: caption
  * Type: String
  * Description: Optional. Caption of the video to be sent, 0-1024 characters after entities parsing
* Field: parse_mode
  * Type: String
  * Description: Optional. Mode for parsing entities in the video caption. See formatting options for more details.
* Field: caption_entities
  * Type: Array of MessageEntity
  * Description: Optional. List of special entities that appear in the caption, which can be specified instead of parse_mode
* Field: show_caption_above_media
  * Type: Boolean
  * Description: Optional. Pass True, if the caption must be shown above the message media
* Field: width
  * Type: Integer
  * Description: Optional. Video width
* Field: height
  * Type: Integer
  * Description: Optional. Video height
* Field: duration
  * Type: Integer
  * Description: Optional. Video duration in seconds
* Field: supports_streaming
  * Type: Boolean
  * Description: Optional. Pass True if the uploaded video is suitable for streaming
* Field: has_spoiler
  * Type: Boolean
  * Description: Optional. Pass True if the video needs to be covered with a spoiler animation


#### [](#inputfile)InputFile

This object represents the contents of a file to be uploaded. Must be posted using multipart/form-data in the usual way that files are uploaded via the browser.

#### [](#inputpaidmedia)InputPaidMedia

This object describes the paid media to be sent. Currently, it can be one of

*   [InputPaidMediaLivePhoto](#inputpaidmedialivephoto)
*   [InputPaidMediaPhoto](#inputpaidmediaphoto)
*   [InputPaidMediaVideo](#inputpaidmediavideo)

#### [](#inputpaidmedialivephoto)InputPaidMediaLivePhoto

The paid media to send is a live photo.



* Field: type
  * Type: String
  * Description: Type of the media, must be live_photo
* Field: media
  * Type: String
  * Description: Video of the live photo to send. Pass a file_id to send a file that exists on the Telegram servers (recommended) or pass “attach://<file_attach_name>” to upload a new one using multipart/form-data under <file_attach_name> name. More information on Sending Files ». Sending live photos by a URL is currently unsupported.
* Field: photo
  * Type: String
  * Description: The static photo to send. Pass a file_id to send a file that exists on the Telegram servers (recommended) or pass “attach://<file_attach_name>” to upload a new one using multipart/form-data under <file_attach_name> name. More information on Sending Files ». Sending live photos by a URL is currently unsupported.


#### [](#inputpaidmediaphoto)InputPaidMediaPhoto

The paid media to send is a photo.



* Field: type
  * Type: String
  * Description: Type of the media, must be photo
* Field: media
  * Type: String
  * Description: File to send. Pass a file_id to send a file that exists on the Telegram servers (recommended), pass an HTTP URL for Telegram to get a file from the Internet, or pass “attach://<file_attach_name>” to upload a new one using multipart/form-data under <file_attach_name> name. More information on Sending Files »


#### [](#inputpaidmediavideo)InputPaidMediaVideo

The paid media to send is a video.



* Field: type
  * Type: String
  * Description: Type of the media, must be video
* Field: media
  * Type: String
  * Description: File to send. Pass a file_id to send a file that exists on the Telegram servers (recommended), pass an HTTP URL for Telegram to get a file from the Internet, or pass “attach://<file_attach_name>” to upload a new one using multipart/form-data under <file_attach_name> name. More information on Sending Files »
* Field: thumbnail
  * Type: String
  * Description: Optional. Thumbnail of the file sent; can be ignored if thumbnail generation for the file is supported server-side. The thumbnail should be in JPEG format and less than 200 kB in size. A thumbnail's width and height should not exceed 320. Ignored if the file is not uploaded using multipart/form-data. Thumbnails can't be reused and can be only uploaded as a new file, so you can pass “attach://<file_attach_name>” if the thumbnail was uploaded using multipart/form-data under <file_attach_name>. More information on Sending Files »
* Field: cover
  * Type: String
  * Description: Optional. Cover for the video in the message. Pass a file_id to send a file that exists on the Telegram servers (recommended), pass an HTTP URL for Telegram to get a file from the Internet, or pass “attach://<file_attach_name>” to upload a new one using multipart/form-data under <file_attach_name> name. More information on Sending Files »
* Field: start_timestamp
  * Type: Integer
  * Description: Optional. Start timestamp for the video in the message
* Field: width
  * Type: Integer
  * Description: Optional. Video width
* Field: height
  * Type: Integer
  * Description: Optional. Video height
* Field: duration
  * Type: Integer
  * Description: Optional. Video duration in seconds
* Field: supports_streaming
  * Type: Boolean
  * Description: Optional. Pass True if the uploaded video is suitable for streaming


#### [](#inputprofilephoto)InputProfilePhoto

This object describes a profile photo to set. Currently, it can be one of

*   [InputProfilePhotoStatic](#inputprofilephotostatic)
*   [InputProfilePhotoAnimated](#inputprofilephotoanimated)

#### [](#inputprofilephotostatic)InputProfilePhotoStatic

A static profile photo in the .JPG format.



* Field: type
  * Type: String
  * Description: Type of the profile photo, must be static
* Field: photo
  * Type: String
  * Description: The static profile photo. Profile photos can't be reused and can only be uploaded as a new file, so you can pass “attach://<file_attach_name>” if the photo was uploaded using multipart/form-data under <file_attach_name>. More information on Sending Files »


#### [](#inputprofilephotoanimated)InputProfilePhotoAnimated

An animated profile photo in the MPEG4 format.



* Field: type
  * Type: String
  * Description: Type of the profile photo, must be animated
* Field: animation
  * Type: String
  * Description: The animated profile photo. Profile photos can't be reused and can only be uploaded as a new file, so you can pass “attach://<file_attach_name>” if the photo was uploaded using multipart/form-data under <file_attach_name>. More information on Sending Files »
* Field: main_frame_timestamp
  * Type: Float
  * Description: Optional. Timestamp in seconds of the frame that will be used as the static profile photo. Defaults to 0.0.


#### [](#inputstorycontent)InputStoryContent

This object describes the content of a story to post. Currently, it can be one of

*   [InputStoryContentPhoto](#inputstorycontentphoto)
*   [InputStoryContentVideo](#inputstorycontentvideo)

#### [](#inputstorycontentphoto)InputStoryContentPhoto

Describes a photo to post as a story.



* Field: type
  * Type: String
  * Description: Type of the content, must be photo
* Field: photo
  * Type: String
  * Description: The photo to post as a story. The photo must be of the size 1080x1920 and must not exceed 10 MB. The photo can't be reused and can only be uploaded as a new file, so you can pass “attach://<file_attach_name>” if the photo was uploaded using multipart/form-data under <file_attach_name>. More information on Sending Files »


#### [](#inputstorycontentvideo)InputStoryContentVideo

Describes a video to post as a story.



* Field: type
  * Type: String
  * Description: Type of the content, must be video
* Field: video
  * Type: String
  * Description: The video to post as a story. The video must be of the size 720x1280, streamable, encoded with H.265 codec, with key frames added each second in the MPEG4 format, and must not exceed 30 MB. The video can't be reused and can only be uploaded as a new file, so you can pass “attach://<file_attach_name>” if the video was uploaded using multipart/form-data under <file_attach_name>. More information on Sending Files »
* Field: duration
  * Type: Float
  * Description: Optional. Precise duration of the video in seconds; 0-60
* Field: cover_frame_timestamp
  * Type: Float
  * Description: Optional. Timestamp in seconds of the frame that will be used as the static cover for the story. Defaults to 0.0.
* Field: is_animation
  * Type: Boolean
  * Description: Optional. Pass True if the video has no sound


#### [](#sending-files)Sending files

There are three ways to send files (photos, stickers, audio, media, etc.):

1.  If the file is already stored somewhere on the Telegram servers, you don't need to reupload it: each file object has a **file\_id** field, simply pass this **file\_id** as a parameter instead of uploading. There are **no limits** for files sent this way.
2.  Provide Telegram with an HTTP URL for the file to be sent. Telegram will download and send the file. 5 MB max size for photos and 20 MB max for other types of content.
3.  Post the file using multipart/form-data in the usual way that files are uploaded via the browser. 10 MB max size for photos, 50 MB for other files.

**Sending by file\_id**

*   It is not possible to change the file type when resending by **file\_id**. I.e. a [video](#video) can't be [sent as a photo](#sendphoto), a [photo](#photosize) can't be [sent as a document](#senddocument), etc.
*   It is not possible to resend thumbnails.
*   Resending a photo by **file\_id** will send all of its [sizes](#photosize).
*   **file\_id** is unique for each individual bot and **can't** be transferred from one bot to another.
*   **file\_id** uniquely identifies a file, but a file can have different valid **file\_id**s even for the same bot.

**Sending by URL**

*   When sending by URL the target file must have the correct MIME type (e.g., audio/mpeg for [sendAudio](#sendaudio), etc.).
*   In [sendDocument](#senddocument), sending by URL will currently only work for **.PDF** and **.ZIP** files.
*   To use [sendVoice](#sendvoice), the file must have the type audio/ogg and be no more than 1MB in size. 1-20MB voice notes will be sent as files.
*   Other configurations may work but we can't guarantee that they will.

#### [](#accent-colors)Accent colors

Colors with identifiers 0 (red), 1 (orange), 2 (purple/violet), 3 (green), 4 (cyan), 5 (blue), 6 (pink) can be customized by app themes. Additionally, the following colors in RGB format are currently in use.


|Color identifier|Light colors        |Dark colors         |
|----------------|--------------------|--------------------|
|7               |E15052 F9AE63       |FF9380 992F37       |
|8               |E0802B FAC534       |ECB04E C35714       |
|9               |A05FF3 F48FFF       |C697FF 5E31C8       |
|10              |27A910 A7DC57       |A7EB6E 167E2D       |
|11              |27ACCE 82E8D6       |40D8D0 045C7F       |
|12              |3391D4 7DD3F0       |52BFFF 0B5494       |
|13              |DD4371 FFBE9F       |FF86A6 8E366E       |
|14              |247BED F04856 FFFFFF|3FA2FE E5424F FFFFFF|
|15              |D67722 1EA011 FFFFFF|FF905E 32A527 FFFFFF|
|16              |179E42 E84A3F FFFFFF|66D364 D5444F FFFFFF|
|17              |2894AF 6FC456 FFFFFF|22BCE2 3DA240 FFFFFF|
|18              |0C9AB3 FFAD95 FFE6B5|22BCE2 FF9778 FFDA6B|
|19              |7757D6 F79610 FFDE8E|9791FF F2731D FFDB59|
|20              |1585CF F2AB1D FFFFFF|3DA6EB EEA51D FFFFFF|


#### [](#profile-accent-colors)Profile accent colors

Currently, the following colors in RGB format are in use for profile backgrounds.


|Color identifier|Light colors |Dark colors  |
|----------------|-------------|-------------|
|0               |BA5650       |9C4540       |
|1               |C27C3E       |945E2C       |
|2               |956AC8       |715099       |
|3               |49A355       |33713B       |
|4               |3E97AD       |387E87       |
|5               |5A8FBB       |477194       |
|6               |B85378       |944763       |
|7               |7F8B95       |435261       |
|8               |C9565D D97C57|994343 AC583E|
|9               |CF7244 CC9433|8F552F A17232|
|10              |9662D4 B966B6|634691 9250A2|
|11              |3D9755 89A650|296A43 5F8F44|
|12              |3D95BA 50AD98|306C7C 3E987E|
|13              |538BC2 4DA8BD|38618C 458BA1|
|14              |B04F74 D1666D|884160 A65259|
|15              |637482 7B8A97|53606E 384654|


#### [](#inline-mode-objects)Inline mode objects

Objects and methods used in the inline mode are described in the [Inline mode section](#inline-mode).

### [](#available-methods)Available methods

> All methods in the Bot API are case-insensitive. We support **GET** and **POST** HTTP methods. Use either [URL query string](https://en.wikipedia.org/wiki/Query_string) or _application/json_ or _application/x-www-form-urlencoded_ or _multipart/form-data_ for passing parameters in Bot API requests.  
> On successful call, a JSON-object containing the result will be returned.

#### [](#getme)getMe

A simple method for testing your bot's authentication token. Requires no parameters. Returns basic information about the bot in form of a [User](#user) object.

#### [](#logout)logOut

Use this method to log out from the cloud Bot API server before launching the bot locally. You **must** log out the bot before running it locally, otherwise there is no guarantee that the bot will receive updates. After a successful call, you can immediately log in on a local server, but will not be able to log in back to the cloud Bot API server for 10 minutes. Returns _True_ on success. Requires no parameters.

#### [](#close)close

Use this method to close the bot instance before moving it from one local server to another. You need to delete the webhook before calling this method to ensure that the bot isn't launched again after server restart. The method will return error 429 in the first 10 minutes after the bot is launched. Returns _True_ on success. Requires no parameters.

#### [](#sendmessage)sendMessage

Use this method to send text messages. On success, the sent [Message](#message) is returned.



* Parameter: business_connection_id
  * Type: String
  * Required: Optional
  * Description: Unique identifier of the business connection on behalf of which the message will be sent
* Parameter: chat_id
  * Type: Integer or String
  * Required: Yes
  * Description: Unique identifier for the target chat or username of the target bot, supergroup or channel in the format @username
* Parameter: message_thread_id
  * Type: Integer
  * Required: Optional
  * Description: Unique identifier for the target message thread (topic) of a forum; for forum supergroups and private chats of bots with forum topic mode enabled only
* Parameter: direct_messages_topic_id
  * Type: Integer
  * Required: Optional
  * Description: Identifier of the direct messages topic to which the message will be sent; required if the message is sent to a direct messages chat
* Parameter: text
  * Type: String
  * Required: Yes
  * Description: Text of the message to be sent, 1-4096 characters after entities parsing
* Parameter: parse_mode
  * Type: String
  * Required: Optional
  * Description: Mode for parsing entities in the message text. See formatting options for more details.
* Parameter: entities
  * Type: Array of MessageEntity
  * Required: Optional
  * Description: A JSON-serialized list of special entities that appear in message text, which can be specified instead of parse_mode
* Parameter: link_preview_options
  * Type: LinkPreviewOptions
  * Required: Optional
  * Description: Link preview generation options for the message
* Parameter: disable_notification
  * Type: Boolean
  * Required: Optional
  * Description: Sends the message silently. Users will receive a notification with no sound.
* Parameter: protect_content
  * Type: Boolean
  * Required: Optional
  * Description: Protects the contents of the sent message from forwarding and saving
* Parameter: allow_paid_broadcast
  * Type: Boolean
  * Required: Optional
  * Description: Pass True to allow up to 1000 messages per second, ignoring broadcasting limits for a fee of 0.1 Telegram Stars per message. The relevant Stars will be withdrawn from the bot's balance.
* Parameter: message_effect_id
  * Type: String
  * Required: Optional
  * Description: Unique identifier of the message effect to be added to the message; for private chats only
* Parameter: suggested_post_parameters
  * Type: SuggestedPostParameters
  * Required: Optional
  * Description: A JSON-serialized object containing the parameters of the suggested post to send; for direct messages chats only. If the message is sent as a reply to another suggested post, then that suggested post is automatically declined.
* Parameter: reply_parameters
  * Type: ReplyParameters
  * Required: Optional
  * Description: Description of the message to reply to
* Parameter: reply_markup
  * Type: InlineKeyboardMarkup or ReplyKeyboardMarkup or ReplyKeyboardRemove or ForceReply
  * Required: Optional
  * Description: Additional interface options. A JSON-serialized object for an inline keyboard, custom reply keyboard, instructions to remove a reply keyboard or to force a reply from the user.


#### [](#formatting-options)Formatting options

The Bot API supports basic formatting for messages. You can use bold, italic, underlined, strikethrough, spoiler text, block quotations as well as inline links and pre-formatted code in your bots' messages. Telegram clients will render them accordingly. You can specify text entities directly, or use markdown-style or HTML-style formatting.

Note that Telegram clients will display an **alert** to the user before opening an inline link ('Open this link?' together with the full URL).

Message entities can be nested, providing following restrictions are met:  
\- If two entities have common characters, then one of them is fully contained inside another.  
\- _bold_, _italic_, _underline_, _strikethrough_, and _spoiler_ entities can contain and can be part of any other entities, except _pre_ and _code_.  
\- _blockquote_ and _expandable\_blockquote_ entities can't be nested.  
\- All other entities can't contain each other.

Links `tg://user?id=<user_id>` can be used to mention a user by their identifier without using a username. Please note:

*   These links will work **only** if they are used inside an inline link or in an inline keyboard button. For example, they will not work, when used in a message text.
*   Unless the user is a member of the chat where they were mentioned, these mentions are only guaranteed to work if the user has contacted the bot in private in the past or has sent a callback query to the bot via an inline button and doesn't have Forwarded Messages privacy enabled for the bot.

You can find the list of programming and markup languages for which syntax highlighting is supported at [libprisma#supported-languages](https://github.com/TelegramMessenger/libprisma#supported-languages).

###### [](#date-time-entity-formatting)Date-time entity formatting

Date-time entity formatting is specified by a format string, which must adhere to the following regular expression: `r|w?[dD]?[tT]?`.

If the format string is empty, the underlying text is displayed as-is; however, the user can still receive the underlying date in their local format. When populated, the format string determines the output based on the presence of the following control characters:

*   **`r`**: Displays the time relative to the current time. Cannot be combined with any other control characters.
*   **`w`**: Displays the day of the week in the user's localized language.
*   **`d`**: Displays the date in short form (e.g., “17.03.22”).
*   **`D`**: Displays the date in long form (e.g., “March 17, 2022”).
*   **`t`**: Displays the time in short form (e.g., “22:45”).
*   **`T`**: Displays the time in long form (e.g., “22:45:00”).

###### [](#markdownv2-style)MarkdownV2 style

To use this mode, pass _MarkdownV2_ in the _parse\_mode_ field. Use the following syntax in your message:

```
*bold \*text*
_italic \*text_
__underline__
~strikethrough~
||spoiler||
*bold _italic bold ~italic bold strikethrough ||italic bold strikethrough spoiler||~ __underline italic bold___ bold*
[inline URL](http://www.example.com/)
[inline mention of a user](tg://user?id=123456789)
![](tg://emoji?id=5368324170671202286)
![22:45 tomorrow](tg://time?unix=1647531900&format=wDT)
![22:45 tomorrow](tg://time?unix=1647531900&format=t)
![22:45 tomorrow](tg://time?unix=1647531900&format=r)
![22:45 tomorrow](tg://time?unix=1647531900)
`inline fixed-width code`
```
pre-formatted fixed-width code block
```
```python
pre-formatted fixed-width code block written in the Python programming language
```
>Block quotation started
>Block quotation continued
>Block quotation continued
>Block quotation continued
>The last line of the block quotation
**>The expandable block quotation started right after the previous block quotation
>It is separated from the previous block quotation by an empty bold entity
>Expandable block quotation continued
>Hidden by default part of the expandable block quotation started
>Expandable block quotation continued
>The last line of the expandable block quotation with the expandability mark||
```


Please note:

*   Any character with code between 1 and 126 inclusively can be escaped anywhere with a preceding '\\' character, in which case it is treated as an ordinary character and not a part of the markup. This implies that '\\' character usually must be escaped with a preceding '\\' character.
*   Inside `pre` and `code` entities, all '\`' and '\\' characters must be escaped with a preceding '\\' character.
*   Inside the `(...)` part of the inline link and custom emoji definition, all ')' and '\\' must be escaped with a preceding '\\' character.
*   In all other places characters '\_', '\*', '\[', '\]', '(', ')', '~', '\`', '>', '#', '+', '-', '=', '|', '{', '}', '.', '!' must be escaped with the preceding character '\\'.
*   In case of ambiguity between `italic` and `underline` entities `__` is always greedily treated from left to right as beginning or end of an `underline` entity, so instead of `___italic underline___` use `___italic underline_**__`, adding an empty bold entity as a separator.
*   A valid emoji must be provided as an alternative value for the custom emoji. The emoji will be shown instead of the custom emoji in places where a custom emoji cannot be displayed (e.g., system notifications) or if the message is forwarded by a non-premium user. It is recommended to use the emoji from the **emoji** field of the custom emoji [sticker](#sticker).
*   Custom emoji entities can only be used by bots that purchased additional usernames on [Fragment](https://fragment.com/) or in the messages directly sent by the bot to private, group and supergroup chats if the owner of the bot has a Telegram Premium subscription.
*   See [date-time entity formatting](#date-time-entity-formatting) for more details about supported date-time formats.

###### [](#html-style)HTML style

To use this mode, pass _HTML_ in the _parse\_mode_ field. The following tags are currently supported:

```
<b>bold</b>, <strong>bold</strong>
<i>italic</i>, <em>italic</em>
<u>underline</u>, <ins>underline</ins>
<s>strikethrough</s>, <strike>strikethrough</strike>, <del>strikethrough</del>
<span class="tg-spoiler">spoiler</span>, <tg-spoiler>spoiler</tg-spoiler>
<b>bold <i>italic bold <s>italic bold strikethrough <span class="tg-spoiler">italic bold strikethrough spoiler</span></s> <u>underline italic bold</u></i> bold</b>
<a href="http://www.example.com/">inline URL</a>
<a href="tg://user?id=123456789">inline mention of a user</a>
<tg-emoji emoji-id="5368324170671202286"></tg-emoji>
<tg-time unix="1647531900" format="wDT">22:45 tomorrow</tg-time>
<tg-time unix="1647531900" format="t">22:45 tomorrow</tg-time>
<tg-time unix="1647531900" format="r">22:45 tomorrow</tg-time>
<tg-time unix="1647531900">22:45 tomorrow</tg-time>
<code>inline fixed-width code</code>
<pre>pre-formatted fixed-width code block</pre>
<pre><code class="language-python">pre-formatted fixed-width code block written in the Python programming language</code></pre>
<blockquote>Block quotation started\nBlock quotation continued\nThe last line of the block quotation</blockquote>
<blockquote expandable>Expandable block quotation started\nExpandable block quotation continued\nExpandable block quotation continued\nHidden by default part of the block quotation started\nExpandable block quotation continued\nThe last line of the block quotation</blockquote>
```


Please note:

*   Only the tags mentioned above are currently supported.
*   All `<`, `>` and `&` symbols that are not a part of a tag or an HTML entity must be replaced with the corresponding HTML entities (`<` with `&lt;`, `>` with `&gt;` and `&` with `&amp;`).
*   All numerical HTML entities are supported.
*   The API currently supports only the following named HTML entities: `&lt;`, `&gt;`, `&amp;` and `&quot;`.
*   Use nested `pre` and `code` tags, to define programming language for `pre` entity.
*   Programming language can't be specified for standalone `code` tags.
*   A valid emoji must be used as the content of the `tg-emoji` tag. The emoji will be shown instead of the custom emoji in places where a custom emoji cannot be displayed (e.g., system notifications) or if the message is forwarded by a non-premium user. It is recommended to use the emoji from the **emoji** field of the custom emoji [sticker](#sticker).
*   Custom emoji entities can only be used by bots that purchased additional usernames on [Fragment](https://fragment.com/) or in the messages directly sent by the bot to private, group and supergroup chats if the owner of the bot has a Telegram Premium subscription.
*   See [date-time entity formatting](#date-time-entity-formatting) for more details about supported date-time formats.

###### [](#markdown-style)Markdown style

This is a legacy mode, retained for backward compatibility. To use this mode, pass _Markdown_ in the _parse\_mode_ field. Use the following syntax in your message:

```
*bold text*
_italic text_
[inline URL](http://www.example.com/)
[inline mention of a user](tg://user?id=123456789)
`inline fixed-width code`
```
pre-formatted fixed-width code block
```
```python
pre-formatted fixed-width code block written in the Python programming language
```
```


Please note:

*   Entities must not be nested, use parse mode [MarkdownV2](#markdownv2-style) instead.
*   There is no way to specify “underline”, “strikethrough”, “spoiler”, “blockquote”, “expandable\_blockquote”, “custom\_emoji”, and “date\_time” entities, use parse mode [MarkdownV2](#markdownv2-style) instead.
*   To escape characters '\_', '\*', '\`', '\[' outside of an entity, prepend the character '\\' before them.
*   Escaping inside entities is not allowed, so entity must be closed first and reopened again: use `_snake_\__case_` for italic `snake_case` and `*2*\**2=4*` for bold `2*2=4`.

#### [](#paid-broadcasts)Paid Broadcasts

By default, all bots are able to broadcast up to [30 messages](https://core.telegram.org/bots/faq#my-bot-is-hitting-limits-how-do-i-avoid-this) per second to their users. Developers can increase this limit by enabling _Paid Broadcasts_ in [@BotFather](https://t.me/botfather) - allowing their bot to broadcast **up to 1000 messages** per second.

Each message broadcasted over the free amount of 30 messages per second incurs a cost of 0.1 Stars per message, paid with Telegram Stars from the bot's balance. In order to use this feature, a bot must have at least _10,000 Stars_ on its balance.

> Bots with increased limits are only charged for messages that are broadcasted successfully.

#### [](#forwardmessage)forwardMessage

Use this method to forward messages of any kind. Service messages and messages with protected content can't be forwarded. On success, the sent [Message](#message) is returned.



* Parameter: chat_id
  * Type: Integer or String
  * Required: Yes
  * Description: Unique identifier for the target chat or username of the target bot, supergroup or channel in the format @username
* Parameter: message_thread_id
  * Type: Integer
  * Required: Optional
  * Description: Unique identifier for the target message thread (topic) of a forum; for forum supergroups and private chats of bots with forum topic mode enabled only
* Parameter: direct_messages_topic_id
  * Type: Integer
  * Required: Optional
  * Description: Identifier of the direct messages topic to which the message will be forwarded; required if the message is forwarded to a direct messages chat
* Parameter: from_chat_id
  * Type: Integer or String
  * Required: Yes
  * Description: Unique identifier for the chat where the original message was sent (or username of the target bot, supergroup or channel in the format @username)
* Parameter: video_start_timestamp
  * Type: Integer
  * Required: Optional
  * Description: New start timestamp for the forwarded video in the message
* Parameter: disable_notification
  * Type: Boolean
  * Required: Optional
  * Description: Sends the message silently. Users will receive a notification with no sound.
* Parameter: protect_content
  * Type: Boolean
  * Required: Optional
  * Description: Protects the contents of the forwarded message from forwarding and saving
* Parameter: message_effect_id
  * Type: String
  * Required: Optional
  * Description: Unique identifier of the message effect to be added to the message; only available when forwarding to private chats
* Parameter: suggested_post_parameters
  * Type: SuggestedPostParameters
  * Required: Optional
  * Description: A JSON-serialized object containing the parameters of the suggested post to send; for direct messages chats only
* Parameter: message_id
  * Type: Integer
  * Required: Yes
  * Description: Message identifier in the chat specified in from_chat_id


#### [](#forwardmessages)forwardMessages

Use this method to forward multiple messages of any kind. If some of the specified messages can't be found or forwarded, they are skipped. Service messages and messages with protected content can't be forwarded. Album grouping is kept for forwarded messages. On success, an array of [MessageId](#messageid) of the sent messages is returned.



* Parameter: chat_id
  * Type: Integer or String
  * Required: Yes
  * Description: Unique identifier for the target chat or username of the target bot, supergroup or channel in the format @username
* Parameter: message_thread_id
  * Type: Integer
  * Required: Optional
  * Description: Unique identifier for the target message thread (topic) of a forum; for forum supergroups and private chats of bots with forum topic mode enabled only
* Parameter: direct_messages_topic_id
  * Type: Integer
  * Required: Optional
  * Description: Identifier of the direct messages topic to which the messages will be forwarded; required if the messages are forwarded to a direct messages chat
* Parameter: from_chat_id
  * Type: Integer or String
  * Required: Yes
  * Description: Unique identifier for the chat where the original messages were sent (or username of the target bot, supergroup or channel in the format @username)
* Parameter: message_ids
  * Type: Array of Integer
  * Required: Yes
  * Description: A JSON-serialized list of 1-100 identifiers of messages in the chat from_chat_id to forward. The identifiers must be specified in a strictly increasing order.
* Parameter: disable_notification
  * Type: Boolean
  * Required: Optional
  * Description: Sends the messages silently. Users will receive a notification with no sound.
* Parameter: protect_content
  * Type: Boolean
  * Required: Optional
  * Description: Protects the contents of the forwarded messages from forwarding and saving


#### [](#copymessage)copyMessage

Use this method to copy messages of any kind. Service messages, paid media messages, giveaway messages, giveaway winners messages, and invoice messages can't be copied. A quiz [poll](#poll) can be copied only if the value of the field _correct\_option\_id_ is known to the bot. The method is analogous to the method [forwardMessage](#forwardmessage), but the copied message doesn't have a link to the original message. Returns the [MessageId](#messageid) of the sent message on success.



* Parameter: chat_id
  * Type: Integer or String
  * Required: Yes
  * Description: Unique identifier for the target chat or username of the target bot, supergroup or channel in the format @username
* Parameter: message_thread_id
  * Type: Integer
  * Required: Optional
  * Description: Unique identifier for the target message thread (topic) of a forum; for forum supergroups and private chats of bots with forum topic mode enabled only
* Parameter: direct_messages_topic_id
  * Type: Integer
  * Required: Optional
  * Description: Identifier of the direct messages topic to which the message will be sent; required if the message is sent to a direct messages chat
* Parameter: from_chat_id
  * Type: Integer or String
  * Required: Yes
  * Description: Unique identifier for the chat where the original message was sent (or username of the target bot, supergroup or channel in the format @username)
* Parameter: message_id
  * Type: Integer
  * Required: Yes
  * Description: Message identifier in the chat specified in from_chat_id
* Parameter: video_start_timestamp
  * Type: Integer
  * Required: Optional
  * Description: New start timestamp for the copied video in the message
* Parameter: caption
  * Type: String
  * Required: Optional
  * Description: New caption for media, 0-1024 characters after entities parsing. If not specified, the original caption is kept.
* Parameter: parse_mode
  * Type: String
  * Required: Optional
  * Description: Mode for parsing entities in the new caption. See formatting options for more details.
* Parameter: caption_entities
  * Type: Array of MessageEntity
  * Required: Optional
  * Description: A JSON-serialized list of special entities that appear in the new caption, which can be specified instead of parse_mode
* Parameter: show_caption_above_media
  * Type: Boolean
  * Required: Optional
  * Description: Pass True, if the caption must be shown above the message media. Ignored if a new caption isn't specified.
* Parameter: disable_notification
  * Type: Boolean
  * Required: Optional
  * Description: Sends the message silently. Users will receive a notification with no sound.
* Parameter: protect_content
  * Type: Boolean
  * Required: Optional
  * Description: Protects the contents of the sent message from forwarding and saving
* Parameter: allow_paid_broadcast
  * Type: Boolean
  * Required: Optional
  * Description: Pass True to allow up to 1000 messages per second, ignoring broadcasting limits for a fee of 0.1 Telegram Stars per message. The relevant Stars will be withdrawn from the bot's balance.
* Parameter: message_effect_id
  * Type: String
  * Required: Optional
  * Description: Unique identifier of the message effect to be added to the message; only available when copying to private chats
* Parameter: suggested_post_parameters
  * Type: SuggestedPostParameters
  * Required: Optional
  * Description: A JSON-serialized object containing the parameters of the suggested post to send; for direct messages chats only. If the message is sent as a reply to another suggested post, then that suggested post is automatically declined.
* Parameter: reply_parameters
  * Type: ReplyParameters
  * Required: Optional
  * Description: Description of the message to reply to
* Parameter: reply_markup
  * Type: InlineKeyboardMarkup or ReplyKeyboardMarkup or ReplyKeyboardRemove or ForceReply
  * Required: Optional
  * Description: Additional interface options. A JSON-serialized object for an inline keyboard, custom reply keyboard, instructions to remove a reply keyboard or to force a reply from the user.


#### [](#copymessages)copyMessages

Use this method to copy messages of any kind. If some of the specified messages can't be found or copied, they are skipped. Service messages, paid media messages, giveaway messages, giveaway winners messages, and invoice messages can't be copied. A quiz [poll](#poll) can be copied only if the value of the field _correct\_option\_id_ is known to the bot. The method is analogous to the method [forwardMessages](#forwardmessages), but the copied messages don't have a link to the original message. Album grouping is kept for copied messages. On success, an array of [MessageId](#messageid) of the sent messages is returned.



* Parameter: chat_id
  * Type: Integer or String
  * Required: Yes
  * Description: Unique identifier for the target chat or username of the target bot, supergroup or channel in the format @username
* Parameter: message_thread_id
  * Type: Integer
  * Required: Optional
  * Description: Unique identifier for the target message thread (topic) of a forum; for forum supergroups and private chats of bots with forum topic mode enabled only
* Parameter: direct_messages_topic_id
  * Type: Integer
  * Required: Optional
  * Description: Identifier of the direct messages topic to which the messages will be sent; required if the messages are sent to a direct messages chat
* Parameter: from_chat_id
  * Type: Integer or String
  * Required: Yes
  * Description: Unique identifier for the chat where the original messages were sent (or username of the target bot, supergroup or channel in the format @username)
* Parameter: message_ids
  * Type: Array of Integer
  * Required: Yes
  * Description: A JSON-serialized list of 1-100 identifiers of messages in the chat from_chat_id to copy. The identifiers must be specified in a strictly increasing order.
* Parameter: disable_notification
  * Type: Boolean
  * Required: Optional
  * Description: Sends the messages silently. Users will receive a notification with no sound.
* Parameter: protect_content
  * Type: Boolean
  * Required: Optional
  * Description: Protects the contents of the sent messages from forwarding and saving
* Parameter: remove_caption
  * Type: Boolean
  * Required: Optional
  * Description: Pass True to copy the messages without their captions


#### [](#sendphoto)sendPhoto

Use this method to send photos. On success, the sent [Message](#message) is returned.



* Parameter: business_connection_id
  * Type: String
  * Required: Optional
  * Description: Unique identifier of the business connection on behalf of which the message will be sent
* Parameter: chat_id
  * Type: Integer or String
  * Required: Yes
  * Description: Unique identifier for the target chat or username of the target bot, supergroup or channel in the format @username
* Parameter: message_thread_id
  * Type: Integer
  * Required: Optional
  * Description: Unique identifier for the target message thread (topic) of a forum; for forum supergroups and private chats of bots with forum topic mode enabled only
* Parameter: direct_messages_topic_id
  * Type: Integer
  * Required: Optional
  * Description: Identifier of the direct messages topic to which the message will be sent; required if the message is sent to a direct messages chat
* Parameter: photo
  * Type: InputFile or String
  * Required: Yes
  * Description: Photo to send. Pass a file_id as String to send a photo that exists on the Telegram servers (recommended), pass an HTTP URL as a String for Telegram to get a photo from the Internet, or upload a new photo using multipart/form-data. The photo must be at most 10 MB in size. The photo's width and height must not exceed 10000 in total. Width and height ratio must be at most 20. More information on Sending Files »
* Parameter: caption
  * Type: String
  * Required: Optional
  * Description: Photo caption (may also be used when resending photos by file_id), 0-1024 characters after entities parsing
* Parameter: parse_mode
  * Type: String
  * Required: Optional
  * Description: Mode for parsing entities in the photo caption. See formatting options for more details.
* Parameter: caption_entities
  * Type: Array of MessageEntity
  * Required: Optional
  * Description: A JSON-serialized list of special entities that appear in the caption, which can be specified instead of parse_mode
* Parameter: show_caption_above_media
  * Type: Boolean
  * Required: Optional
  * Description: Pass True, if the caption must be shown above the message media
* Parameter: has_spoiler
  * Type: Boolean
  * Required: Optional
  * Description: Pass True if the photo needs to be covered with a spoiler animation
* Parameter: disable_notification
  * Type: Boolean
  * Required: Optional
  * Description: Sends the message silently. Users will receive a notification with no sound.
* Parameter: protect_content
  * Type: Boolean
  * Required: Optional
  * Description: Protects the contents of the sent message from forwarding and saving
* Parameter: allow_paid_broadcast
  * Type: Boolean
  * Required: Optional
  * Description: Pass True to allow up to 1000 messages per second, ignoring broadcasting limits for a fee of 0.1 Telegram Stars per message. The relevant Stars will be withdrawn from the bot's balance.
* Parameter: message_effect_id
  * Type: String
  * Required: Optional
  * Description: Unique identifier of the message effect to be added to the message; for private chats only
* Parameter: suggested_post_parameters
  * Type: SuggestedPostParameters
  * Required: Optional
  * Description: A JSON-serialized object containing the parameters of the suggested post to send; for direct messages chats only. If the message is sent as a reply to another suggested post, then that suggested post is automatically declined.
* Parameter: reply_parameters
  * Type: ReplyParameters
  * Required: Optional
  * Description: Description of the message to reply to
* Parameter: reply_markup
  * Type: InlineKeyboardMarkup or ReplyKeyboardMarkup or ReplyKeyboardRemove or ForceReply
  * Required: Optional
  * Description: Additional interface options. A JSON-serialized object for an inline keyboard, custom reply keyboard, instructions to remove a reply keyboard or to force a reply from the user.


#### [](#sendlivephoto)sendLivePhoto

Use this method to send live photos. On success, the sent [Message](#message) is returned.



* Parameter: business_connection_id
  * Type: String
  * Required: Optional
  * Description: Unique identifier of the business connection on behalf of which the message will be sent
* Parameter: chat_id
  * Type: Integer or String
  * Required: Yes
  * Description: Unique identifier for the target chat or username of the target channel (in the format @channelusername)
* Parameter: message_thread_id
  * Type: Integer
  * Required: Optional
  * Description: Unique identifier for the target message thread (topic) of a forum; for forum supergroups and private chats of bots with forum topic mode enabled only
* Parameter: direct_messages_topic_id
  * Type: Integer
  * Required: Optional
  * Description: Identifier of the direct messages topic to which the message will be sent; required if the message is sent to a direct messages chat
* Parameter: live_photo
  * Type: InputFile or String
  * Required: Yes
  * Description: Live photo video to send. The video must be no longer than 10 seconds and must not exceed 10 MB in size. Pass a file_id as String to send a video that exists on the Telegram servers (recommended) or upload a new video using multipart/form-data. More information on Sending Files ». Sending live photos by a URL is currently unsupported.
* Parameter: photo
  * Type: InputFile or String
  * Required: Yes
  * Description: The static photo to send. Pass a file_id as String to send a photo that exists on the Telegram servers (recommended) or upload a new video using multipart/form-data. More information on Sending Files ». Sending live photos by a URL is currently unsupported.
* Parameter: caption
  * Type: String
  * Required: Optional
  * Description: Video caption (may also be used when resending videos by file_id), 0-1024 characters after entities parsing
* Parameter: parse_mode
  * Type: String
  * Required: Optional
  * Description: Mode for parsing entities in the video caption. See formatting options for more details.
* Parameter: caption_entities
  * Type: Array of MessageEntity
  * Required: Optional
  * Description: A JSON-serialized list of special entities that appear in the caption, which can be specified instead of parse_mode
* Parameter: show_caption_above_media
  * Type: Boolean
  * Required: Optional
  * Description: Pass True, if the caption must be shown above the message media
* Parameter: has_spoiler
  * Type: Boolean
  * Required: Optional
  * Description: Pass True if the video needs to be covered with a spoiler animation
* Parameter: disable_notification
  * Type: Boolean
  * Required: Optional
  * Description: Sends the message silently. Users will receive a notification with no sound.
* Parameter: protect_content
  * Type: Boolean
  * Required: Optional
  * Description: Protects the contents of the sent message from forwarding and saving
* Parameter: allow_paid_broadcast
  * Type: Boolean
  * Required: Optional
  * Description: Pass True to allow up to 1000 messages per second, ignoring broadcasting limits for a fee of 0.1 Telegram Stars per message. The relevant Stars will be withdrawn from the bot's balance.
* Parameter: message_effect_id
  * Type: String
  * Required: Optional
  * Description: Unique identifier of the message effect to be added to the message; for private chats only
* Parameter: suggested_post_parameters
  * Type: SuggestedPostParameters
  * Required: Optional
  * Description: A JSON-serialized object containing the parameters of the suggested post to send; for direct messages chats only. If the message is sent as a reply to another suggested post, then that suggested post is automatically declined.
* Parameter: reply_parameters
  * Type: ReplyParameters
  * Required: Optional
  * Description: Description of the message to reply to
* Parameter: reply_markup
  * Type: InlineKeyboardMarkup or ReplyKeyboardMarkup or ReplyKeyboardRemove or ForceReply
  * Required: Optional
  * Description: Additional interface options. A JSON-serialized object for an inline keyboard, custom reply keyboard, instructions to remove a reply keyboard or to force a reply from the user.


#### [](#sendaudio)sendAudio

Use this method to send audio files, if you want Telegram clients to display them in the music player. Your audio must be in the .MP3 or .M4A format. On success, the sent [Message](#message) is returned. Bots can currently send audio files of up to 50 MB in size, this limit may be changed in the future.

For sending voice messages, use the [sendVoice](#sendvoice) method instead.



* Parameter: business_connection_id
  * Type: String
  * Required: Optional
  * Description: Unique identifier of the business connection on behalf of which the message will be sent
* Parameter: chat_id
  * Type: Integer or String
  * Required: Yes
  * Description: Unique identifier for the target chat or username of the target bot, supergroup or channel in the format @username
* Parameter: message_thread_id
  * Type: Integer
  * Required: Optional
  * Description: Unique identifier for the target message thread (topic) of a forum; for forum supergroups and private chats of bots with forum topic mode enabled only
* Parameter: direct_messages_topic_id
  * Type: Integer
  * Required: Optional
  * Description: Identifier of the direct messages topic to which the message will be sent; required if the message is sent to a direct messages chat
* Parameter: audio
  * Type: InputFile or String
  * Required: Yes
  * Description: Audio file to send. Pass a file_id as String to send an audio file that exists on the Telegram servers (recommended), pass an HTTP URL as a String for Telegram to get an audio file from the Internet, or upload a new one using multipart/form-data. More information on Sending Files »
* Parameter: caption
  * Type: String
  * Required: Optional
  * Description: Audio caption, 0-1024 characters after entities parsing
* Parameter: parse_mode
  * Type: String
  * Required: Optional
  * Description: Mode for parsing entities in the audio caption. See formatting options for more details.
* Parameter: caption_entities
  * Type: Array of MessageEntity
  * Required: Optional
  * Description: A JSON-serialized list of special entities that appear in the caption, which can be specified instead of parse_mode
* Parameter: duration
  * Type: Integer
  * Required: Optional
  * Description: Duration of the audio in seconds
* Parameter: performer
  * Type: String
  * Required: Optional
  * Description: Performer
* Parameter: title
  * Type: String
  * Required: Optional
  * Description: Track name
* Parameter: thumbnail
  * Type: InputFile or String
  * Required: Optional
  * Description: Thumbnail of the file sent; can be ignored if thumbnail generation for the file is supported server-side. The thumbnail should be in JPEG format and less than 200 kB in size. A thumbnail's width and height should not exceed 320. Ignored if the file is not uploaded using multipart/form-data. Thumbnails can't be reused and can be only uploaded as a new file, so you can pass “attach://<file_attach_name>” if the thumbnail was uploaded using multipart/form-data under <file_attach_name>. More information on Sending Files »
* Parameter: disable_notification
  * Type: Boolean
  * Required: Optional
  * Description: Sends the message silently. Users will receive a notification with no sound.
* Parameter: protect_content
  * Type: Boolean
  * Required: Optional
  * Description: Protects the contents of the sent message from forwarding and saving
* Parameter: allow_paid_broadcast
  * Type: Boolean
  * Required: Optional
  * Description: Pass True to allow up to 1000 messages per second, ignoring broadcasting limits for a fee of 0.1 Telegram Stars per message. The relevant Stars will be withdrawn from the bot's balance.
* Parameter: message_effect_id
  * Type: String
  * Required: Optional
  * Description: Unique identifier of the message effect to be added to the message; for private chats only
* Parameter: suggested_post_parameters
  * Type: SuggestedPostParameters
  * Required: Optional
  * Description: A JSON-serialized object containing the parameters of the suggested post to send; for direct messages chats only. If the message is sent as a reply to another suggested post, then that suggested post is automatically declined.
* Parameter: reply_parameters
  * Type: ReplyParameters
  * Required: Optional
  * Description: Description of the message to reply to
* Parameter: reply_markup
  * Type: InlineKeyboardMarkup or ReplyKeyboardMarkup or ReplyKeyboardRemove or ForceReply
  * Required: Optional
  * Description: Additional interface options. A JSON-serialized object for an inline keyboard, custom reply keyboard, instructions to remove a reply keyboard or to force a reply from the user.


#### [](#senddocument)sendDocument

Use this method to send general files. On success, the sent [Message](#message) is returned. Bots can currently send files of any type of up to 50 MB in size, this limit may be changed in the future.



* Parameter: business_connection_id
  * Type: String
  * Required: Optional
  * Description: Unique identifier of the business connection on behalf of which the message will be sent
* Parameter: chat_id
  * Type: Integer or String
  * Required: Yes
  * Description: Unique identifier for the target chat or username of the target bot, supergroup or channel in the format @username
* Parameter: message_thread_id
  * Type: Integer
  * Required: Optional
  * Description: Unique identifier for the target message thread (topic) of a forum; for forum supergroups and private chats of bots with forum topic mode enabled only
* Parameter: direct_messages_topic_id
  * Type: Integer
  * Required: Optional
  * Description: Identifier of the direct messages topic to which the message will be sent; required if the message is sent to a direct messages chat
* Parameter: document
  * Type: InputFile or String
  * Required: Yes
  * Description: File to send. Pass a file_id as String to send a file that exists on the Telegram servers (recommended), pass an HTTP URL as a String for Telegram to get a file from the Internet, or upload a new one using multipart/form-data. More information on Sending Files »
* Parameter: thumbnail
  * Type: InputFile or String
  * Required: Optional
  * Description: Thumbnail of the file sent; can be ignored if thumbnail generation for the file is supported server-side. The thumbnail should be in JPEG format and less than 200 kB in size. A thumbnail's width and height should not exceed 320. Ignored if the file is not uploaded using multipart/form-data. Thumbnails can't be reused and can be only uploaded as a new file, so you can pass “attach://<file_attach_name>” if the thumbnail was uploaded using multipart/form-data under <file_attach_name>. More information on Sending Files »
* Parameter: caption
  * Type: String
  * Required: Optional
  * Description: Document caption (may also be used when resending documents by file_id), 0-1024 characters after entities parsing
* Parameter: parse_mode
  * Type: String
  * Required: Optional
  * Description: Mode for parsing entities in the document caption. See formatting options for more details.
* Parameter: caption_entities
  * Type: Array of MessageEntity
  * Required: Optional
  * Description: A JSON-serialized list of special entities that appear in the caption, which can be specified instead of parse_mode
* Parameter: disable_content_type_detection
  * Type: Boolean
  * Required: Optional
  * Description: Disables automatic server-side content type detection for files uploaded using multipart/form-data
* Parameter: disable_notification
  * Type: Boolean
  * Required: Optional
  * Description: Sends the message silently. Users will receive a notification with no sound.
* Parameter: protect_content
  * Type: Boolean
  * Required: Optional
  * Description: Protects the contents of the sent message from forwarding and saving
* Parameter: allow_paid_broadcast
  * Type: Boolean
  * Required: Optional
  * Description: Pass True to allow up to 1000 messages per second, ignoring broadcasting limits for a fee of 0.1 Telegram Stars per message. The relevant Stars will be withdrawn from the bot's balance.
* Parameter: message_effect_id
  * Type: String
  * Required: Optional
  * Description: Unique identifier of the message effect to be added to the message; for private chats only
* Parameter: suggested_post_parameters
  * Type: SuggestedPostParameters
  * Required: Optional
  * Description: A JSON-serialized object containing the parameters of the suggested post to send; for direct messages chats only. If the message is sent as a reply to another suggested post, then that suggested post is automatically declined.
* Parameter: reply_parameters
  * Type: ReplyParameters
  * Required: Optional
  * Description: Description of the message to reply to
* Parameter: reply_markup
  * Type: InlineKeyboardMarkup or ReplyKeyboardMarkup or ReplyKeyboardRemove or ForceReply
  * Required: Optional
  * Description: Additional interface options. A JSON-serialized object for an inline keyboard, custom reply keyboard, instructions to remove a reply keyboard or to force a reply from the user.


#### [](#sendvideo)sendVideo

Use this method to send video files, Telegram clients support MPEG4 videos (other formats may be sent as [Document](#document)). On success, the sent [Message](#message) is returned. Bots can currently send video files of up to 50 MB in size, this limit may be changed in the future.



* Parameter: business_connection_id
  * Type: String
  * Required: Optional
  * Description: Unique identifier of the business connection on behalf of which the message will be sent
* Parameter: chat_id
  * Type: Integer or String
  * Required: Yes
  * Description: Unique identifier for the target chat or username of the target bot, supergroup or channel in the format @username
* Parameter: message_thread_id
  * Type: Integer
  * Required: Optional
  * Description: Unique identifier for the target message thread (topic) of a forum; for forum supergroups and private chats of bots with forum topic mode enabled only
* Parameter: direct_messages_topic_id
  * Type: Integer
  * Required: Optional
  * Description: Identifier of the direct messages topic to which the message will be sent; required if the message is sent to a direct messages chat
* Parameter: video
  * Type: InputFile or String
  * Required: Yes
  * Description: Video to send. Pass a file_id as String to send a video that exists on the Telegram servers (recommended), pass an HTTP URL as a String for Telegram to get a video from the Internet, or upload a new video using multipart/form-data. More information on Sending Files »
* Parameter: duration
  * Type: Integer
  * Required: Optional
  * Description: Duration of sent video in seconds
* Parameter: width
  * Type: Integer
  * Required: Optional
  * Description: Video width
* Parameter: height
  * Type: Integer
  * Required: Optional
  * Description: Video height
* Parameter: thumbnail
  * Type: InputFile or String
  * Required: Optional
  * Description: Thumbnail of the file sent; can be ignored if thumbnail generation for the file is supported server-side. The thumbnail should be in JPEG format and less than 200 kB in size. A thumbnail's width and height should not exceed 320. Ignored if the file is not uploaded using multipart/form-data. Thumbnails can't be reused and can be only uploaded as a new file, so you can pass “attach://<file_attach_name>” if the thumbnail was uploaded using multipart/form-data under <file_attach_name>. More information on Sending Files »
* Parameter: cover
  * Type: InputFile or String
  * Required: Optional
  * Description: Cover for the video in the message. Pass a file_id to send a file that exists on the Telegram servers (recommended), pass an HTTP URL for Telegram to get a file from the Internet, or pass “attach://<file_attach_name>” to upload a new one using multipart/form-data under <file_attach_name> name. More information on Sending Files »
* Parameter: start_timestamp
  * Type: Integer
  * Required: Optional
  * Description: Start timestamp for the video in the message
* Parameter: caption
  * Type: String
  * Required: Optional
  * Description: Video caption (may also be used when resending videos by file_id), 0-1024 characters after entities parsing
* Parameter: parse_mode
  * Type: String
  * Required: Optional
  * Description: Mode for parsing entities in the video caption. See formatting options for more details.
* Parameter: caption_entities
  * Type: Array of MessageEntity
  * Required: Optional
  * Description: A JSON-serialized list of special entities that appear in the caption, which can be specified instead of parse_mode
* Parameter: show_caption_above_media
  * Type: Boolean
  * Required: Optional
  * Description: Pass True, if the caption must be shown above the message media
* Parameter: has_spoiler
  * Type: Boolean
  * Required: Optional
  * Description: Pass True if the video needs to be covered with a spoiler animation
* Parameter: supports_streaming
  * Type: Boolean
  * Required: Optional
  * Description: Pass True if the uploaded video is suitable for streaming
* Parameter: disable_notification
  * Type: Boolean
  * Required: Optional
  * Description: Sends the message silently. Users will receive a notification with no sound.
* Parameter: protect_content
  * Type: Boolean
  * Required: Optional
  * Description: Protects the contents of the sent message from forwarding and saving
* Parameter: allow_paid_broadcast
  * Type: Boolean
  * Required: Optional
  * Description: Pass True to allow up to 1000 messages per second, ignoring broadcasting limits for a fee of 0.1 Telegram Stars per message. The relevant Stars will be withdrawn from the bot's balance.
* Parameter: message_effect_id
  * Type: String
  * Required: Optional
  * Description: Unique identifier of the message effect to be added to the message; for private chats only
* Parameter: suggested_post_parameters
  * Type: SuggestedPostParameters
  * Required: Optional
  * Description: A JSON-serialized object containing the parameters of the suggested post to send; for direct messages chats only. If the message is sent as a reply to another suggested post, then that suggested post is automatically declined.
* Parameter: reply_parameters
  * Type: ReplyParameters
  * Required: Optional
  * Description: Description of the message to reply to
* Parameter: reply_markup
  * Type: InlineKeyboardMarkup or ReplyKeyboardMarkup or ReplyKeyboardRemove or ForceReply
  * Required: Optional
  * Description: Additional interface options. A JSON-serialized object for an inline keyboard, custom reply keyboard, instructions to remove a reply keyboard or to force a reply from the user.


#### [](#sendanimation)sendAnimation

Use this method to send animation files (GIF or H.264/MPEG-4 AVC video without sound). On success, the sent [Message](#message) is returned. Bots can currently send animation files of up to 50 MB in size, this limit may be changed in the future.



* Parameter: business_connection_id
  * Type: String
  * Required: Optional
  * Description: Unique identifier of the business connection on behalf of which the message will be sent
* Parameter: chat_id
  * Type: Integer or String
  * Required: Yes
  * Description: Unique identifier for the target chat or username of the target bot, supergroup or channel in the format @username
* Parameter: message_thread_id
  * Type: Integer
  * Required: Optional
  * Description: Unique identifier for the target message thread (topic) of a forum; for forum supergroups and private chats of bots with forum topic mode enabled only
* Parameter: direct_messages_topic_id
  * Type: Integer
  * Required: Optional
  * Description: Identifier of the direct messages topic to which the message will be sent; required if the message is sent to a direct messages chat
* Parameter: animation
  * Type: InputFile or String
  * Required: Yes
  * Description: Animation to send. Pass a file_id as String to send an animation that exists on the Telegram servers (recommended), pass an HTTP URL as a String for Telegram to get an animation from the Internet, or upload a new animation using multipart/form-data. More information on Sending Files »
* Parameter: duration
  * Type: Integer
  * Required: Optional
  * Description: Duration of sent animation in seconds
* Parameter: width
  * Type: Integer
  * Required: Optional
  * Description: Animation width
* Parameter: height
  * Type: Integer
  * Required: Optional
  * Description: Animation height
* Parameter: thumbnail
  * Type: InputFile or String
  * Required: Optional
  * Description: Thumbnail of the file sent; can be ignored if thumbnail generation for the file is supported server-side. The thumbnail should be in JPEG format and less than 200 kB in size. A thumbnail's width and height should not exceed 320. Ignored if the file is not uploaded using multipart/form-data. Thumbnails can't be reused and can be only uploaded as a new file, so you can pass “attach://<file_attach_name>” if the thumbnail was uploaded using multipart/form-data under <file_attach_name>. More information on Sending Files »
* Parameter: caption
  * Type: String
  * Required: Optional
  * Description: Animation caption (may also be used when resending animation by file_id), 0-1024 characters after entities parsing
* Parameter: parse_mode
  * Type: String
  * Required: Optional
  * Description: Mode for parsing entities in the animation caption. See formatting options for more details.
* Parameter: caption_entities
  * Type: Array of MessageEntity
  * Required: Optional
  * Description: A JSON-serialized list of special entities that appear in the caption, which can be specified instead of parse_mode
* Parameter: show_caption_above_media
  * Type: Boolean
  * Required: Optional
  * Description: Pass True, if the caption must be shown above the message media
* Parameter: has_spoiler
  * Type: Boolean
  * Required: Optional
  * Description: Pass True if the animation needs to be covered with a spoiler animation
* Parameter: disable_notification
  * Type: Boolean
  * Required: Optional
  * Description: Sends the message silently. Users will receive a notification with no sound.
* Parameter: protect_content
  * Type: Boolean
  * Required: Optional
  * Description: Protects the contents of the sent message from forwarding and saving
* Parameter: allow_paid_broadcast
  * Type: Boolean
  * Required: Optional
  * Description: Pass True to allow up to 1000 messages per second, ignoring broadcasting limits for a fee of 0.1 Telegram Stars per message. The relevant Stars will be withdrawn from the bot's balance.
* Parameter: message_effect_id
  * Type: String
  * Required: Optional
  * Description: Unique identifier of the message effect to be added to the message; for private chats only
* Parameter: suggested_post_parameters
  * Type: SuggestedPostParameters
  * Required: Optional
  * Description: A JSON-serialized object containing the parameters of the suggested post to send; for direct messages chats only. If the message is sent as a reply to another suggested post, then that suggested post is automatically declined.
* Parameter: reply_parameters
  * Type: ReplyParameters
  * Required: Optional
  * Description: Description of the message to reply to
* Parameter: reply_markup
  * Type: InlineKeyboardMarkup or ReplyKeyboardMarkup or ReplyKeyboardRemove or ForceReply
  * Required: Optional
  * Description: Additional interface options. A JSON-serialized object for an inline keyboard, custom reply keyboard, instructions to remove a reply keyboard or to force a reply from the user.


#### [](#sendvoice)sendVoice

Use this method to send audio files, if you want Telegram clients to display the file as a playable voice message. For this to work, your audio must be in an .OGG file encoded with OPUS, or in .MP3 format, or in .M4A format (other formats may be sent as [Audio](#audio) or [Document](#document)). On success, the sent [Message](#message) is returned. Bots can currently send voice messages of up to 50 MB in size, this limit may be changed in the future.



* Parameter: business_connection_id
  * Type: String
  * Required: Optional
  * Description: Unique identifier of the business connection on behalf of which the message will be sent
* Parameter: chat_id
  * Type: Integer or String
  * Required: Yes
  * Description: Unique identifier for the target chat or username of the target bot, supergroup or channel in the format @username
* Parameter: message_thread_id
  * Type: Integer
  * Required: Optional
  * Description: Unique identifier for the target message thread (topic) of a forum; for forum supergroups and private chats of bots with forum topic mode enabled only
* Parameter: direct_messages_topic_id
  * Type: Integer
  * Required: Optional
  * Description: Identifier of the direct messages topic to which the message will be sent; required if the message is sent to a direct messages chat
* Parameter: voice
  * Type: InputFile or String
  * Required: Yes
  * Description: Audio file to send. Pass a file_id as String to send a file that exists on the Telegram servers (recommended), pass an HTTP URL as a String for Telegram to get a file from the Internet, or upload a new one using multipart/form-data. More information on Sending Files »
* Parameter: caption
  * Type: String
  * Required: Optional
  * Description: Voice message caption, 0-1024 characters after entities parsing
* Parameter: parse_mode
  * Type: String
  * Required: Optional
  * Description: Mode for parsing entities in the voice message caption. See formatting options for more details.
* Parameter: caption_entities
  * Type: Array of MessageEntity
  * Required: Optional
  * Description: A JSON-serialized list of special entities that appear in the caption, which can be specified instead of parse_mode
* Parameter: duration
  * Type: Integer
  * Required: Optional
  * Description: Duration of the voice message in seconds
* Parameter: disable_notification
  * Type: Boolean
  * Required: Optional
  * Description: Sends the message silently. Users will receive a notification with no sound.
* Parameter: protect_content
  * Type: Boolean
  * Required: Optional
  * Description: Protects the contents of the sent message from forwarding and saving
* Parameter: allow_paid_broadcast
  * Type: Boolean
  * Required: Optional
  * Description: Pass True to allow up to 1000 messages per second, ignoring broadcasting limits for a fee of 0.1 Telegram Stars per message. The relevant Stars will be withdrawn from the bot's balance.
* Parameter: message_effect_id
  * Type: String
  * Required: Optional
  * Description: Unique identifier of the message effect to be added to the message; for private chats only
* Parameter: suggested_post_parameters
  * Type: SuggestedPostParameters
  * Required: Optional
  * Description: A JSON-serialized object containing the parameters of the suggested post to send; for direct messages chats only. If the message is sent as a reply to another suggested post, then that suggested post is automatically declined.
* Parameter: reply_parameters
  * Type: ReplyParameters
  * Required: Optional
  * Description: Description of the message to reply to
* Parameter: reply_markup
  * Type: InlineKeyboardMarkup or ReplyKeyboardMarkup or ReplyKeyboardRemove or ForceReply
  * Required: Optional
  * Description: Additional interface options. A JSON-serialized object for an inline keyboard, custom reply keyboard, instructions to remove a reply keyboard or to force a reply from the user.


#### [](#sendvideonote)sendVideoNote

As of [v.4.0](https://telegram.org/blog/video-messages-and-telescope), Telegram clients support rounded square MPEG4 videos of up to 1 minute long. Use this method to send video messages. On success, the sent [Message](#message) is returned.



* Parameter: business_connection_id
  * Type: String
  * Required: Optional
  * Description: Unique identifier of the business connection on behalf of which the message will be sent
* Parameter: chat_id
  * Type: Integer or String
  * Required: Yes
  * Description: Unique identifier for the target chat or username of the target bot, supergroup or channel in the format @username
* Parameter: message_thread_id
  * Type: Integer
  * Required: Optional
  * Description: Unique identifier for the target message thread (topic) of a forum; for forum supergroups and private chats of bots with forum topic mode enabled only
* Parameter: direct_messages_topic_id
  * Type: Integer
  * Required: Optional
  * Description: Identifier of the direct messages topic to which the message will be sent; required if the message is sent to a direct messages chat
* Parameter: video_note
  * Type: InputFile or String
  * Required: Yes
  * Description: Video note to send. Pass a file_id as String to send a video note that exists on the Telegram servers (recommended) or upload a new video using multipart/form-data. More information on Sending Files ». Sending video notes by a URL is currently unsupported.
* Parameter: duration
  * Type: Integer
  * Required: Optional
  * Description: Duration of sent video in seconds
* Parameter: length
  * Type: Integer
  * Required: Optional
  * Description: Video width and height, i.e. diameter of the video message
* Parameter: thumbnail
  * Type: InputFile or String
  * Required: Optional
  * Description: Thumbnail of the file sent; can be ignored if thumbnail generation for the file is supported server-side. The thumbnail should be in JPEG format and less than 200 kB in size. A thumbnail's width and height should not exceed 320. Ignored if the file is not uploaded using multipart/form-data. Thumbnails can't be reused and can be only uploaded as a new file, so you can pass “attach://<file_attach_name>” if the thumbnail was uploaded using multipart/form-data under <file_attach_name>. More information on Sending Files »
* Parameter: disable_notification
  * Type: Boolean
  * Required: Optional
  * Description: Sends the message silently. Users will receive a notification with no sound.
* Parameter: protect_content
  * Type: Boolean
  * Required: Optional
  * Description: Protects the contents of the sent message from forwarding and saving
* Parameter: allow_paid_broadcast
  * Type: Boolean
  * Required: Optional
  * Description: Pass True to allow up to 1000 messages per second, ignoring broadcasting limits for a fee of 0.1 Telegram Stars per message. The relevant Stars will be withdrawn from the bot's balance.
* Parameter: message_effect_id
  * Type: String
  * Required: Optional
  * Description: Unique identifier of the message effect to be added to the message; for private chats only
* Parameter: suggested_post_parameters
  * Type: SuggestedPostParameters
  * Required: Optional
  * Description: A JSON-serialized object containing the parameters of the suggested post to send; for direct messages chats only. If the message is sent as a reply to another suggested post, then that suggested post is automatically declined.
* Parameter: reply_parameters
  * Type: ReplyParameters
  * Required: Optional
  * Description: Description of the message to reply to
* Parameter: reply_markup
  * Type: InlineKeyboardMarkup or ReplyKeyboardMarkup or ReplyKeyboardRemove or ForceReply
  * Required: Optional
  * Description: Additional interface options. A JSON-serialized object for an inline keyboard, custom reply keyboard, instructions to remove a reply keyboard or to force a reply from the user.


#### [](#sendpaidmedia)sendPaidMedia

Use this method to send paid media. On success, the sent [Message](#message) is returned.



* Parameter: business_connection_id
  * Type: String
  * Required: Optional
  * Description: Unique identifier of the business connection on behalf of which the message will be sent
* Parameter: chat_id
  * Type: Integer or String
  * Required: Yes
  * Description: Unique identifier for the target chat or username of the target bot, supergroup or channel in the format @username. If the chat is a channel, all Telegram Star proceeds from this media will be credited to the chat's balance. Otherwise, they will be credited to the bot's balance.
* Parameter: message_thread_id
  * Type: Integer
  * Required: Optional
  * Description: Unique identifier for the target message thread (topic) of a forum; for forum supergroups and private chats of bots with forum topic mode enabled only
* Parameter: direct_messages_topic_id
  * Type: Integer
  * Required: Optional
  * Description: Identifier of the direct messages topic to which the message will be sent; required if the message is sent to a direct messages chat
* Parameter: star_count
  * Type: Integer
  * Required: Yes
  * Description: The number of Telegram Stars that must be paid to buy access to the media; 1-25000
* Parameter: media
  * Type: Array of InputPaidMedia
  * Required: Yes
  * Description: A JSON-serialized array describing the media to be sent; up to 10 items
* Parameter: payload
  * Type: String
  * Required: Optional
  * Description: Bot-defined paid media payload, 0-128 bytes. This will not be displayed to the user, use it for your internal processes.
* Parameter: caption
  * Type: String
  * Required: Optional
  * Description: Media caption, 0-1024 characters after entities parsing
* Parameter: parse_mode
  * Type: String
  * Required: Optional
  * Description: Mode for parsing entities in the media caption. See formatting options for more details.
* Parameter: caption_entities
  * Type: Array of MessageEntity
  * Required: Optional
  * Description: A JSON-serialized list of special entities that appear in the caption, which can be specified instead of parse_mode
* Parameter: show_caption_above_media
  * Type: Boolean
  * Required: Optional
  * Description: Pass True, if the caption must be shown above the message media
* Parameter: disable_notification
  * Type: Boolean
  * Required: Optional
  * Description: Sends the message silently. Users will receive a notification with no sound.
* Parameter: protect_content
  * Type: Boolean
  * Required: Optional
  * Description: Protects the contents of the sent message from forwarding and saving
* Parameter: allow_paid_broadcast
  * Type: Boolean
  * Required: Optional
  * Description: Pass True to allow up to 1000 messages per second, ignoring broadcasting limits for a fee of 0.1 Telegram Stars per message. The relevant Stars will be withdrawn from the bot's balance.
* Parameter: suggested_post_parameters
  * Type: SuggestedPostParameters
  * Required: Optional
  * Description: A JSON-serialized object containing the parameters of the suggested post to send; for direct messages chats only. If the message is sent as a reply to another suggested post, then that suggested post is automatically declined.
* Parameter: reply_parameters
  * Type: ReplyParameters
  * Required: Optional
  * Description: Description of the message to reply to
* Parameter: reply_markup
  * Type: InlineKeyboardMarkup or ReplyKeyboardMarkup or ReplyKeyboardRemove or ForceReply
  * Required: Optional
  * Description: Additional interface options. A JSON-serialized object for an inline keyboard, custom reply keyboard, instructions to remove a reply keyboard or to force a reply from the user.


#### [](#sendmediagroup)sendMediaGroup

Use this method to send a group of photos, live photos, videos, documents or audios as an album. Documents and audio files can be only grouped in an album with messages of the same type. On success, an array of [Message](#message) objects that were sent is returned.



* Parameter: business_connection_id
  * Type: String
  * Required: Optional
  * Description: Unique identifier of the business connection on behalf of which the message will be sent
* Parameter: chat_id
  * Type: Integer or String
  * Required: Yes
  * Description: Unique identifier for the target chat or username of the target bot, supergroup or channel in the format @username
* Parameter: message_thread_id
  * Type: Integer
  * Required: Optional
  * Description: Unique identifier for the target message thread (topic) of a forum; for forum supergroups and private chats of bots with forum topic mode enabled only
* Parameter: direct_messages_topic_id
  * Type: Integer
  * Required: Optional
  * Description: Identifier of the direct messages topic to which the messages will be sent; required if the messages are sent to a direct messages chat
* Parameter: media
  * Type: Array of InputMediaAudio, InputMediaDocument, InputMediaLivePhoto, InputMediaPhoto and InputMediaVideo
  * Required: Yes
  * Description: A JSON-serialized array describing messages to be sent, must include 2-10 items
* Parameter: disable_notification
  * Type: Boolean
  * Required: Optional
  * Description: Sends messages silently. Users will receive a notification with no sound.
* Parameter: protect_content
  * Type: Boolean
  * Required: Optional
  * Description: Protects the contents of the sent messages from forwarding and saving
* Parameter: allow_paid_broadcast
  * Type: Boolean
  * Required: Optional
  * Description: Pass True to allow up to 1000 messages per second, ignoring broadcasting limits for a fee of 0.1 Telegram Stars per message. The relevant Stars will be withdrawn from the bot's balance.
* Parameter: message_effect_id
  * Type: String
  * Required: Optional
  * Description: Unique identifier of the message effect to be added to the message; for private chats only
* Parameter: reply_parameters
  * Type: ReplyParameters
  * Required: Optional
  * Description: Description of the message to reply to


#### [](#sendlocation)sendLocation

Use this method to send point on the map. On success, the sent [Message](#message) is returned.



* Parameter: business_connection_id
  * Type: String
  * Required: Optional
  * Description: Unique identifier of the business connection on behalf of which the message will be sent
* Parameter: chat_id
  * Type: Integer or String
  * Required: Yes
  * Description: Unique identifier for the target chat or username of the target bot, supergroup or channel in the format @username
* Parameter: message_thread_id
  * Type: Integer
  * Required: Optional
  * Description: Unique identifier for the target message thread (topic) of a forum; for forum supergroups and private chats of bots with forum topic mode enabled only
* Parameter: direct_messages_topic_id
  * Type: Integer
  * Required: Optional
  * Description: Identifier of the direct messages topic to which the message will be sent; required if the message is sent to a direct messages chat
* Parameter: latitude
  * Type: Float
  * Required: Yes
  * Description: Latitude of the location
* Parameter: longitude
  * Type: Float
  * Required: Yes
  * Description: Longitude of the location
* Parameter: horizontal_accuracy
  * Type: Float
  * Required: Optional
  * Description: The radius of uncertainty for the location, measured in meters; 0-1500
* Parameter: live_period
  * Type: Integer
  * Required: Optional
  * Description: Period in seconds during which the location will be updated (see Live Locations, should be between 60 and 86400, or 0x7FFFFFFF for live locations that can be edited indefinitely
* Parameter: heading
  * Type: Integer
  * Required: Optional
  * Description: For live locations, a direction in which the user is moving, in degrees. Must be between 1 and 360 if specified.
* Parameter: proximity_alert_radius
  * Type: Integer
  * Required: Optional
  * Description: For live locations, a maximum distance for proximity alerts about approaching another chat member, in meters. Must be between 1 and 100000 if specified.
* Parameter: disable_notification
  * Type: Boolean
  * Required: Optional
  * Description: Sends the message silently. Users will receive a notification with no sound.
* Parameter: protect_content
  * Type: Boolean
  * Required: Optional
  * Description: Protects the contents of the sent message from forwarding and saving
* Parameter: allow_paid_broadcast
  * Type: Boolean
  * Required: Optional
  * Description: Pass True to allow up to 1000 messages per second, ignoring broadcasting limits for a fee of 0.1 Telegram Stars per message. The relevant Stars will be withdrawn from the bot's balance.
* Parameter: message_effect_id
  * Type: String
  * Required: Optional
  * Description: Unique identifier of the message effect to be added to the message; for private chats only
* Parameter: suggested_post_parameters
  * Type: SuggestedPostParameters
  * Required: Optional
  * Description: A JSON-serialized object containing the parameters of the suggested post to send; for direct messages chats only. If the message is sent as a reply to another suggested post, then that suggested post is automatically declined.
* Parameter: reply_parameters
  * Type: ReplyParameters
  * Required: Optional
  * Description: Description of the message to reply to
* Parameter: reply_markup
  * Type: InlineKeyboardMarkup or ReplyKeyboardMarkup or ReplyKeyboardRemove or ForceReply
  * Required: Optional
  * Description: Additional interface options. A JSON-serialized object for an inline keyboard, custom reply keyboard, instructions to remove a reply keyboard or to force a reply from the user.


#### [](#sendvenue)sendVenue

Use this method to send information about a venue. On success, the sent [Message](#message) is returned.



* Parameter: business_connection_id
  * Type: String
  * Required: Optional
  * Description: Unique identifier of the business connection on behalf of which the message will be sent
* Parameter: chat_id
  * Type: Integer or String
  * Required: Yes
  * Description: Unique identifier for the target chat or username of the target bot, supergroup or channel in the format @username
* Parameter: message_thread_id
  * Type: Integer
  * Required: Optional
  * Description: Unique identifier for the target message thread (topic) of a forum; for forum supergroups and private chats of bots with forum topic mode enabled only
* Parameter: direct_messages_topic_id
  * Type: Integer
  * Required: Optional
  * Description: Identifier of the direct messages topic to which the message will be sent; required if the message is sent to a direct messages chat
* Parameter: latitude
  * Type: Float
  * Required: Yes
  * Description: Latitude of the venue
* Parameter: longitude
  * Type: Float
  * Required: Yes
  * Description: Longitude of the venue
* Parameter: title
  * Type: String
  * Required: Yes
  * Description: Name of the venue
* Parameter: address
  * Type: String
  * Required: Yes
  * Description: Address of the venue
* Parameter: foursquare_id
  * Type: String
  * Required: Optional
  * Description: Foursquare identifier of the venue
* Parameter: foursquare_type
  * Type: String
  * Required: Optional
  * Description: Foursquare type of the venue, if known. (For example, “arts_entertainment/default”, “arts_entertainment/aquarium” or “food/icecream”.)
* Parameter: google_place_id
  * Type: String
  * Required: Optional
  * Description: Google Places identifier of the venue
* Parameter: google_place_type
  * Type: String
  * Required: Optional
  * Description: Google Places type of the venue. (See supported types.)
* Parameter: disable_notification
  * Type: Boolean
  * Required: Optional
  * Description: Sends the message silently. Users will receive a notification with no sound.
* Parameter: protect_content
  * Type: Boolean
  * Required: Optional
  * Description: Protects the contents of the sent message from forwarding and saving
* Parameter: allow_paid_broadcast
  * Type: Boolean
  * Required: Optional
  * Description: Pass True to allow up to 1000 messages per second, ignoring broadcasting limits for a fee of 0.1 Telegram Stars per message. The relevant Stars will be withdrawn from the bot's balance.
* Parameter: message_effect_id
  * Type: String
  * Required: Optional
  * Description: Unique identifier of the message effect to be added to the message; for private chats only
* Parameter: suggested_post_parameters
  * Type: SuggestedPostParameters
  * Required: Optional
  * Description: A JSON-serialized object containing the parameters of the suggested post to send; for direct messages chats only. If the message is sent as a reply to another suggested post, then that suggested post is automatically declined.
* Parameter: reply_parameters
  * Type: ReplyParameters
  * Required: Optional
  * Description: Description of the message to reply to
* Parameter: reply_markup
  * Type: InlineKeyboardMarkup or ReplyKeyboardMarkup or ReplyKeyboardRemove or ForceReply
  * Required: Optional
  * Description: Additional interface options. A JSON-serialized object for an inline keyboard, custom reply keyboard, instructions to remove a reply keyboard or to force a reply from the user.


#### [](#sendcontact)sendContact

Use this method to send phone contacts. On success, the sent [Message](#message) is returned.



* Parameter: business_connection_id
  * Type: String
  * Required: Optional
  * Description: Unique identifier of the business connection on behalf of which the message will be sent
* Parameter: chat_id
  * Type: Integer or String
  * Required: Yes
  * Description: Unique identifier for the target chat or username of the target bot, supergroup or channel in the format @username
* Parameter: message_thread_id
  * Type: Integer
  * Required: Optional
  * Description: Unique identifier for the target message thread (topic) of a forum; for forum supergroups and private chats of bots with forum topic mode enabled only
* Parameter: direct_messages_topic_id
  * Type: Integer
  * Required: Optional
  * Description: Identifier of the direct messages topic to which the message will be sent; required if the message is sent to a direct messages chat
* Parameter: phone_number
  * Type: String
  * Required: Yes
  * Description: Contact's phone number
* Parameter: first_name
  * Type: String
  * Required: Yes
  * Description: Contact's first name
* Parameter: last_name
  * Type: String
  * Required: Optional
  * Description: Contact's last name
* Parameter: vcard
  * Type: String
  * Required: Optional
  * Description: Additional data about the contact in the form of a vCard, 0-2048 bytes
* Parameter: disable_notification
  * Type: Boolean
  * Required: Optional
  * Description: Sends the message silently. Users will receive a notification with no sound.
* Parameter: protect_content
  * Type: Boolean
  * Required: Optional
  * Description: Protects the contents of the sent message from forwarding and saving
* Parameter: allow_paid_broadcast
  * Type: Boolean
  * Required: Optional
  * Description: Pass True to allow up to 1000 messages per second, ignoring broadcasting limits for a fee of 0.1 Telegram Stars per message. The relevant Stars will be withdrawn from the bot's balance.
* Parameter: message_effect_id
  * Type: String
  * Required: Optional
  * Description: Unique identifier of the message effect to be added to the message; for private chats only
* Parameter: suggested_post_parameters
  * Type: SuggestedPostParameters
  * Required: Optional
  * Description: A JSON-serialized object containing the parameters of the suggested post to send; for direct messages chats only. If the message is sent as a reply to another suggested post, then that suggested post is automatically declined.
* Parameter: reply_parameters
  * Type: ReplyParameters
  * Required: Optional
  * Description: Description of the message to reply to
* Parameter: reply_markup
  * Type: InlineKeyboardMarkup or ReplyKeyboardMarkup or ReplyKeyboardRemove or ForceReply
  * Required: Optional
  * Description: Additional interface options. A JSON-serialized object for an inline keyboard, custom reply keyboard, instructions to remove a reply keyboard or to force a reply from the user.


#### [](#sendpoll)sendPoll

Use this method to send a native poll. On success, the sent [Message](#message) is returned.



* Parameter: business_connection_id
  * Type: String
  * Required: Optional
  * Description: Unique identifier of the business connection on behalf of which the message will be sent
* Parameter: chat_id
  * Type: Integer or String
  * Required: Yes
  * Description: Unique identifier for the target chat or username of the target bot, supergroup or channel in the format @username. Polls can't be sent to channel direct messages chats.
* Parameter: message_thread_id
  * Type: Integer
  * Required: Optional
  * Description: Unique identifier for the target message thread (topic) of a forum; for forum supergroups and private chats of bots with forum topic mode enabled only
* Parameter: question
  * Type: String
  * Required: Yes
  * Description: Poll question, 1-300 characters
* Parameter: question_parse_mode
  * Type: String
  * Required: Optional
  * Description: Mode for parsing entities in the question. See formatting options for more details. Currently, only custom emoji entities are allowed.
* Parameter: question_entities
  * Type: Array of MessageEntity
  * Required: Optional
  * Description: A JSON-serialized list of special entities that appear in the poll question. It can be specified instead of question_parse_mode.
* Parameter: options
  * Type: Array of InputPollOption
  * Required: Yes
  * Description: A JSON-serialized list of 1-12 answer options
* Parameter: is_anonymous
  * Type: Boolean
  * Required: Optional
  * Description: True, if the poll needs to be anonymous, defaults to True
* Parameter: type
  * Type: String
  * Required: Optional
  * Description: Poll type, “quiz” or “regular”, defaults to “regular”
* Parameter: allows_multiple_answers
  * Type: Boolean
  * Required: Optional
  * Description: Pass True, if the poll allows multiple answers, defaults to False
* Parameter: allows_revoting
  * Type: Boolean
  * Required: Optional
  * Description: Pass True, if the poll allows to change chosen answer options, defaults to False for quizzes and to True for regular polls
* Parameter: shuffle_options
  * Type: Boolean
  * Required: Optional
  * Description: Pass True, if the poll options must be shown in random order
* Parameter: allow_adding_options
  * Type: Boolean
  * Required: Optional
  * Description: Pass True, if answer options can be added to the poll after creation; not supported for anonymous polls and quizzes
* Parameter: hide_results_until_closes
  * Type: Boolean
  * Required: Optional
  * Description: Pass True, if poll results must be shown only after the poll closes
* Parameter: members_only
  * Type: Boolean
  * Required: Optional
  * Description: Pass True, if voting is limited to users who have been members of the chat where the poll is being sent for more than 24 hours; for channel chats only
* Parameter: country_codes
  * Type: Array of String
  * Required: Optional
  * Description: A JSON-serialized list of 0-12 two-letter ISO 3166-1 alpha-2 country codes indicating the countries from which users can vote in the poll; for channel chats only. Use “FT” as a country code to allow users with anonymous numbers to vote. If omitted or empty, then users from any country can participate in the poll.
* Parameter: correct_option_ids
  * Type: Array of Integer
  * Required: Optional
  * Description: A JSON-serialized list of monotonically increasing 0-based identifiers of the correct answer options, required for polls in quiz mode
* Parameter: explanation
  * Type: String
  * Required: Optional
  * Description: Text that is shown when a user chooses an incorrect answer or taps on the lamp icon in a quiz-style poll, 0-200 characters with at most 2 line feeds after entities parsing
* Parameter: explanation_parse_mode
  * Type: String
  * Required: Optional
  * Description: Mode for parsing entities in the explanation. See formatting options for more details.
* Parameter: explanation_entities
  * Type: Array of MessageEntity
  * Required: Optional
  * Description: A JSON-serialized list of special entities that appear in the poll explanation. It can be specified instead of explanation_parse_mode.
* Parameter: explanation_media
  * Type: InputPollMedia
  * Required: Optional
  * Description: Media added to the quiz explanation
* Parameter: open_period
  * Type: Integer
  * Required: Optional
  * Description: Amount of time in seconds the poll will be active after creation, 5-2628000. Can't be used together with close_date.
* Parameter: close_date
  * Type: Integer
  * Required: Optional
  * Description: Point in time (Unix timestamp) when the poll will be automatically closed. Must be at least 5 and no more than 2628000 seconds in the future. Can't be used together with open_period.
* Parameter: is_closed
  * Type: Boolean
  * Required: Optional
  * Description: Pass True if the poll needs to be immediately closed. This can be useful for poll preview.
* Parameter: description
  * Type: String
  * Required: Optional
  * Description: Description of the poll to be sent, 0-1024 characters after entities parsing
* Parameter: description_parse_mode
  * Type: String
  * Required: Optional
  * Description: Mode for parsing entities in the poll description. See formatting options for more details.
* Parameter: description_entities
  * Type: Array of MessageEntity
  * Required: Optional
  * Description: A JSON-serialized list of special entities that appear in the poll description, which can be specified instead of description_parse_mode
* Parameter: media
  * Type: InputPollMedia
  * Required: Optional
  * Description: Media added to the poll description
* Parameter: disable_notification
  * Type: Boolean
  * Required: Optional
  * Description: Sends the message silently. Users will receive a notification with no sound.
* Parameter: protect_content
  * Type: Boolean
  * Required: Optional
  * Description: Protects the contents of the sent message from forwarding and saving
* Parameter: allow_paid_broadcast
  * Type: Boolean
  * Required: Optional
  * Description: Pass True to allow up to 1000 messages per second, ignoring broadcasting limits for a fee of 0.1 Telegram Stars per message. The relevant Stars will be withdrawn from the bot's balance.
* Parameter: message_effect_id
  * Type: String
  * Required: Optional
  * Description: Unique identifier of the message effect to be added to the message; for private chats only
* Parameter: reply_parameters
  * Type: ReplyParameters
  * Required: Optional
  * Description: Description of the message to reply to
* Parameter: reply_markup
  * Type: InlineKeyboardMarkup or ReplyKeyboardMarkup or ReplyKeyboardRemove or ForceReply
  * Required: Optional
  * Description: Additional interface options. A JSON-serialized object for an inline keyboard, custom reply keyboard, instructions to remove a reply keyboard or to force a reply from the user.


#### [](#sendchecklist)sendChecklist

Use this method to send a checklist on behalf of a connected business account. On success, the sent [Message](#message) is returned.



* Parameter: business_connection_id
  * Type: String
  * Required: Yes
  * Description: Unique identifier of the business connection on behalf of which the message will be sent
* Parameter: chat_id
  * Type: Integer or String
  * Required: Yes
  * Description: Unique identifier for the target chat or username of the target bot in the format @username
* Parameter: checklist
  * Type: InputChecklist
  * Required: Yes
  * Description: A JSON-serialized object for the checklist to send
* Parameter: disable_notification
  * Type: Boolean
  * Required: Optional
  * Description: Sends the message silently. Users will receive a notification with no sound.
* Parameter: protect_content
  * Type: Boolean
  * Required: Optional
  * Description: Protects the contents of the sent message from forwarding and saving
* Parameter: message_effect_id
  * Type: String
  * Required: Optional
  * Description: Unique identifier of the message effect to be added to the message
* Parameter: reply_parameters
  * Type: ReplyParameters
  * Required: Optional
  * Description: A JSON-serialized object for description of the message to reply to
* Parameter: reply_markup
  * Type: InlineKeyboardMarkup
  * Required: Optional
  * Description: A JSON-serialized object for an inline keyboard


#### [](#senddice)sendDice

Use this method to send an animated emoji that will display a random value. On success, the sent [Message](#message) is returned.



* Parameter: business_connection_id
  * Type: String
  * Required: Optional
  * Description: Unique identifier of the business connection on behalf of which the message will be sent
* Parameter: chat_id
  * Type: Integer or String
  * Required: Yes
  * Description: Unique identifier for the target chat or username of the target bot, supergroup or channel in the format @username
* Parameter: message_thread_id
  * Type: Integer
  * Required: Optional
  * Description: Unique identifier for the target message thread (topic) of a forum; for forum supergroups and private chats of bots with forum topic mode enabled only
* Parameter: direct_messages_topic_id
  * Type: Integer
  * Required: Optional
  * Description: Identifier of the direct messages topic to which the message will be sent; required if the message is sent to a direct messages chat
* Parameter: emoji
  * Type: String
  * Required: Optional
  * Description: Emoji on which the dice throw animation is based. Currently, must be one of “”, “”, “”, “”, “”, or “”. Dice can have values 1-6 for “”, “” and “”, values 1-5 for “” and “”, and values 1-64 for “”. Defaults to “”.
* Parameter: disable_notification
  * Type: Boolean
  * Required: Optional
  * Description: Sends the message silently. Users will receive a notification with no sound.
* Parameter: protect_content
  * Type: Boolean
  * Required: Optional
  * Description: Protects the contents of the sent message from forwarding
* Parameter: allow_paid_broadcast
  * Type: Boolean
  * Required: Optional
  * Description: Pass True to allow up to 1000 messages per second, ignoring broadcasting limits for a fee of 0.1 Telegram Stars per message. The relevant Stars will be withdrawn from the bot's balance.
* Parameter: message_effect_id
  * Type: String
  * Required: Optional
  * Description: Unique identifier of the message effect to be added to the message; for private chats only
* Parameter: suggested_post_parameters
  * Type: SuggestedPostParameters
  * Required: Optional
  * Description: A JSON-serialized object containing the parameters of the suggested post to send; for direct messages chats only. If the message is sent as a reply to another suggested post, then that suggested post is automatically declined.
* Parameter: reply_parameters
  * Type: ReplyParameters
  * Required: Optional
  * Description: Description of the message to reply to
* Parameter: reply_markup
  * Type: InlineKeyboardMarkup or ReplyKeyboardMarkup or ReplyKeyboardRemove or ForceReply
  * Required: Optional
  * Description: Additional interface options. A JSON-serialized object for an inline keyboard, custom reply keyboard, instructions to remove a reply keyboard or to force a reply from the user.


#### [](#sendmessagedraft)sendMessageDraft

Use this method to stream a partial message to a user while the message is being generated. Note that the streamed draft is ephemeral and acts as a temporary 30-second preview - once the output is finalized, you **must** call [sendMessage](#sendmessage) with the complete message to persist it in the user's chat. Returns _True_ on success.



* Parameter: chat_id
  * Type: Integer
  * Required: Yes
  * Description: Unique identifier for the target private chat
* Parameter: message_thread_id
  * Type: Integer
  * Required: Optional
  * Description: Unique identifier for the target message thread
* Parameter: draft_id
  * Type: Integer
  * Required: Yes
  * Description: Unique identifier of the message draft; must be non-zero. Changes of drafts with the same identifier are animated.
* Parameter: text
  * Type: String
  * Required: Optional
  * Description: Text of the message to be sent, 0-4096 characters after entities parsing. Pass an empty text to show a “Thinking…” placeholder.
* Parameter: parse_mode
  * Type: String
  * Required: Optional
  * Description: Mode for parsing entities in the message text. See formatting options for more details.
* Parameter: entities
  * Type: Array of MessageEntity
  * Required: Optional
  * Description: A JSON-serialized list of special entities that appear in message text, which can be specified instead of parse_mode


#### [](#sendchataction)sendChatAction

Use this method when you need to tell the user that something is happening on the bot's side. The status is set for 5 seconds or less (when a message arrives from your bot, Telegram clients clear its typing status). Returns _True_ on success.

> Example: The [ImageBot](https://t.me/imagebot) needs some time to process a request and upload the image. Instead of sending a text message along the lines of “Retrieving image, please wait…”, the bot may use [sendChatAction](#sendchataction) with _action_ = _upload\_photo_. The user will see a “sending photo” status for the bot.

We only recommend using this method when a response from the bot will take a **noticeable** amount of time to arrive.



* Parameter: business_connection_id
  * Type: String
  * Required: Optional
  * Description: Unique identifier of the business connection on behalf of which the action will be sent
* Parameter: chat_id
  * Type: Integer or String
  * Required: Yes
  * Description: Unique identifier for the target chat or username of the target bot or supergroup in the format @username. Channel chats and channel direct messages chats aren't supported.
* Parameter: message_thread_id
  * Type: Integer
  * Required: Optional
  * Description: Unique identifier for the target message thread or topic of a forum; for supergroups and private chats of bots with forum topic mode enabled only
* Parameter: action
  * Type: String
  * Required: Yes
  * Description: Type of action to broadcast. Choose one, depending on what the user is about to receive: typing for text messages, upload_photo for photos, record_video or upload_video for videos, record_voice or upload_voice for voice notes, upload_document for general files, choose_sticker for stickers, find_location for location data, record_video_note or upload_video_note for video notes.


#### [](#setmessagereaction)setMessageReaction

Use this method to change the chosen reactions on a message. Service messages of some types can't be reacted to. Automatically forwarded messages from a channel to its discussion group have the same available reactions as messages in the channel. Bots can't use paid reactions. Returns _True_ on success.



* Parameter: chat_id
  * Type: Integer or String
  * Required: Yes
  * Description: Unique identifier for the target chat or username of the target bot, supergroup or channel in the format @username
* Parameter: message_id
  * Type: Integer
  * Required: Yes
  * Description: Identifier of the target message. If the message belongs to a media group, the reaction is set to the first non-deleted message in the group instead.
* Parameter: reaction
  * Type: Array of ReactionType
  * Required: Optional
  * Description: A JSON-serialized list of reaction types to set on the message. Currently, as non-premium users, bots can set up to one reaction per message. A custom emoji reaction can be used if it is either already present on the message or explicitly allowed by chat administrators. Paid reactions can't be used by bots.
* Parameter: is_big
  * Type: Boolean
  * Required: Optional
  * Description: Pass True to set the reaction with a big animation


#### [](#getuserprofilephotos)getUserProfilePhotos

Use this method to get a list of profile pictures for a user. Returns a [UserProfilePhotos](#userprofilephotos) object.



* Parameter: user_id
  * Type: Integer
  * Required: Yes
  * Description: Unique identifier of the target user
* Parameter: offset
  * Type: Integer
  * Required: Optional
  * Description: Sequential number of the first photo to be returned. By default, all photos are returned.
* Parameter: limit
  * Type: Integer
  * Required: Optional
  * Description: Limits the number of photos to be retrieved. Values between 1-100 are accepted. Defaults to 100.


#### [](#getuserprofileaudios)getUserProfileAudios

Use this method to get a list of profile audios for a user. Returns a [UserProfileAudios](#userprofileaudios) object.



* Parameter: user_id
  * Type: Integer
  * Required: Yes
  * Description: Unique identifier of the target user
* Parameter: offset
  * Type: Integer
  * Required: Optional
  * Description: Sequential number of the first audio to be returned. By default, all audios are returned.
* Parameter: limit
  * Type: Integer
  * Required: Optional
  * Description: Limits the number of audios to be retrieved. Values between 1-100 are accepted. Defaults to 100.


#### [](#setuseremojistatus)setUserEmojiStatus

Changes the emoji status for a given user that previously allowed the bot to manage their emoji status via the Mini App method [requestEmojiStatusAccess](about:/bots/webapps#initializing-mini-apps). Returns _True_ on success.



* Parameter: user_id
  * Type: Integer
  * Required: Yes
  * Description: Unique identifier of the target user
* Parameter: emoji_status_custom_emoji_id
  * Type: String
  * Required: Optional
  * Description: Custom emoji identifier of the emoji status to set. Pass an empty string to remove the status.
* Parameter: emoji_status_expiration_date
  * Type: Integer
  * Required: Optional
  * Description: Expiration date of the emoji status, if any


#### [](#getfile)getFile

Use this method to get basic information about a file and prepare it for downloading. For the moment, bots can download files of up to 20MB in size. On success, a [File](#file) object is returned. The file can then be downloaded via the link `https://api.telegram.org/file/bot<token>/<file_path>`, where `<file_path>` is taken from the response. It is guaranteed that the link will be valid for at least 1 hour. When the link expires, a new one can be requested by calling [getFile](#getfile) again.


|Parameter|Type  |Required|Description                             |
|---------|------|--------|----------------------------------------|
|file_id  |String|Yes     |File identifier to get information about|


**Note:** This function may not preserve the original file name and MIME type. You should save the file's MIME type and name (if available) when the File object is received.

#### [](#banchatmember)banChatMember

Use this method to ban a user in a group, a supergroup or a channel. In the case of supergroups and channels, the user will not be able to return to the chat on their own using invite links, etc., unless [unbanned](#unbanchatmember) first. The bot must be an administrator in the chat for this to work and must have the appropriate administrator rights. Returns _True_ on success.



* Parameter: chat_id
  * Type: Integer or String
  * Required: Yes
  * Description: Unique identifier for the target group or username of the target supergroup or channel in the format @username
* Parameter: user_id
  * Type: Integer
  * Required: Yes
  * Description: Unique identifier of the target user
* Parameter: until_date
  * Type: Integer
  * Required: Optional
  * Description: Date when the user will be unbanned; Unix time. If user is banned for more than 366 days or less than 30 seconds from the current time they are considered to be banned forever. Applied for supergroups and channels only.
* Parameter: revoke_messages
  * Type: Boolean
  * Required: Optional
  * Description: Pass True to delete all messages from the chat for the user that is being removed. If False, the user will be able to see messages in the group that were sent before the user was removed. Always True for supergroups and channels.


#### [](#unbanchatmember)unbanChatMember

Use this method to unban a previously banned user in a supergroup or channel. The user will **not** return to the group or channel automatically, but will be able to join via link, etc. The bot must be an administrator for this to work. By default, this method guarantees that after the call the user is not a member of the chat, but will be able to join it. So if the user is a member of the chat they will also be **removed** from the chat. If you don't want this, use the parameter _only\_if\_banned_. Returns _True_ on success.



* Parameter: chat_id
  * Type: Integer or String
  * Required: Yes
  * Description: Unique identifier for the target group or username of the target supergroup or channel in the format @username
* Parameter: user_id
  * Type: Integer
  * Required: Yes
  * Description: Unique identifier of the target user
* Parameter: only_if_banned
  * Type: Boolean
  * Required: Optional
  * Description: Do nothing if the user is not banned


#### [](#restrictchatmember)restrictChatMember

Use this method to restrict a user in a supergroup. The bot must be an administrator in the supergroup for this to work and must have the appropriate administrator rights. Pass _True_ for all permissions to lift restrictions from a user. Returns _True_ on success.



* Parameter: chat_id
  * Type: Integer or String
  * Required: Yes
  * Description: Unique identifier for the target chat or username of the target supergroup in the format @username
* Parameter: user_id
  * Type: Integer
  * Required: Yes
  * Description: Unique identifier of the target user
* Parameter: permissions
  * Type: ChatPermissions
  * Required: Yes
  * Description: A JSON-serialized object for new user permissions
* Parameter: use_independent_chat_permissions
  * Type: Boolean
  * Required: Optional
  * Description: Pass True if chat permissions are set independently. Otherwise, the can_send_other_messages and can_add_web_page_previews permissions will imply the can_send_messages, can_send_audios, can_send_documents, can_send_photos, can_send_videos, can_send_video_notes, and can_send_voice_notes permissions; the can_send_polls permission will imply the can_send_messages permission.
* Parameter: until_date
  * Type: Integer
  * Required: Optional
  * Description: Date when restrictions will be lifted for the user; Unix time. If user is restricted for more than 366 days or less than 30 seconds from the current time, they are considered to be restricted forever.


#### [](#promotechatmember)promoteChatMember

Use this method to promote or demote a user in a supergroup or a channel. The bot must be an administrator in the chat for this to work and must have the appropriate administrator rights. Pass _False_ for all boolean parameters to demote a user. Returns _True_ on success.



* Parameter: chat_id
  * Type: Integer or String
  * Required: Yes
  * Description: Unique identifier for the target chat or username of the target channel in the format @username
* Parameter: user_id
  * Type: Integer
  * Required: Yes
  * Description: Unique identifier of the target user
* Parameter: is_anonymous
  * Type: Boolean
  * Required: Optional
  * Description: Pass True if the administrator's presence in the chat is hidden
* Parameter: can_manage_chat
  * Type: Boolean
  * Required: Optional
  * Description: Pass True if the administrator can access the chat event log, get boost list, see hidden supergroup and channel members, report spam messages, ignore slow mode, and send messages to the chat without paying Telegram Stars. Implied by any other administrator privilege.
* Parameter: can_delete_messages
  * Type: Boolean
  * Required: Optional
  * Description: Pass True if the administrator can delete messages of other users
* Parameter: can_manage_video_chats
  * Type: Boolean
  * Required: Optional
  * Description: Pass True if the administrator can manage video chats
* Parameter: can_restrict_members
  * Type: Boolean
  * Required: Optional
  * Description: Pass True if the administrator can restrict, ban or unban chat members, or access supergroup statistics. For backward compatibility, defaults to True for promotions of channel administrators.
* Parameter: can_promote_members
  * Type: Boolean
  * Required: Optional
  * Description: Pass True if the administrator can add new administrators with a subset of their own privileges or demote administrators that they have promoted, directly or indirectly (promoted by administrators that were appointed by him)
* Parameter: can_change_info
  * Type: Boolean
  * Required: Optional
  * Description: Pass True if the administrator can change chat title, photo and other settings
* Parameter: can_invite_users
  * Type: Boolean
  * Required: Optional
  * Description: Pass True if the administrator can invite new users to the chat
* Parameter: can_post_stories
  * Type: Boolean
  * Required: Optional
  * Description: Pass True if the administrator can post stories to the chat
* Parameter: can_edit_stories
  * Type: Boolean
  * Required: Optional
  * Description: Pass True if the administrator can edit stories posted by other users, post stories to the chat page, pin chat stories, and access the chat's story archive
* Parameter: can_delete_stories
  * Type: Boolean
  * Required: Optional
  * Description: Pass True if the administrator can delete stories posted by other users
* Parameter: can_post_messages
  * Type: Boolean
  * Required: Optional
  * Description: Pass True if the administrator can post messages in the channel, approve suggested posts, or access channel statistics; for channels only
* Parameter: can_edit_messages
  * Type: Boolean
  * Required: Optional
  * Description: Pass True if the administrator can edit messages of other users and can pin messages; for channels only
* Parameter: can_pin_messages
  * Type: Boolean
  * Required: Optional
  * Description: Pass True if the administrator can pin messages; for supergroups only
* Parameter: can_manage_topics
  * Type: Boolean
  * Required: Optional
  * Description: Pass True if the user is allowed to create, rename, close, and reopen forum topics; for supergroups only
* Parameter: can_manage_direct_messages
  * Type: Boolean
  * Required: Optional
  * Description: Pass True if the administrator can manage direct messages within the channel and decline suggested posts; for channels only
* Parameter: can_manage_tags
  * Type: Boolean
  * Required: Optional
  * Description: Pass True if the administrator can edit the tags of regular members; for groups and supergroups only


#### [](#setchatadministratorcustomtitle)setChatAdministratorCustomTitle

Use this method to set a custom title for an administrator in a supergroup promoted by the bot. Returns _True_ on success.



* Parameter: chat_id
  * Type: Integer or String
  * Required: Yes
  * Description: Unique identifier for the target chat or username of the target supergroup in the format @username
* Parameter: user_id
  * Type: Integer
  * Required: Yes
  * Description: Unique identifier of the target user
* Parameter: custom_title
  * Type: String
  * Required: Yes
  * Description: New custom title for the administrator; 0-16 characters, emoji are not allowed


#### [](#setchatmembertag)setChatMemberTag

Use this method to set a tag for a regular member in a group or a supergroup. The bot must be an administrator in the chat for this to work and must have the _can\_manage\_tags_ administrator right. Returns _True_ on success.



* Parameter: chat_id
  * Type: Integer or String
  * Required: Yes
  * Description: Unique identifier for the target chat or username of the target supergroup in the format @username
* Parameter: user_id
  * Type: Integer
  * Required: Yes
  * Description: Unique identifier of the target user
* Parameter: tag
  * Type: String
  * Required: Optional
  * Description: New tag for the member; 0-16 characters, emoji are not allowed


#### [](#banchatsenderchat)banChatSenderChat

Use this method to ban a channel chat in a supergroup or a channel. Until the chat is [unbanned](#unbanchatsenderchat), the owner of the banned chat won't be able to send messages on behalf of **any of their channels**. The bot must be an administrator in the supergroup or channel for this to work and must have the appropriate administrator rights. Returns _True_ on success.



* Parameter: chat_id
  * Type: Integer or String
  * Required: Yes
  * Description: Unique identifier for the target chat or username of the target channel in the format @username
* Parameter: sender_chat_id
  * Type: Integer
  * Required: Yes
  * Description: Unique identifier of the target sender chat


#### [](#unbanchatsenderchat)unbanChatSenderChat

Use this method to unban a previously banned channel chat in a supergroup or channel. The bot must be an administrator for this to work and must have the appropriate administrator rights. Returns _True_ on success.



* Parameter: chat_id
  * Type: Integer or String
  * Required: Yes
  * Description: Unique identifier for the target chat or username of the target channel in the format @username
* Parameter: sender_chat_id
  * Type: Integer
  * Required: Yes
  * Description: Unique identifier of the target sender chat


#### [](#setchatpermissions)setChatPermissions

Use this method to set default chat permissions for all members. The bot must be an administrator in the group or a supergroup for this to work and must have the _can\_restrict\_members_ administrator rights. Returns _True_ on success.



* Parameter: chat_id
  * Type: Integer or String
  * Required: Yes
  * Description: Unique identifier for the target chat or username of the target supergroup in the format @username
* Parameter: permissions
  * Type: ChatPermissions
  * Required: Yes
  * Description: A JSON-serialized object for new default chat permissions
* Parameter: use_independent_chat_permissions
  * Type: Boolean
  * Required: Optional
  * Description: Pass True if chat permissions are set independently. Otherwise, the can_send_other_messages and can_add_web_page_previews permissions will imply the can_send_messages, can_send_audios, can_send_documents, can_send_photos, can_send_videos, can_send_video_notes, and can_send_voice_notes permissions; the can_send_polls permission will imply the can_send_messages permission.


#### [](#exportchatinvitelink)exportChatInviteLink

Use this method to generate a new primary invite link for a chat; any previously generated primary link is revoked. The bot must be an administrator in the chat for this to work and must have the appropriate administrator rights. Returns the new invite link as _String_ on success.



* Parameter: chat_id
  * Type: Integer or String
  * Required: Yes
  * Description: Unique identifier for the target chat or username of the target channel in the format @username


> Note: Each administrator in a chat generates their own invite links. Bots can't use invite links generated by other administrators. If you want your bot to work with invite links, it will need to generate its own link using [exportChatInviteLink](#exportchatinvitelink) or by calling the [getChat](#getchat) method. If your bot needs to generate a new primary invite link replacing its previous one, use [exportChatInviteLink](#exportchatinvitelink) again.

#### [](#createchatinvitelink)createChatInviteLink

Use this method to create an additional invite link for a chat. The bot must be an administrator in the chat for this to work and must have the appropriate administrator rights. The link can be revoked using the method [revokeChatInviteLink](#revokechatinvitelink). Returns the new invite link as [ChatInviteLink](#chatinvitelink) object.



* Parameter: chat_id
  * Type: Integer or String
  * Required: Yes
  * Description: Unique identifier for the target chat or username of the target channel in the format @username
* Parameter: name
  * Type: String
  * Required: Optional
  * Description: Invite link name; 0-32 characters
* Parameter: expire_date
  * Type: Integer
  * Required: Optional
  * Description: Point in time (Unix timestamp) when the link will expire
* Parameter: member_limit
  * Type: Integer
  * Required: Optional
  * Description: The maximum number of users that can be members of the chat simultaneously after joining the chat via this invite link; 1-99999
* Parameter: creates_join_request
  * Type: Boolean
  * Required: Optional
  * Description: True, if users joining the chat via the link need to be approved by chat administrators. If True, member_limit can't be specified.


#### [](#editchatinvitelink)editChatInviteLink

Use this method to edit a non-primary invite link created by the bot. The bot must be an administrator in the chat for this to work and must have the appropriate administrator rights. Returns the edited invite link as a [ChatInviteLink](#chatinvitelink) object.



* Parameter: chat_id
  * Type: Integer or String
  * Required: Yes
  * Description: Unique identifier for the target chat or username of the target channel in the format @username
* Parameter: invite_link
  * Type: String
  * Required: Yes
  * Description: The invite link to edit
* Parameter: name
  * Type: String
  * Required: Optional
  * Description: Invite link name; 0-32 characters
* Parameter: expire_date
  * Type: Integer
  * Required: Optional
  * Description: Point in time (Unix timestamp) when the link will expire
* Parameter: member_limit
  * Type: Integer
  * Required: Optional
  * Description: The maximum number of users that can be members of the chat simultaneously after joining the chat via this invite link; 1-99999
* Parameter: creates_join_request
  * Type: Boolean
  * Required: Optional
  * Description: True, if users joining the chat via the link need to be approved by chat administrators. If True, member_limit can't be specified.


#### [](#createchatsubscriptioninvitelink)createChatSubscriptionInviteLink

Use this method to create a [subscription invite link](https://telegram.org/blog/superchannels-star-reactions-subscriptions#star-subscriptions) for a channel chat. The bot must have the _can\_invite\_users_ administrator rights. The link can be edited using the method [editChatSubscriptionInviteLink](#editchatsubscriptioninvitelink) or revoked using the method [revokeChatInviteLink](#revokechatinvitelink). Returns the new invite link as a [ChatInviteLink](#chatinvitelink) object.



* Parameter: chat_id
  * Type: Integer or String
  * Required: Yes
  * Description: Unique identifier for the target channel chat or username of the target channel in the format @username
* Parameter: name
  * Type: String
  * Required: Optional
  * Description: Invite link name; 0-32 characters
* Parameter: subscription_period
  * Type: Integer
  * Required: Yes
  * Description: The number of seconds the subscription will be active for before the next payment. Currently, it must always be 2592000 (30 days).
* Parameter: subscription_price
  * Type: Integer
  * Required: Yes
  * Description: The amount of Telegram Stars a user must pay initially and after each subsequent subscription period to be a member of the chat; 1-10000


#### [](#editchatsubscriptioninvitelink)editChatSubscriptionInviteLink

Use this method to edit a subscription invite link created by the bot. The bot must have the _can\_invite\_users_ administrator rights. Returns the edited invite link as a [ChatInviteLink](#chatinvitelink) object.



* Parameter: chat_id
  * Type: Integer or String
  * Required: Yes
  * Description: Unique identifier for the target chat or username of the target channel in the format @username
* Parameter: invite_link
  * Type: String
  * Required: Yes
  * Description: The invite link to edit
* Parameter: name
  * Type: String
  * Required: Optional
  * Description: Invite link name; 0-32 characters


#### [](#revokechatinvitelink)revokeChatInviteLink

Use this method to revoke an invite link created by the bot. If the primary link is revoked, a new link is automatically generated. The bot must be an administrator in the chat for this to work and must have the appropriate administrator rights. Returns the revoked invite link as [ChatInviteLink](#chatinvitelink) object.



* Parameter: chat_id
  * Type: Integer or String
  * Required: Yes
  * Description: Unique identifier of the target chat or username of the target channel in the format @username
* Parameter: invite_link
  * Type: String
  * Required: Yes
  * Description: The invite link to revoke


#### [](#approvechatjoinrequest)approveChatJoinRequest

Use this method to approve a chat join request. The bot must be an administrator in the chat for this to work and must have the _can\_invite\_users_ administrator right. Returns _True_ on success.



* Parameter: chat_id
  * Type: Integer or String
  * Required: Yes
  * Description: Unique identifier for the target chat or username of the target channel in the format @username
* Parameter: user_id
  * Type: Integer
  * Required: Yes
  * Description: Unique identifier of the target user


#### [](#declinechatjoinrequest)declineChatJoinRequest

Use this method to decline a chat join request. The bot must be an administrator in the chat for this to work and must have the _can\_invite\_users_ administrator right. Returns _True_ on success.



* Parameter: chat_id
  * Type: Integer or String
  * Required: Yes
  * Description: Unique identifier for the target chat or username of the target channel in the format @username
* Parameter: user_id
  * Type: Integer
  * Required: Yes
  * Description: Unique identifier of the target user


#### [](#setchatphoto)setChatPhoto

Use this method to set a new profile photo for the chat. Photos can't be changed for private chats. The bot must be an administrator in the chat for this to work and must have the appropriate administrator rights. Returns _True_ on success.



* Parameter: chat_id
  * Type: Integer or String
  * Required: Yes
  * Description: Unique identifier for the target chat or username of the target channel in the format @username
* Parameter: photo
  * Type: InputFile
  * Required: Yes
  * Description: New chat photo, uploaded using multipart/form-data


#### [](#deletechatphoto)deleteChatPhoto

Use this method to delete a chat photo. Photos can't be changed for private chats. The bot must be an administrator in the chat for this to work and must have the appropriate administrator rights. Returns _True_ on success.



* Parameter: chat_id
  * Type: Integer or String
  * Required: Yes
  * Description: Unique identifier for the target chat or username of the target channel in the format @username


#### [](#setchattitle)setChatTitle

Use this method to change the title of a chat. Titles can't be changed for private chats. The bot must be an administrator in the chat for this to work and must have the appropriate administrator rights. Returns _True_ on success.



* Parameter: chat_id
  * Type: Integer or String
  * Required: Yes
  * Description: Unique identifier for the target chat or username of the target channel in the format @username
* Parameter: title
  * Type: String
  * Required: Yes
  * Description: New chat title, 1-128 characters


#### [](#setchatdescription)setChatDescription

Use this method to change the description of a group, a supergroup or a channel. The bot must be an administrator in the chat for this to work and must have the appropriate administrator rights. Returns _True_ on success.



* Parameter: chat_id
  * Type: Integer or String
  * Required: Yes
  * Description: Unique identifier for the target chat or username of the target channel in the format @username
* Parameter: description
  * Type: String
  * Required: Optional
  * Description: New chat description, 0-255 characters


#### [](#pinchatmessage)pinChatMessage

Use this method to add a message to the list of pinned messages in a chat. In private chats and channel direct messages chats, all non-service messages can be pinned. Conversely, the bot must be an administrator with the 'can\_pin\_messages' right or the 'can\_edit\_messages' right to pin messages in groups and channels respectively. Returns _True_ on success.



* Parameter: business_connection_id
  * Type: String
  * Required: Optional
  * Description: Unique identifier of the business connection on behalf of which the message will be pinned
* Parameter: chat_id
  * Type: Integer or String
  * Required: Yes
  * Description: Unique identifier for the target chat or username of the target channel in the format @username
* Parameter: message_id
  * Type: Integer
  * Required: Yes
  * Description: Identifier of a message to pin
* Parameter: disable_notification
  * Type: Boolean
  * Required: Optional
  * Description: Pass True if it is not necessary to send a notification to all chat members about the new pinned message. Notifications are always disabled in channels and private chats.


#### [](#unpinchatmessage)unpinChatMessage

Use this method to remove a message from the list of pinned messages in a chat. In private chats and channel direct messages chats, all messages can be unpinned. Conversely, the bot must be an administrator with the 'can\_pin\_messages' right or the 'can\_edit\_messages' right to unpin messages in groups and channels respectively. Returns _True_ on success.



* Parameter: business_connection_id
  * Type: String
  * Required: Optional
  * Description: Unique identifier of the business connection on behalf of which the message will be unpinned
* Parameter: chat_id
  * Type: Integer or String
  * Required: Yes
  * Description: Unique identifier for the target chat or username of the target channel in the format @username
* Parameter: message_id
  * Type: Integer
  * Required: Optional
  * Description: Identifier of the message to unpin. Required if business_connection_id is specified. If not specified, the most recent pinned message (by sending date) will be unpinned.


#### [](#unpinallchatmessages)unpinAllChatMessages

Use this method to clear the list of pinned messages in a chat. In private chats and channel direct messages chats, no additional rights are required to unpin all pinned messages. Conversely, the bot must be an administrator with the 'can\_pin\_messages' right or the 'can\_edit\_messages' right to unpin all pinned messages in groups and channels respectively. Returns _True_ on success.



* Parameter: chat_id
  * Type: Integer or String
  * Required: Yes
  * Description: Unique identifier for the target chat or username of the target channel in the format @username


#### [](#leavechat)leaveChat

Use this method for your bot to leave a group, supergroup or channel. Returns _True_ on success.



* Parameter: chat_id
  * Type: Integer or String
  * Required: Yes
  * Description: Unique identifier for the target chat or username of the target supergroup or channel in the format @username. Channel direct messages chats aren't supported; leave the corresponding channel instead.


#### [](#getchat)getChat

Use this method to get up-to-date information about the chat. Returns a [ChatFullInfo](#chatfullinfo) object on success.



* Parameter: chat_id
  * Type: Integer or String
  * Required: Yes
  * Description: Unique identifier for the target chat or username of the target supergroup or channel in the format @username


#### [](#getchatadministrators)getChatAdministrators

Use this method to get a list of administrators in a chat. Returns an Array of [ChatMember](#chatmember) objects.



* Parameter: chat_id
  * Type: Integer or String
  * Required: Yes
  * Description: Unique identifier for the target chat or username of the target supergroup or channel in the format @username
* Parameter: return_bots
  * Type: Boolean
  * Required: Optional
  * Description: Pass True to additionally receive all bots that are administrators of the chat. By default, bots other than the current bot are omitted.


#### [](#getchatmembercount)getChatMemberCount

Use this method to get the number of members in a chat. Returns _Int_ on success.



* Parameter: chat_id
  * Type: Integer or String
  * Required: Yes
  * Description: Unique identifier for the target chat or username of the target supergroup or channel in the format @username


#### [](#getchatmember)getChatMember

Use this method to get information about a member of a chat. The method is only guaranteed to work for other users if the bot is an administrator in the chat. Returns a [ChatMember](#chatmember) object on success.



* Parameter: chat_id
  * Type: Integer or String
  * Required: Yes
  * Description: Unique identifier for the target chat or username of the target supergroup or channel in the format @username
* Parameter: user_id
  * Type: Integer
  * Required: Yes
  * Description: Unique identifier of the target user


#### [](#getuserpersonalchatmessages)getUserPersonalChatMessages

Use this method to get the last messages from the personal chat (i.e., the chat currently added to their profile) of a given user. On success, an array of [Message](#message) objects is returned.


|Parameter|Type   |Required|Description                                   |
|---------|-------|--------|----------------------------------------------|
|user_id  |Integer|Yes     |Unique identifier for the target user         |
|limit    |Integer|Yes     |The maximum number of messages to return; 1-20|


#### [](#setchatstickerset)setChatStickerSet

Use this method to set a new group sticker set for a supergroup. The bot must be an administrator in the chat for this to work and must have the appropriate administrator rights. Use the field _can\_set\_sticker\_set_ optionally returned in [getChat](#getchat) requests to check if the bot can use this method. Returns _True_ on success.



* Parameter: chat_id
  * Type: Integer or String
  * Required: Yes
  * Description: Unique identifier for the target chat or username of the target supergroup in the format @username
* Parameter: sticker_set_name
  * Type: String
  * Required: Yes
  * Description: Name of the sticker set to be set as the group sticker set


#### [](#deletechatstickerset)deleteChatStickerSet

Use this method to delete a group sticker set from a supergroup. The bot must be an administrator in the chat for this to work and must have the appropriate administrator rights. Use the field _can\_set\_sticker\_set_ optionally returned in [getChat](#getchat) requests to check if the bot can use this method. Returns _True_ on success.



* Parameter: chat_id
  * Type: Integer or String
  * Required: Yes
  * Description: Unique identifier for the target chat or username of the target supergroup in the format @username


#### [](#getforumtopiciconstickers)getForumTopicIconStickers

Use this method to get custom emoji stickers, which can be used as a forum topic icon by any user. Requires no parameters. Returns an Array of [Sticker](#sticker) objects.

#### [](#createforumtopic)createForumTopic

Use this method to create a topic in a forum supergroup chat or a private chat with a user. In the case of a supergroup chat the bot must be an administrator in the chat for this to work and must have the _can\_manage\_topics_ administrator right. Returns information about the created topic as a [ForumTopic](#forumtopic) object.



* Parameter: chat_id
  * Type: Integer or String
  * Required: Yes
  * Description: Unique identifier for the target chat or username of the target supergroup in the format @username
* Parameter: name
  * Type: String
  * Required: Yes
  * Description: Topic name, 1-128 characters
* Parameter: icon_color
  * Type: Integer
  * Required: Optional
  * Description: Color of the topic icon in RGB format. Currently, must be one of 7322096 (0x6FB9F0), 16766590 (0xFFD67E), 13338331 (0xCB86DB), 9367192 (0x8EEE98), 16749490 (0xFF93B2), or 16478047 (0xFB6F5F).
* Parameter: icon_custom_emoji_id
  * Type: String
  * Required: Optional
  * Description: Unique identifier of the custom emoji shown as the topic icon. Use getForumTopicIconStickers to get all allowed custom emoji identifiers.


#### [](#editforumtopic)editForumTopic

Use this method to edit name and icon of a topic in a forum supergroup chat or a private chat with a user. In the case of a supergroup chat the bot must be an administrator in the chat for this to work and must have the _can\_manage\_topics_ administrator rights, unless it is the creator of the topic. Returns _True_ on success.



* Parameter: chat_id
  * Type: Integer or String
  * Required: Yes
  * Description: Unique identifier for the target chat or username of the target supergroup in the format @username
* Parameter: message_thread_id
  * Type: Integer
  * Required: Yes
  * Description: Unique identifier for the target message thread of the forum topic
* Parameter: name
  * Type: String
  * Required: Optional
  * Description: New topic name, 0-128 characters. If not specified or empty, the current name of the topic will be kept.
* Parameter: icon_custom_emoji_id
  * Type: String
  * Required: Optional
  * Description: New unique identifier of the custom emoji shown as the topic icon. Use getForumTopicIconStickers to get all allowed custom emoji identifiers. Pass an empty string to remove the icon. If not specified, the current icon will be kept.


#### [](#closeforumtopic)closeForumTopic

Use this method to close an open topic in a forum supergroup chat. The bot must be an administrator in the chat for this to work and must have the _can\_manage\_topics_ administrator rights, unless it is the creator of the topic. Returns _True_ on success.



* Parameter: chat_id
  * Type: Integer or String
  * Required: Yes
  * Description: Unique identifier for the target chat or username of the target supergroup in the format @username
* Parameter: message_thread_id
  * Type: Integer
  * Required: Yes
  * Description: Unique identifier for the target message thread of the forum topic


#### [](#reopenforumtopic)reopenForumTopic

Use this method to reopen a closed topic in a forum supergroup chat. The bot must be an administrator in the chat for this to work and must have the _can\_manage\_topics_ administrator rights, unless it is the creator of the topic. Returns _True_ on success.



* Parameter: chat_id
  * Type: Integer or String
  * Required: Yes
  * Description: Unique identifier for the target chat or username of the target supergroup in the format @username
* Parameter: message_thread_id
  * Type: Integer
  * Required: Yes
  * Description: Unique identifier for the target message thread of the forum topic


#### [](#deleteforumtopic)deleteForumTopic

Use this method to delete a forum topic along with all its messages in a forum supergroup chat or a private chat with a user. In the case of a supergroup chat the bot must be an administrator in the chat for this to work and must have the _can\_delete\_messages_ administrator rights. Returns _True_ on success.



* Parameter: chat_id
  * Type: Integer or String
  * Required: Yes
  * Description: Unique identifier for the target chat or username of the target supergroup in the format @username
* Parameter: message_thread_id
  * Type: Integer
  * Required: Yes
  * Description: Unique identifier for the target message thread of the forum topic


#### [](#unpinallforumtopicmessages)unpinAllForumTopicMessages

Use this method to clear the list of pinned messages in a forum topic in a forum supergroup chat or a private chat with a user. In the case of a supergroup chat the bot must be an administrator in the chat for this to work and must have the _can\_pin\_messages_ administrator right in the supergroup. Returns _True_ on success.



* Parameter: chat_id
  * Type: Integer or String
  * Required: Yes
  * Description: Unique identifier for the target chat or username of the target supergroup in the format @username
* Parameter: message_thread_id
  * Type: Integer
  * Required: Yes
  * Description: Unique identifier for the target message thread of the forum topic


#### [](#editgeneralforumtopic)editGeneralForumTopic

Use this method to edit the name of the 'General' topic in a forum supergroup chat. The bot must be an administrator in the chat for this to work and must have the _can\_manage\_topics_ administrator rights. Returns _True_ on success.



* Parameter: chat_id
  * Type: Integer or String
  * Required: Yes
  * Description: Unique identifier for the target chat or username of the target supergroup in the format @username
* Parameter: name
  * Type: String
  * Required: Yes
  * Description: New topic name, 1-128 characters


#### [](#closegeneralforumtopic)closeGeneralForumTopic

Use this method to close an open 'General' topic in a forum supergroup chat. The bot must be an administrator in the chat for this to work and must have the _can\_manage\_topics_ administrator rights. Returns _True_ on success.



* Parameter: chat_id
  * Type: Integer or String
  * Required: Yes
  * Description: Unique identifier for the target chat or username of the target supergroup in the format @username


#### [](#reopengeneralforumtopic)reopenGeneralForumTopic

Use this method to reopen a closed 'General' topic in a forum supergroup chat. The bot must be an administrator in the chat for this to work and must have the _can\_manage\_topics_ administrator rights. The topic will be automatically unhidden if it was hidden. Returns _True_ on success.



* Parameter: chat_id
  * Type: Integer or String
  * Required: Yes
  * Description: Unique identifier for the target chat or username of the target supergroup in the format @username


#### [](#hidegeneralforumtopic)hideGeneralForumTopic

Use this method to hide the 'General' topic in a forum supergroup chat. The bot must be an administrator in the chat for this to work and must have the _can\_manage\_topics_ administrator rights. The topic will be automatically closed if it was open. Returns _True_ on success.



* Parameter: chat_id
  * Type: Integer or String
  * Required: Yes
  * Description: Unique identifier for the target chat or username of the target supergroup in the format @username


#### [](#unhidegeneralforumtopic)unhideGeneralForumTopic

Use this method to unhide the 'General' topic in a forum supergroup chat. The bot must be an administrator in the chat for this to work and must have the _can\_manage\_topics_ administrator rights. Returns _True_ on success.



* Parameter: chat_id
  * Type: Integer or String
  * Required: Yes
  * Description: Unique identifier for the target chat or username of the target supergroup in the format @username


#### [](#unpinallgeneralforumtopicmessages)unpinAllGeneralForumTopicMessages

Use this method to clear the list of pinned messages in a General forum topic. The bot must be an administrator in the chat for this to work and must have the _can\_pin\_messages_ administrator right in the supergroup. Returns _True_ on success.



* Parameter: chat_id
  * Type: Integer or String
  * Required: Yes
  * Description: Unique identifier for the target chat or username of the target supergroup in the format @username


#### [](#answercallbackquery)answerCallbackQuery

Use this method to send answers to callback queries sent from [inline keyboards](about:/bots/features#inline-keyboards). The answer will be displayed to the user as a notification at the top of the chat screen or as an alert. On success, _True_ is returned.

> Alternatively, the user can be redirected to the specified Game URL. For this option to work, you must first create a game for your bot via [@BotFather](https://t.me/botfather) and accept the terms. Otherwise, you may use links like `t.me/your_bot?start=XXXX` that open your bot with a parameter.



* Parameter: callback_query_id
  * Type: String
  * Required: Yes
  * Description: Unique identifier for the query to be answered
* Parameter: text
  * Type: String
  * Required: Optional
  * Description: Text of the notification. If not specified, nothing will be shown to the user, 0-200 characters.
* Parameter: show_alert
  * Type: Boolean
  * Required: Optional
  * Description: If True, an alert will be shown by the client instead of a notification at the top of the chat screen. Defaults to false.
* Parameter: url
  * Type: String
  * Required: Optional
  * Description: URL that will be opened by the user's client. If you have created a Game and accepted the conditions via @BotFather, specify the URL that opens your game - note that this will only work if the query comes from a callback_game button.Otherwise, you may use links like t.me/your_bot?start=XXXX that open your bot with a parameter.
* Parameter: cache_time
  * Type: Integer
  * Required: Optional
  * Description: The maximum amount of time in seconds that the result of the callback query may be cached client-side. Telegram apps will support caching starting in version 3.14. Defaults to 0.


#### [](#answerguestquery)answerGuestQuery

Use this method to reply to a received guest message. On success, a [SentGuestMessage](#sentguestmessage) object is returned.



* Parameter: guest_query_id
  * Type: String
  * Required: Yes
  * Description: Unique identifier for the query to be answered
* Parameter: result
  * Type: InlineQueryResult
  * Required: Yes
  * Description: A JSON-serialized object describing the message to be sent


#### [](#getuserchatboosts)getUserChatBoosts

Use this method to get the list of boosts added to a chat by a user. Requires administrator rights in the chat. Returns a [UserChatBoosts](#userchatboosts) object.



* Parameter: chat_id
  * Type: Integer or String
  * Required: Yes
  * Description: Unique identifier for the chat or username of the channel in the format @username
* Parameter: user_id
  * Type: Integer
  * Required: Yes
  * Description: Unique identifier of the target user


#### [](#getbusinessconnection)getBusinessConnection

Use this method to get information about the connection of the bot with a business account. Returns a [BusinessConnection](#businessconnection) object on success.


|Parameter             |Type  |Required|Description                                 |
|----------------------|------|--------|--------------------------------------------|
|business_connection_id|String|Yes     |Unique identifier of the business connection|


#### [](#getmanagedbottoken)getManagedBotToken

Use this method to get the token of a managed bot. Returns the token as _String_ on success.


|Parameter|Type   |Required|Description                                                    |
|---------|-------|--------|---------------------------------------------------------------|
|user_id  |Integer|Yes     |User identifier of the managed bot whose token will be returned|


#### [](#replacemanagedbottoken)replaceManagedBotToken

Use this method to revoke the current token of a managed bot and generate a new one. Returns the new token as _String_ on success.


|Parameter|Type   |Required|Description                                                    |
|---------|-------|--------|---------------------------------------------------------------|
|user_id  |Integer|Yes     |User identifier of the managed bot whose token will be replaced|


#### [](#getmanagedbotaccesssettings)getManagedBotAccessSettings

Use this method to get the access settings of a managed bot. Returns a [BotAccessSettings](#botaccesssettings) object on success.



* Parameter: user_id
  * Type: Integer
  * Required: Yes
  * Description: User identifier of the managed bot whose access settings will be returned


#### [](#setmanagedbotaccesssettings)setManagedBotAccessSettings

Use this method to change the access settings of a managed bot. Returns _True_ on success.



* Parameter: user_id
  * Type: Integer
  * Required: Yes
  * Description: User identifier of the managed bot whose access settings will be changed
* Parameter: is_access_restricted
  * Type: Boolean
  * Required: Yes
  * Description: Pass True, if only selected users can access the bot. The bot's owner can always access it.
* Parameter: added_user_ids
  * Type: Array of Integer
  * Required: Optional
  * Description: A JSON-serialized list of up to 10 identifiers of users who will have access to the bot in addition to its owner. Ignored if is_access_restricted is false.


#### [](#setmycommands)setMyCommands

Use this method to change the list of the bot's commands. See [this manual](about:/bots/features#commands) for more details about bot commands. Returns _True_ on success.



* Parameter: commands
  * Type: Array of BotCommand
  * Required: Yes
  * Description: A JSON-serialized list of bot commands to be set as the list of the bot's commands. At most 100 commands can be specified.
* Parameter: scope
  * Type: BotCommandScope
  * Required: Optional
  * Description: A JSON-serialized object, describing scope of users for which the commands are relevant. Defaults to BotCommandScopeDefault.
* Parameter: language_code
  * Type: String
  * Required: Optional
  * Description: A two-letter ISO 639-1 language code. If empty, commands will be applied to all users from the given scope, for whose language there are no dedicated commands.


#### [](#deletemycommands)deleteMyCommands

Use this method to delete the list of the bot's commands for the given scope and user language. After deletion, [higher level commands](#determining-list-of-commands) will be shown to affected users. Returns _True_ on success.



* Parameter: scope
  * Type: BotCommandScope
  * Required: Optional
  * Description: A JSON-serialized object, describing scope of users for which the commands are relevant. Defaults to BotCommandScopeDefault.
* Parameter: language_code
  * Type: String
  * Required: Optional
  * Description: A two-letter ISO 639-1 language code. If empty, commands will be applied to all users from the given scope, for whose language there are no dedicated commands.


#### [](#getmycommands)getMyCommands

Use this method to get the current list of the bot's commands for the given scope and user language. Returns an Array of [BotCommand](#botcommand) objects. If commands aren't set, an empty list is returned.



* Parameter: scope
  * Type: BotCommandScope
  * Required: Optional
  * Description: A JSON-serialized object, describing scope of users. Defaults to BotCommandScopeDefault.
* Parameter: language_code
  * Type: String
  * Required: Optional
  * Description: A two-letter ISO 639-1 language code or an empty string


#### [](#setmyname)setMyName

Use this method to change the bot's name. Returns _True_ on success.



* Parameter: name
  * Type: String
  * Required: Optional
  * Description: New bot name; 0-64 characters. Pass an empty string to remove the dedicated name for the given language.
* Parameter: language_code
  * Type: String
  * Required: Optional
  * Description: A two-letter ISO 639-1 language code. If empty, the name will be shown to all users for whose language there is no dedicated name.


#### [](#getmyname)getMyName

Use this method to get the current bot name for the given user language. Returns [BotName](#botname) on success.


|Parameter    |Type  |Required|Description                                            |
|-------------|------|--------|-------------------------------------------------------|
|language_code|String|Optional|A two-letter ISO 639-1 language code or an empty string|


#### [](#setmydescription)setMyDescription

Use this method to change the bot's description, which is shown in the chat with the bot if the chat is empty. Returns _True_ on success.



* Parameter: description
  * Type: String
  * Required: Optional
  * Description: New bot description; 0-512 characters. Pass an empty string to remove the dedicated description for the given language.
* Parameter: language_code
  * Type: String
  * Required: Optional
  * Description: A two-letter ISO 639-1 language code. If empty, the description will be applied to all users for whose language there is no dedicated description.


#### [](#getmydescription)getMyDescription

Use this method to get the current bot description for the given user language. Returns [BotDescription](#botdescription) on success.


|Parameter    |Type  |Required|Description                                            |
|-------------|------|--------|-------------------------------------------------------|
|language_code|String|Optional|A two-letter ISO 639-1 language code or an empty string|


#### [](#setmyshortdescription)setMyShortDescription

Use this method to change the bot's short description, which is shown on the bot's profile page and is sent together with the link when users share the bot. Returns _True_ on success.



* Parameter: short_description
  * Type: String
  * Required: Optional
  * Description: New short description for the bot; 0-120 characters. Pass an empty string to remove the dedicated short description for the given language.
* Parameter: language_code
  * Type: String
  * Required: Optional
  * Description: A two-letter ISO 639-1 language code. If empty, the short description will be applied to all users for whose language there is no dedicated short description.


#### [](#getmyshortdescription)getMyShortDescription

Use this method to get the current bot short description for the given user language. Returns [BotShortDescription](#botshortdescription) on success.


|Parameter    |Type  |Required|Description                                            |
|-------------|------|--------|-------------------------------------------------------|
|language_code|String|Optional|A two-letter ISO 639-1 language code or an empty string|


#### [](#setmyprofilephoto)setMyProfilePhoto

Changes the profile photo of the bot. Returns _True_ on success.


|Parameter|Type             |Required|Description                 |
|---------|-----------------|--------|----------------------------|
|photo    |InputProfilePhoto|Yes     |The new profile photo to set|


#### [](#removemyprofilephoto)removeMyProfilePhoto

Removes the profile photo of the bot. Requires no parameters. Returns _True_ on success.

#### [](#setchatmenubutton)setChatMenuButton

Use this method to change the bot's menu button in a private chat, or the default menu button. Returns _True_ on success.



* Parameter: chat_id
  * Type: Integer
  * Required: Optional
  * Description: Unique identifier for the target private chat. If not specified, the bot's default menu button will be changed.
* Parameter: menu_button
  * Type: MenuButton
  * Required: Optional
  * Description: A JSON-serialized object for the bot's new menu button. Defaults to MenuButtonDefault.


#### [](#getchatmenubutton)getChatMenuButton

Use this method to get the current value of the bot's menu button in a private chat, or the default menu button. Returns [MenuButton](#menubutton) on success.



* Parameter: chat_id
  * Type: Integer
  * Required: Optional
  * Description: Unique identifier for the target private chat. If not specified, the bot's default menu button will be returned.


#### [](#setmydefaultadministratorrights)setMyDefaultAdministratorRights

Use this method to change the default administrator rights requested by the bot when it's added as an administrator to groups or channels. These rights will be suggested to users, but they are free to modify the list before adding the bot. Returns _True_ on success.



* Parameter: rights
  * Type: ChatAdministratorRights
  * Required: Optional
  * Description: A JSON-serialized object describing new default administrator rights. If not specified, the default administrator rights will be cleared.
* Parameter: for_channels
  * Type: Boolean
  * Required: Optional
  * Description: Pass True to change the default administrator rights of the bot in channels. Otherwise, the default administrator rights of the bot for groups and supergroups will be changed.


#### [](#getmydefaultadministratorrights)getMyDefaultAdministratorRights

Use this method to get the current default administrator rights of the bot. Returns [ChatAdministratorRights](#chatadministratorrights) on success.



* Parameter: for_channels
  * Type: Boolean
  * Required: Optional
  * Description: Pass True to get default administrator rights of the bot in channels. Otherwise, default administrator rights of the bot for groups and supergroups will be returned.


#### [](#getavailablegifts)getAvailableGifts

Returns the list of gifts that can be sent by the bot to users and channel chats. Requires no parameters. Returns a [Gifts](#gifts) object.

#### [](#sendgift)sendGift

Sends a gift to the given user or channel chat. The gift can't be converted to Telegram Stars by the receiver. Returns _True_ on success.



* Parameter: user_id
  * Type: Integer
  * Required: Optional
  * Description: Required if chat_id is not specified. Unique identifier of the target user who will receive the gift.
* Parameter: chat_id
  * Type: Integer or String
  * Required: Optional
  * Description: Required if user_id is not specified. Unique identifier for the chat or username of the channel (in the format @username) that will receive the gift.
* Parameter: gift_id
  * Type: String
  * Required: Yes
  * Description: Identifier of the gift; limited gifts can't be sent to channel chats
* Parameter: pay_for_upgrade
  * Type: Boolean
  * Required: Optional
  * Description: Pass True to pay for the gift upgrade from the bot's balance, thereby making the upgrade free for the receiver
* Parameter: text
  * Type: String
  * Required: Optional
  * Description: Text that will be shown along with the gift; 0-128 characters
* Parameter: text_parse_mode
  * Type: String
  * Required: Optional
  * Description: Mode for parsing entities in the text. See formatting options for more details. Entities other than “bold”, “italic”, “underline”, “strikethrough”, “spoiler”, “custom_emoji”, and “date_time” are ignored.
* Parameter: text_entities
  * Type: Array of MessageEntity
  * Required: Optional
  * Description: A JSON-serialized list of special entities that appear in the gift text. It can be specified instead of text_parse_mode. Entities other than “bold”, “italic”, “underline”, “strikethrough”, “spoiler”, “custom_emoji”, and “date_time” are ignored.


#### [](#giftpremiumsubscription)giftPremiumSubscription

Gifts a Telegram Premium subscription to the given user. Returns _True_ on success.



* Parameter: user_id
  * Type: Integer
  * Required: Yes
  * Description: Unique identifier of the target user who will receive a Telegram Premium subscription
* Parameter: month_count
  * Type: Integer
  * Required: Yes
  * Description: Number of months the Telegram Premium subscription will be active for the user; must be one of 3, 6, or 12
* Parameter: star_count
  * Type: Integer
  * Required: Yes
  * Description: Number of Telegram Stars to pay for the Telegram Premium subscription; must be 1000 for 3 months, 1500 for 6 months, and 2500 for 12 months
* Parameter: text
  * Type: String
  * Required: Optional
  * Description: Text that will be shown along with the service message about the subscription; 0-128 characters
* Parameter: text_parse_mode
  * Type: String
  * Required: Optional
  * Description: Mode for parsing entities in the text. See formatting options for more details. Entities other than “bold”, “italic”, “underline”, “strikethrough”, “spoiler”, “custom_emoji”, and “date_time” are ignored.
* Parameter: text_entities
  * Type: Array of MessageEntity
  * Required: Optional
  * Description: A JSON-serialized list of special entities that appear in the gift text. It can be specified instead of text_parse_mode. Entities other than “bold”, “italic”, “underline”, “strikethrough”, “spoiler”, “custom_emoji”, and “date_time” are ignored.


#### [](#verifyuser)verifyUser

Verifies a user [on behalf of the organization](https://telegram.org/verify#third-party-verification) which is represented by the bot. Returns _True_ on success.



* Parameter: user_id
  * Type: Integer
  * Required: Yes
  * Description: Unique identifier of the target user
* Parameter: custom_description
  * Type: String
  * Required: Optional
  * Description: Custom description for the verification; 0-70 characters. Must be empty if the organization isn't allowed to provide a custom verification description.


#### [](#verifychat)verifyChat

Verifies a chat [on behalf of the organization](https://telegram.org/verify#third-party-verification) which is represented by the bot. Returns _True_ on success.



* Parameter: chat_id
  * Type: Integer or String
  * Required: Yes
  * Description: Unique identifier for the target chat or username of the target bot, supergroup or channel in the format @username. Channel direct messages chats can't be verified.
* Parameter: custom_description
  * Type: String
  * Required: Optional
  * Description: Custom description for the verification; 0-70 characters. Must be empty if the organization isn't allowed to provide a custom verification description.


#### [](#removeuserverification)removeUserVerification

Removes verification from a user who is currently verified [on behalf of the organization](https://telegram.org/verify#third-party-verification) represented by the bot. Returns _True_ on success.


|Parameter|Type   |Required|Description                         |
|---------|-------|--------|------------------------------------|
|user_id  |Integer|Yes     |Unique identifier of the target user|


#### [](#removechatverification)removeChatVerification

Removes verification from a chat that is currently verified [on behalf of the organization](https://telegram.org/verify#third-party-verification) represented by the bot. Returns _True_ on success.



* Parameter: chat_id
  * Type: Integer or String
  * Required: Yes
  * Description: Unique identifier for the target chat or username of the target bot or channel in the format @username


#### [](#readbusinessmessage)readBusinessMessage

Marks incoming message as read on behalf of a business account. Requires the _can\_read\_messages_ business bot right. Returns _True_ on success.



* Parameter: business_connection_id
  * Type: String
  * Required: Yes
  * Description: Unique identifier of the business connection on behalf of which to read the message
* Parameter: chat_id
  * Type: Integer
  * Required: Yes
  * Description: Unique identifier of the chat in which the message was received. The chat must have been active in the last 24 hours.
* Parameter: message_id
  * Type: Integer
  * Required: Yes
  * Description: Unique identifier of the message to mark as read


#### [](#deletebusinessmessages)deleteBusinessMessages

Delete messages on behalf of a business account. Requires the _can\_delete\_sent\_messages_ business bot right to delete messages sent by the bot itself, or the _can\_delete\_all\_messages_ business bot right to delete any message. Returns _True_ on success.



* Parameter: business_connection_id
  * Type: String
  * Required: Yes
  * Description: Unique identifier of the business connection on behalf of which to delete the messages
* Parameter: message_ids
  * Type: Array of Integer
  * Required: Yes
  * Description: A JSON-serialized list of 1-100 identifiers of messages to delete. All messages must be from the same chat. See deleteMessage for limitations on which messages can be deleted.


#### [](#setbusinessaccountname)setBusinessAccountName

Changes the first and last name of a managed business account. Requires the _can\_change\_name_ business bot right. Returns _True_ on success.



* Parameter: business_connection_id
  * Type: String
  * Required: Yes
  * Description: Unique identifier of the business connection
* Parameter: first_name
  * Type: String
  * Required: Yes
  * Description: The new value of the first name for the business account; 1-64 characters
* Parameter: last_name
  * Type: String
  * Required: Optional
  * Description: The new value of the last name for the business account; 0-64 characters


#### [](#setbusinessaccountusername)setBusinessAccountUsername

Changes the username of a managed business account. Requires the _can\_change\_username_ business bot right. Returns _True_ on success.



* Parameter: business_connection_id
  * Type: String
  * Required: Yes
  * Description: Unique identifier of the business connection
* Parameter: username
  * Type: String
  * Required: Optional
  * Description: The new value of the username for the business account; 0-32 characters


#### [](#setbusinessaccountbio)setBusinessAccountBio

Changes the bio of a managed business account. Requires the _can\_change\_bio_ business bot right. Returns _True_ on success.



* Parameter: business_connection_id
  * Type: String
  * Required: Yes
  * Description: Unique identifier of the business connection
* Parameter: bio
  * Type: String
  * Required: Optional
  * Description: The new value of the bio for the business account; 0-140 characters


#### [](#setbusinessaccountprofilephoto)setBusinessAccountProfilePhoto

Changes the profile photo of a managed business account. Requires the _can\_edit\_profile\_photo_ business bot right. Returns _True_ on success.



* Parameter: business_connection_id
  * Type: String
  * Required: Yes
  * Description: Unique identifier of the business connection
* Parameter: photo
  * Type: InputProfilePhoto
  * Required: Yes
  * Description: The new profile photo to set
* Parameter: is_public
  * Type: Boolean
  * Required: Optional
  * Description: Pass True to set the public photo, which will be visible even if the main photo is hidden by the business account's privacy settings. An account can have only one public photo.


#### [](#removebusinessaccountprofilephoto)removeBusinessAccountProfilePhoto

Removes the current profile photo of a managed business account. Requires the _can\_edit\_profile\_photo_ business bot right. Returns _True_ on success.



* Parameter: business_connection_id
  * Type: String
  * Required: Yes
  * Description: Unique identifier of the business connection
* Parameter: is_public
  * Type: Boolean
  * Required: Optional
  * Description: Pass True to remove the public photo, which is visible even if the main photo is hidden by the business account's privacy settings. After the main photo is removed, the previous profile photo (if present) becomes the main photo.


#### [](#setbusinessaccountgiftsettings)setBusinessAccountGiftSettings

Changes the privacy settings pertaining to incoming gifts in a managed business account. Requires the _can\_change\_gift\_settings_ business bot right. Returns _True_ on success.



* Parameter: business_connection_id
  * Type: String
  * Required: Yes
  * Description: Unique identifier of the business connection
* Parameter: show_gift_button
  * Type: Boolean
  * Required: Yes
  * Description: Pass True, if a button for sending a gift to the user or by the business account must always be shown in the input field
* Parameter: accepted_gift_types
  * Type: AcceptedGiftTypes
  * Required: Yes
  * Description: Types of gifts accepted by the business account


#### [](#getbusinessaccountstarbalance)getBusinessAccountStarBalance

Returns the amount of Telegram Stars owned by a managed business account. Requires the _can\_view\_gifts\_and\_stars_ business bot right. Returns [StarAmount](#staramount) on success.


|Parameter             |Type  |Required|Description                                 |
|----------------------|------|--------|--------------------------------------------|
|business_connection_id|String|Yes     |Unique identifier of the business connection|


#### [](#transferbusinessaccountstars)transferBusinessAccountStars

Transfers Telegram Stars from the business account balance to the bot's balance. Requires the _can\_transfer\_stars_ business bot right. Returns _True_ on success.


|Parameter             |Type   |Required|Description                                  |
|----------------------|-------|--------|---------------------------------------------|
|business_connection_id|String |Yes     |Unique identifier of the business connection |
|star_count            |Integer|Yes     |Number of Telegram Stars to transfer; 1-10000|


#### [](#getbusinessaccountgifts)getBusinessAccountGifts

Returns the gifts received and owned by a managed business account. Requires the _can\_view\_gifts\_and\_stars_ business bot right. Returns [OwnedGifts](#ownedgifts) on success.



* Parameter: business_connection_id
  * Type: String
  * Required: Yes
  * Description: Unique identifier of the business connection
* Parameter: exclude_unsaved
  * Type: Boolean
  * Required: Optional
  * Description: Pass True to exclude gifts that aren't saved to the account's profile page
* Parameter: exclude_saved
  * Type: Boolean
  * Required: Optional
  * Description: Pass True to exclude gifts that are saved to the account's profile page
* Parameter: exclude_unlimited
  * Type: Boolean
  * Required: Optional
  * Description: Pass True to exclude gifts that can be purchased an unlimited number of times
* Parameter: exclude_limited_upgradable
  * Type: Boolean
  * Required: Optional
  * Description: Pass True to exclude gifts that can be purchased a limited number of times and can be upgraded to unique
* Parameter: exclude_limited_non_upgradable
  * Type: Boolean
  * Required: Optional
  * Description: Pass True to exclude gifts that can be purchased a limited number of times and can't be upgraded to unique
* Parameter: exclude_unique
  * Type: Boolean
  * Required: Optional
  * Description: Pass True to exclude unique gifts
* Parameter: exclude_from_blockchain
  * Type: Boolean
  * Required: Optional
  * Description: Pass True to exclude gifts that were assigned from the TON blockchain and can't be resold or transferred in Telegram
* Parameter: sort_by_price
  * Type: Boolean
  * Required: Optional
  * Description: Pass True to sort results by gift price instead of send date. Sorting is applied before pagination.
* Parameter: offset
  * Type: String
  * Required: Optional
  * Description: Offset of the first entry to return as received from the previous request; use empty string to get the first chunk of results
* Parameter: limit
  * Type: Integer
  * Required: Optional
  * Description: The maximum number of gifts to be returned; 1-100. Defaults to 100.


#### [](#getusergifts)getUserGifts

Returns the gifts owned and hosted by a user. Returns [OwnedGifts](#ownedgifts) on success.



* Parameter: user_id
  * Type: Integer
  * Required: Yes
  * Description: Unique identifier of the user
* Parameter: exclude_unlimited
  * Type: Boolean
  * Required: Optional
  * Description: Pass True to exclude gifts that can be purchased an unlimited number of times
* Parameter: exclude_limited_upgradable
  * Type: Boolean
  * Required: Optional
  * Description: Pass True to exclude gifts that can be purchased a limited number of times and can be upgraded to unique
* Parameter: exclude_limited_non_upgradable
  * Type: Boolean
  * Required: Optional
  * Description: Pass True to exclude gifts that can be purchased a limited number of times and can't be upgraded to unique
* Parameter: exclude_from_blockchain
  * Type: Boolean
  * Required: Optional
  * Description: Pass True to exclude gifts that were assigned from the TON blockchain and can't be resold or transferred in Telegram
* Parameter: exclude_unique
  * Type: Boolean
  * Required: Optional
  * Description: Pass True to exclude unique gifts
* Parameter: sort_by_price
  * Type: Boolean
  * Required: Optional
  * Description: Pass True to sort results by gift price instead of send date. Sorting is applied before pagination.
* Parameter: offset
  * Type: String
  * Required: Optional
  * Description: Offset of the first entry to return as received from the previous request; use an empty string to get the first chunk of results
* Parameter: limit
  * Type: Integer
  * Required: Optional
  * Description: The maximum number of gifts to be returned; 1-100. Defaults to 100.


#### [](#getchatgifts)getChatGifts

Returns the gifts owned by a chat. Returns [OwnedGifts](#ownedgifts) on success.



* Parameter: chat_id
  * Type: Integer or String
  * Required: Yes
  * Description: Unique identifier for the target chat or username of the target channel in the format @username
* Parameter: exclude_unsaved
  * Type: Boolean
  * Required: Optional
  * Description: Pass True to exclude gifts that aren't saved to the chat's profile page. Always True, unless the bot has the can_post_messages administrator right in the channel.
* Parameter: exclude_saved
  * Type: Boolean
  * Required: Optional
  * Description: Pass True to exclude gifts that are saved to the chat's profile page. Always False, unless the bot has the can_post_messages administrator right in the channel.
* Parameter: exclude_unlimited
  * Type: Boolean
  * Required: Optional
  * Description: Pass True to exclude gifts that can be purchased an unlimited number of times
* Parameter: exclude_limited_upgradable
  * Type: Boolean
  * Required: Optional
  * Description: Pass True to exclude gifts that can be purchased a limited number of times and can be upgraded to unique
* Parameter: exclude_limited_non_upgradable
  * Type: Boolean
  * Required: Optional
  * Description: Pass True to exclude gifts that can be purchased a limited number of times and can't be upgraded to unique
* Parameter: exclude_from_blockchain
  * Type: Boolean
  * Required: Optional
  * Description: Pass True to exclude gifts that were assigned from the TON blockchain and can't be resold or transferred in Telegram
* Parameter: exclude_unique
  * Type: Boolean
  * Required: Optional
  * Description: Pass True to exclude unique gifts
* Parameter: sort_by_price
  * Type: Boolean
  * Required: Optional
  * Description: Pass True to sort results by gift price instead of send date. Sorting is applied before pagination.
* Parameter: offset
  * Type: String
  * Required: Optional
  * Description: Offset of the first entry to return as received from the previous request; use an empty string to get the first chunk of results
* Parameter: limit
  * Type: Integer
  * Required: Optional
  * Description: The maximum number of gifts to be returned; 1-100. Defaults to 100.


#### [](#convertgifttostars)convertGiftToStars

Converts a given regular gift to Telegram Stars. Requires the _can\_convert\_gifts\_to\_stars_ business bot right. Returns _True_ on success.



* Parameter: business_connection_id
  * Type: String
  * Required: Yes
  * Description: Unique identifier of the business connection
* Parameter: owned_gift_id
  * Type: String
  * Required: Yes
  * Description: Unique identifier of the regular gift that should be converted to Telegram Stars


#### [](#upgradegift)upgradeGift

Upgrades a given regular gift to a unique gift. Requires the _can\_transfer\_and\_upgrade\_gifts_ business bot right. Additionally requires the _can\_transfer\_stars_ business bot right if the upgrade is paid. Returns _True_ on success.



* Parameter: business_connection_id
  * Type: String
  * Required: Yes
  * Description: Unique identifier of the business connection
* Parameter: owned_gift_id
  * Type: String
  * Required: Yes
  * Description: Unique identifier of the regular gift that should be upgraded to a unique one
* Parameter: keep_original_details
  * Type: Boolean
  * Required: Optional
  * Description: Pass True to keep the original gift text, sender and receiver in the upgraded gift
* Parameter: star_count
  * Type: Integer
  * Required: Optional
  * Description: The amount of Telegram Stars that will be paid for the upgrade from the business account balance. If gift.prepaid_upgrade_star_count > 0, then pass 0, otherwise, the can_transfer_stars business bot right is required and gift.upgrade_star_count must be passed.


#### [](#transfergift)transferGift

Transfers an owned unique gift to another user. Requires the _can\_transfer\_and\_upgrade\_gifts_ business bot right. Requires _can\_transfer\_stars_ business bot right if the transfer is paid. Returns _True_ on success.



* Parameter: business_connection_id
  * Type: String
  * Required: Yes
  * Description: Unique identifier of the business connection
* Parameter: owned_gift_id
  * Type: String
  * Required: Yes
  * Description: Unique identifier of the regular gift that should be transferred
* Parameter: new_owner_chat_id
  * Type: Integer
  * Required: Yes
  * Description: Unique identifier of the chat which will own the gift. The chat must be active in the last 24 hours.
* Parameter: star_count
  * Type: Integer
  * Required: Optional
  * Description: The amount of Telegram Stars that will be paid for the transfer from the business account balance. If positive, then the can_transfer_stars business bot right is required.


#### [](#poststory)postStory

Posts a story on behalf of a managed business account. Requires the _can\_manage\_stories_ business bot right. Returns [Story](#story) on success.



* Parameter: business_connection_id
  * Type: String
  * Required: Yes
  * Description: Unique identifier of the business connection
* Parameter: content
  * Type: InputStoryContent
  * Required: Yes
  * Description: Content of the story
* Parameter: active_period
  * Type: Integer
  * Required: Yes
  * Description: Period after which the story is moved to the archive, in seconds; must be one of 6 * 3600, 12 * 3600, 86400, or 2 * 86400
* Parameter: caption
  * Type: String
  * Required: Optional
  * Description: Caption of the story, 0-2048 characters after entities parsing
* Parameter: parse_mode
  * Type: String
  * Required: Optional
  * Description: Mode for parsing entities in the story caption. See formatting options for more details.
* Parameter: caption_entities
  * Type: Array of MessageEntity
  * Required: Optional
  * Description: A JSON-serialized list of special entities that appear in the caption, which can be specified instead of parse_mode
* Parameter: areas
  * Type: Array of StoryArea
  * Required: Optional
  * Description: A JSON-serialized list of clickable areas to be shown on the story
* Parameter: post_to_chat_page
  * Type: Boolean
  * Required: Optional
  * Description: Pass True to keep the story accessible after it expires
* Parameter: protect_content
  * Type: Boolean
  * Required: Optional
  * Description: Pass True if the content of the story must be protected from forwarding and screenshotting


#### [](#repoststory)repostStory

Reposts a story on behalf of a business account from another business account. Both business accounts must be managed by the same bot, and the story on the source account must have been posted (or reposted) by the bot. Requires the _can\_manage\_stories_ business bot right for both business accounts. Returns [Story](#story) on success.



* Parameter: business_connection_id
  * Type: String
  * Required: Yes
  * Description: Unique identifier of the business connection
* Parameter: from_chat_id
  * Type: Integer
  * Required: Yes
  * Description: Unique identifier of the chat which posted the story that should be reposted
* Parameter: from_story_id
  * Type: Integer
  * Required: Yes
  * Description: Unique identifier of the story that should be reposted
* Parameter: active_period
  * Type: Integer
  * Required: Yes
  * Description: Period after which the story is moved to the archive, in seconds; must be one of 6 * 3600, 12 * 3600, 86400, or 2 * 86400
* Parameter: post_to_chat_page
  * Type: Boolean
  * Required: Optional
  * Description: Pass True to keep the story accessible after it expires
* Parameter: protect_content
  * Type: Boolean
  * Required: Optional
  * Description: Pass True if the content of the story must be protected from forwarding and screenshotting


#### [](#editstory)editStory

Edits a story previously posted by the bot on behalf of a managed business account. Requires the _can\_manage\_stories_ business bot right. Returns [Story](#story) on success.



* Parameter: business_connection_id
  * Type: String
  * Required: Yes
  * Description: Unique identifier of the business connection
* Parameter: story_id
  * Type: Integer
  * Required: Yes
  * Description: Unique identifier of the story to edit
* Parameter: content
  * Type: InputStoryContent
  * Required: Yes
  * Description: Content of the story
* Parameter: caption
  * Type: String
  * Required: Optional
  * Description: Caption of the story, 0-2048 characters after entities parsing
* Parameter: parse_mode
  * Type: String
  * Required: Optional
  * Description: Mode for parsing entities in the story caption. See formatting options for more details.
* Parameter: caption_entities
  * Type: Array of MessageEntity
  * Required: Optional
  * Description: A JSON-serialized list of special entities that appear in the caption, which can be specified instead of parse_mode
* Parameter: areas
  * Type: Array of StoryArea
  * Required: Optional
  * Description: A JSON-serialized list of clickable areas to be shown on the story


#### [](#deletestory)deleteStory

Deletes a story previously posted by the bot on behalf of a managed business account. Requires the _can\_manage\_stories_ business bot right. Returns _True_ on success.


|Parameter             |Type   |Required|Description                                 |
|----------------------|-------|--------|--------------------------------------------|
|business_connection_id|String |Yes     |Unique identifier of the business connection|
|story_id              |Integer|Yes     |Unique identifier of the story to delete    |


#### [](#answerwebappquery)answerWebAppQuery

Use this method to set the result of an interaction with a [Web App](https://core.telegram.org/bots/webapps) and send a corresponding message on behalf of the user to the chat from which the query originated. On success, a [SentWebAppMessage](#sentwebappmessage) object is returned.



* Parameter: web_app_query_id
  * Type: String
  * Required: Yes
  * Description: Unique identifier for the query to be answered
* Parameter: result
  * Type: InlineQueryResult
  * Required: Yes
  * Description: A JSON-serialized object describing the message to be sent


#### [](#savepreparedinlinemessage)savePreparedInlineMessage

Stores a message that can be sent by a user of a Mini App. Returns a [PreparedInlineMessage](#preparedinlinemessage) object.



* Parameter: user_id
  * Type: Integer
  * Required: Yes
  * Description: Unique identifier of the target user that can use the prepared message
* Parameter: result
  * Type: InlineQueryResult
  * Required: Yes
  * Description: A JSON-serialized object describing the message to be sent
* Parameter: allow_user_chats
  * Type: Boolean
  * Required: Optional
  * Description: Pass True if the message can be sent to private chats with users
* Parameter: allow_bot_chats
  * Type: Boolean
  * Required: Optional
  * Description: Pass True if the message can be sent to private chats with bots
* Parameter: allow_group_chats
  * Type: Boolean
  * Required: Optional
  * Description: Pass True if the message can be sent to group and supergroup chats
* Parameter: allow_channel_chats
  * Type: Boolean
  * Required: Optional
  * Description: Pass True if the message can be sent to channel chats


#### [](#savepreparedkeyboardbutton)savePreparedKeyboardButton

Stores a keyboard button that can be used by a user within a Mini App. Returns a [PreparedKeyboardButton](#preparedkeyboardbutton) object.



* Parameter: user_id
  * Type: Integer
  * Required: Yes
  * Description: Unique identifier of the target user that can use the button
* Parameter: button
  * Type: KeyboardButton
  * Required: Yes
  * Description: A JSON-serialized object describing the button to be saved. The button must be of the type request_users, request_chat, or request_managed_bot.


#### [](#inline-mode-methods)Inline mode methods

Methods and objects used in the inline mode are described in the [Inline mode section](#inline-mode).

### [](#updating-messages)Updating messages

The following methods allow you to change an existing message in the message history instead of sending a new one with a result of an action. This is most useful for messages with [inline keyboards](about:/bots/features#inline-keyboards) using callback queries, but can also help reduce clutter in conversations with regular chat bots.

Please note, that it is currently only possible to edit messages without _reply\_markup_ or with [inline keyboards](about:/bots/features#inline-keyboards).

#### [](#editmessagetext)editMessageText

Use this method to edit text and [game](#games) messages. On success, if the edited message is not an inline message, the edited [Message](#message) is returned, otherwise _True_ is returned. Note that business messages that were not sent by the bot and do not contain an inline keyboard can only be edited within **48 hours** from the time they were sent.



* Parameter: business_connection_id
  * Type: String
  * Required: Optional
  * Description: Unique identifier of the business connection on behalf of which the message to be edited was sent
* Parameter: chat_id
  * Type: Integer or String
  * Required: Optional
  * Description: Required if inline_message_id is not specified. Unique identifier for the target chat or username of the target bot, supergroup or channel in the format @username.
* Parameter: message_id
  * Type: Integer
  * Required: Optional
  * Description: Required if inline_message_id is not specified. Identifier of the message to edit.
* Parameter: inline_message_id
  * Type: String
  * Required: Optional
  * Description: Required if chat_id and message_id are not specified. Identifier of the inline message.
* Parameter: text
  * Type: String
  * Required: Yes
  * Description: New text of the message, 1-4096 characters after entities parsing
* Parameter: parse_mode
  * Type: String
  * Required: Optional
  * Description: Mode for parsing entities in the message text. See formatting options for more details.
* Parameter: entities
  * Type: Array of MessageEntity
  * Required: Optional
  * Description: A JSON-serialized list of special entities that appear in message text, which can be specified instead of parse_mode
* Parameter: link_preview_options
  * Type: LinkPreviewOptions
  * Required: Optional
  * Description: Link preview generation options for the message
* Parameter: reply_markup
  * Type: InlineKeyboardMarkup
  * Required: Optional
  * Description: A JSON-serialized object for an inline keyboard


#### [](#editmessagecaption)editMessageCaption

Use this method to edit captions of messages. On success, if the edited message is not an inline message, the edited [Message](#message) is returned, otherwise _True_ is returned. Note that business messages that were not sent by the bot and do not contain an inline keyboard can only be edited within **48 hours** from the time they were sent.



* Parameter: business_connection_id
  * Type: String
  * Required: Optional
  * Description: Unique identifier of the business connection on behalf of which the message to be edited was sent
* Parameter: chat_id
  * Type: Integer or String
  * Required: Optional
  * Description: Required if inline_message_id is not specified. Unique identifier for the target chat or username of the target bot, supergroup or channel in the format @username.
* Parameter: message_id
  * Type: Integer
  * Required: Optional
  * Description: Required if inline_message_id is not specified. Identifier of the message to edit.
* Parameter: inline_message_id
  * Type: String
  * Required: Optional
  * Description: Required if chat_id and message_id are not specified. Identifier of the inline message.
* Parameter: caption
  * Type: String
  * Required: Optional
  * Description: New caption of the message, 0-1024 characters after entities parsing
* Parameter: parse_mode
  * Type: String
  * Required: Optional
  * Description: Mode for parsing entities in the message caption. See formatting options for more details.
* Parameter: caption_entities
  * Type: Array of MessageEntity
  * Required: Optional
  * Description: A JSON-serialized list of special entities that appear in the caption, which can be specified instead of parse_mode
* Parameter: show_caption_above_media
  * Type: Boolean
  * Required: Optional
  * Description: Pass True, if the caption must be shown above the message media. Supported only for animation, photo and video messages.
* Parameter: reply_markup
  * Type: InlineKeyboardMarkup
  * Required: Optional
  * Description: A JSON-serialized object for an inline keyboard


#### [](#editmessagemedia)editMessageMedia

Use this method to edit animation, audio, document, live photo, photo, or video messages, or to add media to text messages. If a message is part of a message album, then it can be edited only to an audio for audio albums, only to a document for document albums and to a photo, a live photo, or a video otherwise. When an inline message is edited, a new file can't be uploaded; use a previously uploaded file via its file\_id or specify a URL. On success, if the edited message is not an inline message, the edited [Message](#message) is returned, otherwise _True_ is returned. Note that business messages that were not sent by the bot and do not contain an inline keyboard can only be edited within **48 hours** from the time they were sent.



* Parameter: business_connection_id
  * Type: String
  * Required: Optional
  * Description: Unique identifier of the business connection on behalf of which the message to be edited was sent
* Parameter: chat_id
  * Type: Integer or String
  * Required: Optional
  * Description: Required if inline_message_id is not specified. Unique identifier for the target chat or username of the target bot, supergroup or channel in the format @username.
* Parameter: message_id
  * Type: Integer
  * Required: Optional
  * Description: Required if inline_message_id is not specified. Identifier of the message to edit.
* Parameter: inline_message_id
  * Type: String
  * Required: Optional
  * Description: Required if chat_id and message_id are not specified. Identifier of the inline message.
* Parameter: media
  * Type: InputMedia
  * Required: Yes
  * Description: A JSON-serialized object for a new media content of the message
* Parameter: reply_markup
  * Type: InlineKeyboardMarkup
  * Required: Optional
  * Description: A JSON-serialized object for a new inline keyboard


#### [](#editmessagelivelocation)editMessageLiveLocation

Use this method to edit live location messages. A location can be edited until its _live\_period_ expires or editing is explicitly disabled by a call to [stopMessageLiveLocation](#stopmessagelivelocation). On success, if the edited message is not an inline message, the edited [Message](#message) is returned, otherwise _True_ is returned.



* Parameter: business_connection_id
  * Type: String
  * Required: Optional
  * Description: Unique identifier of the business connection on behalf of which the message to be edited was sent
* Parameter: chat_id
  * Type: Integer or String
  * Required: Optional
  * Description: Required if inline_message_id is not specified. Unique identifier for the target chat or username of the target bot, supergroup or channel in the format @username.
* Parameter: message_id
  * Type: Integer
  * Required: Optional
  * Description: Required if inline_message_id is not specified. Identifier of the message to edit.
* Parameter: inline_message_id
  * Type: String
  * Required: Optional
  * Description: Required if chat_id and message_id are not specified. Identifier of the inline message.
* Parameter: latitude
  * Type: Float
  * Required: Yes
  * Description: Latitude of new location
* Parameter: longitude
  * Type: Float
  * Required: Yes
  * Description: Longitude of new location
* Parameter: live_period
  * Type: Integer
  * Required: Optional
  * Description: New period in seconds during which the location can be updated, starting from the message send date. If 0x7FFFFFFF is specified, then the location can be updated forever. Otherwise, the new value must not exceed the current live_period by more than a day, and the live location expiration date must remain within the next 90 days. If not specified, then live_period remains unchanged.
* Parameter: horizontal_accuracy
  * Type: Float
  * Required: Optional
  * Description: The radius of uncertainty for the location, measured in meters; 0-1500
* Parameter: heading
  * Type: Integer
  * Required: Optional
  * Description: Direction in which the user is moving, in degrees. Must be between 1 and 360 if specified.
* Parameter: proximity_alert_radius
  * Type: Integer
  * Required: Optional
  * Description: The maximum distance for proximity alerts about approaching another chat member, in meters. Must be between 1 and 100000 if specified.
* Parameter: reply_markup
  * Type: InlineKeyboardMarkup
  * Required: Optional
  * Description: A JSON-serialized object for a new inline keyboard


#### [](#stopmessagelivelocation)stopMessageLiveLocation

Use this method to stop updating a live location message before _live\_period_ expires. On success, if the message is not an inline message, the edited [Message](#message) is returned, otherwise _True_ is returned.



* Parameter: business_connection_id
  * Type: String
  * Required: Optional
  * Description: Unique identifier of the business connection on behalf of which the message to be edited was sent
* Parameter: chat_id
  * Type: Integer or String
  * Required: Optional
  * Description: Required if inline_message_id is not specified. Unique identifier for the target chat or username of the target bot, supergroup or channel in the format @username.
* Parameter: message_id
  * Type: Integer
  * Required: Optional
  * Description: Required if inline_message_id is not specified. Identifier of the message with live location to stop.
* Parameter: inline_message_id
  * Type: String
  * Required: Optional
  * Description: Required if chat_id and message_id are not specified. Identifier of the inline message.
* Parameter: reply_markup
  * Type: InlineKeyboardMarkup
  * Required: Optional
  * Description: A JSON-serialized object for a new inline keyboard


#### [](#editmessagechecklist)editMessageChecklist

Use this method to edit a checklist on behalf of a connected business account. On success, the edited [Message](#message) is returned.



* Parameter: business_connection_id
  * Type: String
  * Required: Yes
  * Description: Unique identifier of the business connection on behalf of which the message will be sent
* Parameter: chat_id
  * Type: Integer or String
  * Required: Yes
  * Description: Unique identifier for the target chat or username of the target bot in the format @username
* Parameter: message_id
  * Type: Integer
  * Required: Yes
  * Description: Unique identifier for the target message
* Parameter: checklist
  * Type: InputChecklist
  * Required: Yes
  * Description: A JSON-serialized object for the new checklist
* Parameter: reply_markup
  * Type: InlineKeyboardMarkup
  * Required: Optional
  * Description: A JSON-serialized object for the new inline keyboard for the message


#### [](#editmessagereplymarkup)editMessageReplyMarkup

Use this method to edit only the reply markup of messages. On success, if the edited message is not an inline message, the edited [Message](#message) is returned, otherwise _True_ is returned. Note that business messages that were not sent by the bot and do not contain an inline keyboard can only be edited within **48 hours** from the time they were sent.



* Parameter: business_connection_id
  * Type: String
  * Required: Optional
  * Description: Unique identifier of the business connection on behalf of which the message to be edited was sent
* Parameter: chat_id
  * Type: Integer or String
  * Required: Optional
  * Description: Required if inline_message_id is not specified. Unique identifier for the target chat or username of the target bot, supergroup or channel in the format @username.
* Parameter: message_id
  * Type: Integer
  * Required: Optional
  * Description: Required if inline_message_id is not specified. Identifier of the message to edit.
* Parameter: inline_message_id
  * Type: String
  * Required: Optional
  * Description: Required if chat_id and message_id are not specified. Identifier of the inline message.
* Parameter: reply_markup
  * Type: InlineKeyboardMarkup
  * Required: Optional
  * Description: A JSON-serialized object for an inline keyboard


#### [](#stoppoll)stopPoll

Use this method to stop a poll which was sent by the bot. On success, the stopped [Poll](#poll) is returned.



* Parameter: business_connection_id
  * Type: String
  * Required: Optional
  * Description: Unique identifier of the business connection on behalf of which the message to be edited was sent
* Parameter: chat_id
  * Type: Integer or String
  * Required: Yes
  * Description: Unique identifier for the target chat or username of the target bot, supergroup or channel in the format @username
* Parameter: message_id
  * Type: Integer
  * Required: Yes
  * Description: Identifier of the original message with the poll
* Parameter: reply_markup
  * Type: InlineKeyboardMarkup
  * Required: Optional
  * Description: A JSON-serialized object for a new message inline keyboard


#### [](#approvesuggestedpost)approveSuggestedPost

Use this method to approve a suggested post in a direct messages chat. The bot must have the 'can\_post\_messages' administrator right in the corresponding channel chat. Returns _True_ on success.



* Parameter: chat_id
  * Type: Integer
  * Required: Yes
  * Description: Unique identifier for the target direct messages chat
* Parameter: message_id
  * Type: Integer
  * Required: Yes
  * Description: Identifier of a suggested post message to approve
* Parameter: send_date
  * Type: Integer
  * Required: Optional
  * Description: Point in time (Unix timestamp) when the post is expected to be published; omit if the date has already been specified when the suggested post was created. If specified, then the date must be not more than 2678400 seconds (30 days) in the future.


#### [](#declinesuggestedpost)declineSuggestedPost

Use this method to decline a suggested post in a direct messages chat. The bot must have the 'can\_manage\_direct\_messages' administrator right in the corresponding channel chat. Returns _True_ on success.


|Parameter |Type   |Required|Description                                                    |
|----------|-------|--------|---------------------------------------------------------------|
|chat_id   |Integer|Yes     |Unique identifier for the target direct messages chat          |
|message_id|Integer|Yes     |Identifier of a suggested post message to decline              |
|comment   |String |Optional|Comment for the creator of the suggested post; 0-128 characters|


#### [](#deletemessage)deleteMessage

Use this method to delete a message, including service messages, with the following limitations:  
\- A message can only be deleted if it was sent less than 48 hours ago.  
\- Service messages about a supergroup, channel, or forum topic creation can't be deleted.  
\- A dice message in a private chat can only be deleted if it was sent more than 24 hours ago.  
\- Bots can delete outgoing messages in private chats, groups, and supergroups.  
\- Bots can delete incoming messages in private chats.  
\- Bots granted _can\_post\_messages_ permissions can delete outgoing messages in channels.  
\- If the bot is an administrator of a group, it can delete any message there.  
\- If the bot has _can\_delete\_messages_ administrator right in a supergroup or a channel, it can delete any message there.  
\- If the bot has _can\_manage\_direct\_messages_ administrator right in a channel, it can delete any message in the corresponding direct messages chat.  
Returns _True_ on success.



* Parameter: chat_id
  * Type: Integer or String
  * Required: Yes
  * Description: Unique identifier for the target chat or username of the target bot, supergroup or channel in the format @username
* Parameter: message_id
  * Type: Integer
  * Required: Yes
  * Description: Identifier of the message to delete


#### [](#deletemessages)deleteMessages

Use this method to delete multiple messages simultaneously. If some of the specified messages can't be found, they are skipped. Returns _True_ on success.



* Parameter: chat_id
  * Type: Integer or String
  * Required: Yes
  * Description: Unique identifier for the target chat or username of the target bot, supergroup or channel in the format @username
* Parameter: message_ids
  * Type: Array of Integer
  * Required: Yes
  * Description: A JSON-serialized list of 1-100 identifiers of messages to delete. See deleteMessage for limitations on which messages can be deleted.


#### [](#deletemessagereaction)deleteMessageReaction

Use this method to remove a reaction from a message in a group or a supergroup chat. The bot must have the 'can\_delete\_messages' administrator right in the chat. Returns _True_ on success.



* Parameter: chat_id
  * Type: Integer or String
  * Required: Yes
  * Description: Unique identifier for the target chat or username of the target supergroup in the format @username
* Parameter: message_id
  * Type: Integer
  * Required: Yes
  * Description: Identifier of the target message
* Parameter: user_id
  * Type: Integer
  * Required: Optional
  * Description: Identifier of the user whose reaction will be removed, if the reaction was added by a user
* Parameter: actor_chat_id
  * Type: Integer
  * Required: Optional
  * Description: Identifier of the chat whose reaction will be removed, if the reaction was added by a chat


#### [](#deleteallmessagereactions)deleteAllMessageReactions

Use this method to remove up to 10000 recent reactions in a group or a supergroup chat added by a given user or chat. The bot must have the 'can\_delete\_messages' administrator right in the chat. Returns _True_ on success.



* Parameter: chat_id
  * Type: Integer or String
  * Required: Yes
  * Description: Unique identifier for the target chat or username of the target supergroup in the format @username
* Parameter: user_id
  * Type: Integer
  * Required: Optional
  * Description: Identifier of the user whose reactions will be removed, if the reactions were added by a user
* Parameter: actor_chat_id
  * Type: Integer
  * Required: Optional
  * Description: Identifier of the chat whose reactions will be removed, if the reactions were added by a chat


### [](#stickers)Stickers

The following methods and objects allow your bot to handle stickers and sticker sets.

#### [](#sticker)Sticker

This object represents a sticker.



* Field: file_id
  * Type: String
  * Description: Identifier for this file, which can be used to download or reuse the file
* Field: file_unique_id
  * Type: String
  * Description: Unique identifier for this file, which is supposed to be the same over time and for different bots. Can't be used to download or reuse the file.
* Field: type
  * Type: String
  * Description: Type of the sticker, currently one of “regular”, “mask”, “custom_emoji”. The type of the sticker is independent from its format, which is determined by the fields is_animated and is_video.
* Field: width
  * Type: Integer
  * Description: Sticker width
* Field: height
  * Type: Integer
  * Description: Sticker height
* Field: is_animated
  * Type: Boolean
  * Description: True, if the sticker is animated
* Field: is_video
  * Type: Boolean
  * Description: True, if the sticker is a video sticker
* Field: thumbnail
  * Type: PhotoSize
  * Description: Optional. Sticker thumbnail in the .WEBP or .JPG format
* Field: emoji
  * Type: String
  * Description: Optional. Emoji associated with the sticker
* Field: set_name
  * Type: String
  * Description: Optional. Name of the sticker set to which the sticker belongs
* Field: premium_animation
  * Type: File
  * Description: Optional. For premium regular stickers, premium animation for the sticker
* Field: mask_position
  * Type: MaskPosition
  * Description: Optional. For mask stickers, the position where the mask should be placed
* Field: custom_emoji_id
  * Type: String
  * Description: Optional. For custom emoji stickers, unique identifier of the custom emoji
* Field: needs_repainting
  * Type: True
  * Description: Optional. True, if the sticker must be repainted to a text color in messages, the color of the Telegram Premium badge in emoji status, white color on chat photos, or another appropriate color in other places
* Field: file_size
  * Type: Integer
  * Description: Optional. File size in bytes


#### [](#stickerset)StickerSet

This object represents a sticker set.



* Field: name
  * Type: String
  * Description: Sticker set name
* Field: title
  * Type: String
  * Description: Sticker set title
* Field: sticker_type
  * Type: String
  * Description: Type of stickers in the set, currently one of “regular”, “mask”, “custom_emoji”
* Field: stickers
  * Type: Array of Sticker
  * Description: List of all set stickers
* Field: thumbnail
  * Type: PhotoSize
  * Description: Optional. Sticker set thumbnail in the .WEBP, .TGS, or .WEBM format


#### [](#maskposition)MaskPosition

This object describes the position on faces where a mask should be placed by default.



* Field: point
  * Type: String
  * Description: The part of the face relative to which the mask should be placed. One of “forehead”, “eyes”, “mouth”, or “chin”.
* Field: x_shift
  * Type: Float
  * Description: Shift by X-axis measured in widths of the mask scaled to the face size, from left to right. For example, choosing -1.0 will place mask just to the left of the default mask position.
* Field: y_shift
  * Type: Float
  * Description: Shift by Y-axis measured in heights of the mask scaled to the face size, from top to bottom. For example, 1.0 will place the mask just below the default mask position.
* Field: scale
  * Type: Float
  * Description: Mask scaling coefficient. For example, 2.0 means double size.


#### [](#inputsticker)InputSticker

This object describes a sticker to be added to a sticker set.



* Field: sticker
  * Type: String
  * Description: The added sticker. Pass a file_id as a String to send a file that already exists on the Telegram servers, pass an HTTP URL as a String for Telegram to get a file from the Internet, or pass “attach://<file_attach_name>” to upload a new file using multipart/form-data under <file_attach_name> name. Animated and video stickers can't be uploaded via HTTP URL. More information on Sending Files »
* Field: format
  * Type: String
  * Description: Format of the added sticker, must be one of “static” for a .WEBP or .PNG image, “animated” for a .TGS animation, “video” for a .WEBM video
* Field: emoji_list
  * Type: Array of String
  * Description: List of 1-20 emoji associated with the sticker
* Field: mask_position
  * Type: MaskPosition
  * Description: Optional. Position where the mask should be placed on faces. For “mask” stickers only.
* Field: keywords
  * Type: Array of String
  * Description: Optional. List of 0-20 search keywords for the sticker with total length of up to 64 characters. For “regular” and “custom_emoji” stickers only.


#### [](#sendsticker)sendSticker

Use this method to send static .WEBP, [animated](https://telegram.org/blog/animated-stickers) .TGS, or [video](https://telegram.org/blog/video-stickers-better-reactions) .WEBM stickers. On success, the sent [Message](#message) is returned.



* Parameter: business_connection_id
  * Type: String
  * Required: Optional
  * Description: Unique identifier of the business connection on behalf of which the message will be sent
* Parameter: chat_id
  * Type: Integer or String
  * Required: Yes
  * Description: Unique identifier for the target chat or username of the target bot, supergroup or channel in the format @username
* Parameter: message_thread_id
  * Type: Integer
  * Required: Optional
  * Description: Unique identifier for the target message thread (topic) of a forum; for forum supergroups and private chats of bots with forum topic mode enabled only
* Parameter: direct_messages_topic_id
  * Type: Integer
  * Required: Optional
  * Description: Identifier of the direct messages topic to which the message will be sent; required if the message is sent to a direct messages chat
* Parameter: sticker
  * Type: InputFile or String
  * Required: Yes
  * Description: Sticker to send. Pass a file_id as String to send a file that exists on the Telegram servers (recommended), pass an HTTP URL as a String for Telegram to get a .WEBP sticker from the Internet, or upload a new .WEBP, .TGS, or .WEBM sticker using multipart/form-data. More information on Sending Files ». Video and animated stickers can't be sent via an HTTP URL.
* Parameter: emoji
  * Type: String
  * Required: Optional
  * Description: Emoji associated with the sticker; only for just uploaded stickers
* Parameter: disable_notification
  * Type: Boolean
  * Required: Optional
  * Description: Sends the message silently. Users will receive a notification with no sound.
* Parameter: protect_content
  * Type: Boolean
  * Required: Optional
  * Description: Protects the contents of the sent message from forwarding and saving
* Parameter: allow_paid_broadcast
  * Type: Boolean
  * Required: Optional
  * Description: Pass True to allow up to 1000 messages per second, ignoring broadcasting limits for a fee of 0.1 Telegram Stars per message. The relevant Stars will be withdrawn from the bot's balance.
* Parameter: message_effect_id
  * Type: String
  * Required: Optional
  * Description: Unique identifier of the message effect to be added to the message; for private chats only
* Parameter: suggested_post_parameters
  * Type: SuggestedPostParameters
  * Required: Optional
  * Description: A JSON-serialized object containing the parameters of the suggested post to send; for direct messages chats only. If the message is sent as a reply to another suggested post, then that suggested post is automatically declined.
* Parameter: reply_parameters
  * Type: ReplyParameters
  * Required: Optional
  * Description: Description of the message to reply to
* Parameter: reply_markup
  * Type: InlineKeyboardMarkup or ReplyKeyboardMarkup or ReplyKeyboardRemove or ForceReply
  * Required: Optional
  * Description: Additional interface options. A JSON-serialized object for an inline keyboard, custom reply keyboard, instructions to remove a reply keyboard or to force a reply from the user.


#### [](#getstickerset)getStickerSet

Use this method to get a sticker set. On success, a [StickerSet](#stickerset) object is returned.


|Parameter|Type  |Required|Description            |
|---------|------|--------|-----------------------|
|name     |String|Yes     |Name of the sticker set|


#### [](#getcustomemojistickers)getCustomEmojiStickers

Use this method to get information about custom emoji stickers by their identifiers. Returns an Array of [Sticker](#sticker) objects.



* Parameter: custom_emoji_ids
  * Type: Array of String
  * Required: Yes
  * Description: A JSON-serialized list of custom emoji identifiers. At most 200 custom emoji identifiers can be specified.


#### [](#uploadstickerfile)uploadStickerFile

Use this method to upload a file with a sticker for later use in the [createNewStickerSet](#createnewstickerset), [addStickerToSet](#addstickertoset), or [replaceStickerInSet](#replacestickerinset) methods (the file can be used multiple times). Returns the uploaded [File](#file) on success.



* Parameter: user_id
  * Type: Integer
  * Required: Yes
  * Description: User identifier of sticker file owner
* Parameter: sticker
  * Type: InputFile
  * Required: Yes
  * Description: A file with the sticker in .WEBP, .PNG, .TGS, or .WEBM format. See https://core.telegram.org/stickers for technical requirements. More information on Sending Files »
* Parameter: sticker_format
  * Type: String
  * Required: Yes
  * Description: Format of the sticker, must be one of “static”, “animated”, “video”


#### [](#createnewstickerset)createNewStickerSet

Use this method to create a new sticker set owned by a user. The bot will be able to edit the sticker set thus created. Returns _True_ on success.



* Parameter: user_id
  * Type: Integer
  * Required: Yes
  * Description: User identifier of created sticker set owner
* Parameter: name
  * Type: String
  * Required: Yes
  * Description: Short name of sticker set, to be used in t.me/addstickers/ URLs (e.g., animals). Can contain only English letters, digits and underscores. Must begin with a letter, can't contain consecutive underscores and must end in "_by_<bot_username>". <bot_username> is case insensitive. 1-64 characters.
* Parameter: title
  * Type: String
  * Required: Yes
  * Description: Sticker set title, 1-64 characters
* Parameter: stickers
  * Type: Array of InputSticker
  * Required: Yes
  * Description: A JSON-serialized list of 1-50 initial stickers to be added to the sticker set
* Parameter: sticker_type
  * Type: String
  * Required: Optional
  * Description: Type of stickers in the set, pass “regular”, “mask”, or “custom_emoji”. By default, a regular sticker set is created.
* Parameter: needs_repainting
  * Type: Boolean
  * Required: Optional
  * Description: Pass True if stickers in the sticker set must be repainted to the color of text when used in messages, the accent color if used as emoji status, white on chat photos, or another appropriate color based on context; for custom emoji sticker sets only


#### [](#addstickertoset)addStickerToSet

Use this method to add a new sticker to a set created by the bot. Emoji sticker sets can have up to 200 stickers. Other sticker sets can have up to 120 stickers. Returns _True_ on success.



* Parameter: user_id
  * Type: Integer
  * Required: Yes
  * Description: User identifier of sticker set owner
* Parameter: name
  * Type: String
  * Required: Yes
  * Description: Sticker set name
* Parameter: sticker
  * Type: InputSticker
  * Required: Yes
  * Description: A JSON-serialized object with information about the added sticker. If exactly the same sticker had already been added to the set, then the set isn't changed.


#### [](#setstickerpositioninset)setStickerPositionInSet

Use this method to move a sticker in a set created by the bot to a specific position. Returns _True_ on success.


|Parameter|Type   |Required|Description                                |
|---------|-------|--------|-------------------------------------------|
|sticker  |String |Yes     |File identifier of the sticker             |
|position |Integer|Yes     |New sticker position in the set, zero-based|


#### [](#deletestickerfromset)deleteStickerFromSet

Use this method to delete a sticker from a set created by the bot. Returns _True_ on success.


|Parameter|Type  |Required|Description                   |
|---------|------|--------|------------------------------|
|sticker  |String|Yes     |File identifier of the sticker|


#### [](#replacestickerinset)replaceStickerInSet

Use this method to replace an existing sticker in a sticker set with a new one. The method is equivalent to calling [deleteStickerFromSet](#deletestickerfromset), then [addStickerToSet](#addstickertoset), then [setStickerPositionInSet](#setstickerpositioninset). Returns _True_ on success.



* Parameter: user_id
  * Type: Integer
  * Required: Yes
  * Description: User identifier of the sticker set owner
* Parameter: name
  * Type: String
  * Required: Yes
  * Description: Sticker set name
* Parameter: old_sticker
  * Type: String
  * Required: Yes
  * Description: File identifier of the replaced sticker
* Parameter: sticker
  * Type: InputSticker
  * Required: Yes
  * Description: A JSON-serialized object with information about the added sticker. If exactly the same sticker had already been added to the set, then the set remains unchanged.


#### [](#setstickeremojilist)setStickerEmojiList

Use this method to change the list of emoji assigned to a regular or custom emoji sticker. The sticker must belong to a sticker set created by the bot. Returns _True_ on success.



* Parameter: sticker
  * Type: String
  * Required: Yes
  * Description: File identifier of the sticker
* Parameter: emoji_list
  * Type: Array of String
  * Required: Yes
  * Description: A JSON-serialized list of 1-20 emoji associated with the sticker


#### [](#setstickerkeywords)setStickerKeywords

Use this method to change search keywords assigned to a regular or custom emoji sticker. The sticker must belong to a sticker set created by the bot. Returns _True_ on success.



* Parameter: sticker
  * Type: String
  * Required: Yes
  * Description: File identifier of the sticker
* Parameter: keywords
  * Type: Array of String
  * Required: Optional
  * Description: A JSON-serialized list of 0-20 search keywords for the sticker with total length of up to 64 characters


#### [](#setstickermaskposition)setStickerMaskPosition

Use this method to change the [mask position](#maskposition) of a mask sticker. The sticker must belong to a sticker set that was created by the bot. Returns _True_ on success.



* Parameter: sticker
  * Type: String
  * Required: Yes
  * Description: File identifier of the sticker
* Parameter: mask_position
  * Type: MaskPosition
  * Required: Optional
  * Description: A JSON-serialized object with the position where the mask should be placed on faces. Omit the parameter to remove the mask position.


#### [](#setstickersettitle)setStickerSetTitle

Use this method to set the title of a created sticker set. Returns _True_ on success.


|Parameter|Type  |Required|Description                       |
|---------|------|--------|----------------------------------|
|name     |String|Yes     |Sticker set name                  |
|title    |String|Yes     |Sticker set title, 1-64 characters|


#### [](#setstickersetthumbnail)setStickerSetThumbnail

Use this method to set the thumbnail of a regular or mask sticker set. The format of the thumbnail file must match the format of the stickers in the set. Returns _True_ on success.



* Parameter: name
  * Type: String
  * Required: Yes
  * Description: Sticker set name
* Parameter: user_id
  * Type: Integer
  * Required: Yes
  * Description: User identifier of the sticker set owner
* Parameter: thumbnail
  * Type: InputFile or String
  * Required: Optional
  * Description: A .WEBP or .PNG image with the thumbnail, must be up to 128 kilobytes in size and have a width and height of exactly 100px, or a .TGS animation with a thumbnail up to 32 kilobytes in size (see https://core.telegram.org/stickers#animation-requirements for animated sticker technical requirements), or a .WEBM video with the thumbnail up to 32 kilobytes in size; see https://core.telegram.org/stickers#video-requirements for video sticker technical requirements. Pass a file_id as a String to send a file that already exists on the Telegram servers, pass an HTTP URL as a String for Telegram to get a file from the Internet, or upload a new one using multipart/form-data. More information on Sending Files ». Animated and video sticker set thumbnails can't be uploaded via HTTP URL. If omitted, then the thumbnail is dropped and the first sticker is used as the thumbnail.
* Parameter: format
  * Type: String
  * Required: Yes
  * Description: Format of the thumbnail, must be one of “static” for a .WEBP or .PNG image, “animated” for a .TGS animation, or “video” for a .WEBM video


#### [](#setcustomemojistickersetthumbnail)setCustomEmojiStickerSetThumbnail

Use this method to set the thumbnail of a custom emoji sticker set. Returns _True_ on success.



* Parameter: name
  * Type: String
  * Required: Yes
  * Description: Sticker set name
* Parameter: custom_emoji_id
  * Type: String
  * Required: Optional
  * Description: Custom emoji identifier of a sticker from the sticker set; pass an empty string to drop the thumbnail and use the first sticker as the thumbnail


#### [](#deletestickerset)deleteStickerSet

Use this method to delete a sticker set that was created by the bot. Returns _True_ on success.


|Parameter|Type  |Required|Description     |
|---------|------|--------|----------------|
|name     |String|Yes     |Sticker set name|


### [](#inline-mode)Inline mode

The following methods and objects allow your bot to work in [inline mode](https://core.telegram.org/bots/inline).  
Please see our [Introduction to Inline bots](https://core.telegram.org/bots/inline) for more details.

To enable this option, send the `/setinline` command to [@BotFather](https://t.me/botfather) and provide the placeholder text that the user will see in the input field after typing your bot's name.

#### [](#inlinequery)InlineQuery

This object represents an incoming inline query. When the user sends an empty query, your bot could return some default or trending results.



* Field: id
  * Type: String
  * Description: Unique identifier for this query
* Field: from
  * Type: User
  * Description: Sender
* Field: query
  * Type: String
  * Description: Text of the query (up to 256 characters)
* Field: offset
  * Type: String
  * Description: Offset of the results to be returned, can be controlled by the bot
* Field: chat_type
  * Type: String
  * Description: Optional. Type of the chat from which the inline query was sent. Can be either “sender” for a private chat with the inline query sender, “private”, “group”, “supergroup”, or “channel”. The chat type should be always known for requests sent from official clients and most third-party clients, unless the request was sent from a secret chat.
* Field: location
  * Type: Location
  * Description: Optional. Sender location, only for bots that request user location


#### [](#answerinlinequery)answerInlineQuery

Use this method to send answers to an inline query. On success, _True_ is returned.  
No more than **50** results per query are allowed.



* Parameter: inline_query_id
  * Type: String
  * Required: Yes
  * Description: Unique identifier for the answered query
* Parameter: results
  * Type: Array of InlineQueryResult
  * Required: Yes
  * Description: A JSON-serialized array of results for the inline query
* Parameter: cache_time
  * Type: Integer
  * Required: Optional
  * Description: The maximum amount of time in seconds that the result of the inline query may be cached on the server. Defaults to 300.
* Parameter: is_personal
  * Type: Boolean
  * Required: Optional
  * Description: Pass True if results may be cached on the server side only for the user that sent the query. By default, results may be returned to any user who sends the same query.
* Parameter: next_offset
  * Type: String
  * Required: Optional
  * Description: Pass the offset that a client should send in the next query with the same text to receive more results. Pass an empty string if there are no more results or if you don't support pagination. Offset length can't exceed 64 bytes.
* Parameter: button
  * Type: InlineQueryResultsButton
  * Required: Optional
  * Description: A JSON-serialized object describing a button to be shown above inline query results


#### [](#inlinequeryresultsbutton)InlineQueryResultsButton

This object represents a button to be shown above inline query results. You **must** use exactly one of the optional fields.



* Field: text
  * Type: String
  * Description: Label text on the button
* Field: web_app
  * Type: WebAppInfo
  * Description: Optional. Description of the Web App that will be launched when the user presses the button. The Web App will be able to switch back to the inline mode using the method switchInlineQuery inside the Web App.
* Field: start_parameter
  * Type: String
  * Description: Optional. Deep-linking parameter for the /start message sent to the bot when a user presses the button. 1-64 characters, only A-Z, a-z, 0-9, _ and - are allowed.Example: An inline bot that sends YouTube videos can ask the user to connect the bot to their YouTube account to adapt search results accordingly. To do this, it displays a 'Connect your YouTube account' button above the results, or even before showing any. The user presses the button, switches to a private chat with the bot and, in doing so, passes a start parameter that instructs the bot to return an OAuth link. Once done, the bot can offer a switch_inline button so that the user can easily return to the chat where they wanted to use the bot's inline capabilities.


#### [](#inlinequeryresult)InlineQueryResult

This object represents one result of an inline query. Telegram clients currently support results of the following 20 types:

*   [InlineQueryResultCachedAudio](#inlinequeryresultcachedaudio)
*   [InlineQueryResultCachedDocument](#inlinequeryresultcacheddocument)
*   [InlineQueryResultCachedGif](#inlinequeryresultcachedgif)
*   [InlineQueryResultCachedMpeg4Gif](#inlinequeryresultcachedmpeg4gif)
*   [InlineQueryResultCachedPhoto](#inlinequeryresultcachedphoto)
*   [InlineQueryResultCachedSticker](#inlinequeryresultcachedsticker)
*   [InlineQueryResultCachedVideo](#inlinequeryresultcachedvideo)
*   [InlineQueryResultCachedVoice](#inlinequeryresultcachedvoice)
*   [InlineQueryResultArticle](#inlinequeryresultarticle)
*   [InlineQueryResultAudio](#inlinequeryresultaudio)
*   [InlineQueryResultContact](#inlinequeryresultcontact)
*   [InlineQueryResultGame](#inlinequeryresultgame)
*   [InlineQueryResultDocument](#inlinequeryresultdocument)
*   [InlineQueryResultGif](#inlinequeryresultgif)
*   [InlineQueryResultLocation](#inlinequeryresultlocation)
*   [InlineQueryResultMpeg4Gif](#inlinequeryresultmpeg4gif)
*   [InlineQueryResultPhoto](#inlinequeryresultphoto)
*   [InlineQueryResultVenue](#inlinequeryresultvenue)
*   [InlineQueryResultVideo](#inlinequeryresultvideo)
*   [InlineQueryResultVoice](#inlinequeryresultvoice)

**Note:** All URLs passed in inline query results will be available to end users and therefore must be assumed to be **public**.

#### [](#inlinequeryresultarticle)InlineQueryResultArticle

Represents a link to an article or web page.


|Field                |Type                |Description                                      |
|---------------------|--------------------|-------------------------------------------------|
|type                 |String              |Type of the result, must be article              |
|id                   |String              |Unique identifier for this result, 1-64 Bytes    |
|title                |String              |Title of the result                              |
|input_message_content|InputMessageContent |Content of the message to be sent                |
|reply_markup         |InlineKeyboardMarkup|Optional. Inline keyboard attached to the message|
|url                  |String              |Optional. URL of the result                      |
|description          |String              |Optional. Short description of the result        |
|thumbnail_url        |String              |Optional. Url of the thumbnail for the result    |
|thumbnail_width      |Integer             |Optional. Thumbnail width                        |
|thumbnail_height     |Integer             |Optional. Thumbnail height                       |


#### [](#inlinequeryresultphoto)InlineQueryResultPhoto

Represents a link to a photo. By default, this photo will be sent by the user with optional caption. Alternatively, you can use _input\_message\_content_ to send a message with the specified content instead of the photo.



* Field: type
  * Type: String
  * Description: Type of the result, must be photo
* Field: id
  * Type: String
  * Description: Unique identifier for this result, 1-64 bytes
* Field: photo_url
  * Type: String
  * Description: A valid URL of the photo. Photo must be in JPEG format. Photo size must not exceed 5MB.
* Field: thumbnail_url
  * Type: String
  * Description: URL of the thumbnail for the photo
* Field: photo_width
  * Type: Integer
  * Description: Optional. Width of the photo
* Field: photo_height
  * Type: Integer
  * Description: Optional. Height of the photo
* Field: title
  * Type: String
  * Description: Optional. Title for the result
* Field: description
  * Type: String
  * Description: Optional. Short description of the result
* Field: caption
  * Type: String
  * Description: Optional. Caption of the photo to be sent, 0-1024 characters after entities parsing
* Field: parse_mode
  * Type: String
  * Description: Optional. Mode for parsing entities in the photo caption. See formatting options for more details.
* Field: caption_entities
  * Type: Array of MessageEntity
  * Description: Optional. List of special entities that appear in the caption, which can be specified instead of parse_mode
* Field: show_caption_above_media
  * Type: Boolean
  * Description: Optional. Pass True, if the caption must be shown above the message media
* Field: reply_markup
  * Type: InlineKeyboardMarkup
  * Description: Optional. Inline keyboard attached to the message
* Field: input_message_content
  * Type: InputMessageContent
  * Description: Optional. Content of the message to be sent instead of the photo


#### [](#inlinequeryresultgif)InlineQueryResultGif

Represents a link to an animated GIF file. By default, this animated GIF file will be sent by the user with optional caption. Alternatively, you can use _input\_message\_content_ to send a message with the specified content instead of the animation.



* Field: type
  * Type: String
  * Description: Type of the result, must be gif
* Field: id
  * Type: String
  * Description: Unique identifier for this result, 1-64 bytes
* Field: gif_url
  * Type: String
  * Description: A valid URL for the GIF file
* Field: gif_width
  * Type: Integer
  * Description: Optional. Width of the GIF
* Field: gif_height
  * Type: Integer
  * Description: Optional. Height of the GIF
* Field: gif_duration
  * Type: Integer
  * Description: Optional. Duration of the GIF in seconds
* Field: thumbnail_url
  * Type: String
  * Description: URL of the static (JPEG or GIF) or animated (MPEG4) thumbnail for the result
* Field: thumbnail_mime_type
  * Type: String
  * Description: Optional. MIME type of the thumbnail, must be one of “image/jpeg”, “image/gif”, or “video/mp4”. Defaults to “image/jpeg”.
* Field: title
  * Type: String
  * Description: Optional. Title for the result
* Field: caption
  * Type: String
  * Description: Optional. Caption of the GIF file to be sent, 0-1024 characters after entities parsing
* Field: parse_mode
  * Type: String
  * Description: Optional. Mode for parsing entities in the caption. See formatting options for more details.
* Field: caption_entities
  * Type: Array of MessageEntity
  * Description: Optional. List of special entities that appear in the caption, which can be specified instead of parse_mode
* Field: show_caption_above_media
  * Type: Boolean
  * Description: Optional. Pass True, if the caption must be shown above the message media
* Field: reply_markup
  * Type: InlineKeyboardMarkup
  * Description: Optional. Inline keyboard attached to the message
* Field: input_message_content
  * Type: InputMessageContent
  * Description: Optional. Content of the message to be sent instead of the GIF animation


#### [](#inlinequeryresultmpeg4gif)InlineQueryResultMpeg4Gif

Represents a link to a video animation (H.264/MPEG-4 AVC video without sound). By default, this animated MPEG-4 file will be sent by the user with optional caption. Alternatively, you can use _input\_message\_content_ to send a message with the specified content instead of the animation.



* Field: type
  * Type: String
  * Description: Type of the result, must be mpeg4_gif
* Field: id
  * Type: String
  * Description: Unique identifier for this result, 1-64 bytes
* Field: mpeg4_url
  * Type: String
  * Description: A valid URL for the MPEG4 file
* Field: mpeg4_width
  * Type: Integer
  * Description: Optional. Video width
* Field: mpeg4_height
  * Type: Integer
  * Description: Optional. Video height
* Field: mpeg4_duration
  * Type: Integer
  * Description: Optional. Video duration in seconds
* Field: thumbnail_url
  * Type: String
  * Description: URL of the static (JPEG or GIF) or animated (MPEG4) thumbnail for the result
* Field: thumbnail_mime_type
  * Type: String
  * Description: Optional. MIME type of the thumbnail, must be one of “image/jpeg”, “image/gif”, or “video/mp4”. Defaults to “image/jpeg”.
* Field: title
  * Type: String
  * Description: Optional. Title for the result
* Field: caption
  * Type: String
  * Description: Optional. Caption of the MPEG-4 file to be sent, 0-1024 characters after entities parsing
* Field: parse_mode
  * Type: String
  * Description: Optional. Mode for parsing entities in the caption. See formatting options for more details.
* Field: caption_entities
  * Type: Array of MessageEntity
  * Description: Optional. List of special entities that appear in the caption, which can be specified instead of parse_mode
* Field: show_caption_above_media
  * Type: Boolean
  * Description: Optional. Pass True, if the caption must be shown above the message media
* Field: reply_markup
  * Type: InlineKeyboardMarkup
  * Description: Optional. Inline keyboard attached to the message
* Field: input_message_content
  * Type: InputMessageContent
  * Description: Optional. Content of the message to be sent instead of the video animation


#### [](#inlinequeryresultvideo)InlineQueryResultVideo

Represents a link to a page containing an embedded video player or a video file. By default, this video file will be sent by the user with an optional caption. Alternatively, you can use _input\_message\_content_ to send a message with the specified content instead of the video.

> If an InlineQueryResultVideo message contains an embedded video (e.g., YouTube), you **must** replace its content using _input\_message\_content_.



* Field: type
  * Type: String
  * Description: Type of the result, must be video
* Field: id
  * Type: String
  * Description: Unique identifier for this result, 1-64 bytes
* Field: video_url
  * Type: String
  * Description: A valid URL for the embedded video player or video file
* Field: mime_type
  * Type: String
  * Description: MIME type of the content of the video URL, “text/html” or “video/mp4”
* Field: thumbnail_url
  * Type: String
  * Description: URL of the thumbnail (JPEG only) for the video
* Field: title
  * Type: String
  * Description: Title for the result
* Field: caption
  * Type: String
  * Description: Optional. Caption of the video to be sent, 0-1024 characters after entities parsing
* Field: parse_mode
  * Type: String
  * Description: Optional. Mode for parsing entities in the video caption. See formatting options for more details.
* Field: caption_entities
  * Type: Array of MessageEntity
  * Description: Optional. List of special entities that appear in the caption, which can be specified instead of parse_mode
* Field: show_caption_above_media
  * Type: Boolean
  * Description: Optional. Pass True, if the caption must be shown above the message media
* Field: video_width
  * Type: Integer
  * Description: Optional. Video width
* Field: video_height
  * Type: Integer
  * Description: Optional. Video height
* Field: video_duration
  * Type: Integer
  * Description: Optional. Video duration in seconds
* Field: description
  * Type: String
  * Description: Optional. Short description of the result
* Field: reply_markup
  * Type: InlineKeyboardMarkup
  * Description: Optional. Inline keyboard attached to the message
* Field: input_message_content
  * Type: InputMessageContent
  * Description: Optional. Content of the message to be sent instead of the video. This field is required if InlineQueryResultVideo is used to send an HTML-page as a result (e.g., a YouTube video).


#### [](#inlinequeryresultaudio)InlineQueryResultAudio

Represents a link to an MP3 audio file. By default, this audio file will be sent by the user. Alternatively, you can use _input\_message\_content_ to send a message with the specified content instead of the audio.



* Field: type
  * Type: String
  * Description: Type of the result, must be audio
* Field: id
  * Type: String
  * Description: Unique identifier for this result, 1-64 bytes
* Field: audio_url
  * Type: String
  * Description: A valid URL for the audio file
* Field: title
  * Type: String
  * Description: Title
* Field: caption
  * Type: String
  * Description: Optional. Caption, 0-1024 characters after entities parsing
* Field: parse_mode
  * Type: String
  * Description: Optional. Mode for parsing entities in the audio caption. See formatting options for more details.
* Field: caption_entities
  * Type: Array of MessageEntity
  * Description: Optional. List of special entities that appear in the caption, which can be specified instead of parse_mode
* Field: performer
  * Type: String
  * Description: Optional. Performer
* Field: audio_duration
  * Type: Integer
  * Description: Optional. Audio duration in seconds
* Field: reply_markup
  * Type: InlineKeyboardMarkup
  * Description: Optional. Inline keyboard attached to the message
* Field: input_message_content
  * Type: InputMessageContent
  * Description: Optional. Content of the message to be sent instead of the audio


#### [](#inlinequeryresultvoice)InlineQueryResultVoice

Represents a link to a voice recording in an .OGG container encoded with OPUS. By default, this voice recording will be sent by the user. Alternatively, you can use _input\_message\_content_ to send a message with the specified content instead of the the voice message.



* Field: type
  * Type: String
  * Description: Type of the result, must be voice
* Field: id
  * Type: String
  * Description: Unique identifier for this result, 1-64 bytes
* Field: voice_url
  * Type: String
  * Description: A valid URL for the voice recording
* Field: title
  * Type: String
  * Description: Recording title
* Field: caption
  * Type: String
  * Description: Optional. Caption, 0-1024 characters after entities parsing
* Field: parse_mode
  * Type: String
  * Description: Optional. Mode for parsing entities in the voice message caption. See formatting options for more details.
* Field: caption_entities
  * Type: Array of MessageEntity
  * Description: Optional. List of special entities that appear in the caption, which can be specified instead of parse_mode
* Field: voice_duration
  * Type: Integer
  * Description: Optional. Recording duration in seconds
* Field: reply_markup
  * Type: InlineKeyboardMarkup
  * Description: Optional. Inline keyboard attached to the message
* Field: input_message_content
  * Type: InputMessageContent
  * Description: Optional. Content of the message to be sent instead of the voice recording


#### [](#inlinequeryresultdocument)InlineQueryResultDocument

Represents a link to a file. By default, this file will be sent by the user with an optional caption. Alternatively, you can use _input\_message\_content_ to send a message with the specified content instead of the file. Currently, only **.PDF** and **.ZIP** files can be sent using this method.



* Field: type
  * Type: String
  * Description: Type of the result, must be document
* Field: id
  * Type: String
  * Description: Unique identifier for this result, 1-64 bytes
* Field: title
  * Type: String
  * Description: Title for the result
* Field: caption
  * Type: String
  * Description: Optional. Caption of the document to be sent, 0-1024 characters after entities parsing
* Field: parse_mode
  * Type: String
  * Description: Optional. Mode for parsing entities in the document caption. See formatting options for more details.
* Field: caption_entities
  * Type: Array of MessageEntity
  * Description: Optional. List of special entities that appear in the caption, which can be specified instead of parse_mode
* Field: document_url
  * Type: String
  * Description: A valid URL for the file
* Field: mime_type
  * Type: String
  * Description: MIME type of the content of the file, either “application/pdf” or “application/zip”
* Field: description
  * Type: String
  * Description: Optional. Short description of the result
* Field: reply_markup
  * Type: InlineKeyboardMarkup
  * Description: Optional. Inline keyboard attached to the message
* Field: input_message_content
  * Type: InputMessageContent
  * Description: Optional. Content of the message to be sent instead of the file
* Field: thumbnail_url
  * Type: String
  * Description: Optional. URL of the thumbnail (JPEG only) for the file
* Field: thumbnail_width
  * Type: Integer
  * Description: Optional. Thumbnail width
* Field: thumbnail_height
  * Type: Integer
  * Description: Optional. Thumbnail height


#### [](#inlinequeryresultlocation)InlineQueryResultLocation

Represents a location on a map. By default, the location will be sent by the user. Alternatively, you can use _input\_message\_content_ to send a message with the specified content instead of the location.



* Field: type
  * Type: String
  * Description: Type of the result, must be location
* Field: id
  * Type: String
  * Description: Unique identifier for this result, 1-64 Bytes
* Field: latitude
  * Type: Float
  * Description: Location latitude in degrees
* Field: longitude
  * Type: Float
  * Description: Location longitude in degrees
* Field: title
  * Type: String
  * Description: Location title
* Field: horizontal_accuracy
  * Type: Float
  * Description: Optional. The radius of uncertainty for the location, measured in meters; 0-1500
* Field: live_period
  * Type: Integer
  * Description: Optional. Period in seconds during which the location can be updated, must be between 60 and 86400, or 0x7FFFFFFF for live locations that can be edited indefinitely
* Field: heading
  * Type: Integer
  * Description: Optional. For live locations, a direction in which the user is moving, in degrees. Must be between 1 and 360 if specified.
* Field: proximity_alert_radius
  * Type: Integer
  * Description: Optional. For live locations, a maximum distance for proximity alerts about approaching another chat member, in meters. Must be between 1 and 100000 if specified.
* Field: reply_markup
  * Type: InlineKeyboardMarkup
  * Description: Optional. Inline keyboard attached to the message
* Field: input_message_content
  * Type: InputMessageContent
  * Description: Optional. Content of the message to be sent instead of the location
* Field: thumbnail_url
  * Type: String
  * Description: Optional. Url of the thumbnail for the result
* Field: thumbnail_width
  * Type: Integer
  * Description: Optional. Thumbnail width
* Field: thumbnail_height
  * Type: Integer
  * Description: Optional. Thumbnail height


#### [](#inlinequeryresultvenue)InlineQueryResultVenue

Represents a venue. By default, the venue will be sent by the user. Alternatively, you can use _input\_message\_content_ to send a message with the specified content instead of the venue.



* Field: type
  * Type: String
  * Description: Type of the result, must be venue
* Field: id
  * Type: String
  * Description: Unique identifier for this result, 1-64 Bytes
* Field: latitude
  * Type: Float
  * Description: Latitude of the venue location in degrees
* Field: longitude
  * Type: Float
  * Description: Longitude of the venue location in degrees
* Field: title
  * Type: String
  * Description: Title of the venue
* Field: address
  * Type: String
  * Description: Address of the venue
* Field: foursquare_id
  * Type: String
  * Description: Optional. Foursquare identifier of the venue if known
* Field: foursquare_type
  * Type: String
  * Description: Optional. Foursquare type of the venue, if known. (For example, “arts_entertainment/default”, “arts_entertainment/aquarium” or “food/icecream”.)
* Field: google_place_id
  * Type: String
  * Description: Optional. Google Places identifier of the venue
* Field: google_place_type
  * Type: String
  * Description: Optional. Google Places type of the venue. (See supported types.)
* Field: reply_markup
  * Type: InlineKeyboardMarkup
  * Description: Optional. Inline keyboard attached to the message
* Field: input_message_content
  * Type: InputMessageContent
  * Description: Optional. Content of the message to be sent instead of the venue
* Field: thumbnail_url
  * Type: String
  * Description: Optional. Url of the thumbnail for the result
* Field: thumbnail_width
  * Type: Integer
  * Description: Optional. Thumbnail width
* Field: thumbnail_height
  * Type: Integer
  * Description: Optional. Thumbnail height


#### [](#inlinequeryresultcontact)InlineQueryResultContact

Represents a contact with a phone number. By default, this contact will be sent by the user. Alternatively, you can use _input\_message\_content_ to send a message with the specified content instead of the contact.



* Field: type
  * Type: String
  * Description: Type of the result, must be contact
* Field: id
  * Type: String
  * Description: Unique identifier for this result, 1-64 Bytes
* Field: phone_number
  * Type: String
  * Description: Contact's phone number
* Field: first_name
  * Type: String
  * Description: Contact's first name
* Field: last_name
  * Type: String
  * Description: Optional. Contact's last name
* Field: vcard
  * Type: String
  * Description: Optional. Additional data about the contact in the form of a vCard, 0-2048 bytes
* Field: reply_markup
  * Type: InlineKeyboardMarkup
  * Description: Optional. Inline keyboard attached to the message
* Field: input_message_content
  * Type: InputMessageContent
  * Description: Optional. Content of the message to be sent instead of the contact
* Field: thumbnail_url
  * Type: String
  * Description: Optional. Url of the thumbnail for the result
* Field: thumbnail_width
  * Type: Integer
  * Description: Optional. Thumbnail width
* Field: thumbnail_height
  * Type: Integer
  * Description: Optional. Thumbnail height


#### [](#inlinequeryresultgame)InlineQueryResultGame

Represents a [Game](#games).


|Field          |Type                |Description                                      |
|---------------|--------------------|-------------------------------------------------|
|type           |String              |Type of the result, must be game                 |
|id             |String              |Unique identifier for this result, 1-64 bytes    |
|game_short_name|String              |Short name of the game                           |
|reply_markup   |InlineKeyboardMarkup|Optional. Inline keyboard attached to the message|


#### [](#inlinequeryresultcachedphoto)InlineQueryResultCachedPhoto

Represents a link to a photo stored on the Telegram servers. By default, this photo will be sent by the user with an optional caption. Alternatively, you can use _input\_message\_content_ to send a message with the specified content instead of the photo.



* Field: type
  * Type: String
  * Description: Type of the result, must be photo
* Field: id
  * Type: String
  * Description: Unique identifier for this result, 1-64 bytes
* Field: photo_file_id
  * Type: String
  * Description: A valid file identifier of the photo
* Field: title
  * Type: String
  * Description: Optional. Title for the result
* Field: description
  * Type: String
  * Description: Optional. Short description of the result
* Field: caption
  * Type: String
  * Description: Optional. Caption of the photo to be sent, 0-1024 characters after entities parsing
* Field: parse_mode
  * Type: String
  * Description: Optional. Mode for parsing entities in the photo caption. See formatting options for more details.
* Field: caption_entities
  * Type: Array of MessageEntity
  * Description: Optional. List of special entities that appear in the caption, which can be specified instead of parse_mode
* Field: show_caption_above_media
  * Type: Boolean
  * Description: Optional. Pass True, if the caption must be shown above the message media
* Field: reply_markup
  * Type: InlineKeyboardMarkup
  * Description: Optional. Inline keyboard attached to the message
* Field: input_message_content
  * Type: InputMessageContent
  * Description: Optional. Content of the message to be sent instead of the photo


#### [](#inlinequeryresultcachedgif)InlineQueryResultCachedGif

Represents a link to an animated GIF file stored on the Telegram servers. By default, this animated GIF file will be sent by the user with an optional caption. Alternatively, you can use _input\_message\_content_ to send a message with specified content instead of the animation.



* Field: type
  * Type: String
  * Description: Type of the result, must be gif
* Field: id
  * Type: String
  * Description: Unique identifier for this result, 1-64 bytes
* Field: gif_file_id
  * Type: String
  * Description: A valid file identifier for the GIF file
* Field: title
  * Type: String
  * Description: Optional. Title for the result
* Field: caption
  * Type: String
  * Description: Optional. Caption of the GIF file to be sent, 0-1024 characters after entities parsing
* Field: parse_mode
  * Type: String
  * Description: Optional. Mode for parsing entities in the caption. See formatting options for more details.
* Field: caption_entities
  * Type: Array of MessageEntity
  * Description: Optional. List of special entities that appear in the caption, which can be specified instead of parse_mode
* Field: show_caption_above_media
  * Type: Boolean
  * Description: Optional. Pass True, if the caption must be shown above the message media
* Field: reply_markup
  * Type: InlineKeyboardMarkup
  * Description: Optional. Inline keyboard attached to the message
* Field: input_message_content
  * Type: InputMessageContent
  * Description: Optional. Content of the message to be sent instead of the GIF animation


#### [](#inlinequeryresultcachedmpeg4gif)InlineQueryResultCachedMpeg4Gif

Represents a link to a video animation (H.264/MPEG-4 AVC video without sound) stored on the Telegram servers. By default, this animated MPEG-4 file will be sent by the user with an optional caption. Alternatively, you can use _input\_message\_content_ to send a message with the specified content instead of the animation.



* Field: type
  * Type: String
  * Description: Type of the result, must be mpeg4_gif
* Field: id
  * Type: String
  * Description: Unique identifier for this result, 1-64 bytes
* Field: mpeg4_file_id
  * Type: String
  * Description: A valid file identifier for the MPEG4 file
* Field: title
  * Type: String
  * Description: Optional. Title for the result
* Field: caption
  * Type: String
  * Description: Optional. Caption of the MPEG-4 file to be sent, 0-1024 characters after entities parsing
* Field: parse_mode
  * Type: String
  * Description: Optional. Mode for parsing entities in the caption. See formatting options for more details.
* Field: caption_entities
  * Type: Array of MessageEntity
  * Description: Optional. List of special entities that appear in the caption, which can be specified instead of parse_mode
* Field: show_caption_above_media
  * Type: Boolean
  * Description: Optional. Pass True, if the caption must be shown above the message media
* Field: reply_markup
  * Type: InlineKeyboardMarkup
  * Description: Optional. Inline keyboard attached to the message
* Field: input_message_content
  * Type: InputMessageContent
  * Description: Optional. Content of the message to be sent instead of the video animation


#### [](#inlinequeryresultcachedsticker)InlineQueryResultCachedSticker

Represents a link to a sticker stored on the Telegram servers. By default, this sticker will be sent by the user. Alternatively, you can use _input\_message\_content_ to send a message with the specified content instead of the sticker.



* Field: type
  * Type: String
  * Description: Type of the result, must be sticker
* Field: id
  * Type: String
  * Description: Unique identifier for this result, 1-64 bytes
* Field: sticker_file_id
  * Type: String
  * Description: A valid file identifier of the sticker
* Field: reply_markup
  * Type: InlineKeyboardMarkup
  * Description: Optional. Inline keyboard attached to the message
* Field: input_message_content
  * Type: InputMessageContent
  * Description: Optional. Content of the message to be sent instead of the sticker


#### [](#inlinequeryresultcacheddocument)InlineQueryResultCachedDocument

Represents a link to a file stored on the Telegram servers. By default, this file will be sent by the user with an optional caption. Alternatively, you can use _input\_message\_content_ to send a message with the specified content instead of the file.



* Field: type
  * Type: String
  * Description: Type of the result, must be document
* Field: id
  * Type: String
  * Description: Unique identifier for this result, 1-64 bytes
* Field: title
  * Type: String
  * Description: Title for the result
* Field: document_file_id
  * Type: String
  * Description: A valid file identifier for the file
* Field: description
  * Type: String
  * Description: Optional. Short description of the result
* Field: caption
  * Type: String
  * Description: Optional. Caption of the document to be sent, 0-1024 characters after entities parsing
* Field: parse_mode
  * Type: String
  * Description: Optional. Mode for parsing entities in the document caption. See formatting options for more details.
* Field: caption_entities
  * Type: Array of MessageEntity
  * Description: Optional. List of special entities that appear in the caption, which can be specified instead of parse_mode
* Field: reply_markup
  * Type: InlineKeyboardMarkup
  * Description: Optional. Inline keyboard attached to the message
* Field: input_message_content
  * Type: InputMessageContent
  * Description: Optional. Content of the message to be sent instead of the file


#### [](#inlinequeryresultcachedvideo)InlineQueryResultCachedVideo

Represents a link to a video file stored on the Telegram servers. By default, this video file will be sent by the user with an optional caption. Alternatively, you can use _input\_message\_content_ to send a message with the specified content instead of the video.



* Field: type
  * Type: String
  * Description: Type of the result, must be video
* Field: id
  * Type: String
  * Description: Unique identifier for this result, 1-64 bytes
* Field: video_file_id
  * Type: String
  * Description: A valid file identifier for the video file
* Field: title
  * Type: String
  * Description: Title for the result
* Field: description
  * Type: String
  * Description: Optional. Short description of the result
* Field: caption
  * Type: String
  * Description: Optional. Caption of the video to be sent, 0-1024 characters after entities parsing
* Field: parse_mode
  * Type: String
  * Description: Optional. Mode for parsing entities in the video caption. See formatting options for more details.
* Field: caption_entities
  * Type: Array of MessageEntity
  * Description: Optional. List of special entities that appear in the caption, which can be specified instead of parse_mode
* Field: show_caption_above_media
  * Type: Boolean
  * Description: Optional. Pass True, if the caption must be shown above the message media
* Field: reply_markup
  * Type: InlineKeyboardMarkup
  * Description: Optional. Inline keyboard attached to the message
* Field: input_message_content
  * Type: InputMessageContent
  * Description: Optional. Content of the message to be sent instead of the video


#### [](#inlinequeryresultcachedvoice)InlineQueryResultCachedVoice

Represents a link to a voice message stored on the Telegram servers. By default, this voice message will be sent by the user. Alternatively, you can use _input\_message\_content_ to send a message with the specified content instead of the voice message.



* Field: type
  * Type: String
  * Description: Type of the result, must be voice
* Field: id
  * Type: String
  * Description: Unique identifier for this result, 1-64 bytes
* Field: voice_file_id
  * Type: String
  * Description: A valid file identifier for the voice message
* Field: title
  * Type: String
  * Description: Voice message title
* Field: caption
  * Type: String
  * Description: Optional. Caption, 0-1024 characters after entities parsing
* Field: parse_mode
  * Type: String
  * Description: Optional. Mode for parsing entities in the voice message caption. See formatting options for more details.
* Field: caption_entities
  * Type: Array of MessageEntity
  * Description: Optional. List of special entities that appear in the caption, which can be specified instead of parse_mode
* Field: reply_markup
  * Type: InlineKeyboardMarkup
  * Description: Optional. Inline keyboard attached to the message
* Field: input_message_content
  * Type: InputMessageContent
  * Description: Optional. Content of the message to be sent instead of the voice message


#### [](#inlinequeryresultcachedaudio)InlineQueryResultCachedAudio

Represents a link to an MP3 audio file stored on the Telegram servers. By default, this audio file will be sent by the user. Alternatively, you can use _input\_message\_content_ to send a message with the specified content instead of the audio.



* Field: type
  * Type: String
  * Description: Type of the result, must be audio
* Field: id
  * Type: String
  * Description: Unique identifier for this result, 1-64 bytes
* Field: audio_file_id
  * Type: String
  * Description: A valid file identifier for the audio file
* Field: caption
  * Type: String
  * Description: Optional. Caption, 0-1024 characters after entities parsing
* Field: parse_mode
  * Type: String
  * Description: Optional. Mode for parsing entities in the audio caption. See formatting options for more details.
* Field: caption_entities
  * Type: Array of MessageEntity
  * Description: Optional. List of special entities that appear in the caption, which can be specified instead of parse_mode
* Field: reply_markup
  * Type: InlineKeyboardMarkup
  * Description: Optional. Inline keyboard attached to the message
* Field: input_message_content
  * Type: InputMessageContent
  * Description: Optional. Content of the message to be sent instead of the audio


#### [](#inputmessagecontent)InputMessageContent

This object represents the content of a message to be sent as a result of an inline query. Telegram clients currently support the following 5 types:

*   [InputTextMessageContent](#inputtextmessagecontent)
*   [InputLocationMessageContent](#inputlocationmessagecontent)
*   [InputVenueMessageContent](#inputvenuemessagecontent)
*   [InputContactMessageContent](#inputcontactmessagecontent)
*   [InputInvoiceMessageContent](#inputinvoicemessagecontent)

#### [](#inputtextmessagecontent)InputTextMessageContent

Represents the [content](#inputmessagecontent) of a text message to be sent as the result of an inline query.



* Field: message_text
  * Type: String
  * Description: Text of the message to be sent, 1-4096 characters
* Field: parse_mode
  * Type: String
  * Description: Optional. Mode for parsing entities in the message text. See formatting options for more details.
* Field: entities
  * Type: Array of MessageEntity
  * Description: Optional. List of special entities that appear in message text, which can be specified instead of parse_mode
* Field: link_preview_options
  * Type: LinkPreviewOptions
  * Description: Optional. Link preview generation options for the message


#### [](#inputlocationmessagecontent)InputLocationMessageContent

Represents the [content](#inputmessagecontent) of a location message to be sent as the result of an inline query.



* Field: latitude
  * Type: Float
  * Description: Latitude of the location in degrees
* Field: longitude
  * Type: Float
  * Description: Longitude of the location in degrees
* Field: horizontal_accuracy
  * Type: Float
  * Description: Optional. The radius of uncertainty for the location, measured in meters; 0-1500
* Field: live_period
  * Type: Integer
  * Description: Optional. Period in seconds during which the location can be updated, must be between 60 and 86400, or 0x7FFFFFFF for live locations that can be edited indefinitely
* Field: heading
  * Type: Integer
  * Description: Optional. For live locations, a direction in which the user is moving, in degrees. Must be between 1 and 360 if specified.
* Field: proximity_alert_radius
  * Type: Integer
  * Description: Optional. For live locations, a maximum distance for proximity alerts about approaching another chat member, in meters. Must be between 1 and 100000 if specified.


#### [](#inputvenuemessagecontent)InputVenueMessageContent

Represents the [content](#inputmessagecontent) of a venue message to be sent as the result of an inline query.



* Field: latitude
  * Type: Float
  * Description: Latitude of the venue in degrees
* Field: longitude
  * Type: Float
  * Description: Longitude of the venue in degrees
* Field: title
  * Type: String
  * Description: Name of the venue
* Field: address
  * Type: String
  * Description: Address of the venue
* Field: foursquare_id
  * Type: String
  * Description: Optional. Foursquare identifier of the venue, if known
* Field: foursquare_type
  * Type: String
  * Description: Optional. Foursquare type of the venue, if known. (For example, “arts_entertainment/default”, “arts_entertainment/aquarium” or “food/icecream”.)
* Field: google_place_id
  * Type: String
  * Description: Optional. Google Places identifier of the venue
* Field: google_place_type
  * Type: String
  * Description: Optional. Google Places type of the venue. (See supported types.)


#### [](#inputcontactmessagecontent)InputContactMessageContent

Represents the [content](#inputmessagecontent) of a contact message to be sent as the result of an inline query.



* Field: phone_number
  * Type: String
  * Description: Contact's phone number
* Field: first_name
  * Type: String
  * Description: Contact's first name
* Field: last_name
  * Type: String
  * Description: Optional. Contact's last name
* Field: vcard
  * Type: String
  * Description: Optional. Additional data about the contact in the form of a vCard, 0-2048 bytes


#### [](#inputinvoicemessagecontent)InputInvoiceMessageContent

Represents the [content](#inputmessagecontent) of an invoice message to be sent as the result of an inline query.



* Field: title
  * Type: String
  * Description: Product name, 1-32 characters
* Field: description
  * Type: String
  * Description: Product description, 1-255 characters
* Field: payload
  * Type: String
  * Description: Bot-defined invoice payload, 1-128 bytes. This will not be displayed to the user, use it for your internal processes.
* Field: provider_token
  * Type: String
  * Description: Optional. Payment provider token, obtained via @BotFather. Pass an empty string for payments in Telegram Stars.
* Field: currency
  * Type: String
  * Description: Three-letter ISO 4217 currency code, see more on currencies. Pass “XTR” for payments in Telegram Stars.
* Field: prices
  * Type: Array of LabeledPrice
  * Description: Price breakdown, a JSON-serialized list of components (e.g. product price, tax, discount, delivery cost, delivery tax, bonus, etc.). Must contain exactly one item for payments in Telegram Stars.
* Field: max_tip_amount
  * Type: Integer
  * Description: Optional. The maximum accepted amount for tips in the smallest units of the currency (integer, not float/double). For example, for a maximum tip of US$ 1.45 pass max_tip_amount = 145. See the exp parameter in currencies.json, it shows the number of digits past the decimal point for each currency (2 for the majority of currencies). Defaults to 0. Not supported for payments in Telegram Stars.
* Field: suggested_tip_amounts
  * Type: Array of Integer
  * Description: Optional. A JSON-serialized array of suggested amounts of tip in the smallest units of the currency (integer, not float/double). At most 4 suggested tip amounts can be specified. The suggested tip amounts must be positive, passed in a strictly increased order and must not exceed max_tip_amount.
* Field: provider_data
  * Type: String
  * Description: Optional. A JSON-serialized object for data about the invoice, which will be shared with the payment provider. A detailed description of the required fields should be provided by the payment provider.
* Field: photo_url
  * Type: String
  * Description: Optional. URL of the product photo for the invoice. Can be a photo of the goods or a marketing image for a service.
* Field: photo_size
  * Type: Integer
  * Description: Optional. Photo size in bytes
* Field: photo_width
  * Type: Integer
  * Description: Optional. Photo width
* Field: photo_height
  * Type: Integer
  * Description: Optional. Photo height
* Field: need_name
  * Type: Boolean
  * Description: Optional. Pass True if you require the user's full name to complete the order. Ignored for payments in Telegram Stars.
* Field: need_phone_number
  * Type: Boolean
  * Description: Optional. Pass True if you require the user's phone number to complete the order. Ignored for payments in Telegram Stars.
* Field: need_email
  * Type: Boolean
  * Description: Optional. Pass True if you require the user's email address to complete the order. Ignored for payments in Telegram Stars.
* Field: need_shipping_address
  * Type: Boolean
  * Description: Optional. Pass True if you require the user's shipping address to complete the order. Ignored for payments in Telegram Stars.
* Field: send_phone_number_to_provider
  * Type: Boolean
  * Description: Optional. Pass True if the user's phone number should be sent to the provider. Ignored for payments in Telegram Stars.
* Field: send_email_to_provider
  * Type: Boolean
  * Description: Optional. Pass True if the user's email address should be sent to the provider. Ignored for payments in Telegram Stars.
* Field: is_flexible
  * Type: Boolean
  * Description: Optional. Pass True if the final price depends on the shipping method. Ignored for payments in Telegram Stars.


#### [](#choseninlineresult)ChosenInlineResult

Represents a [result](#inlinequeryresult) of an inline query that was chosen by the user and sent to their chat partner.



* Field: result_id
  * Type: String
  * Description: The unique identifier for the result that was chosen
* Field: from
  * Type: User
  * Description: The user that chose the result
* Field: location
  * Type: Location
  * Description: Optional. Sender location, only for bots that require user location
* Field: inline_message_id
  * Type: String
  * Description: Optional. Identifier of the sent inline message. Available only if there is an inline keyboard attached to the message. Will be also received in callback queries and can be used to edit the message.
* Field: query
  * Type: String
  * Description: The query that was used to obtain the result


**Note:** It is necessary to enable [inline feedback](about:/bots/inline#collecting-feedback) via [@BotFather](https://t.me/botfather) in order to receive these objects in updates.

### [](#payments)Payments

Your bot can accept payments from Telegram users. Please see the [introduction to payments](https://core.telegram.org/bots/payments) for more details on the process and how to set up payments for your bot.

#### [](#sendinvoice)sendInvoice

Use this method to send invoices. On success, the sent [Message](#message) is returned.



* Parameter: chat_id
  * Type: Integer or String
  * Required: Yes
  * Description: Unique identifier for the target chat or username of the target bot, supergroup or channel in the format @username
* Parameter: message_thread_id
  * Type: Integer
  * Required: Optional
  * Description: Unique identifier for the target message thread (topic) of a forum; for forum supergroups and private chats of bots with forum topic mode enabled only
* Parameter: direct_messages_topic_id
  * Type: Integer
  * Required: Optional
  * Description: Identifier of the direct messages topic to which the message will be sent; required if the message is sent to a direct messages chat
* Parameter: title
  * Type: String
  * Required: Yes
  * Description: Product name, 1-32 characters
* Parameter: description
  * Type: String
  * Required: Yes
  * Description: Product description, 1-255 characters
* Parameter: payload
  * Type: String
  * Required: Yes
  * Description: Bot-defined invoice payload, 1-128 bytes. This will not be displayed to the user, use it for your internal processes.
* Parameter: provider_token
  * Type: String
  * Required: Optional
  * Description: Payment provider token, obtained via @BotFather. Pass an empty string for payments in Telegram Stars.
* Parameter: currency
  * Type: String
  * Required: Yes
  * Description: Three-letter ISO 4217 currency code, see more on currencies. Pass “XTR” for payments in Telegram Stars.
* Parameter: prices
  * Type: Array of LabeledPrice
  * Required: Yes
  * Description: Price breakdown, a JSON-serialized list of components (e.g. product price, tax, discount, delivery cost, delivery tax, bonus, etc.). Must contain exactly one item for payments in Telegram Stars.
* Parameter: max_tip_amount
  * Type: Integer
  * Required: Optional
  * Description: The maximum accepted amount for tips in the smallest units of the currency (integer, not float/double). For example, for a maximum tip of US$ 1.45 pass max_tip_amount = 145. See the exp parameter in currencies.json, it shows the number of digits past the decimal point for each currency (2 for the majority of currencies). Defaults to 0. Not supported for payments in Telegram Stars.
* Parameter: suggested_tip_amounts
  * Type: Array of Integer
  * Required: Optional
  * Description: A JSON-serialized array of suggested amounts of tips in the smallest units of the currency (integer, not float/double). At most 4 suggested tip amounts can be specified. The suggested tip amounts must be positive, passed in a strictly increased order and must not exceed max_tip_amount.
* Parameter: start_parameter
  * Type: String
  * Required: Optional
  * Description: Unique deep-linking parameter. If left empty, forwarded copies of the sent message will have a Pay button, allowing multiple users to pay directly from the forwarded message, using the same invoice. If non-empty, forwarded copies of the sent message will have a URL button with a deep link to the bot (instead of a Pay button), with the value used as the start parameter.
* Parameter: provider_data
  * Type: String
  * Required: Optional
  * Description: JSON-serialized data about the invoice, which will be shared with the payment provider. A detailed description of required fields should be provided by the payment provider.
* Parameter: photo_url
  * Type: String
  * Required: Optional
  * Description: URL of the product photo for the invoice. Can be a photo of the goods or a marketing image for a service. People like it better when they see what they are paying for.
* Parameter: photo_size
  * Type: Integer
  * Required: Optional
  * Description: Photo size in bytes
* Parameter: photo_width
  * Type: Integer
  * Required: Optional
  * Description: Photo width
* Parameter: photo_height
  * Type: Integer
  * Required: Optional
  * Description: Photo height
* Parameter: need_name
  * Type: Boolean
  * Required: Optional
  * Description: Pass True if you require the user's full name to complete the order. Ignored for payments in Telegram Stars.
* Parameter: need_phone_number
  * Type: Boolean
  * Required: Optional
  * Description: Pass True if you require the user's phone number to complete the order. Ignored for payments in Telegram Stars.
* Parameter: need_email
  * Type: Boolean
  * Required: Optional
  * Description: Pass True if you require the user's email address to complete the order. Ignored for payments in Telegram Stars.
* Parameter: need_shipping_address
  * Type: Boolean
  * Required: Optional
  * Description: Pass True if you require the user's shipping address to complete the order. Ignored for payments in Telegram Stars.
* Parameter: send_phone_number_to_provider
  * Type: Boolean
  * Required: Optional
  * Description: Pass True if the user's phone number should be sent to the provider. Ignored for payments in Telegram Stars.
* Parameter: send_email_to_provider
  * Type: Boolean
  * Required: Optional
  * Description: Pass True if the user's email address should be sent to the provider. Ignored for payments in Telegram Stars.
* Parameter: is_flexible
  * Type: Boolean
  * Required: Optional
  * Description: Pass True if the final price depends on the shipping method. Ignored for payments in Telegram Stars.
* Parameter: disable_notification
  * Type: Boolean
  * Required: Optional
  * Description: Sends the message silently. Users will receive a notification with no sound.
* Parameter: protect_content
  * Type: Boolean
  * Required: Optional
  * Description: Protects the contents of the sent message from forwarding and saving
* Parameter: allow_paid_broadcast
  * Type: Boolean
  * Required: Optional
  * Description: Pass True to allow up to 1000 messages per second, ignoring broadcasting limits for a fee of 0.1 Telegram Stars per message. The relevant Stars will be withdrawn from the bot's balance.
* Parameter: message_effect_id
  * Type: String
  * Required: Optional
  * Description: Unique identifier of the message effect to be added to the message; for private chats only
* Parameter: suggested_post_parameters
  * Type: SuggestedPostParameters
  * Required: Optional
  * Description: A JSON-serialized object containing the parameters of the suggested post to send; for direct messages chats only. If the message is sent as a reply to another suggested post, then that suggested post is automatically declined.
* Parameter: reply_parameters
  * Type: ReplyParameters
  * Required: Optional
  * Description: Description of the message to reply to
* Parameter: reply_markup
  * Type: InlineKeyboardMarkup
  * Required: Optional
  * Description: A JSON-serialized object for an inline keyboard. If empty, one 'Pay total price' button will be shown. If not empty, the first button must be a Pay button.


#### [](#createinvoicelink)createInvoiceLink

Use this method to create a link for an invoice. Returns the created invoice link as _String_ on success.



* Parameter: business_connection_id
  * Type: String
  * Required: Optional
  * Description: Unique identifier of the business connection on behalf of which the link will be created. For payments in Telegram Stars only.
* Parameter: title
  * Type: String
  * Required: Yes
  * Description: Product name, 1-32 characters
* Parameter: description
  * Type: String
  * Required: Yes
  * Description: Product description, 1-255 characters
* Parameter: payload
  * Type: String
  * Required: Yes
  * Description: Bot-defined invoice payload, 1-128 bytes. This will not be displayed to the user, use it for your internal processes.
* Parameter: provider_token
  * Type: String
  * Required: Optional
  * Description: Payment provider token, obtained via @BotFather. Pass an empty string for payments in Telegram Stars.
* Parameter: currency
  * Type: String
  * Required: Yes
  * Description: Three-letter ISO 4217 currency code, see more on currencies. Pass “XTR” for payments in Telegram Stars.
* Parameter: prices
  * Type: Array of LabeledPrice
  * Required: Yes
  * Description: Price breakdown, a JSON-serialized list of components (e.g. product price, tax, discount, delivery cost, delivery tax, bonus, etc.). Must contain exactly one item for payments in Telegram Stars.
* Parameter: subscription_period
  * Type: Integer
  * Required: Optional
  * Description: The number of seconds the subscription will be active for before the next payment. The currency must be set to “XTR” (Telegram Stars) if the parameter is used. Currently, it must always be 2592000 (30 days) if specified. Any number of subscriptions can be active for a given bot at the same time, including multiple concurrent subscriptions from the same user. Subscription price must no exceed 10000 Telegram Stars.
* Parameter: max_tip_amount
  * Type: Integer
  * Required: Optional
  * Description: The maximum accepted amount for tips in the smallest units of the currency (integer, not float/double). For example, for a maximum tip of US$ 1.45 pass max_tip_amount = 145. See the exp parameter in currencies.json, it shows the number of digits past the decimal point for each currency (2 for the majority of currencies). Defaults to 0. Not supported for payments in Telegram Stars.
* Parameter: suggested_tip_amounts
  * Type: Array of Integer
  * Required: Optional
  * Description: A JSON-serialized array of suggested amounts of tips in the smallest units of the currency (integer, not float/double). At most 4 suggested tip amounts can be specified. The suggested tip amounts must be positive, passed in a strictly increased order and must not exceed max_tip_amount.
* Parameter: provider_data
  * Type: String
  * Required: Optional
  * Description: JSON-serialized data about the invoice, which will be shared with the payment provider. A detailed description of required fields should be provided by the payment provider.
* Parameter: photo_url
  * Type: String
  * Required: Optional
  * Description: URL of the product photo for the invoice. Can be a photo of the goods or a marketing image for a service.
* Parameter: photo_size
  * Type: Integer
  * Required: Optional
  * Description: Photo size in bytes
* Parameter: photo_width
  * Type: Integer
  * Required: Optional
  * Description: Photo width
* Parameter: photo_height
  * Type: Integer
  * Required: Optional
  * Description: Photo height
* Parameter: need_name
  * Type: Boolean
  * Required: Optional
  * Description: Pass True if you require the user's full name to complete the order. Ignored for payments in Telegram Stars.
* Parameter: need_phone_number
  * Type: Boolean
  * Required: Optional
  * Description: Pass True if you require the user's phone number to complete the order. Ignored for payments in Telegram Stars.
* Parameter: need_email
  * Type: Boolean
  * Required: Optional
  * Description: Pass True if you require the user's email address to complete the order. Ignored for payments in Telegram Stars.
* Parameter: need_shipping_address
  * Type: Boolean
  * Required: Optional
  * Description: Pass True if you require the user's shipping address to complete the order. Ignored for payments in Telegram Stars.
* Parameter: send_phone_number_to_provider
  * Type: Boolean
  * Required: Optional
  * Description: Pass True if the user's phone number should be sent to the provider. Ignored for payments in Telegram Stars.
* Parameter: send_email_to_provider
  * Type: Boolean
  * Required: Optional
  * Description: Pass True if the user's email address should be sent to the provider. Ignored for payments in Telegram Stars.
* Parameter: is_flexible
  * Type: Boolean
  * Required: Optional
  * Description: Pass True if the final price depends on the shipping method. Ignored for payments in Telegram Stars.


#### [](#answershippingquery)answerShippingQuery

If you sent an invoice requesting a shipping address and the parameter _is\_flexible_ was specified, the Bot API will send an [Update](#update) with a _shipping\_query_ field to the bot. Use this method to reply to shipping queries. On success, _True_ is returned.



* Parameter: shipping_query_id
  * Type: String
  * Required: Yes
  * Description: Unique identifier for the query to be answered
* Parameter: ok
  * Type: Boolean
  * Required: Yes
  * Description: Pass True if delivery to the specified address is possible and False if there are any problems (for example, if delivery to the specified address is not possible)
* Parameter: shipping_options
  * Type: Array of ShippingOption
  * Required: Optional
  * Description: Required if ok is True. A JSON-serialized array of available shipping options.
* Parameter: error_message
  * Type: String
  * Required: Optional
  * Description: Required if ok is False. Error message in human readable form that explains why it is impossible to complete the order (e.g. “Sorry, delivery to your desired address is unavailable”). Telegram will display this message to the user.


#### [](#answerprecheckoutquery)answerPreCheckoutQuery

Once the user has confirmed their payment and shipping details, the Bot API sends the final confirmation in the form of an [Update](#update) with the field _pre\_checkout\_query_. Use this method to respond to such pre-checkout queries. On success, _True_ is returned. **Note:** The Bot API must receive an answer within 10 seconds after the pre-checkout query was sent.



* Parameter: pre_checkout_query_id
  * Type: String
  * Required: Yes
  * Description: Unique identifier for the query to be answered
* Parameter: ok
  * Type: Boolean
  * Required: Yes
  * Description: Specify True if everything is alright (goods are available, etc.) and the bot is ready to proceed with the order. Use False if there are any problems.
* Parameter: error_message
  * Type: String
  * Required: Optional
  * Description: Required if ok is False. Error message in human readable form that explains the reason for failure to proceed with the checkout (e.g. "Sorry, somebody just bought the last of our amazing black T-shirts while you were busy filling out your payment details. Please choose a different color or garment!"). Telegram will display this message to the user.


#### [](#getmystarbalance)getMyStarBalance

A method to get the current Telegram Stars balance of the bot. Requires no parameters. On success, returns a [StarAmount](#staramount) object.

#### [](#getstartransactions)getStarTransactions

Returns the bot's Telegram Star transactions in chronological order. On success, returns a [StarTransactions](#startransactions) object.



* Parameter: offset
  * Type: Integer
  * Required: Optional
  * Description: Number of transactions to skip in the response
* Parameter: limit
  * Type: Integer
  * Required: Optional
  * Description: The maximum number of transactions to be retrieved. Values between 1-100 are accepted. Defaults to 100.


#### [](#refundstarpayment)refundStarPayment

Refunds a successful payment in [Telegram Stars](https://t.me/BotNews/90). Returns _True_ on success.


|Parameter                 |Type   |Required|Description                                          |
|--------------------------|-------|--------|-----------------------------------------------------|
|user_id                   |Integer|Yes     |Identifier of the user whose payment will be refunded|
|telegram_payment_charge_id|String |Yes     |Telegram payment identifier                          |


#### [](#edituserstarsubscription)editUserStarSubscription

Allows the bot to cancel or re-enable extension of a subscription paid in Telegram Stars. Returns _True_ on success.



* Parameter: user_id
  * Type: Integer
  * Required: Yes
  * Description: Identifier of the user whose subscription will be edited
* Parameter: telegram_payment_charge_id
  * Type: String
  * Required: Yes
  * Description: Telegram payment identifier for the subscription
* Parameter: is_canceled
  * Type: Boolean
  * Required: Yes
  * Description: Pass True to cancel extension of the user subscription; the subscription must be active up to the end of the current subscription period. Pass False to allow the user to re-enable a subscription that was previously canceled by the bot.


#### [](#labeledprice)LabeledPrice

This object represents a portion of the price for goods or services.



* Field: label
  * Type: String
  * Description: Portion label
* Field: amount
  * Type: Integer
  * Description: Price of the product in the smallest units of the currency (integer, not float/double). For example, for a price of US$ 1.45 pass amount = 145. See the exp parameter in currencies.json, it shows the number of digits past the decimal point for each currency (2 for the majority of currencies).


#### [](#invoice)Invoice

This object contains basic information about an invoice.



* Field: title
  * Type: String
  * Description: Product name
* Field: description
  * Type: String
  * Description: Product description
* Field: start_parameter
  * Type: String
  * Description: Unique bot deep-linking parameter that can be used to generate this invoice
* Field: currency
  * Type: String
  * Description: Three-letter ISO 4217 currency code, or “XTR” for payments in Telegram Stars
* Field: total_amount
  * Type: Integer
  * Description: Total price in the smallest units of the currency (integer, not float/double). For example, for a price of US$ 1.45 pass amount = 145. See the exp parameter in currencies.json, it shows the number of digits past the decimal point for each currency (2 for the majority of currencies).


#### [](#shippingaddress)ShippingAddress

This object represents a shipping address.


|Field       |Type  |Description                               |
|------------|------|------------------------------------------|
|country_code|String|Two-letter ISO 3166-1 alpha-2 country code|
|state       |String|State, if applicable                      |
|city        |String|City                                      |
|street_line1|String|First line for the address                |
|street_line2|String|Second line for the address               |
|post_code   |String|Address post code                         |


#### [](#orderinfo)OrderInfo

This object represents information about an order.


|Field           |Type           |Description                    |
|----------------|---------------|-------------------------------|
|name            |String         |Optional. User name            |
|phone_number    |String         |Optional. User's phone number  |
|email           |String         |Optional. User email           |
|shipping_address|ShippingAddress|Optional. User shipping address|


#### [](#shippingoption)ShippingOption

This object represents one shipping option.


|Field |Type                 |Description               |
|------|---------------------|--------------------------|
|id    |String               |Shipping option identifier|
|title |String               |Option title              |
|prices|Array of LabeledPrice|List of price portions    |


#### [](#successfulpayment)SuccessfulPayment

This object contains basic information about a successful payment. Note that if the buyer initiates a chargeback with the relevant payment provider following this transaction, the funds may be debited from your balance. This is outside of Telegram's control.



* Field: currency
  * Type: String
  * Description: Three-letter ISO 4217 currency code, or “XTR” for payments in Telegram Stars
* Field: total_amount
  * Type: Integer
  * Description: Total price in the smallest units of the currency (integer, not float/double). For example, for a price of US$ 1.45 pass amount = 145. See the exp parameter in currencies.json, it shows the number of digits past the decimal point for each currency (2 for the majority of currencies).
* Field: invoice_payload
  * Type: String
  * Description: Bot-specified invoice payload
* Field: subscription_expiration_date
  * Type: Integer
  * Description: Optional. Expiration date of the subscription, in Unix time; for recurring payments only
* Field: is_recurring
  * Type: True
  * Description: Optional. True, if the payment is a recurring payment for a subscription
* Field: is_first_recurring
  * Type: True
  * Description: Optional. True, if the payment is the first payment for a subscription
* Field: shipping_option_id
  * Type: String
  * Description: Optional. Identifier of the shipping option chosen by the user
* Field: order_info
  * Type: OrderInfo
  * Description: Optional. Order information provided by the user
* Field: telegram_payment_charge_id
  * Type: String
  * Description: Telegram payment identifier
* Field: provider_payment_charge_id
  * Type: String
  * Description: Provider payment identifier


#### [](#refundedpayment)RefundedPayment

This object contains basic information about a refunded payment.



* Field: currency
  * Type: String
  * Description: Three-letter ISO 4217 currency code, or “XTR” for payments in Telegram Stars. Currently, always “XTR”.
* Field: total_amount
  * Type: Integer
  * Description: Total refunded price in the smallest units of the currency (integer, not float/double). For example, for a price of US$ 1.45, total_amount = 145. See the exp parameter in currencies.json, it shows the number of digits past the decimal point for each currency (2 for the majority of currencies).
* Field: invoice_payload
  * Type: String
  * Description: Bot-specified invoice payload
* Field: telegram_payment_charge_id
  * Type: String
  * Description: Telegram payment identifier
* Field: provider_payment_charge_id
  * Type: String
  * Description: Optional. Provider payment identifier


#### [](#shippingquery)ShippingQuery

This object contains information about an incoming shipping query.


|Field           |Type           |Description                    |
|----------------|---------------|-------------------------------|
|id              |String         |Unique query identifier        |
|from            |User           |User who sent the query        |
|invoice_payload |String         |Bot-specified invoice payload  |
|shipping_address|ShippingAddress|User specified shipping address|


#### [](#precheckoutquery)PreCheckoutQuery

This object contains information about an incoming pre-checkout query.



* Field: id
  * Type: String
  * Description: Unique query identifier
* Field: from
  * Type: User
  * Description: User who sent the query
* Field: currency
  * Type: String
  * Description: Three-letter ISO 4217 currency code, or “XTR” for payments in Telegram Stars
* Field: total_amount
  * Type: Integer
  * Description: Total price in the smallest units of the currency (integer, not float/double). For example, for a price of US$ 1.45 pass amount = 145. See the exp parameter in currencies.json, it shows the number of digits past the decimal point for each currency (2 for the majority of currencies).
* Field: invoice_payload
  * Type: String
  * Description: Bot-specified invoice payload
* Field: shipping_option_id
  * Type: String
  * Description: Optional. Identifier of the shipping option chosen by the user
* Field: order_info
  * Type: OrderInfo
  * Description: Optional. Order information provided by the user


#### [](#paidmediapurchased)PaidMediaPurchased

This object contains information about a paid media purchase.


|Field             |Type  |Description                     |
|------------------|------|--------------------------------|
|from              |User  |User who purchased the media    |
|paid_media_payload|String|Bot-specified paid media payload|


#### [](#revenuewithdrawalstate)RevenueWithdrawalState

This object describes the state of a revenue withdrawal operation. Currently, it can be one of

*   [RevenueWithdrawalStatePending](#revenuewithdrawalstatepending)
*   [RevenueWithdrawalStateSucceeded](#revenuewithdrawalstatesucceeded)
*   [RevenueWithdrawalStateFailed](#revenuewithdrawalstatefailed)

#### [](#revenuewithdrawalstatepending)RevenueWithdrawalStatePending

The withdrawal is in progress.


|Field|Type  |Description                        |
|-----|------|-----------------------------------|
|type |String|Type of the state, always “pending”|


#### [](#revenuewithdrawalstatesucceeded)RevenueWithdrawalStateSucceeded

The withdrawal succeeded.


|Field|Type   |Description                                             |
|-----|-------|--------------------------------------------------------|
|type |String |Type of the state, always “succeeded”                   |
|date |Integer|Date the withdrawal was completed in Unix time          |
|url  |String |An HTTPS URL that can be used to see transaction details|


#### [](#revenuewithdrawalstatefailed)RevenueWithdrawalStateFailed

The withdrawal failed and the transaction was refunded.


|Field|Type  |Description                       |
|-----|------|----------------------------------|
|type |String|Type of the state, always “failed”|


#### [](#affiliateinfo)AffiliateInfo

Contains information about the affiliate that received a commission via this transaction.



* Field: affiliate_user
  * Type: User
  * Description: Optional. The bot or the user that received an affiliate commission if it was received by a bot or a user
* Field: affiliate_chat
  * Type: Chat
  * Description: Optional. The chat that received an affiliate commission if it was received by a chat
* Field: commission_per_mille
  * Type: Integer
  * Description: The number of Telegram Stars received by the affiliate for each 1000 Telegram Stars received by the bot from referred users
* Field: amount
  * Type: Integer
  * Description: Integer amount of Telegram Stars received by the affiliate from the transaction, rounded to 0; can be negative for refunds
* Field: nanostar_amount
  * Type: Integer
  * Description: Optional. The number of 1/1000000000 shares of Telegram Stars received by the affiliate; from -999999999 to 999999999; can be negative for refunds


#### [](#transactionpartner)TransactionPartner

This object describes the source of a transaction, or its recipient for outgoing transactions. Currently, it can be one of

*   [TransactionPartnerUser](#transactionpartneruser)
*   [TransactionPartnerChat](#transactionpartnerchat)
*   [TransactionPartnerAffiliateProgram](#transactionpartneraffiliateprogram)
*   [TransactionPartnerFragment](#transactionpartnerfragment)
*   [TransactionPartnerTelegramAds](#transactionpartnertelegramads)
*   [TransactionPartnerTelegramApi](#transactionpartnertelegramapi)
*   [TransactionPartnerOther](#transactionpartnerother)

#### [](#transactionpartneruser)TransactionPartnerUser

Describes a transaction with a user.



* Field: type
  * Type: String
  * Description: Type of the transaction partner, always “user”
* Field: transaction_type
  * Type: String
  * Description: Type of the transaction, currently one of “invoice_payment” for payments via invoices, “paid_media_payment” for payments for paid media, “gift_purchase” for gifts sent by the bot, “premium_purchase” for Telegram Premium subscriptions gifted by the bot, “business_account_transfer” for direct transfers from managed business accounts
* Field: user
  * Type: User
  * Description: Information about the user
* Field: affiliate
  * Type: AffiliateInfo
  * Description: Optional. Information about the affiliate that received a commission via this transaction. Can be available only for “invoice_payment” and “paid_media_payment” transactions.
* Field: invoice_payload
  * Type: String
  * Description: Optional. Bot-specified invoice payload. Can be available only for “invoice_payment” transactions.
* Field: subscription_period
  * Type: Integer
  * Description: Optional. The duration of the paid subscription. Can be available only for “invoice_payment” transactions.
* Field: paid_media
  * Type: Array of PaidMedia
  * Description: Optional. Information about the paid media bought by the user; for “paid_media_payment” transactions only
* Field: paid_media_payload
  * Type: String
  * Description: Optional. Bot-specified paid media payload. Can be available only for “paid_media_payment” transactions.
* Field: gift
  * Type: Gift
  * Description: Optional. The gift sent to the user by the bot; for “gift_purchase” transactions only
* Field: premium_subscription_duration
  * Type: Integer
  * Description: Optional. Number of months the gifted Telegram Premium subscription will be active for; for “premium_purchase” transactions only


#### [](#transactionpartnerchat)TransactionPartnerChat

Describes a transaction with a chat.


|Field|Type  |Description                                   |
|-----|------|----------------------------------------------|
|type |String|Type of the transaction partner, always “chat”|
|chat |Chat  |Information about the chat                    |
|gift |Gift  |Optional. The gift sent to the chat by the bot|


#### [](#transactionpartneraffiliateprogram)TransactionPartnerAffiliateProgram

Describes the affiliate program that issued the affiliate commission received via this transaction.



* Field: type
  * Type: String
  * Description: Type of the transaction partner, always “affiliate_program”
* Field: sponsor_user
  * Type: User
  * Description: Optional. Information about the bot that sponsored the affiliate program
* Field: commission_per_mille
  * Type: Integer
  * Description: The number of Telegram Stars received by the bot for each 1000 Telegram Stars received by the affiliate program sponsor from referred users


#### [](#transactionpartnerfragment)TransactionPartnerFragment

Describes a withdrawal transaction with Fragment.



* Field: type
  * Type: String
  * Description: Type of the transaction partner, always “fragment”
* Field: withdrawal_state
  * Type: RevenueWithdrawalState
  * Description: Optional. State of the transaction if the transaction is outgoing


#### [](#transactionpartnertelegramads)TransactionPartnerTelegramAds

Describes a withdrawal transaction to the Telegram Ads platform.


|Field|Type  |Description                                           |
|-----|------|------------------------------------------------------|
|type |String|Type of the transaction partner, always “telegram_ads”|


#### [](#transactionpartnertelegramapi)TransactionPartnerTelegramApi

Describes a transaction with payment for [paid broadcasting](#paid-broadcasts).



* Field: type
  * Type: String
  * Description: Type of the transaction partner, always “telegram_api”
* Field: request_count
  * Type: Integer
  * Description: The number of successful requests that exceeded regular limits and were therefore billed


#### [](#transactionpartnerother)TransactionPartnerOther

Describes a transaction with an unknown source or recipient.


|Field|Type  |Description                                    |
|-----|------|-----------------------------------------------|
|type |String|Type of the transaction partner, always “other”|


#### [](#startransaction)StarTransaction

Describes a Telegram Star transaction. Note that if the buyer initiates a chargeback with the payment provider from whom they acquired Stars (e.g., Apple, Google) following this transaction, the refunded Stars will be deducted from the bot's balance. This is outside of Telegram's control.



* Field: id
  * Type: String
  * Description: Unique identifier of the transaction. Coincides with the identifier of the original transaction for refund transactions. Coincides with SuccessfulPayment.telegram_payment_charge_id for successful incoming payments from users.
* Field: amount
  * Type: Integer
  * Description: Integer amount of Telegram Stars transferred by the transaction
* Field: nanostar_amount
  * Type: Integer
  * Description: Optional. The number of 1/1000000000 shares of Telegram Stars transferred by the transaction; from 0 to 999999999
* Field: date
  * Type: Integer
  * Description: Date the transaction was created in Unix time
* Field: source
  * Type: TransactionPartner
  * Description: Optional. Source of an incoming transaction (e.g., a user purchasing goods or services, Fragment refunding a failed withdrawal). Only for incoming transactions.
* Field: receiver
  * Type: TransactionPartner
  * Description: Optional. Receiver of an outgoing transaction (e.g., a user for a purchase refund, Fragment for a withdrawal). Only for outgoing transactions.


#### [](#startransactions)StarTransactions

Contains a list of Telegram Star transactions.


|Field       |Type                    |Description             |
|------------|------------------------|------------------------|
|transactions|Array of StarTransaction|The list of transactions|


### [](#telegram-passport)Telegram Passport

**Telegram Passport** is a unified authorization method for services that require personal identification. Users can upload their documents once, then instantly share their data with services that require real-world ID (finance, ICOs, etc.). Please see the [manual](https://core.telegram.org/passport) for details.

#### [](#passportdata)PassportData

Describes Telegram Passport data shared with the bot by the user.



* Field: data
  * Type: Array of EncryptedPassportElement
  * Description: Array with information about documents and other Telegram Passport elements that was shared with the bot
* Field: credentials
  * Type: EncryptedCredentials
  * Description: Encrypted credentials required to decrypt the data


#### [](#passportfile)PassportFile

This object represents a file uploaded to Telegram Passport. Currently all Telegram Passport files are in JPEG format when decrypted and don't exceed 10MB.



* Field: file_id
  * Type: String
  * Description: Identifier for this file, which can be used to download or reuse the file
* Field: file_unique_id
  * Type: String
  * Description: Unique identifier for this file, which is supposed to be the same over time and for different bots. Can't be used to download or reuse the file.
* Field: file_size
  * Type: Integer
  * Description: File size in bytes
* Field: file_date
  * Type: Integer
  * Description: Unix time when the file was uploaded


#### [](#encryptedpassportelement)EncryptedPassportElement

Describes documents or other Telegram Passport elements shared with the bot by the user.



* Field: type
  * Type: String
  * Description: Element type. One of “personal_details”, “passport”, “driver_license”, “identity_card”, “internal_passport”, “address”, “utility_bill”, “bank_statement”, “rental_agreement”, “passport_registration”, “temporary_registration”, “phone_number”, “email”.
* Field: data
  * Type: String
  * Description: Optional. Base64-encoded encrypted Telegram Passport element data provided by the user; available only for “personal_details”, “passport”, “driver_license”, “identity_card”, “internal_passport” and “address” types. Can be decrypted and verified using the accompanying EncryptedCredentials.
* Field: phone_number
  * Type: String
  * Description: Optional. User's verified phone number; available only for “phone_number” type
* Field: email
  * Type: String
  * Description: Optional. User's verified email address; available only for “email” type
* Field: files
  * Type: Array of PassportFile
  * Description: Optional. Array of encrypted files with documents provided by the user; available only for “utility_bill”, “bank_statement”, “rental_agreement”, “passport_registration” and “temporary_registration” types. Files can be decrypted and verified using the accompanying EncryptedCredentials.
* Field: front_side
  * Type: PassportFile
  * Description: Optional. Encrypted file with the front side of the document, provided by the user; available only for “passport”, “driver_license”, “identity_card” and “internal_passport”. The file can be decrypted and verified using the accompanying EncryptedCredentials.
* Field: reverse_side
  * Type: PassportFile
  * Description: Optional. Encrypted file with the reverse side of the document, provided by the user; available only for “driver_license” and “identity_card”. The file can be decrypted and verified using the accompanying EncryptedCredentials.
* Field: selfie
  * Type: PassportFile
  * Description: Optional. Encrypted file with the selfie of the user holding a document, provided by the user; available if requested for “passport”, “driver_license”, “identity_card” and “internal_passport”. The file can be decrypted and verified using the accompanying EncryptedCredentials.
* Field: translation
  * Type: Array of PassportFile
  * Description: Optional. Array of encrypted files with translated versions of documents provided by the user; available if requested for “passport”, “driver_license”, “identity_card”, “internal_passport”, “utility_bill”, “bank_statement”, “rental_agreement”, “passport_registration” and “temporary_registration” types. Files can be decrypted and verified using the accompanying EncryptedCredentials.
* Field: hash
  * Type: String
  * Description: Base64-encoded element hash for using in PassportElementErrorUnspecified


#### [](#encryptedcredentials)EncryptedCredentials

Describes data required for decrypting and authenticating [EncryptedPassportElement](#encryptedpassportelement). See the [Telegram Passport Documentation](about:/passport#receiving-information) for a complete description of the data decryption and authentication processes.



* Field: data
  * Type: String
  * Description: Base64-encoded encrypted JSON-serialized data with unique user's payload, data hashes and secrets required for EncryptedPassportElement decryption and authentication
* Field: hash
  * Type: String
  * Description: Base64-encoded data hash for data authentication
* Field: secret
  * Type: String
  * Description: Base64-encoded secret, encrypted with the bot's public RSA key, required for data decryption


#### [](#setpassportdataerrors)setPassportDataErrors

Informs a user that some of the Telegram Passport elements they provided contains errors. The user will not be able to re-submit their Passport to you until the errors are fixed (the contents of the field for which you returned the error must change). Returns _True_ on success.

Use this if the data submitted by the user doesn't satisfy the standards your service requires for any reason. For example, if a birthday date seems invalid, a submitted document is blurry, a scan shows evidence of tampering, etc. Supply some details in the error message to make sure the user knows how to correct the issues.


|Parameter|Type                         |Required|Description                                  |
|---------|-----------------------------|--------|---------------------------------------------|
|user_id  |Integer                      |Yes     |User identifier                              |
|errors   |Array of PassportElementError|Yes     |A JSON-serialized array describing the errors|


#### [](#passportelementerror)PassportElementError

This object represents an error in the Telegram Passport element which was submitted that should be resolved by the user. It should be one of:

*   [PassportElementErrorDataField](#passportelementerrordatafield)
*   [PassportElementErrorFrontSide](#passportelementerrorfrontside)
*   [PassportElementErrorReverseSide](#passportelementerrorreverseside)
*   [PassportElementErrorSelfie](#passportelementerrorselfie)
*   [PassportElementErrorFile](#passportelementerrorfile)
*   [PassportElementErrorFiles](#passportelementerrorfiles)
*   [PassportElementErrorTranslationFile](#passportelementerrortranslationfile)
*   [PassportElementErrorTranslationFiles](#passportelementerrortranslationfiles)
*   [PassportElementErrorUnspecified](#passportelementerrorunspecified)

#### [](#passportelementerrordatafield)PassportElementErrorDataField

Represents an issue in one of the data fields that was provided by the user. The error is considered resolved when the field's value changes.



* Field: source
  * Type: String
  * Description: Error source, must be data
* Field: type
  * Type: String
  * Description: The section of the user's Telegram Passport which has the error, one of “personal_details”, “passport”, “driver_license”, “identity_card”, “internal_passport”, “address”
* Field: field_name
  * Type: String
  * Description: Name of the data field which has the error
* Field: data_hash
  * Type: String
  * Description: Base64-encoded data hash
* Field: message
  * Type: String
  * Description: Error message


#### [](#passportelementerrorfrontside)PassportElementErrorFrontSide

Represents an issue with the front side of a document. The error is considered resolved when the file with the front side of the document changes.



* Field: source
  * Type: String
  * Description: Error source, must be front_side
* Field: type
  * Type: String
  * Description: The section of the user's Telegram Passport which has the issue, one of “passport”, “driver_license”, “identity_card”, “internal_passport”
* Field: file_hash
  * Type: String
  * Description: Base64-encoded hash of the file with the front side of the document
* Field: message
  * Type: String
  * Description: Error message


#### [](#passportelementerrorreverseside)PassportElementErrorReverseSide

Represents an issue with the reverse side of a document. The error is considered resolved when the file with reverse side of the document changes.



* Field: source
  * Type: String
  * Description: Error source, must be reverse_side
* Field: type
  * Type: String
  * Description: The section of the user's Telegram Passport which has the issue, one of “driver_license”, “identity_card”
* Field: file_hash
  * Type: String
  * Description: Base64-encoded hash of the file with the reverse side of the document
* Field: message
  * Type: String
  * Description: Error message


#### [](#passportelementerrorselfie)PassportElementErrorSelfie

Represents an issue with the selfie with a document. The error is considered resolved when the file with the selfie changes.



* Field: source
  * Type: String
  * Description: Error source, must be selfie
* Field: type
  * Type: String
  * Description: The section of the user's Telegram Passport which has the issue, one of “passport”, “driver_license”, “identity_card”, “internal_passport”
* Field: file_hash
  * Type: String
  * Description: Base64-encoded hash of the file with the selfie
* Field: message
  * Type: String
  * Description: Error message


#### [](#passportelementerrorfile)PassportElementErrorFile

Represents an issue with a document scan. The error is considered resolved when the file with the document scan changes.



* Field: source
  * Type: String
  * Description: Error source, must be file
* Field: type
  * Type: String
  * Description: The section of the user's Telegram Passport which has the issue, one of “utility_bill”, “bank_statement”, “rental_agreement”, “passport_registration”, “temporary_registration”
* Field: file_hash
  * Type: String
  * Description: Base64-encoded file hash
* Field: message
  * Type: String
  * Description: Error message


#### [](#passportelementerrorfiles)PassportElementErrorFiles

Represents an issue with a list of scans. The error is considered resolved when the list of files containing the scans changes.



* Field: source
  * Type: String
  * Description: Error source, must be files
* Field: type
  * Type: String
  * Description: The section of the user's Telegram Passport which has the issue, one of “utility_bill”, “bank_statement”, “rental_agreement”, “passport_registration”, “temporary_registration”
* Field: file_hashes
  * Type: Array of String
  * Description: List of base64-encoded file hashes
* Field: message
  * Type: String
  * Description: Error message


#### [](#passportelementerrortranslationfile)PassportElementErrorTranslationFile

Represents an issue with one of the files that constitute the translation of a document. The error is considered resolved when the file changes.



* Field: source
  * Type: String
  * Description: Error source, must be translation_file
* Field: type
  * Type: String
  * Description: Type of element of the user's Telegram Passport which has the issue, one of “passport”, “driver_license”, “identity_card”, “internal_passport”, “utility_bill”, “bank_statement”, “rental_agreement”, “passport_registration”, “temporary_registration”
* Field: file_hash
  * Type: String
  * Description: Base64-encoded file hash
* Field: message
  * Type: String
  * Description: Error message


#### [](#passportelementerrortranslationfiles)PassportElementErrorTranslationFiles

Represents an issue with the translated version of a document. The error is considered resolved when a file with the document translation change.



* Field: source
  * Type: String
  * Description: Error source, must be translation_files
* Field: type
  * Type: String
  * Description: Type of element of the user's Telegram Passport which has the issue, one of “passport”, “driver_license”, “identity_card”, “internal_passport”, “utility_bill”, “bank_statement”, “rental_agreement”, “passport_registration”, “temporary_registration”
* Field: file_hashes
  * Type: Array of String
  * Description: List of base64-encoded file hashes
* Field: message
  * Type: String
  * Description: Error message


#### [](#passportelementerrorunspecified)PassportElementErrorUnspecified

Represents an issue in an unspecified place. The error is considered resolved when new data is added.


|Field       |Type  |Description                                                        |
|------------|------|-------------------------------------------------------------------|
|source      |String|Error source, must be unspecified                                  |
|type        |String|Type of element of the user's Telegram Passport which has the issue|
|element_hash|String|Base64-encoded element hash                                        |
|message     |String|Error message                                                      |


### [](#games)Games

Your bot can offer users **HTML5 games** to play solo or to compete against each other in groups and one-on-one chats. Create games via [@BotFather](https://t.me/botfather) using the _/newgame_ command. Please note that this kind of power requires responsibility: you will need to accept the terms for each game that your bots will be offering.

*   Games are a new type of content on Telegram, represented by the [Game](#game) and [InlineQueryResultGame](#inlinequeryresultgame) objects.
*   Once you've created a game via [BotFather](https://t.me/botfather), you can send games to chats as regular messages using the [sendGame](#sendgame) method, or use [inline mode](#inline-mode) with [InlineQueryResultGame](#inlinequeryresultgame).
*   If you send the game message without any buttons, it will automatically have a 'Play _GameName_' button. When this button is pressed, your bot gets a [CallbackQuery](#callbackquery) with the _game\_short\_name_ of the requested game. You provide the correct URL for this particular user and the app opens the game in the in-app browser.
*   You can manually add multiple buttons to your game message. Please note that the first button in the first row **must always** launch the game, using the field _callback\_game_ in [InlineKeyboardButton](#inlinekeyboardbutton). You can add extra buttons according to taste: e.g., for a description of the rules, or to open the game's official community.
*   To make your game more attractive, you can upload a GIF animation that demonstrates the game to the users via [BotFather](https://t.me/botfather) (see [Lumberjack](https://t.me/gamebot?game=lumberjack) for example).
*   A game message will also display high scores for the current chat. Use [setGameScore](#setgamescore) to post high scores to the chat with the game, add the _disable\_edit\_message_ parameter to disable automatic update of the message with the current scoreboard.
*   Use [getGameHighScores](#getgamehighscores) to get data for in-game high score tables.
*   You can also add an extra [sharing button](about:/bots/games#sharing-your-game-to-telegram-chats) for users to share their best score to different chats.
*   For examples of what can be done using this new stuff, check the [@gamebot](https://t.me/gamebot) and [@gamee](https://t.me/gamee) bots.

#### [](#sendgame)sendGame

Use this method to send a game. On success, the sent [Message](#message) is returned.



* Parameter: business_connection_id
  * Type: String
  * Required: Optional
  * Description: Unique identifier of the business connection on behalf of which the message will be sent
* Parameter: chat_id
  * Type: Integer or String
  * Required: Yes
  * Description: Unique identifier for the target chat or username of the target bot in the format @username. Games can't be sent to channel direct messages chats and channel chats.
* Parameter: message_thread_id
  * Type: Integer
  * Required: Optional
  * Description: Unique identifier for the target message thread (topic) of a forum; for forum supergroups and private chats of bots with forum topic mode enabled only
* Parameter: game_short_name
  * Type: String
  * Required: Yes
  * Description: Short name of the game, serves as the unique identifier for the game. Set up your games via @BotFather.
* Parameter: disable_notification
  * Type: Boolean
  * Required: Optional
  * Description: Sends the message silently. Users will receive a notification with no sound.
* Parameter: protect_content
  * Type: Boolean
  * Required: Optional
  * Description: Protects the contents of the sent message from forwarding and saving
* Parameter: allow_paid_broadcast
  * Type: Boolean
  * Required: Optional
  * Description: Pass True to allow up to 1000 messages per second, ignoring broadcasting limits for a fee of 0.1 Telegram Stars per message. The relevant Stars will be withdrawn from the bot's balance.
* Parameter: message_effect_id
  * Type: String
  * Required: Optional
  * Description: Unique identifier of the message effect to be added to the message; for private chats only
* Parameter: reply_parameters
  * Type: ReplyParameters
  * Required: Optional
  * Description: Description of the message to reply to
* Parameter: reply_markup
  * Type: InlineKeyboardMarkup
  * Required: Optional
  * Description: A JSON-serialized object for an inline keyboard. If empty, one 'Play game_title' button will be shown. If not empty, the first button must launch the game.


#### [](#game)Game

This object represents a game. Use BotFather to create and edit games, their short names will act as unique identifiers.



* Field: title
  * Type: String
  * Description: Title of the game
* Field: description
  * Type: String
  * Description: Description of the game
* Field: photo
  * Type: Array of PhotoSize
  * Description: Photo that will be displayed in the game message in chats
* Field: text
  * Type: String
  * Description: Optional. Brief description of the game or high scores included in the game message. Can be automatically edited to include current high scores for the game when the bot calls setGameScore, or manually edited using editMessageText. 0-4096 characters.
* Field: text_entities
  * Type: Array of MessageEntity
  * Description: Optional. Special entities that appear in text, such as usernames, URLs, bot commands, etc.
* Field: animation
  * Type: Animation
  * Description: Optional. Animation that will be displayed in the game message in chats. Upload via BotFather.


#### [](#callbackgame)CallbackGame

A placeholder, currently holds no information. Use [BotFather](https://t.me/botfather) to set up your game.

#### [](#setgamescore)setGameScore

Use this method to set the score of the specified user in a game message. On success, if the message is not an inline message, the [Message](#message) is returned, otherwise _True_ is returned. Returns an error, if the new score is not greater than the user's current score in the chat and _force_ is _False_.



* Parameter: user_id
  * Type: Integer
  * Required: Yes
  * Description: User identifier
* Parameter: score
  * Type: Integer
  * Required: Yes
  * Description: New score, must be non-negative
* Parameter: force
  * Type: Boolean
  * Required: Optional
  * Description: Pass True if the high score is allowed to decrease. This can be useful when fixing mistakes or banning cheaters.
* Parameter: disable_edit_message
  * Type: Boolean
  * Required: Optional
  * Description: Pass True if the game message should not be automatically edited to include the current scoreboard
* Parameter: chat_id
  * Type: Integer
  * Required: Optional
  * Description: Required if inline_message_id is not specified. Unique identifier for the target chat.
* Parameter: message_id
  * Type: Integer
  * Required: Optional
  * Description: Required if inline_message_id is not specified. Identifier of the sent message.
* Parameter: inline_message_id
  * Type: String
  * Required: Optional
  * Description: Required if chat_id and message_id are not specified. Identifier of the inline message.


#### [](#getgamehighscores)getGameHighScores

Use this method to get data for high score tables. Will return the score of the specified user and several of their neighbors in a game. Returns an Array of [GameHighScore](#gamehighscore) objects.

> This method will currently return scores for the target user, plus two of their closest neighbors on each side. Will also return the top three users if the user and their neighbors are not among them. Please note that this behavior is subject to change.



* Parameter: user_id
  * Type: Integer
  * Required: Yes
  * Description: Target user id
* Parameter: chat_id
  * Type: Integer
  * Required: Optional
  * Description: Required if inline_message_id is not specified. Unique identifier for the target chat.
* Parameter: message_id
  * Type: Integer
  * Required: Optional
  * Description: Required if inline_message_id is not specified. Identifier of the sent message.
* Parameter: inline_message_id
  * Type: String
  * Required: Optional
  * Description: Required if chat_id and message_id are not specified. Identifier of the inline message.


#### [](#gamehighscore)GameHighScore

This object represents one row of the high scores table for a game.


|Field   |Type   |Description                              |
|--------|-------|-----------------------------------------|
|position|Integer|Position in high score table for the game|
|user    |User   |User                                     |
|score   |Integer|Score                                    |


* * *

And that's about all we've got for now.  
If you've got any questions, please check out our [**Bot FAQ »**](https://core.telegram.org/bots/faq)
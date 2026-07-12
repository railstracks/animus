# Bots: An introduction for developers
Bots are **small applications** that run entirely within the Telegram app. Users interact with bots through **flexible interfaces** that can support **any kind of task or service**. For more information, see:

*   [Detailed Guide to Bot Features](https://core.telegram.org/bots/features)
*   [Full API Reference for Developers](https://core.telegram.org/bots/api)
*   [Basic Tutorial: From @BotFather to 'Hello World'](https://core.telegram.org/bots/tutorial)

The **Telegram Bot Platform** hosts more than **10 million** bots and is **free** for both users and developers.

### [](#what-can-you-do-with-bots)What Can You Do with Bots?

*   [Replace Entire Websites](#replace-entire-websites)
*   [Natively Integrate AI Chatbots](#natively-integrate-ai-chatbots)
*   [Manage Your Business](#manage-your-business)
*   [Receive Payments](#receive-payments)
*   [Create Custom Tools](#create-custom-tools)
*   [Integrate with Services and Devices](#integrate-with-services-and-devices)
*   [Host Games](#host-games)
*   [Build Social Networks](#build-social-networks)
*   [Monetize Your Service](#monetize-your-service)
*   [Promote Your Project](#promote-your-project)
*   [Anything Else!](#anything-else)

#### [](#replace-entire-websites)Replace Entire Websites

Telegram bots can host [Mini Apps](https://core.telegram.org/bots/webapps) built with _JavaScript_. This allows for **infinitely flexible** interfaces that can power everything from online stores to arcade games. Unlike websites, bots support [seamless authorization](https://core.telegram.org/api/url-authorization) and notifications through Telegram out of the box.

> Try [@DurgerKingBot](https://t.me/durgerkingbot) – or check out the [dedicated guide to Mini Apps](https://core.telegram.org/bots/webapps) to build your own.

#### [](#natively-integrate-ai-chatbots)Natively Integrate AI Chatbots

Bots natively support threaded conversations to manage several different topics in parallel. This is especially useful for building AI chatbots — and lets users easily access information from previous chats.

Instead of waiting for full replies, chatbots can also [stream live responses](https://core.telegram.org/bots/api#sendmessagedraft) as they’re generated.

> You can easily enable topics in private chats by toggling on _Threaded Mode_ via [@BotFather](https://t.me/botfather).

> This feature is subject to an additional fee for Telegram Star purchases as described in [Section 6.2.6](https://telegram.org/tos/bot-developers#6-2-6-enabling-topics-in-private-chats) of our Terms of Service for Bot Developers.

#### [](#manage-your-business)Manage Your Business

[Telegram Business](https://telegram.org/blog/telegram-business) users can connect Telegram bots to process and answer messages **on their behalf**, via their personal account. This allows businesses to **seamlessly integrate** any existing tools and workflows, or add new AI assistants to **increase productivity**.

As we continue to expand the set of **free tools** [available to bots](https://core.telegram.org/bots) through this integration, we encourage all developers to **innovate** and **develop** useful applications and services for **businesses** on Telegram.

> Developers can turn on [Business Mode](https://core.telegram.org/bots/features#bots-for-business) in [@BotFather](https://t.me/BotFather) if their bot supports [integration](https://core.telegram.org/bots/api#businessconnection) with Telegram Business accounts.

##### [](#receive-payments)Receive Payments

Bots can sell all kinds of goods and services on Telegram – to anyone in the world. [Telegram Stars](https://telegram.org/blog/telegram-stars) allow users to securely and effortlessly buy **digital products** via in-app purchases. In addition, **physical products** can be easily purchased through [third-party providers](https://core.telegram.org/bots/payments#payments-for-physical-products) that support integration with Mini Apps.

> Try [@ShopBot](https://t.me/shopbot) – or check out our dedicated guides for [digital](https://core.telegram.org/bots/payments-stars) and [physical](https://core.telegram.org/bots/payments) products to build your own.

#### [](#create-custom-tools)Create Custom Tools

Increase your productivity by creating bots for **specific tasks** – like converting files, managing chats or fetching today’s forecast. Users can chat directly with bots, or add them to groups and channels to introduce extra features.

> Mini apps can generate media and files – that users can effortlessly share to [other chats](about:blank/webapps#sharing-media) or a post [as a story](about:blank/webapps#sharing-from-mini-apps-to-stories).

#### [](#integrate-with-services-and-devices)Integrate with Services and Devices

Mini apps can **seamlessly integrate** with third-party services, APIs and devices to instantly process and update information – like changing a user's [emoji status](about:blank/webapps#setting-emoji-status) when they start a game ![🎮](https://telegram.org/img/emoji/40/F09F8EAE.png) or get in a taxi ![🚕](https://telegram.org/img/emoji/40/F09F9A95.png).

> By default, Mini Apps seamlessly integrate with Android and iOS, allowing users to add [direct shortcuts](https://telegram.org/blog/fullscreen-miniapps-and-more#home-screen-shortcuts) to their device’s home screen.

Likewise, many popular platforms already have official Telegram bots, which allow users to comfortably access content in one app – or perform quick searches using [inline mode](https://core.telegram.org/bots/inline).

[![](https://core.telegram.org/file/464001186/11e04/7XO37b9iccE.133932/a29f8bf593af567fcc)](/file/464001186/11e04/7XO37b9iccE.133932/a29f8bf593af567fcc)

> Try [@GMailBot](https://t.me/gmailbot), [@GitHubBot](https://t.me/githubbot), [@Bing](https://t.me/bing), [@YouTube](https://t.me/youtube), [@wiki](https://t.me/wiki) and more.

#### [](#host-games)Host Games

Developers can create both lightweight [HTML5 Games](https://core.telegram.org/bots/games) and immersive **full-screen modern games** with support for [detailed motion controls](about:blank/webapps#accelerometer), location-based [points of interest](about:blank/webapps#locationmanager) and dynamic [hardware optimizations](about:blank/webapps#additional-data-in-user-agent).

> Try some of the games in the [@Gamee](https://t.me/gamee) library – or check out the [HTML5](https://core.telegram.org/bots/games) and [Mini App](https://core.telegram.org/bots/webapps) manuals to build your own.

#### [](#build-social-networks)Build Social Networks

Bots can serve as an intermediary to connect users based on shared interests, location, and more. Coordinate meetups, show local services, or help people sell second-hand items.

> Users can place [direct shortcuts](https://telegram.org/blog/fullscreen-miniapps-and-more#home-screen-shortcuts) to specific mini apps on the **home screen** of their devices – accessing services in **one tap**.

#### [](#monetize-your-service)Monetize Your Service

Telegram offers a **robust ecosystem** of monetization features, allowing any bot to support its development with **multiple revenue streams**. Popular bots can passively earn income through [Revenue Sharing](https://telegram.org/blog/dynamic-video-quality-and-more#telegram-ads-in-bots) from Telegram Ads, implement [subscription plans](https://telegram.org/blog/fullscreen-miniapps-and-more#subscription-plans) for users – or offer [paid content](https://telegram.org/blog/superchannels-star-reactions-subscriptions#paid-media-for-bots) and [digital products](https://telegram.org/blog/telegram-stars#telegram-stars) for [Telegram Stars](https://telegram.org/blog/telegram-stars).

> Telegram Stars in your bot's balance can be used to [increase message limits](https://telegram.org/blog/dynamic-video-quality-and-more#increased-message-limits-for-bots), [send gifts](https://core.telegram.org/bots/api#sendgift) to users or [accept rewards](https://telegram.org/blog/monetization-for-channels) in Toncoin.

#### [](#promote-your-project)Promote Your Project

Bots can host [affiliate marketing programs](https://telegram.org/blog/affiliate-programs-ai-sticker-search) – giving developers a **transparent way** to quickly scale with organic growth from **user referrals**.

Affiliate Programs support custom **revenue sharing rates** and variable **commission periods**, allowing you to customize your offers and update your campaign over time.

> To learn more and get started in just a few taps, check out our [dedicated guide](https://telegram.org/tour/affiliate-programs).

#### [](#anything-else)Anything Else

The possibilities for bots are endless – from simple scripts to complex mini apps. Whether you’re a beginner or professional programmer, you can create personalized tools with the help of the [Bot Platform](https://core.telegram.org/bots/api).

> All Mini Apps you build on Telegram can be **highly customized** to fit your brand identity, including by uploading high-quality [media demos](#mini-app-previews) and setting a custom [Loading Screen](#customizable-loading-screen) with your own logo and color palette

* * *

### [](#how-do-bots-work)How Do Bots Work?

> For a detailed explanation of Bot Features, see [this guide](https://core.telegram.org/bots/features)

Telegram bots are special accounts that do not need a phone number to set up. Bots are connected to their owner’s server, which processes inputs and requests from users.

Telegram’s intermediary server handles all encryption and communication with the Telegram API. Developers communicate with this server via an easy HTTPS-interface with a simplified version of the [Telegram API](https://core.telegram.org/api) – known as the [Bot API](https://core.telegram.org/bots/api).

#### [](#how-are-bots-different-from-users)How Are Bots Different from Users?

Bots are able to process inputs and requests in ways that user accounts can’t, but there are several differences between a bot and a normal user.

*   Bots don’t have ‘last seen’ or online statuses – instead they show a ‘bot’ label in the chat.
*   Bots have limited cloud storage – older messages may be removed by the server shortly after they have been processed.
*   Bots can't start conversations with users. A user must either add them to a group or send them a message first. People can search for your bot’s username or start a chat via its unique t.me/bot\_username link.
*   By default, bots added to groups **only see relevant messages** in the chat (see [Privacy Mode](about:/bots/features#privacy-mode)).
*   Bots never eat, sleep or complain (unless expressly programmed otherwise).

#### [](#bot-links)Bot Links

Bot usernames normally require a ‘bot’ suffix, but some bots don’t have them – such as [@stickers](https://t.me/stickers), [@gif](https://t.me/gif), [@wiki](https://t.me/wiki) or [@bing](https://t.me/bing).

Anyone can [assign collectible usernames](https://telegram.org/blog/shareable-folders-custom-wallpapers#bot-links-and-telegram-premium-on-fragment) to bots, including those without the 'bot' suffix.

### [](#how-do-i-create-a-bot)How Do I Create a Bot?

Creating Telegram bots is super-easy, but you will need at least some skills in **computer programming**.

Creating a bot is streamlined by Telegram’s Bot API, which gives the tools and framework required to integrate your code. To get started, message [@BotFather](https://t.me/botfather) on Telegram to register your bot and receive its authentication token.

> Your **bot token** is its unique identifier – store it in a **secure place**, and only share it with people who need direct access to the bot. Everyone who has your token will have **full control** over your bot.

#### [](#what-next)What Next?

We recommend that you check out our guide to [Bot Features](https://core.telegram.org/bots/features) to see what you can teach your bot to do:

*   [Detailed Guide to Bot Features](https://core.telegram.org/bots/features)
*   [Full API Reference for Developers](https://core.telegram.org/bots/api)
*   [Basic Tutorial: From @BotFather to 'Hello World'](https://core.telegram.org/bots/tutorial)
*   [Code Examples](https://core.telegram.org/bots/samples)
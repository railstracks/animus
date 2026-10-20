#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

// Forward-declare SSL type to avoid pulling OpenSSL headers into every
// translation unit that includes this header.
typedef struct ssl_st SSL;

#include <json/value.h>

namespace animus::kernel {

class IrcInterfaceRuntime : public std::enable_shared_from_this<IrcInterfaceRuntime> {
public:
    struct ChannelConfig {
        std::string name;
        std::string key;
    };

    struct Config {
        std::string host;
        int port{6667};
        std::string serverPassword;
        std::string nick;
        std::string username{"animus"};
        std::string realname{"Animus Agent"};
        std::vector<ChannelConfig> channels;
        bool reconnectEnabled{true};
        int reconnectInitialDelayMs{1000};
        int reconnectMaxDelayMs{60000};
        bool forceIpv4{true};  // IPv6 often unavailable in containers/networks
        bool useTls{false};      // IRC over TLS (port 6697)
    };

    using MessageCallback =
        std::function<void(const std::string& nick, const std::string& target, const std::string& message, bool isNotice)>;
    using StatusCallback = std::function<void(bool connected, const std::string& error)>;

    static bool ParseConfig(const Json::Value& input, Config* out, std::string* error);

    IrcInterfaceRuntime(
        std::string interfaceName,
        Config config,
        MessageCallback messageCallback,
        StatusCallback statusCallback);
    ~IrcInterfaceRuntime();

    void Start();
    void Stop();
    bool SendPrivmsg(const std::string& target, const std::string& message);

    // --- #204: text normalization + outbound actions ---
    // Inbound: CTCP frames and mIRC formatting codes are converted to safe
    // markdown-ish text; no raw control byte ever reaches the message
    // callback (raw  in stored turns kills every later LLM call with
    // HTTP 400 — providers reject illegal control bytes in JSON strings).
    // Returns nullopt for non-ACTION CTCP (VERSION, PING, DCC ...): machine
    // probes that must never become conversational turns.
    static std::optional<std::string> NormalizeIncomingText(const std::string& text);
    // Outbound: markdown emphasis pairs -> IRC formatting codes.
    // Conservative: non-empty inner without the marker char, no space at
    // inner edges, word boundaries for single-char markers, bullet lines
    // and snake_case left alone.
    static std::string MarkdownToIrcFormat(const std::string& text);
    // Wrap a chunk in a CTCP ACTION frame.
    static std::string ActionFrame(const std::string& chunk);
    // Send a /me-style action message to a channel or nick.
    bool SendAction(const std::string& target, const std::string& message);

    // Single-line dispatch (public for tests; safe without a connection —
    // SendRaw on a closed socket returns false).
    void HandleLine(const std::string& line, bool* joined);

private:
    struct ParsedLine {
        std::string prefix;
        std::string command;
        std::vector<std::string> params;
        std::string trailing;
    };

    static ParsedLine ParseLine(const std::string& line);
    static std::string NickFromPrefix(const std::string& prefix);

    void ReportStatus(bool connected, const std::string& error);
    void RunLoop();
    bool ConnectAndRun(std::string* error);
    bool Connect(std::string* error);
    bool SendRaw(const std::string& line);
    void CloseSocket();

    Config m_config;
    MessageCallback m_messageCallback;
    StatusCallback m_statusCallback;
    std::thread m_thread;
    std::atomic<bool> m_started{false};
    std::atomic<bool> m_stopRequested{false};
    std::atomic<int> m_socket{-1};
    std::mutex m_sendMutex;
    SSL* m_ssl{nullptr};  // Non-null when using TLS
};

} // namespace animus::kernel

// WhatsAppTransport.h — Raw TCP + TLS + WebSocket transport for WhatsApp
//
// Bypasses Drogon's WebSocket client entirely. Owns the full socket lifecycle:
//   TCP connect → TLS handshake → HTTP Upgrade → WebSocket frame read/write
//
// Why: Drogon's WS client causes WhatsApp to close (1011) after the Noise
// client finish. All alternative transports (Node.js ws, openssl s_client)
// work correctly. The issue is isolated to Drogon's WS send path.
// ============================================================================
#pragma once

#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <atomic>

// Forward-declare SSL to avoid header pollution
typedef struct ssl_st SSL;
typedef struct ssl_ctx_st SSL_CTX;

namespace animus::whatsapp {

struct WhatsAppTransportConfig {
    std::string host = "web.whatsapp.com";
    int port = 443;
    std::string path = "/ws/chat";
    std::string query;                    // e.g. "ED=..." for routing
    std::string origin = "https://web.whatsapp.com";
    int keepAliveIntervalSec = 25;
};

class WhatsAppTransport {
public:
    using FrameCallback = std::function<void(const std::vector<uint8_t>& frame)>;
    using CloseCallback = std::function<void(int code, const std::string& reason)>;
    using ConnectCallback = std::function<void(bool success, const std::string& error)>;

    WhatsAppTransport();
    ~WhatsAppTransport();

    // Configure before connect
    void setConfig(const WhatsAppTransportConfig& cfg);

    // Set callbacks before connect
    void onFrame(FrameCallback cb);
    void onClose(CloseCallback cb);
    void onConnect(ConnectCallback cb);

    // Connect (blocking in background thread, callback fires when ready)
    void connect();

    // Send a binary frame (adds WS framing + masking automatically)
    void send(const std::vector<uint8_t>& payload);

    // Send a ping
    void ping();

    // Close the connection
    void close();

    // Is connected?
    bool isConnected() const { return connected_.load() and !closing_.load(); }

private:
    // Internal helpers
    bool tcpConnect();
    bool tlsConnect();
    bool wsUpgrade();
    void readLoop();
    void writeRaw(const std::vector<uint8_t>& data);
    void writeRaw(const std::string& data);
    void sendWsFrame(uint8_t opcode, const std::vector<uint8_t>& payload);
    void sendCloseFrame(uint16_t code, const std::string& reason);

    // WS frame parsing
    enum class ReadState { HEADER1, HEADER2, EXT_LEN, MASK_KEY, PAYLOAD };
    ReadState readState_ = ReadState::HEADER1;
    uint8_t frameOpcode_ = 0;
    bool frameMasked_ = false;
    uint64_t frameLen_ = 0;
    uint8_t frameMask_[4] = {};
    std::vector<uint8_t> frameBuf_;
    uint64_t frameRead_ = 0;
    size_t extLenRead_ = 0;
    size_t maskRead_ = 0;
    bool processReadByte(uint8_t b);

    WhatsAppTransportConfig config_;
    FrameCallback frameCb_;
    CloseCallback closeCb_;
    ConnectCallback connectCb_;

    // Socket
    int sockfd_ = -1;
    SSL* ssl_ = nullptr;
    SSL_CTX* sslCtx_ = nullptr;

    // Threading
    std::thread readThread_;
    std::mutex writeMutex_;
    std::atomic<bool> connected_{false};
    std::atomic<bool> closing_{false};
    std::atomic<bool> running_{false};
};

} // namespace animus::whatsapp

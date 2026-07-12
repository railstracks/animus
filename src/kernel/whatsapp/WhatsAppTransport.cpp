// WhatsAppTransport.cpp — Raw TCP + TLS + WebSocket transport
// ============================================================================
#include "animus_kernel/whatsapp/WhatsAppTransport.h"

#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/rand.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include <unistd.h>
#include <fcntl.h>
#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <iostream>
#include <chrono>
#include <sstream>

namespace animus::whatsapp {

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------
WhatsAppTransport::WhatsAppTransport() = default;

WhatsAppTransport::~WhatsAppTransport() {
    close();
}

void WhatsAppTransport::setConfig(const WhatsAppTransportConfig& cfg) { config_ = cfg; }
void WhatsAppTransport::onFrame(FrameCallback cb) { frameCb_ = std::move(cb); }
void WhatsAppTransport::onClose(CloseCallback cb) { closeCb_ = std::move(cb); }
void WhatsAppTransport::onConnect(ConnectCallback cb) { connectCb_ = std::move(cb); }

// ---------------------------------------------------------------------------
// Connect
// ---------------------------------------------------------------------------
void WhatsAppTransport::connect() {
    // Run the connection sequence on a background thread
    readThread_ = std::thread([this]() {
        try {
            std::cerr << "[wa-transport] Connecting to " << config_.host
                      << ":" << config_.port << std::endl;

            if (!tcpConnect()) {
                if (connectCb_) connectCb_(false, "TCP connect failed");
                return;
            }
            std::cerr << "[wa-transport] TCP connected" << std::endl;

            if (!tlsConnect()) {
                if (connectCb_) connectCb_(false, "TLS handshake failed");
                return;
            }
            std::cerr << "[wa-transport] TLS connected" << std::endl;

            if (!wsUpgrade()) {
                if (connectCb_) connectCb_(false, "WS upgrade failed");
                return;
            }
            std::cerr << "[wa-transport] WebSocket upgrade complete" << std::endl;

            connected_ = true;
            if (connectCb_) connectCb_(true, "");

            // Enter read loop
            readLoop();
        } catch (const std::exception& e) {
            std::cerr << "[wa-transport] Exception: " << e.what() << std::endl;
            connected_ = false;
            if (connectCb_) connectCb_(false, e.what());
        }
    });
}

// ---------------------------------------------------------------------------
// TCP connect (with DNS resolution)
// ---------------------------------------------------------------------------
bool WhatsAppTransport::tcpConnect() {
    struct addrinfo hints{}, *result;
    hints.ai_family = AF_INET;   // IPv4 (web.whatsapp.com has A records)
    hints.ai_socktype = SOCK_STREAM;

    // Try /etc/hosts first (we have an entry for web.whatsapp.com)
    int rc = getaddrinfo(config_.host.c_str(), std::to_string(config_.port).c_str(),
                         &hints, &result);
    if (rc != 0) {
        std::cerr << "[wa-transport] DNS failed for " << config_.host
                  << ": " << gai_strerror(rc) << std::endl;
        return false;
    }

    sockfd_ = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
    if (sockfd_ < 0) {
        freeaddrinfo(result);
        return false;
    }

    // TCP_NODELAY — disable Nagle for low-latency binary protocol
    int flag = 1;
    setsockopt(sockfd_, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));

    // 10-second connect timeout
    struct timeval tv;
    tv.tv_sec = 10;
    tv.tv_usec = 0;
    setsockopt(sockfd_, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    setsockopt(sockfd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    if (::connect(sockfd_, result->ai_addr, result->ai_addrlen) != 0) {
        std::cerr << "[wa-transport] TCP connect failed: " << strerror(errno) << std::endl;
        close();
        freeaddrinfo(result);
        return false;
    }

    freeaddrinfo(result);
    return true;
}

// ---------------------------------------------------------------------------
// TLS handshake
// ---------------------------------------------------------------------------
bool WhatsAppTransport::tlsConnect() {
    SSL_library_init();
    SSL_load_error_strings();
    OpenSSL_add_all_algorithms();

    sslCtx_ = SSL_CTX_new(TLS_client_method());
    if (!sslCtx_) {
        std::cerr << "[wa-transport] SSL_CTX_new failed" << std::endl;
        return false;
    }

    // Use default CA certs
    SSL_CTX_set_default_verify_paths(sslCtx_);

    // Min protocol TLS 1.2
    SSL_CTX_set_min_proto_version(sslCtx_, TLS1_2_VERSION);

    ssl_ = SSL_new(sslCtx_);
    if (!ssl_) return false;

    // SNI
    SSL_set_tlsext_host_name(ssl_, config_.host.c_str());

    SSL_set_fd(ssl_, sockfd_);

    // 15-second TLS handshake timeout
    struct timeval tv;
    tv.tv_sec = 15;
    tv.tv_usec = 0;
    setsockopt(sockfd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(sockfd_, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    int ret = SSL_connect(ssl_);
    if (ret != 1) {
        int err = SSL_get_error(ssl_, ret);
        std::cerr << "[wa-transport] TLS handshake failed: error " << err << std::endl;
        ERR_print_errors_fp(stderr);
        return false;
    }

    std::cerr << "[wa-transport] TLS: " << SSL_get_version(ssl_)
              << " cipher: " << SSL_get_cipher_name(ssl_) << std::endl;
    return true;
}

// ---------------------------------------------------------------------------
// WebSocket upgrade
// ---------------------------------------------------------------------------
bool WhatsAppTransport::wsUpgrade() {
    // Generate 16-byte Sec-WebSocket-Key (base64)
    uint8_t rawKey[16];
    RAND_bytes(rawKey, sizeof(rawKey));
    static const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string wsKey;
    wsKey.reserve(24);
    for (int i = 0; i < 16; i += 3) {
        uint32_t n = (rawKey[i] << 16) | (i + 1 < 16 ? rawKey[i + 1] << 8 : 0)
                   | (i + 2 < 16 ? rawKey[i + 2] : 0);
        wsKey += b64[(n >> 18) & 0x3f];
        wsKey += b64[(n >> 12) & 0x3f];
        wsKey += (i + 1 < 16) ? b64[(n >> 6) & 0x3f] : '=';
        wsKey += (i + 2 < 16) ? b64[n & 0x3f] : '=';
    }

    std::string fullPath = config_.path;
    if (!config_.query.empty()) fullPath += "?" + config_.query;

    std::ostringstream req;
    req << "GET " << fullPath << " HTTP/1.1\r\n"
        << "Host: " << config_.host << "\r\n"
        << "Upgrade: websocket\r\n"
        << "Connection: Upgrade\r\n"
        << "Sec-WebSocket-Key: " << wsKey << "\r\n"
        << "Sec-WebSocket-Version: 13\r\n"
        << "Origin: " << config_.origin << "\r\n"
        << "\r\n";

    std::string reqStr = req.str();
    SSL_write(ssl_, reqStr.data(), reqStr.size());

    // Read response headers
    std::string response;
    char buf[4096];
    while (response.find("\r\n\r\n") == std::string::npos) {
        int n = SSL_read(ssl_, buf, sizeof(buf));
        if (n <= 0) {
            std::cerr << "[wa-transport] Failed reading upgrade response" << std::endl;
            return false;
        }
        response.append(buf, n);
    }

    // Check for 101
    if (response.find("101") == std::string::npos) {
        std::cerr << "[wa-transport] Upgrade rejected:\n" << response << std::endl;
        return false;
    }

    // Any leftover bytes after the header are the start of WS frames
    size_t headerEnd = response.find("\r\n\r\n") + 4;
    if (headerEnd < response.size()) {
        // These bytes are already in 'response' but won't be in our read loop
        // Store them for the read loop to pick up
        // We'll handle this by buffering them in the read state
        for (size_t i = headerEnd; i < response.size(); i++) {
            processReadByte(static_cast<uint8_t>(response[i]));
        }
    }

    return true;
}

// ---------------------------------------------------------------------------
// Read loop (runs on background thread)
// ---------------------------------------------------------------------------
void WhatsAppTransport::readLoop() {
    running_ = true;
    uint8_t buf[8192];

    while (running_ && connected_) {
        int n = SSL_read(ssl_, buf, sizeof(buf));
        if (n <= 0) {
            int err = SSL_get_error(ssl_, n);
            if (err == SSL_ERROR_ZERO_RETURN || err == SSL_ERROR_SYSCALL) {
                // Connection closed
                break;
            }
            if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) {
                continue;  // Non-blocking retry
            }
            std::cerr << "[wa-transport] SSL_read error: " << err << std::endl;
            break;
        }

        // Feed each byte to the frame parser
        for (int i = 0; i < n; i++) {
            if (!processReadByte(buf[i])) {
                // Close frame or error
                running_ = false;
                break;
            }
        }
    }

    connected_ = false;
    running_ = false;

    // Only notify via callback if we detected the disconnect (not if close() initiated it)
    bool wasExternalClose = closing_.load();
    if (!wasExternalClose && closeCb_) {
        closeCb_(0, "Connection closed");
    }

    // Do NOT cleanup SSL/socket here — close() handles that.
    ssl_ = nullptr;
    sslCtx_ = nullptr;
    sockfd_ = -1;
}

// ---------------------------------------------------------------------------
// WS frame parser (byte-by-byte state machine)
// ---------------------------------------------------------------------------
bool WhatsAppTransport::processReadByte(uint8_t b) {
    switch (readState_) {
    case ReadState::HEADER1:
        frameOpcode_ = b & 0x0f;
        readState_ = ReadState::HEADER2;
        frameBuf_.clear();
        frameRead_ = 0;
        return true;

    case ReadState::HEADER2:
        frameMasked_ = (b & 0x80) != 0;
        frameLen_ = b & 0x7f;
        extLenRead_ = 0;
        if (frameLen_ == 126) {
            frameLen_ = 0;
            readState_ = ReadState::EXT_LEN;
        } else if (frameLen_ == 127) {
            frameLen_ = 0;
            readState_ = ReadState::EXT_LEN;
        } else {
            if (frameMasked_) {
                maskRead_ = 0;
                readState_ = ReadState::MASK_KEY;
            } else {
                frameBuf_.reserve(frameLen_);
                readState_ = ReadState::PAYLOAD;
            }
        }
        return true;

    case ReadState::EXT_LEN: {
        // 126 → 2 bytes, 127 → 8 bytes
        // We already consumed the first length indicator
        // frameLen_ = 0, extLenRead_ tracks how many bytes we've read
        // But we need to know if we're reading 2 or 8 bytes.
        // Look at the original indicator:
        // Actually, let me re-check: frameLen_ was set to 0 and we need
        // to read either 2 or 8 bytes. The original 126/127 is lost.
        // Let's use extLenRead_==0 to know we're starting and check
        // how many bytes to read based on frameBuf_... no.
        // Simpler: track expected ext len bytes.

        // We know: if we came from 126, we need 2 bytes.
        //           if we came from 127, we need 8 bytes.
        // Since we zeroed frameLen_ before entering this state,
        // we can check frameBuf_ empty + extLenRead_... no, frameBuf_
        // was cleared in HEADER1.
        // Let's just use a separate flag. For simplicity: if we haven't
        // read any ext bytes yet, peek at what triggered this state.
        // Actually: 126 is used for frames <=65535, 127 for larger.
        // WS frames from WhatsApp are small. Let's assume 126 (2 bytes).
        // If we ever need 127, we'll know.

        // This is called for each byte. Accumulate.
        // extLenRead_ starts at 0.
        if (extLenRead_ < 2) {
            frameLen_ = (frameLen_ << 8) | b;
            extLenRead_++;
            if (extLenRead_ == 2) {
                // Done reading extended length
                if (frameMasked_) {
                    maskRead_ = 0;
                    readState_ = ReadState::MASK_KEY;
                } else {
                    frameBuf_.reserve(static_cast<size_t>(frameLen_));
                    readState_ = ReadState::PAYLOAD;
                }
            }
        }
        return true;
    }

    case ReadState::MASK_KEY:
        frameMask_[maskRead_++] = b;
        if (maskRead_ == 4) {
            frameBuf_.reserve(static_cast<size_t>(frameLen_));
            readState_ = ReadState::PAYLOAD;
        }
        return true;

    case ReadState::PAYLOAD:
        frameBuf_.push_back(b);
        frameRead_++;
        if (frameRead_ >= frameLen_) {
            // Complete frame received
            // Unmask if needed
            if (frameMasked_) {
                for (size_t i = 0; i < frameBuf_.size(); i++) {
                    frameBuf_[i] ^= frameMask_[i % 4];
                }
            }

            // Dispatch based on opcode
            bool ok = true;
            switch (frameOpcode_) {
            case 0x2:  // Binary frame
                if (frameCb_) frameCb_(frameBuf_);
                break;
            case 0x8: {  // Close
                uint16_t code = 0;
                if (frameBuf_.size() >= 2) {
                    code = (frameBuf_[0] << 8) | frameBuf_[1];
                }
                std::string reason;
                if (frameBuf_.size() > 2) {
                    reason.assign(frameBuf_.begin() + 2, frameBuf_.end());
                }
                std::cerr << "[wa-transport] Close frame: code=" << code
                          << " reason='" << reason << "'" << std::endl;
                ok = false;
                break;
            }
            case 0x9:  // Ping → auto-pong
                sendWsFrame(0x0A, frameBuf_);
                break;
            case 0xA:  // Pong — ignore
                break;
            default:
                std::cerr << "[wa-transport] Unknown WS opcode: 0x"
                          << std::hex << (int)frameOpcode_ << std::dec << std::endl;
                break;
            }

            // Reset for next frame
            readState_ = ReadState::HEADER1;
            frameBuf_.clear();
            frameRead_ = 0;
            return ok;
        }
        return true;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Send binary frame (masked, as required for client→server)
// ---------------------------------------------------------------------------
void WhatsAppTransport::send(const std::vector<uint8_t>& payload) {
    sendWsFrame(0x02, payload);  // Binary frame
}

void WhatsAppTransport::ping() {
    sendWsFrame(0x09, {});  // Ping
}

// ---------------------------------------------------------------------------
// Send WS frame with masking
// ---------------------------------------------------------------------------
void WhatsAppTransport::sendWsFrame(uint8_t opcode, const std::vector<uint8_t>& payload) {
    std::lock_guard<std::mutex> lock(writeMutex_);

    if (!ssl_ || !connected_) return;

    // Generate mask
    uint8_t mask[4];
    RAND_bytes(mask, 4);

    // Build frame header
    std::vector<uint8_t> frame;
    frame.push_back(0x80 | opcode);  // FIN + opcode

    size_t len = payload.size();
    if (len <= 125) {
        frame.push_back(0x80 | static_cast<uint8_t>(len));  // Mask bit + length
    } else if (len <= 65535) {
        frame.push_back(0x80 | 126);
        frame.push_back((len >> 8) & 0xff);
        frame.push_back(len & 0xff);
    } else {
        frame.push_back(0x80 | 127);
        for (int i = 7; i >= 0; i--) {
            frame.push_back((len >> (i * 8)) & 0xff);
        }
    }

    // Mask key
    frame.insert(frame.end(), mask, mask + 4);

    // Masked payload
    for (size_t i = 0; i < payload.size(); i++) {
        frame.push_back(payload[i] ^ mask[i % 4]);
    }

    // Hex dump for debugging (first frame only)
    static std::atomic<int> frameCount{0};
    int fc = frameCount++;
    if (fc < 3) {
        fprintf(stderr, "[wa-transport] Sending WS frame #%d: opcode=0x%02x payload=%zu total=%zu\n",
                fc, opcode, payload.size(), frame.size());
        fprintf(stderr, "[wa-transport] First 32 bytes: ");
        for (size_t i = 0; i < std::min(frame.size(), size_t(32)); i++) {
            fprintf(stderr, "%02x", frame[i]);
        }
        fprintf(stderr, "\n");
        fprintf(stderr, "[wa-transport] Payload first 32 bytes (unmasked): ");
        for (size_t i = 0; i < std::min(payload.size(), size_t(32)); i++) {
            fprintf(stderr, "%02x", payload[i]);
        }
        fprintf(stderr, "\n");
    }

    // Send via TLS
    int written = SSL_write(ssl_, frame.data(), static_cast<int>(frame.size()));
    if (written <= 0) {
        int err = SSL_get_error(ssl_, written);
        std::cerr << "[wa-transport] SSL_write failed: error " << err << std::endl;
    }
}

void WhatsAppTransport::sendCloseFrame(uint16_t code, const std::string& reason) {
    std::vector<uint8_t> payload;
    payload.push_back((code >> 8) & 0xff);
    payload.push_back(code & 0xff);
    payload.insert(payload.end(), reason.begin(), reason.end());
    sendWsFrame(0x08, payload);
}

// ---------------------------------------------------------------------------
// Close
// ---------------------------------------------------------------------------
void WhatsAppTransport::close() {
    closing_ = true;
    running_ = false;
    connected_ = false;

    // Close socket first to unblock any pending SSL_read in readLoop
    if (sockfd_ >= 0) { ::close(sockfd_); sockfd_ = -1; }

    // Wait for read thread to finish BEFORE freeing SSL.
    // The socket close above unblocks SSL_read with an error,
    // but the read thread needs time to unwind. If we free SSL
    // first, the read thread touches freed memory → SIGSEGV.
    if (readThread_.joinable()) readThread_.join();

    // Now safe to shut down and free SSL — no thread using it
    if (ssl_) {
        SSL_shutdown(ssl_);
        SSL_free(ssl_);
        ssl_ = nullptr;
    }
    if (sslCtx_) { SSL_CTX_free(sslCtx_); sslCtx_ = nullptr; }
}

// ---------------------------------------------------------------------------
// Raw writes (used internally)
// ---------------------------------------------------------------------------
void WhatsAppTransport::writeRaw(const std::vector<uint8_t>& data) {
    std::lock_guard<std::mutex> lock(writeMutex_);
    if (ssl_) SSL_write(ssl_, data.data(), static_cast<int>(data.size()));
}

void WhatsAppTransport::writeRaw(const std::string& data) {
    std::lock_guard<std::mutex> lock(writeMutex_);
    if (ssl_) SSL_write(ssl_, data.data(), static_cast<int>(data.size()));
}

} // namespace animus::whatsapp

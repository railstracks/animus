#include "animus_kernel/signal/SignalTypes.h"
#include "animus_kernel/signal/DoubleRatchet.h"

#include <sstream>
#include <cstring>

namespace animus::signal {

// ─── SignalKeypair ───

SignalKeypair SignalKeypair::generate() {
    // Real X25519 keypair via OpenSSL
    auto dh = DHKeyPair::generate();
    SignalKeypair kp;
    std::memcpy(kp.public_key.data(), dh.public_key.data(), 32);
    std::memcpy(kp.private_key.data(), dh.private_key.data(), 32);
    return kp;
}

// ─── SignalAddress ───

std::string SignalAddress::to_string() const {
    if (device_id == 0) return name;
    return name + "." + std::to_string(device_id);
}

// ─── SignalIdentityKey ───

SignalIdentityKey SignalIdentityKey::generate(uint32_t registration_id) {
    return { SignalKeypair::generate(), registration_id };
}

// ─── Serialization helpers ───
// Simple length-prefixed binary format for development.
// Phase 063-B: will use protobuf or libsignal's native serialization.

static void write_u32_be(std::vector<uint8_t>& buf, uint32_t v) {
    buf.push_back((v >> 24) & 0xFF);
    buf.push_back((v >> 16) & 0xFF);
    buf.push_back((v >> 8) & 0xFF);
    buf.push_back(v & 0xFF);
}

static uint32_t read_u32_be(const uint8_t* data) {
    return (uint32_t(data[0]) << 24) | (uint32_t(data[1]) << 16) |
           (uint32_t(data[2]) << 8)  | uint32_t(data[3]);
}

static void write_bytes(std::vector<uint8_t>& buf, const uint8_t* data, size_t len) {
    write_u32_be(buf, static_cast<uint32_t>(len));
    buf.insert(buf.end(), data, data + len);
}

static bool read_bytes(const uint8_t* data, size_t max_len, size_t& offset,
                       std::vector<uint8_t>& out) {
    if (offset + 4 > max_len) return false;
    uint32_t len = read_u32_be(data + offset);
    offset += 4;
    if (offset + len > max_len) return false;
    out.assign(data + offset, data + offset + len);
    offset += len;
    return true;
}

static void write_fixed32(std::vector<uint8_t>& buf, const std::array<uint8_t, 32>& arr) {
    buf.insert(buf.end(), arr.begin(), arr.end());
}

static void read_fixed32(const uint8_t* data, size_t max_len, size_t& offset,
                         std::array<uint8_t, 32>& arr) {
    if (offset + 32 > max_len) return;
    std::memcpy(arr.data(), data + offset, 32);
    offset += 32;
}

// ─── SignalPreKey ───

std::vector<uint8_t> SignalPreKey::serialize() const {
    std::vector<uint8_t> buf;
    write_u32_be(buf, id);
    write_fixed32(buf, keypair.public_key);
    write_fixed32(buf, keypair.private_key);
    return buf;
}

SignalPreKey SignalPreKey::deserialize(const uint8_t* data, size_t len) {
    SignalPreKey pk;
    if (len < 4) return pk;
    size_t offset = 0;
    pk.id = read_u32_be(data);
    offset = 4;
    read_fixed32(data, len, offset, pk.keypair.public_key);
    read_fixed32(data, len, offset, pk.keypair.private_key);
    return pk;
}

// ─── SignalSignedPreKey ───

std::vector<uint8_t> SignalSignedPreKey::serialize() const {
    std::vector<uint8_t> buf;
    write_u32_be(buf, id);
    write_fixed32(buf, keypair.public_key);
    write_fixed32(buf, keypair.private_key);
    write_bytes(buf, signature.data(), signature.size());
    return buf;
}

SignalSignedPreKey SignalSignedPreKey::deserialize(const uint8_t* data, size_t len) {
    SignalSignedPreKey spk;
    if (len < 4) return spk;
    size_t offset = 0;
    spk.id = read_u32_be(data);
    offset = 4;
    read_fixed32(data, len, offset, spk.keypair.public_key);
    read_fixed32(data, len, offset, spk.keypair.private_key);
    std::vector<uint8_t> sig;
    if (read_bytes(data, len, offset, sig)) {
        spk.signature = std::move(sig);
    }
    return spk;
}

// ─── PreKeyBundle ───

std::vector<uint8_t> PreKeyBundle::serialize() const {
    std::vector<uint8_t> buf;
    write_u32_be(buf, registration_id);
    write_u32_be(buf, device_id);
    // Pre-key (optional)
    if (pre_key_id.has_value()) {
        write_u32_be(buf, 1); // present flag
        write_u32_be(buf, *pre_key_id);
        write_fixed32(buf, pre_key.public_key);
    } else {
        write_u32_be(buf, 0);
    }
    // Signed pre-key
    write_u32_be(buf, signed_pre_key.id);
    write_fixed32(buf, signed_pre_key.keypair.public_key);
    write_bytes(buf, signed_pre_key.signature.data(), signed_pre_key.signature.size());
    // Identity key
    write_fixed32(buf, identity_key.public_key);
    return buf;
}

PreKeyBundle PreKeyBundle::deserialize(const uint8_t* data, size_t len) {
    PreKeyBundle b;
    if (len < 12) return b;
    size_t offset = 0;
    b.registration_id = read_u32_be(data); offset += 4;
    b.device_id = read_u32_be(data + offset); offset += 4;
    uint32_t has_pk = read_u32_be(data + offset); offset += 4;
    if (has_pk) {
        if (offset + 36 > len) return b;
        b.pre_key_id = read_u32_be(data + offset); offset += 4;
        read_fixed32(data, len, offset, b.pre_key.public_key);
    }
    if (offset + 4 > len) return b;
    b.signed_pre_key.id = read_u32_be(data + offset); offset += 4;
    read_fixed32(data, len, offset, b.signed_pre_key.keypair.public_key);
    std::vector<uint8_t> sig;
    read_bytes(data, len, offset, sig);
    b.signed_pre_key.signature = std::move(sig);
    read_fixed32(data, len, offset, b.identity_key.public_key);
    return b;
}

// ─── EncryptedMessage ───

std::vector<uint8_t> EncryptedMessage::serialize() const {
    std::vector<uint8_t> buf;
    buf.push_back(static_cast<uint8_t>(type));
    write_bytes(buf, ciphertext.data(), ciphertext.size());
    write_bytes(buf, header.data(), header.size());
    write_u32_be(buf, counter);
    return buf;
}

EncryptedMessage EncryptedMessage::deserialize(const uint8_t* data, size_t len) {
    EncryptedMessage m;
    if (len < 1) return m;
    size_t offset = 0;
    m.type = static_cast<SignalMessageType>(data[0]);
    offset = 1;
    std::vector<uint8_t> ct, hdr;
    if (!read_bytes(data, len, offset, ct)) return m;
    m.ciphertext = std::move(ct);
    if (!read_bytes(data, len, offset, hdr)) return m;
    m.header = std::move(hdr);
    if (offset + 4 > len) return m;
    m.counter = read_u32_be(data + offset);
    return m;
}

// ─── GroupEncryptedMessage ───

std::vector<uint8_t> GroupEncryptedMessage::serialize() const {
    std::vector<uint8_t> buf;
    write_bytes(buf, reinterpret_cast<const uint8_t*>(group_id.data()), group_id.size());
    write_bytes(buf, reinterpret_cast<const uint8_t*>(sender.name.data()), sender.name.size());
    write_u32_be(buf, sender.device_id);
    write_bytes(buf, ciphertext.data(), ciphertext.size());
    write_u32_be(buf, counter);
    return buf;
}

GroupEncryptedMessage GroupEncryptedMessage::deserialize(const uint8_t* data, size_t len) {
    GroupEncryptedMessage m;
    size_t offset = 0;
    std::vector<uint8_t> gid, sname;
    if (!read_bytes(data, len, offset, gid)) return m;
    m.group_id = std::string(gid.begin(), gid.end());
    if (!read_bytes(data, len, offset, sname)) return m;
    m.sender.name = std::string(sname.begin(), sname.end());
    if (offset + 4 > len) return m;
    m.sender.device_id = read_u32_be(data + offset); offset += 4;
    std::vector<uint8_t> ct;
    if (!read_bytes(data, len, offset, ct)) return m;
    m.ciphertext = std::move(ct);
    if (offset + 4 > len) return m;
    m.counter = read_u32_be(data + offset);
    return m;
}

// ─── SessionRecord ───

std::vector<uint8_t> SessionRecord::serialize() const {
    std::vector<uint8_t> buf;
    write_u32_be(buf, version);
    buf.push_back(is_fresh ? 1 : 0);
    write_bytes(buf, state.data(), state.size());
    return buf;
}

SessionRecord SessionRecord::deserialize(const uint8_t* data, size_t len) {
    SessionRecord r;
    if (len < 5) return r;
    size_t offset = 0;
    r.version = read_u32_be(data); offset += 4;
    r.is_fresh = (data[4] != 0); offset += 1;
    std::vector<uint8_t> st;
    if (read_bytes(data, len, offset, st)) {
        r.state = std::move(st);
    }
    return r;
}

// ─── SenderKeyRecord ───

std::vector<uint8_t> SenderKeyRecord::serialize() const {
    std::vector<uint8_t> buf;
    write_bytes(buf, reinterpret_cast<const uint8_t*>(group_id.data()), group_id.size());
    write_bytes(buf, reinterpret_cast<const uint8_t*>(sender.name.data()), sender.name.size());
    write_u32_be(buf, sender.device_id);
    write_u32_be(buf, iteration);
    write_bytes(buf, chain_key.data(), chain_key.size());
    write_fixed32(buf, signing_key.public_key);
    write_fixed32(buf, signing_key.private_key);
    write_bytes(buf, state.data(), state.size());
    return buf;
}

SenderKeyRecord SenderKeyRecord::deserialize(const uint8_t* data, size_t len) {
    SenderKeyRecord r;
    size_t offset = 0;
    std::vector<uint8_t> gid, sname;
    if (!read_bytes(data, len, offset, gid)) return r;
    r.group_id = std::string(gid.begin(), gid.end());
    if (!read_bytes(data, len, offset, sname)) return r;
    r.sender.name = std::string(sname.begin(), sname.end());
    if (offset + 4 > len) return r;
    r.sender.device_id = read_u32_be(data + offset); offset += 4;
    if (offset + 4 > len) return r;
    r.iteration = read_u32_be(data + offset); offset += 4;
    std::vector<uint8_t> ck;
    if (!read_bytes(data, len, offset, ck)) return r;
    r.chain_key = std::move(ck);
    read_fixed32(data, len, offset, r.signing_key.public_key);
    read_fixed32(data, len, offset, r.signing_key.private_key);
    std::vector<uint8_t> st;
    if (read_bytes(data, len, offset, st)) {
        r.state = std::move(st);
    }
    return r;
}

} // namespace animus::signal

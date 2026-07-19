#include "animus_kernel/tools/AttachmentTool.h"

#include <json/json.h>
#include <fstream>
#include <filesystem>
#include <random>
#include <chrono>
#include <sstream>
#include <iomanip>
#include <iostream>

namespace animus::kernel {

// ============================================================================
// Helpers
// ============================================================================

namespace {

Json::Value ParseArgs(const std::string& args) {
    Json::Value root;
    Json::CharReaderBuilder builder;
    std::istringstream stream(args);
    std::string errors;
    Json::parseFromStream(builder, stream, &root, &errors);
    return root;
}

std::string GetStringField(const Json::Value& v, const std::string& key,
                            const std::string& def = "") {
    return (v.isMember(key) && v[key].isString()) ? v[key].asString() : def;
}

} // namespace

// ============================================================================
// Base64 encoding
// ============================================================================

std::string AttachmentTool::EncodeBase64(const std::string& data) const {
    static const char table[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    int val = 0, valb = -6;
    for (unsigned char c : data) {
        val = (val << 8) + c;
        valb += 8;
        while (valb >= 0) {
            out.push_back(table[(val >> valb) & 0x3F]);
            valb -= 6;
        }
    }
    if (valb > -6) {
        out.push_back(table[((val << 8) >> (valb + 8)) & 0x3F]);
    }
    while (out.size() % 4) out.push_back('=');
    return out;
}

// ============================================================================
// MIME type detection
// ============================================================================

std::string AttachmentTool::DetectMimeType(const std::string& filepath) const {
    auto pos = filepath.find_last_of('.');
    if (pos == std::string::npos) return "application/octet-stream";
    std::string ext = filepath.substr(pos);
    // Lowercase
    for (auto& c : ext) c = static_cast<char>(tolower(c));

    if (ext == ".png")  return "image/png";
    if (ext == ".jpg" || ext == ".jpeg") return "image/jpeg";
    if (ext == ".webp") return "image/webp";
    if (ext == ".gif")  return "image/gif";
    if (ext == ".mp4")  return "video/mp4";
    if (ext == ".webm") return "video/webm";
    if (ext == ".mp3")  return "audio/mpeg";
    if (ext == ".wav")  return "audio/wav";
    if (ext == ".flac") return "audio/flac";
    if (ext == ".ogg")  return "audio/ogg";
    if (ext == ".txt")  return "text/plain";
    if (ext == ".md")   return "text/markdown";
    if (ext == ".json") return "application/json";
    if (ext == ".csv")  return "text/csv";
    if (ext == ".html" || ext == ".htm") return "text/html";
    if (ext == ".glb")  return "model/gltf-binary";
    if (ext == ".pdf")  return "application/pdf";
    return "application/octet-stream";
}

std::string AttachmentTool::GenerateId() const {
    auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 15);

    std::ostringstream oss;
    oss << "att_" << std::hex << now;
    for (int i = 0; i < 8; ++i) {
        oss << std::hex << dis(gen);
    }
    return oss.str();
}

std::string AttachmentTool::ExtractFilename(const std::string& filepath) const {
    auto pos = filepath.find_last_of("/\\");
    if (pos == std::string::npos) return filepath;
    return filepath.substr(pos + 1);
}

// ============================================================================
// Tool definition
// ============================================================================

ToolDefinition AttachmentTool::GetDefinition() const {
    ToolDefinition def;
    def.name = "add_chat_attachment";
    def.description = "Attach a file to the current chat session for display to the user. "
                      "The file will be rendered inline in the chat interface (images, audio, video, text). "
                      "Use this to share generated images, audio renders, or other files with the user.";

    ToolParameter pFilepath;
    pFilepath.name = "filepath"; pFilepath.type = "string"; pFilepath.required = true;
    pFilepath.description = "Absolute path to the file to attach";
    def.parameters.push_back(pFilepath);

    ToolParameter pDisplay;
    pDisplay.name = "display_name"; pDisplay.type = "string"; pDisplay.required = false;
    pDisplay.description = "Friendly name for the attachment (defaults to filename)";
    def.parameters.push_back(pDisplay);

    ToolParameter pMime;
    pMime.name = "mime_type"; pMime.type = "string"; pMime.required = false;
    pMime.description = "Explicit MIME type (auto-detected from extension if omitted)";
    def.parameters.push_back(pMime);

    return def;
}

// ============================================================================
// Execute
// ============================================================================

ToolResult AttachmentTool::Execute(const ToolCall& call) {
    ToolResult result;
    result.call_id = call.id;

    auto args = ParseArgs(call.arguments);
    if (args.empty()) {
        result.success = false;
        result.error = "Failed to parse arguments";
        return result;
    }

    std::string filepath = GetStringField(args, "filepath");
    if (filepath.empty()) {
        result.success = false;
        result.error = "filepath is required";
        return result;
    }

    // Check file exists
    if (!std::filesystem::exists(filepath)) {
        result.success = false;
        result.error = "File not found: " + filepath;
        return result;
    }

    // Get session key from injected context
    std::string sessionKey = GetStringField(args, "__session_key");
    if (sessionKey.empty()) {
        result.success = false;
        result.error = "No session context available";
        return result;
    }

    // Determine file properties
    std::string filename = GetStringField(args, "display_name");
    if (filename.empty()) filename = ExtractFilename(filepath);

    std::string mimeType = GetStringField(args, "mime_type");
    if (mimeType.empty()) mimeType = DetectMimeType(filepath);

    // Read file
    std::ifstream file(filepath, std::ios::binary);
    if (!file) {
        result.success = false;
        result.error = "Cannot read file: " + filepath;
        return result;
    }
    std::ostringstream ss;
    ss << file.rdbuf();
    std::string fileData = ss.str();
    file.close();

    int64_t sizeBytes = static_cast<int64_t>(fileData.size());

    // Inline vs file-backed
    // Small text files (< 256KB): inline base64
    // Images and other files < 5MB: inline base64
    // Larger: file-backed (store filepath reference)
    bool inlineStorage = false;
    std::string dataB64;
    std::string storedFilepath;

    size_t inlineLimit = (mimeType.rfind("text/", 0) == 0) ? 256 * 1024 : 5 * 1024 * 1024;
    if (static_cast<size_t>(sizeBytes) <= inlineLimit) {
        dataB64 = EncodeBase64(fileData);
        inlineStorage = true;
    } else {
        // Copy to media/attachments/ directory
        std::string attDir = "media/attachments";
        std::filesystem::create_directories(attDir);
        std::string destPath = attDir + "/" + GenerateId() + "_" + filename;
        std::filesystem::copy_file(filepath, destPath,
            std::filesystem::copy_options::overwrite_existing);
        storedFilepath = destPath;
    }

    // Get the current session to find the latest turn ID
    // We need to associate the attachment with the most recent assistant turn
    // or create a new turn for the attachment
    if (!m_sessions || !m_store) {
        result.success = false;
        result.error = "Attachment store or session manager not available";
        return result;
    }

    // Parse session key
    SessionKey key;
    auto pos = sessionKey.find('|');
    if (pos != std::string::npos) {
        key.connector = sessionKey.substr(0, pos);
        auto rest = sessionKey.substr(pos + 1);
        auto pos2 = rest.find('|');
        if (pos2 != std::string::npos) {
            key.conversation_id = rest.substr(0, pos2);
            key.thread_id = rest.substr(pos2 + 1);
        } else {
            key.conversation_id = rest;
        }
    } else {
        key.connector = sessionKey;
    }

    auto session = m_sessions->GetOrCreate(key);
    if (!session) {
        result.success = false;
        result.error = "Cannot access session: " + sessionKey;
        return result;
    }

    // Add a new turn for the attachment
    SessionTurn turn;
    turn.role = "assistant";
    turn.content = "";
    turn.unix_ms = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());

    session->AddTurn(std::move(turn));

    // Get the turn ID we just added
    const auto& turns = session->Turns();
    uint64_t turnId = turns.back().turn_id;

    // Persist session
    m_sessions->FlushSession(session->Id());

    // Save attachment record
    Attachment att;
    att.id = GenerateId();
    att.turn_id = turnId;
    att.session_key = sessionKey;
    att.filename = filename;
    att.mime_type = mimeType;
    att.filepath = storedFilepath;
    att.data_b64 = dataB64;
    att.size_bytes = sizeBytes;
    att.created_at = static_cast<int64_t>(turn.unix_ms);

    if (!m_store->Save(att)) {
        result.success = false;
        result.error = "Failed to save attachment record";
        return result;
    }

    result.success = true;
    result.output = "Attachment saved: " + filename + " (" + std::to_string(sizeBytes)
                  + " bytes, " + mimeType + ")";
    if (inlineStorage) {
        result.output += " [inline]";
    } else {
        result.output += " [file-backed: " + storedFilepath + "]";
    }
    return result;
}

} // namespace animus::kernel
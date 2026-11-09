#include "animus_kernel/lua/HttpUpload.h"

#include <fstream>
#include <random>
#include <sstream>

namespace animus::kernel {

bool PartHeaderSafe(const std::string& token) {
    if (token.empty()) return false;
    for (const char c : token) {
        if (c == '\r' || c == '\n' || c == '"' || c == '\\') return false;
    }
    return true;
}

std::string BuildMultipartBody(
    const std::vector<std::pair<std::string, std::string>>& fields,
    const std::vector<UploadFilePart>& fileParts,
    const std::string& boundary,
    std::string& contentTypeOut) {
    std::size_t reserve = 64;
    for (const auto& [name, value] : fields) {
        reserve += name.size() + value.size() + 64;
    }
    for (const auto& p : fileParts) {
        reserve += p.content.size() + p.filename.size() + 160;
    }
    std::string body;
    body.reserve(reserve);

    for (const auto& [name, value] : fields) {
        body += "--" + boundary + "\r\n";
        body += "Content-Disposition: form-data; name=\"";
        body += name;
        body += "\"\r\n\r\n";
        body += value;
        body += "\r\n";
    }
    for (const auto& p : fileParts) {
        body += "--" + boundary + "\r\n";
        body += "Content-Disposition: form-data; name=\"";
        body += p.field;
        body += "\"; filename=\"";
        body += p.filename;
        body += "\"\r\nContent-Type: ";
        body += p.mime;
        body += "\r\n\r\n";
        body += p.content;
        body += "\r\n";
    }
    body += "--" + boundary + "--\r\n";

    contentTypeOut = "multipart/form-data; boundary=" + boundary;
    return body;
}

std::string ChooseBoundary(
    const std::vector<std::pair<std::string, std::string>>& fields,
    const std::vector<UploadFilePart>& fileParts) {
    static std::mt19937_64 rng(std::random_device{}());
    for (int attempt = 0; attempt < 8; ++attempt) {
        std::ostringstream os;
        os << "----animus" << std::hex << rng() << rng();
        const std::string boundary = os.str();
        bool collides = false;
        for (const auto& [name, value] : fields) {
            (void)name;
            if (value.find(boundary) != std::string::npos ||
                name.find(boundary) != std::string::npos) {
                collides = true;
                break;
            }
        }
        if (!collides) {
            for (const auto& p : fileParts) {
                if (p.content.find(boundary) != std::string::npos ||
                    p.filename.find(boundary) != std::string::npos) {
                    collides = true;
                    break;
                }
            }
        }
        if (!collides) return boundary;
    }
    return "";
}

bool PathWithinRoots(const std::filesystem::path& path,
                     const std::vector<std::filesystem::path>& roots) {
    std::error_code ec;
    const auto canon = std::filesystem::weakly_canonical(path, ec);
    if (ec) return false;
    const std::string p = canon.lexically_normal().generic_string();

    for (const auto& root : roots) {
        std::error_code rec;
        const auto rcanon = std::filesystem::weakly_canonical(root, rec);
        if (rec) continue;
        const std::string r = rcanon.lexically_normal().generic_string();
        if (r.empty()) continue;
        if (p == r) return true;
        if (p.size() > r.size() && p.compare(0, r.size(), r) == 0 &&
            (r.back() == '/' || p[r.size()] == '/')) {
            return true;
        }
    }
    return false;
}

bool ReadFileBytes(const std::filesystem::path& path, std::uint64_t cap,
                   std::string& out, std::string& err) {
    std::error_code ec;
    if (!std::filesystem::exists(path, ec) || ec) {
        err = "file not found: " + path.string();
        return false;
    }
    const auto size = std::filesystem::file_size(path, ec);
    if (ec) {
        err = "stat failed: " + path.string();
        return false;
    }
    if (static_cast<std::uint64_t>(size) > cap) {
        err = "size cap exceeded: " + std::to_string(size) + " bytes > " +
              std::to_string(cap);
        return false;
    }
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        err = "open failed: " + path.string();
        return false;
    }
    out.assign(static_cast<std::size_t>(size), '\0');
    if (size > 0) {
        f.read(out.data(), size);
        if (f.gcount() != static_cast<std::streamsize>(size)) {
            err = "short read: " + path.string();
            return false;
        }
    }
    return true;
}

} // namespace animus::kernel

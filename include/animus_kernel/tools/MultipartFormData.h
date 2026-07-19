#pragma once

#include <string>
#include <vector>
#include <random>

namespace animus::kernel {

// ============================================================================
// MultipartFormData — helper for constructing multipart/form-data bodies
// Used by providers that require multipart (e.g. Stability AI)
// ============================================================================

class MultipartFormData {
public:
    MultipartFormData() {
        // Generate a random boundary
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(0, 15);
        m_boundary = "----AnimusBoundary";
        for (int i = 0; i < 16; ++i) {
            m_boundary += dis(gen) < 10
                ? static_cast<char>('0' + dis(gen))
                : static_cast<char>('a' + dis(gen) - 10);
        }
    }

    /// Add a text field
    void AddField(const std::string& name, const std::string& value) {
        m_parts.emplace_back(Part{false, name, value, "", ""});
    }

    /// Add a file field (binary data)
    void AddFile(const std::string& name, const std::string& filename,
                 const std::string& contentType, const std::string& data) {
        m_parts.emplace_back(Part{true, name, data, filename, contentType});
    }

    /// Add a file field from a file path
    void AddFileFromPath(const std::string& name, const std::string& filepath,
                         const std::string& contentType);

    /// Build the complete multipart body
    std::string Build() const {
        std::string body;
        for (const auto& part : m_parts) {
            body += "--" + m_boundary + "\r\n";
            if (part.isFile) {
                body += "Content-Disposition: form-data; name=\"" + part.name
                      + "\"; filename=\"" + part.filename + "\"\r\n";
                body += "Content-Type: " + part.contentType + "\r\n\r\n";
                body += part.data + "\r\n";
            } else {
                body += "Content-Disposition: form-data; name=\"" + part.name + "\"\r\n\r\n";
                body += part.data + "\r\n";
            }
        }
        body += "--" + m_boundary + "--\r\n";
        return body;
    }

    /// Get the Content-Type header value for this multipart body
    std::string ContentType() const {
        return "multipart/form-data; boundary=" + m_boundary;
    }

    /// Get the boundary string
    const std::string& Boundary() const { return m_boundary; }

private:
    struct Part {
        bool isFile;
        std::string name;
        std::string data;       // field value or file content
        std::string filename;   // for files only
        std::string contentType; // for files only
    };

    std::string m_boundary;
    std::vector<Part> m_parts;
};

} // namespace animus::kernel
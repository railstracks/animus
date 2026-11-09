#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace animus::kernel {

// ============================================================================
// HttpUpload — pure helpers for the #159 outbound-media upload seam
// (animus.http_upload). File bytes are read and composed kernel-side;
// the Lua sandbox never sees them.
// ============================================================================

/// Hard per-file cap for outbound uploads (matches the #158 media client).
inline constexpr std::uint64_t kUploadByteCap = 64ULL * 1024 * 1024;

struct UploadFilePart {
    std::string field;     // form field name (e.g. "file", "files[0]")
    std::string filename;  // filename attribute
    std::string mime;      // part Content-Type
    std::string content;   // bytes (read kernel-side, never from Lua)
};

/// True for tokens safe inside a Content-Disposition header line: non-empty,
/// no CR/LF, no quotes, no backslash. The upload seam rejects unsafe
/// field names and filenames loudly instead of escaping them.
bool PartHeaderSafe(const std::string& token);

/// Compose a multipart/form-data body from string fields + file parts.
/// contentTypeOut receives "multipart/form-data; boundary=<boundary>".
/// Precondition: names/filenames pass PartHeaderSafe (the seam enforces).
std::string BuildMultipartBody(
    const std::vector<std::pair<std::string, std::string>>& fields,
    const std::vector<UploadFilePart>& fileParts,
    const std::string& boundary,
    std::string& contentTypeOut);

/// Pick a boundary that does not occur as a substring in any field value,
/// filename, or file content (bounded attempts). Returns "" on failure —
/// callers must reject loudly rather than compose unsafely.
std::string ChooseBoundary(
    const std::vector<std::pair<std::string, std::string>>& fields,
    const std::vector<UploadFilePart>& fileParts);

/// Canonical-path scope check: true iff path (after symlink/.. resolution)
/// names a location at or under one of the roots. Defeats traversal and
/// symlink escapes; roots are canonicalized too.
bool PathWithinRoots(const std::filesystem::path& path,
                     const std::vector<std::filesystem::path>& roots);

/// Read a binary file with a size cap. Returns false + err on any failure
/// (missing, unreadable, too large). Never returns partial content.
bool ReadFileBytes(const std::filesystem::path& path, std::uint64_t cap,
                   std::string& out, std::string& err);

} // namespace animus::kernel

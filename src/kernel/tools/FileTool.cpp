#include "animus_kernel/tools/FileTool.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <regex>

#include <json/json.h>

namespace animus::kernel {

namespace {
constexpr int kDefaultReadLimit = 2000;
constexpr int kDefaultPageSize = 50;
constexpr int kMaxPageSize = 200;
constexpr std::size_t kMaxFileSize = 50 * 1024; // 50KB

// Parse a JSON arguments string into a Json::Value.
Json::Value ParseArgs(const std::string& args) {
    Json::Value root;
    Json::CharReaderBuilder builder;
    std::istringstream stream(args);
    std::string errors;
    if (!Json::parseFromStream(builder, stream, &root, &errors)) {
        return {};
    }
    return root;
}

std::string GetAction(const Json::Value& args) {
    if (args.isMember("action") && args["action"].isString()) {
        return args["action"].asString();
    }
    return "";
}

std::string GetStringField(const Json::Value& args, const std::string& key, const std::string& defaultVal = "") {
    if (args.isMember(key) && args[key].isString()) {
        return args[key].asString();
    }
    return defaultVal;
}

int GetIntField(const Json::Value& args, const std::string& key, int defaultVal = 0) {
    if (args.isMember(key) && args[key].isInt()) {
        return args[key].asInt();
    }
    return defaultVal;
}

bool GetBoolField(const Json::Value& args, const std::string& key, bool defaultVal = false) {
    if (args.isMember(key) && args[key].isBool()) {
        return args[key].asBool();
    }
    return defaultVal;
}

struct FileCallPolicy {
    bool hasRestrictToWorkspace{false};
    bool restrictToWorkspace{true};
    std::string workspaceRoot;
    std::vector<std::string> pathAllowlist;
    std::vector<std::string> pathDenylist;
};

std::vector<std::string> JsonArrayToStringVector(const Json::Value& value) {
    std::vector<std::string> out;
    if (!value.isArray()) return out;
    for (Json::ArrayIndex i = 0; i < value.size(); ++i) {
        if (value[i].isString()) out.push_back(value[i].asString());
    }
    return out;
}

FileCallPolicy ParseCallPolicy(const Json::Value& args) {
    FileCallPolicy policy;
    if (!args.isMember("__policy") || !args["__policy"].isObject()) {
        return policy;
    }
    const Json::Value& cfg = args["__policy"];
    if (cfg.isMember("restrict_to_workspace") && cfg["restrict_to_workspace"].isBool()) {
        policy.hasRestrictToWorkspace = true;
        policy.restrictToWorkspace = cfg["restrict_to_workspace"].asBool();
    }
    if (cfg.isMember("workspace_root") && cfg["workspace_root"].isString()) {
        policy.workspaceRoot = cfg["workspace_root"].asString();
    }
    if (cfg.isMember("path_allowlist")) {
        policy.pathAllowlist = JsonArrayToStringVector(cfg["path_allowlist"]);
    }
    if (cfg.isMember("path_denylist")) {
        policy.pathDenylist = JsonArrayToStringVector(cfg["path_denylist"]);
    }
    return policy;
}

bool IsWithinWorkspace(const std::string& resolvedPath, const std::string& workspaceRoot) {
    if (workspaceRoot.empty()) return true;
    if (resolvedPath.size() < workspaceRoot.size()
        || resolvedPath.compare(0, workspaceRoot.size(), workspaceRoot) != 0) {
        return false;
    }
    if (resolvedPath.size() > workspaceRoot.size()) {
        const char sep = std::filesystem::path::preferred_separator;
        if (resolvedPath[workspaceRoot.size()] != sep) {
            return false;
        }
    }
    return true;
}

// Check if a string looks like binary data (null bytes in first 8KB)
bool IsBinaryFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) return false;
    char buf[8192];
    file.read(buf, sizeof(buf));
    std::streamsize n = file.gcount();
    for (std::streamsize i = 0; i < n; ++i) {
        if (buf[i] == '\0') return true;
    }
    return false;
}

} // anonymous namespace

// ============================================================================
// Construction
// ============================================================================

FileTool::FileTool(const std::string& workspaceRoot)
    : m_workspaceRoot(workspaceRoot.empty() ? "" : std::filesystem::weakly_canonical(std::filesystem::path(workspaceRoot)).string()) {}

// ============================================================================
// Definition
// ============================================================================

ToolDefinition FileTool::GetDefinition() const {
    ToolDefinition def;
    def.name = "file";
    def.description =
        "Read, write, edit, search, and manipulate files. Supports pagination, "
        "line-level search, block finding, targeted line insertion/replacement/deletion, "
        "and appending.";
    def.resultMode = ToolResultMode::deliver_to_model;

    ToolParameter actionParam;
    actionParam.name = "action";
    actionParam.type = "string";
    actionParam.description = "The file operation to perform";
    actionParam.required = true;
    actionParam.enum_values = {
        "read", "write", "edit",
        "searchLines", "findBlock",
        "insertLines", "replaceLines", "deleteLines", "appendLines"
    };
    def.parameters.push_back(actionParam);

    ToolParameter pathParam;
    pathParam.name = "path";
    pathParam.type = "string";
    pathParam.description = "File path, relative to the workspace root or absolute";
    pathParam.required = true;
    def.parameters.push_back(pathParam);

    ToolParameter contentParam;
    contentParam.name = "content";
    contentParam.type = "string";
    contentParam.description = "Content for write/insert/replace/append actions";
    contentParam.required = false;
    def.parameters.push_back(contentParam);

    ToolParameter oldTextParam;
    oldTextParam.name = "old_text";
    oldTextParam.type = "string";
    oldTextParam.description = "Exact text to find and replace for 'edit' action. Must be unique in the file.";
    oldTextParam.required = false;
    def.parameters.push_back(oldTextParam);

    ToolParameter offsetParam;
    offsetParam.name = "offset";
    offsetParam.type = "integer";
    offsetParam.description = "Line number to start reading from (1-indexed, for 'read').";
    offsetParam.required = false;
    def.parameters.push_back(offsetParam);

    ToolParameter limitParam;
    limitParam.name = "limit";
    limitParam.type = "integer";
    limitParam.description = "Maximum number of lines to read (for 'read').";
    limitParam.required = false;
    def.parameters.push_back(limitParam);

    ToolParameter pageParam;
    pageParam.name = "page";
    pageParam.type = "integer";
    pageParam.description = "Page number for paginated reading (1-based). Overrides offset/limit.";
    pageParam.required = false;
    def.parameters.push_back(pageParam);

    ToolParameter pageSizeParam;
    pageSizeParam.name = "page_size";
    pageSizeParam.type = "integer";
    pageSizeParam.description = "Lines per page for paginated reading (default 50, max 200).";
    pageSizeParam.required = false;
    def.parameters.push_back(pageSizeParam);

    ToolParameter patternParam;
    patternParam.name = "pattern";
    patternParam.type = "string";
    patternParam.description = "Search pattern for 'searchLines'. Literal substring by default.";
    patternParam.required = false;
    def.parameters.push_back(patternParam);

    ToolParameter regexParam;
    regexParam.name = "regex";
    regexParam.type = "boolean";
    regexParam.description = "Treat pattern/phrase as regex. Default false.";
    regexParam.required = false;
    def.parameters.push_back(regexParam);

    ToolParameter contextParam;
    contextParam.name = "context";
    contextParam.type = "integer";
    contextParam.description = "Lines of context before/after each searchLines match. Default 0.";
    contextParam.required = false;
    def.parameters.push_back(contextParam);

    ToolParameter maxMatchesParam;
    maxMatchesParam.name = "max_matches";
    maxMatchesParam.type = "integer";
    maxMatchesParam.description = "Max matches for searchLines (default 50) or blocks for findBlock (default 10).";
    maxMatchesParam.required = false;
    def.parameters.push_back(maxMatchesParam);

    ToolParameter blockStartParam;
    blockStartParam.name = "blockStart";
    blockStartParam.type = "string";
    blockStartParam.description = "Regex for start of enclosing block (findBlock). Scans backwards from match.";
    blockStartParam.required = false;
    def.parameters.push_back(blockStartParam);

    ToolParameter blockEndParam;
    blockEndParam.name = "blockEnd";
    blockEndParam.type = "string";
    blockEndParam.description = "Regex for end of enclosing block (findBlock). Scans forwards from match.";
    blockEndParam.required = false;
    def.parameters.push_back(blockEndParam);

    ToolParameter nestingParam;
    nestingParam.name = "nesting";
    nestingParam.type = "boolean";
    nestingParam.description = "Count nesting depth for blockEnd (findBlock). Use for brace-delimited blocks.";
    nestingParam.required = false;
    def.parameters.push_back(nestingParam);

    ToolParameter atLineParam;
    atLineParam.name = "at_line";
    atLineParam.type = "integer";
    atLineParam.description = "Line number to insert before (1-based, for 'insertLines').";
    atLineParam.required = false;
    def.parameters.push_back(atLineParam);

    ToolParameter startLineParam;
    startLineParam.name = "start_line";
    startLineParam.type = "integer";
    startLineParam.description = "First line to replace/delete (1-based, inclusive).";
    startLineParam.required = false;
    def.parameters.push_back(startLineParam);

    ToolParameter endLineParam;
    endLineParam.name = "end_line";
    endLineParam.type = "integer";
    endLineParam.description = "Last line to replace/delete (1-based, inclusive).";
    endLineParam.required = false;
    def.parameters.push_back(endLineParam);

    // Config parameters (unchanged)
    ToolParameter cfgRestrict;
    cfgRestrict.name = "restrict_to_workspace";
    cfgRestrict.type = "boolean";
    cfgRestrict.description = "When true, file access is constrained to workspace_root.";
    cfgRestrict.required = false;
    cfgRestrict.default_value_json = "true";
    def.config_parameters.push_back(cfgRestrict);

    ToolParameter cfgWorkspaceRoot;
    cfgWorkspaceRoot.name = "workspace_root";
    cfgWorkspaceRoot.type = "string";
    cfgWorkspaceRoot.description = "Workspace root boundary for this agent's File tool access.";
    cfgWorkspaceRoot.required = false;
    cfgWorkspaceRoot.default_value_json = "\"\"";
    def.config_parameters.push_back(cfgWorkspaceRoot);

    ToolParameter cfgAllowlist;
    cfgAllowlist.name = "path_allowlist";
    cfgAllowlist.type = "array";
    cfgAllowlist.description = "Optional allowlist glob patterns for resolved paths.";
    cfgAllowlist.required = false;
    cfgAllowlist.default_value_json = "[]";
    def.config_parameters.push_back(cfgAllowlist);

    ToolParameter cfgDenylist;
    cfgDenylist.name = "path_denylist";
    cfgDenylist.type = "array";
    cfgDenylist.description = "Optional denylist glob patterns for resolved paths.";
    cfgDenylist.required = false;
    cfgDenylist.default_value_json = "[]";
    def.config_parameters.push_back(cfgDenylist);

    return def;
}

// ============================================================================
// Execute
// ============================================================================

ToolResult FileTool::Execute(const ToolCall& call) {
    ToolResult result;
    result.call_id = call.id;

    auto args = ParseArgs(call.arguments);
    if (args.isNull()) {
        result.success = false;
        result.error = "Failed to parse tool arguments as JSON";
        return result;
    }

    std::string action = GetAction(args);
    std::string path = GetStringField(args, "path");

    if (path.empty()) {
        result.success = false;
        result.error = "Missing required parameter: path";
        return result;
    }

    const FileCallPolicy policy = ParseCallPolicy(args);
    bool effectiveRestrict = m_restrictToWorkspace;
    if (policy.hasRestrictToWorkspace && policy.restrictToWorkspace) {
        effectiveRestrict = true;
    }

    std::string effectiveWorkspaceRoot = m_workspaceRoot;
    if (!policy.workspaceRoot.empty()) {
        const std::string candidateRoot = ResolvePath(policy.workspaceRoot, m_workspaceRoot);
        if (m_restrictToWorkspace && !IsWithinWorkspace(candidateRoot, m_workspaceRoot)) {
            result.success = false;
            result.error = "Configured workspace_root exceeds global workspace boundary";
            return result;
        }
        effectiveWorkspaceRoot = candidateRoot;
        effectiveRestrict = true;
    }

    std::string resolved = ResolvePath(path, effectiveWorkspaceRoot);
    if (!IsPathAllowed(resolved, effectiveWorkspaceRoot, effectiveRestrict, policy.pathAllowlist, policy.pathDenylist)) {
        result.success = false;
        result.error = "Path is outside allowed boundaries: " + path;
        return result;
    }

    // Dispatch
    if (action == "read") {
        int offset = GetIntField(args, "offset", 1);
        int limit = GetIntField(args, "limit", 0);
        int page = GetIntField(args, "page", 0);
        int pageSize = GetIntField(args, "page_size", 0);
        return HandleRead(resolved, offset, limit, page, pageSize);
    } else if (action == "write") {
        std::string content = GetStringField(args, "content");
        return HandleWrite(resolved, content);
    } else if (action == "edit") {
        std::string oldText = GetStringField(args, "old_text");
        std::string newText = GetStringField(args, "content");
        if (oldText.empty()) {
            result.success = false;
            result.error = "Missing required parameter for edit: old_text";
            return result;
        }
        return HandleEdit(resolved, oldText, newText);
    } else if (action == "searchLines") {
        std::string pattern = GetStringField(args, "pattern");
        if (pattern.empty()) {
            result.success = false;
            result.error = "Missing required parameter: pattern";
            return result;
        }
        bool useRegex = GetBoolField(args, "regex", false);
        int context = GetIntField(args, "context", 0);
        int maxMatches = GetIntField(args, "max_matches", 50);
        return HandleSearchLines(resolved, pattern, useRegex, context, maxMatches);
    } else if (action == "findBlock") {
        std::string phrase = GetStringField(args, "pattern");
        if (phrase.empty()) {
            result.success = false;
            result.error = "Missing required parameter: pattern (phrase to search for)";
            return result;
        }
        std::string blockStart = GetStringField(args, "blockStart");
        std::string blockEnd = GetStringField(args, "blockEnd");
        if (blockStart.empty() || blockEnd.empty()) {
            result.success = false;
            result.error = "findBlock requires both blockStart and blockEnd patterns";
            return result;
        }
        bool nesting = GetBoolField(args, "nesting", false);
        bool useRegex = GetBoolField(args, "regex", false);
        int maxBlocks = GetIntField(args, "max_matches", 10);
        return HandleFindBlock(resolved, phrase, blockStart, blockEnd, nesting, useRegex, maxBlocks);
    } else if (action == "insertLines") {
        int atLine = GetIntField(args, "at_line", 0);
        if (atLine < 1) {
            result.success = false;
            result.error = "Missing or invalid at_line (must be >= 1)";
            return result;
        }
        std::string content = GetStringField(args, "content");
        return HandleInsertLines(resolved, atLine, content);
    } else if (action == "replaceLines") {
        int startLine = GetIntField(args, "start_line", 0);
        int endLine = GetIntField(args, "end_line", 0);
        if (startLine < 1 || endLine < startLine) {
            result.success = false;
            result.error = "Invalid start_line/end_line. Both must be >= 1 and end_line >= start_line.";
            return result;
        }
        std::string content = GetStringField(args, "content");
        return HandleReplaceLines(resolved, startLine, endLine, content);
    } else if (action == "deleteLines") {
        int startLine = GetIntField(args, "start_line", 0);
        int endLine = GetIntField(args, "end_line", 0);
        if (startLine < 1 || endLine < startLine) {
            result.success = false;
            result.error = "Invalid start_line/end_line. Both must be >= 1 and end_line >= start_line.";
            return result;
        }
        return HandleDeleteLines(resolved, startLine, endLine);
    } else if (action == "appendLines") {
        std::string content = GetStringField(args, "content");
        return HandleAppendLines(resolved, content);
    } else {
        result.success = false;
        result.error = "Unknown action: " + action +
            ". Valid: read, write, edit, searchLines, findBlock, "
            "insertLines, replaceLines, deleteLines, appendLines";
        return result;
    }
}

// ============================================================================
// Line I/O helpers
// ============================================================================

std::vector<std::string> FileTool::ReadAllLines(const std::string& path, std::string* error) {
    std::vector<std::string> lines;

    if (IsBinaryFile(path)) {
        if (error) *error = "Binary file — cannot perform line operations";
        return lines;
    }

    std::ifstream file(path);
    if (!file.is_open()) {
        if (error) *error = "Cannot open file: " + path;
        return lines;
    }

    std::string line;
    while (std::getline(file, line)) {
        lines.push_back(line);
    }

    // Handle file that doesn't end with newline but has content
    // (getline already captures the last line without trailing newline)

    if (error) error->clear();
    return lines;
}

bool FileTool::WriteAllLines(const std::string& path, const std::vector<std::string>& lines) {
    std::ofstream file(path, std::ios::trunc);
    if (!file.is_open()) return false;
    for (const auto& line : lines) {
        file << line << "\n";
    }
    file.close();
    return !file.fail();
}

// ============================================================================
// Action: read (with pagination support)
// ============================================================================

ToolResult FileTool::HandleRead(const std::string& path, int offset, int limit,
                                 int page, int pageSize) {
    ToolResult result;

    std::error_code ec;
    if (!std::filesystem::exists(path, ec) || ec) {
        result.success = false;
        result.error = "File not found: " + path;
        return result;
    }

    auto fileSize = std::filesystem::file_size(path, ec);
    if (!ec && fileSize > kMaxFileSize * 10) {
        result.success = false;
        result.error = "File too large (" + std::to_string(fileSize) + " bytes). Maximum: " +
                       std::to_string(kMaxFileSize * 10) + " bytes.";
        return result;
    }

    // If page/pageSize requested, convert to offset/limit
    if (page > 0) {
        int effectivePageSize = (pageSize > 0 && pageSize <= kMaxPageSize) ? pageSize : kDefaultPageSize;
        offset = (page - 1) * effectivePageSize + 1;
        limit = effectivePageSize;
    }

    std::ifstream file(path);
    if (!file.is_open()) {
        result.success = false;
        result.error = "Cannot open file: " + path;
        return result;
    }

    // Count total lines first (for pagination metadata)
    std::string line;
    int totalLines = 0;
    {
        std::ifstream countFile(path);
        while (std::getline(countFile, line)) totalLines++;
    }

    int lineNum = 0;
    int linesRead = 0;
    int effectiveLimit = (limit > 0) ? limit : kDefaultReadLimit;
    std::ostringstream output;

    while (std::getline(file, line)) {
        lineNum++;
        if (lineNum < offset) continue;
        if (linesRead >= effectiveLimit) {
            break;
        }
        output << line << "\n";
        linesRead++;
    }

    // Add pagination metadata if page was requested
    if (page > 0) {
        int effectivePageSize = (pageSize > 0 && pageSize <= kMaxPageSize) ? pageSize : kDefaultPageSize;
        int totalPages = (totalLines + effectivePageSize - 1) / effectivePageSize;
        if (totalPages == 0) totalPages = 1;

        Json::Value meta;
        meta["page"] = page;
        meta["page_size"] = effectivePageSize;
        meta["total_lines"] = totalLines;
        meta["total_pages"] = totalPages;
        meta["lines_returned"] = linesRead;
        meta["content"] = output.str();

        Json::StreamWriterBuilder writer;
        result.success = true;
        result.output = Json::writeString(writer, meta);
    } else {
        // Legacy mode — plain text output
        result.success = true;
        result.output = output.str();
        if (lineNum > offset + linesRead - 1) {
            result.output += "... (" + std::to_string(totalLines) + " total lines, " +
                std::to_string(linesRead) + " returned. Use offset=" +
                std::to_string(offset + linesRead) + " to continue.)";
        }
    }

    return result;
}

// ============================================================================
// Action: write
// ============================================================================

ToolResult FileTool::HandleWrite(const std::string& path, const std::string& content) {
    ToolResult result;

    std::error_code ec;
    auto parentDir = std::filesystem::path(path).parent_path();
    if (!parentDir.empty() && !std::filesystem::exists(parentDir)) {
        std::filesystem::create_directories(parentDir, ec);
        if (ec) {
            result.success = false;
            result.error = "Cannot create directory: " + parentDir.string() + " (" + ec.message() + ")";
            return result;
        }
    }

    std::ofstream file(path, std::ios::trunc);
    if (!file.is_open()) {
        result.success = false;
        result.error = "Cannot open file for writing: " + path;
        return result;
    }

    file << content;
    file.close();

    if (file.fail()) {
        result.success = false;
        result.error = "Write failed: " + path;
        return result;
    }

    result.success = true;
    result.output = "File written successfully: " + path + " (" + std::to_string(content.size()) + " bytes)";
    return result;
}

// ============================================================================
// Action: edit (string-level find-and-replace)
// ============================================================================

ToolResult FileTool::HandleEdit(const std::string& path, const std::string& oldText, const std::string& newText) {
    ToolResult result;

    std::ifstream file(path);
    if (!file.is_open()) {
        result.success = false;
        result.error = "Cannot open file: " + path;
        return result;
    }

    std::string content((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());
    file.close();

    std::size_t first = content.find(oldText);
    if (first == std::string::npos) {
        result.success = false;
        result.error = "old_text not found in file";
        return result;
    }

    std::size_t second = content.find(oldText, first + 1);
    if (second != std::string::npos) {
        result.success = false;
        result.error = "old_text is not unique in file (found at multiple positions). "
                       "Provide a longer, unique excerpt.";
        return result;
    }

    content.replace(first, oldText.size(), newText);

    std::ofstream outFile(path, std::ios::trunc);
    if (!outFile.is_open()) {
        result.success = false;
        result.error = "Cannot open file for writing: " + path;
        return result;
    }

    outFile << content;
    outFile.close();

    if (outFile.fail()) {
        result.success = false;
        result.error = "Write failed: " + path;
        return result;
    }

    result.success = true;
    result.output = "File edited successfully: " + path;
    return result;
}

// ============================================================================
// Action: searchLines
// ============================================================================

ToolResult FileTool::HandleSearchLines(const std::string& path, const std::string& pattern,
                                        bool useRegex, int context, int maxMatches) {
    ToolResult result;

    std::string readError;
    auto lines = ReadAllLines(path, &readError);
    if (!readError.empty()) {
        result.success = false;
        result.error = readError;
        return result;
    }

    // Compile regex if needed
    std::regex rx;
    if (useRegex) {
        try {
            rx = std::regex(pattern, std::regex::ECMAScript | std::regex::icase);
        } catch (const std::regex_error& e) {
            result.success = false;
            result.error = std::string("Invalid regex: ") + e.what();
            return result;
        }
    }

    Json::Value matches(Json::arrayValue);
    int matchCount = 0;
    bool truncated = false;

    for (int i = 0; i < static_cast<int>(lines.size()); ++i) {
        bool isMatch = false;
        if (useRegex) {
            isMatch = std::regex_search(lines[i], rx);
        } else {
            // Case-insensitive substring search
            std::string lineLower = lines[i];
            std::string patternLower = pattern;
            std::transform(lineLower.begin(), lineLower.end(), lineLower.begin(), ::tolower);
            std::transform(patternLower.begin(), patternLower.end(), patternLower.begin(), ::tolower);
            isMatch = (lineLower.find(patternLower) != std::string::npos);
        }

        if (!isMatch) continue;

        if (matchCount >= maxMatches) {
            truncated = true;
            break;
        }

        Json::Value match;
        match["line"] = i + 1; // 1-based
        match["text"] = lines[i];

        if (context > 0) {
            Json::Value before(Json::arrayValue);
            for (int c = std::max(0, i - context); c < i; ++c) {
                before.append(lines[c]);
            }
            match["context_before"] = before;

            Json::Value after(Json::arrayValue);
            for (int c = i + 1; c <= std::min(static_cast<int>(lines.size()) - 1, i + context); ++c) {
                after.append(lines[c]);
            }
            match["context_after"] = after;
        }

        matches.append(match);
        matchCount++;
    }

    Json::Value output;
    output["path"] = path;
    output["total_lines"] = static_cast<Json::UInt64>(lines.size());
    output["matches"] = matches;
    output["match_count"] = matchCount;
    output["truncated"] = truncated;

    Json::StreamWriterBuilder writer;
    result.success = true;
    result.output = Json::writeString(writer, output);
    return result;
}

// ============================================================================
// Action: findBlock
// ============================================================================

ToolResult FileTool::HandleFindBlock(const std::string& path, const std::string& phrase,
                                      const std::string& blockStart, const std::string& blockEnd,
                                      bool nesting, bool useRegex, int maxBlocks) {
    ToolResult result;

    std::string readError;
    auto lines = ReadAllLines(path, &readError);
    if (!readError.empty()) {
        result.success = false;
        result.error = readError;
        return result;
    }

    // Compile regexes
    std::regex phraseRx, startRx, endRx;
    try {
        if (useRegex) {
            phraseRx = std::regex(phrase, std::regex::ECMAScript | std::regex::icase);
        }
        startRx = std::regex(blockStart, std::regex::ECMAScript);
        endRx = std::regex(blockEnd, std::regex::ECMAScript);
    } catch (const std::regex_error& e) {
        result.success = false;
        result.error = std::string("Invalid regex: ") + e.what();
        return result;
    }

    // Find all phrase matches
    std::vector<int> phraseMatches;
    for (int i = 0; i < static_cast<int>(lines.size()); ++i) {
        bool match;
        if (useRegex) {
            match = std::regex_search(lines[i], phraseRx);
        } else {
            std::string lineLow = lines[i], phraseLow = phrase;
            std::transform(lineLow.begin(), lineLow.end(), lineLow.begin(), ::tolower);
            std::transform(phraseLow.begin(), phraseLow.end(), phraseLow.begin(), ::tolower);
            match = (lineLow.find(phraseLow) != std::string::npos);
        }
        if (match) phraseMatches.push_back(i);
    }

    Json::Value blocks(Json::arrayValue);
    int blockCount = 0;

    for (int phraseLine : phraseMatches) {
        if (blockCount >= maxBlocks) break;

        // Scan backwards for block start
        int startLine = -1;
        for (int i = phraseLine; i >= 0; --i) {
            if (std::regex_search(lines[i], startRx)) {
                startLine = i;
                break;
            }
        }
        if (startLine < 0) continue; // no enclosing block found

        // Scan forwards for block end
        int endLine = -1;
        if (nesting) {
            int depth = 0;
            for (int i = startLine; i < static_cast<int>(lines.size()); ++i) {
                // Count opening braces on this line
                int opens = std::count(lines[i].begin(), lines[i].end(), '{');
                int closes = std::count(lines[i].begin(), lines[i].end(), '}');

                if (i == startLine) {
                    depth = opens - closes;
                    if (std::regex_search(lines[i], endRx) && depth <= 0) {
                        endLine = i;
                        break;
                    }
                } else {
                    depth += opens - closes;
                    if (depth <= 0 && (std::regex_search(lines[i], endRx) || closes > 0)) {
                        endLine = i;
                        break;
                    }
                }
            }
        } else {
            for (int i = phraseLine; i < static_cast<int>(lines.size()); ++i) {
                if (std::regex_search(lines[i], endRx)) {
                    endLine = i;
                    break;
                }
            }
        }

        if (endLine < 0) endLine = static_cast<int>(lines.size()) - 1; // no end found, use EOF

        Json::Value block;
        block["phrase_line"] = phraseLine + 1;
        block["phrase_text"] = lines[phraseLine];
        block["block_start_line"] = startLine + 1;
        block["block_start_text"] = lines[startLine];
        block["block_end_line"] = endLine + 1;
        block["block_end_text"] = lines[endLine];

        // Include block content
        std::ostringstream content;
        for (int i = startLine; i <= endLine; ++i) {
            content << lines[i] << "\n";
        }
        block["content"] = content.str();

        blocks.append(block);
        blockCount++;
    }

    Json::Value output;
    output["path"] = path;
    output["total_lines"] = static_cast<Json::UInt64>(lines.size());
    output["blocks"] = blocks;
    output["match_count"] = blockCount;

    Json::StreamWriterBuilder writer;
    result.success = true;
    result.output = Json::writeString(writer, output);
    return result;
}

// ============================================================================
// Action: insertLines
// ============================================================================

ToolResult FileTool::HandleInsertLines(const std::string& path, int atLine, const std::string& content) {
    ToolResult result;

    std::string readError;
    auto lines = ReadAllLines(path, &readError);
    if (!readError.empty()) {
        result.success = false;
        result.error = readError;
        return result;
    }

    // Split content into lines
    std::vector<std::string> newLines;
    std::istringstream stream(content);
    std::string line;
    while (std::getline(stream, line)) {
        newLines.push_back(line);
    }

    // atLine is 1-based. Insert before atLine.
    // atLine=1 → insert at beginning. atLine > size → append.
    int insertPos = atLine - 1; // convert to 0-based
    if (insertPos > static_cast<int>(lines.size())) {
        insertPos = static_cast<int>(lines.size());
    }
    if (insertPos < 0) insertPos = 0;

    lines.insert(lines.begin() + insertPos, newLines.begin(), newLines.end());

    if (!WriteAllLines(path, lines)) {
        result.success = false;
        result.error = "Failed to write file: " + path;
        return result;
    }

    result.success = true;
    result.output = "Inserted " + std::to_string(newLines.size()) +
        " lines at line " + std::to_string(atLine) +
        ". Total lines now: " + std::to_string(lines.size());
    return result;
}

// ============================================================================
// Action: replaceLines
// ============================================================================

ToolResult FileTool::HandleReplaceLines(const std::string& path, int startLine, int endLine,
                                         const std::string& content) {
    ToolResult result;

    std::string readError;
    auto lines = ReadAllLines(path, &readError);
    if (!readError.empty()) {
        result.success = false;
        result.error = readError;
        return result;
    }

    // Validate range (1-based, inclusive)
    if (startLine < 1 || endLine < startLine || endLine > static_cast<int>(lines.size())) {
        result.success = false;
        result.error = "Invalid line range: " + std::to_string(startLine) + "-" +
            std::to_string(endLine) + " (file has " + std::to_string(lines.size()) + " lines)";
        return result;
    }

    int linesRemoved = endLine - startLine + 1;

    // Split replacement content into lines
    std::vector<std::string> newLines;
    std::istringstream stream(content);
    std::string line;
    while (std::getline(stream, line)) {
        newLines.push_back(line);
    }

    // Erase old lines (convert to 0-based)
    lines.erase(lines.begin() + startLine - 1, lines.begin() + endLine);

    // Insert new lines at the same position
    lines.insert(lines.begin() + startLine - 1, newLines.begin(), newLines.end());

    if (!WriteAllLines(path, lines)) {
        result.success = false;
        result.error = "Failed to write file: " + path;
        return result;
    }

    result.success = true;
    result.output = "Replaced lines " + std::to_string(startLine) + "-" + std::to_string(endLine) +
        " (" + std::to_string(linesRemoved) + " lines removed, " +
        std::to_string(newLines.size()) + " lines added). Total lines now: " +
        std::to_string(lines.size());
    return result;
}

// ============================================================================
// Action: deleteLines
// ============================================================================

ToolResult FileTool::HandleDeleteLines(const std::string& path, int startLine, int endLine) {
    ToolResult result;

    std::string readError;
    auto lines = ReadAllLines(path, &readError);
    if (!readError.empty()) {
        result.success = false;
        result.error = readError;
        return result;
    }

    if (startLine < 1 || endLine < startLine || endLine > static_cast<int>(lines.size())) {
        result.success = false;
        result.error = "Invalid line range: " + std::to_string(startLine) + "-" +
            std::to_string(endLine) + " (file has " + std::to_string(lines.size()) + " lines)";
        return result;
    }

    int linesDeleted = endLine - startLine + 1;

    // Erase lines (convert to 0-based)
    lines.erase(lines.begin() + startLine - 1, lines.begin() + endLine);

    if (!WriteAllLines(path, lines)) {
        result.success = false;
        result.error = "Failed to write file: " + path;
        return result;
    }

    result.success = true;
    result.output = "Deleted lines " + std::to_string(startLine) + "-" + std::to_string(endLine) +
        " (" + std::to_string(linesDeleted) + " lines deleted). Total lines now: " +
        std::to_string(lines.size());
    return result;
}

// ============================================================================
// Action: appendLines
// ============================================================================

ToolResult FileTool::HandleAppendLines(const std::string& path, const std::string& content) {
    ToolResult result;

    // Open in append mode — no need to read the file
    // Check if file exists and doesn't end with newline
    bool needPrependNewline = false;
    {
        std::ifstream checkFile(path, std::ios::ate);
        if (checkFile.is_open()) {
            checkFile.seekg(-1, std::ios::end);
            if (checkFile.good()) {
                char lastChar;
                checkFile.get(lastChar);
                if (lastChar != '\n') needPrependNewline = true;
            }
        }
    }

    // Create parent directories if needed
    std::error_code ec;
    auto parentDir = std::filesystem::path(path).parent_path();
    if (!parentDir.empty() && !std::filesystem::exists(parentDir)) {
        std::filesystem::create_directories(parentDir, ec);
    }

    std::ofstream file(path, std::ios::app);
    if (!file.is_open()) {
        result.success = false;
        result.error = "Cannot open file for appending: " + path;
        return result;
    }

    if (needPrependNewline) file << "\n";
    file << content;
    file.close();

    if (file.fail()) {
        result.success = false;
        result.error = "Append failed: " + path;
        return result;
    }

    // Count appended lines
    int appendedLines = 0;
    std::istringstream stream(content);
    std::string line;
    while (std::getline(stream, line)) appendedLines++;

    result.success = true;
    result.output = "Appended " + std::to_string(appendedLines) +
        " lines to: " + path;
    return result;
}

// ============================================================================
// Path utilities
// ============================================================================

std::string FileTool::ResolvePath(const std::string& path, const std::string& workspaceRoot) const {
    std::filesystem::path p(path);
    if (p.is_absolute()) {
        return std::filesystem::weakly_canonical(p).string();
    }
    return std::filesystem::weakly_canonical(std::filesystem::path(workspaceRoot) / p).string();
}

// ============================================================================
// Path policy
// ============================================================================

void FileTool::SetPathAllowlist(const std::vector<std::string>& patterns) {
    m_pathAllowlist = patterns;
}

void FileTool::SetPathDenylist(const std::vector<std::string>& patterns) {
    m_pathDenylist = patterns;
}

void FileTool::SetRestrictToWorkspace(bool restrict) {
    m_restrictToWorkspace = restrict;
}

bool FileTool::IsPathAllowed(
    const std::string& resolvedPath,
    const std::string& workspaceRoot,
    bool restrictToWorkspace,
    const std::vector<std::string>& extraAllowlist,
    const std::vector<std::string>& extraDenylist) const {
    for (const auto& pattern : m_pathDenylist) {
        if (GlobMatch(resolvedPath, pattern)) return false;
    }
    for (const auto& pattern : extraDenylist) {
        if (GlobMatch(resolvedPath, pattern)) return false;
    }
    if (restrictToWorkspace && !workspaceRoot.empty()) {
        if (!IsWithinWorkspace(resolvedPath, workspaceRoot)) return false;
    }
    if (!m_pathAllowlist.empty()) {
        bool allowed = false;
        for (const auto& pattern : m_pathAllowlist) {
            if (GlobMatch(resolvedPath, pattern)) { allowed = true; break; }
        }
        if (!allowed) return false;
    }
    if (!extraAllowlist.empty()) {
        bool allowed = false;
        for (const auto& pattern : extraAllowlist) {
            if (GlobMatch(resolvedPath, pattern)) { allowed = true; break; }
        }
        if (!allowed) return false;
    }
    return true;
}

bool FileTool::GlobMatch(const std::string& text, const std::string& pattern) const {
    const auto n = text.size();
    const auto m = pattern.size();
    std::vector<std::vector<bool>> dp(n + 1, std::vector<bool>(m + 1, false));
    dp[0][0] = true;
    for (size_t j = 1; j <= m && pattern[j - 1] == '*'; ++j) {
        dp[0][j] = true;
    }
    for (size_t i = 1; i <= n; ++i) {
        for (size_t j = 1; j <= m; ++j) {
            if (pattern[j - 1] == '*') {
                dp[i][j] = dp[i][j - 1] || dp[i - 1][j];
            } else if (pattern[j - 1] == '?' || pattern[j - 1] == text[i - 1]) {
                dp[i][j] = dp[i - 1][j - 1];
            }
        }
    }
    return dp[n][m];
}

} // namespace animus::kernel

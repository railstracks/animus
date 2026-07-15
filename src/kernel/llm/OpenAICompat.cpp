#include "animus_kernel/llm/OpenAICompat.h"

#include <sstream>

namespace animus::kernel::llm {
namespace openai_compat {

std::string ExtractJsonString(const std::string& json, const std::string& key) {
  std::string needle = "\"" + key + "\"";
  auto pos = json.find(needle);
  if (pos == std::string::npos) return {};

  pos = json.find(':', pos + needle.size());
  if (pos == std::string::npos) return {};

  pos++;
  while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t' ||
                                json[pos] == '\n' || json[pos] == '\r')) {
    pos++;
  }

  if (pos >= json.size() || json[pos] != '"') return {};
  pos++;

  std::string result;
  while (pos < json.size() && json[pos] != '"') {
    if (json[pos] == '\\' && pos + 1 < json.size()) {
      char next = json[pos + 1];
      switch (next) {
        case '"':  result += '"';  pos += 2; break;
        case '\\': result += '\\'; pos += 2; break;
        case 'n':  result += '\n'; pos += 2; break;
        case 't':  result += '\t'; pos += 2; break;
        case 'r':  result += '\r'; pos += 2; break;
        default:   result += next; pos += 2; break;
      }
    } else {
      result += json[pos];
      pos++;
    }
  }
  return result;
}

std::string ExtractJsonNumber(const std::string& json, const std::string& key) {
  std::string needle = "\"" + key + "\"";
  auto pos = json.find(needle);
  if (pos == std::string::npos) return {};
  pos = json.find(':', pos + needle.size());
  if (pos == std::string::npos) return {};
  pos++;
  while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) pos++;
  size_t start = pos;
  while (pos < json.size() && (json[pos] == '-' || json[pos] == '.' ||
                                (json[pos] >= '0' && json[pos] <= '9') ||
                                json[pos] == 'e' || json[pos] == 'E')) {
    pos++;
  }
  return json.substr(start, pos - start);
}

std::string EscapeJson(const std::string& s) {
  std::string out;
  out.reserve(s.size() + 8);
  for (char c : s) {
    switch (c) {
      case '"':  out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n";  break;
      case '\t': out += "\\t";  break;
      case '\r': out += "\\r";  break;
      default:   out += c;      break;
    }
  }
  return out;
}

std::string BuildOpenAIRequestBody(const LLMRequest& request) {
  std::ostringstream ss;
  ss << "{\"model\":\"" << EscapeJson(request.model) << "\"";

  ss << ",\"messages\":[";
  for (size_t i = 0; i < request.messages.size(); ++i) {
    if (i > 0) ss << ",";
    const auto& msg = request.messages[i];
    ss << "{\"role\":\"" << EscapeJson(msg.role) << "\"";

    // Emit content as array (multi-modal) or string (plain text)
    if (msg.HasContentParts()) {
      ss << ",\"content\":[";
      for (size_t p = 0; p < msg.content_parts.size(); ++p) {
        if (p > 0) ss << ",";
        const auto& part = msg.content_parts[p];
        if (part.type == "image_url") {
          ss << "{\"type\":\"image_url\",\"image_url\":{\"url\":\""
             << EscapeJson(part.image_url) << "\"";
          if (!part.detail.empty()) {
            ss << ",\"detail\":\"" << EscapeJson(part.detail) << "\"";
          }
          ss << "}}";
        } else {
          // text part (default)
          ss << "{\"type\":\"text\",\"text\":\""
             << EscapeJson(part.text.empty() ? part.type : part.text)
             << "\"}";
          // If type was explicitly "image_url" but image_url is empty, still emit text
        }
      }
      ss << "]";
    } else {
      ss << ",\"content\":\"" << EscapeJson(msg.content) << "\"";
    }

    // Add tool_calls for assistant messages
    if (msg.role == "assistant" && !msg.tool_calls.empty()) {
      ss << ",\"tool_calls\":[";
      for (size_t j = 0; j < msg.tool_calls.size(); ++j) {
        if (j > 0) ss << ",";
        const auto& tc = msg.tool_calls[j];
        ss << "{\"id\":\"" << EscapeJson(tc.id)
              << "\",\"type\":\"function\",\"function\":{"
              << "\"name\":\"" << EscapeJson(tc.name)
              << "\",\"arguments\":\"" << EscapeJson(tc.arguments)
              << "\"}}";
      }
      ss << "]";
    }

    // Add tool_call_id for tool result messages
    if (!msg.tool_call_id.empty()) {
      ss << ",\"tool_call_id\":\"" << EscapeJson(msg.tool_call_id) << "\"";
    }
    // Add name for tool result messages
    if (!msg.name.empty()) {
      ss << ",\"name\":\"" << EscapeJson(msg.name) << "\"";
    }
    ss << "}";
  }
  ss << "]";

  ss << ",\"temperature\":" << request.temperature;

  if (request.max_tokens > 0) {
    ss << ",\"max_tokens\":" << request.max_tokens;
  }

  ss << ",\"stream\":" << (request.stream ? "true" : "false");

  // Add tool definitions if present
  if (!request.tools.empty()) {
    ss << ",\"tools\":[";
    for (size_t i = 0; i < request.tools.size(); ++i) {
      if (i > 0) ss << ",";
      const auto& tool = request.tools[i];
      ss << "{\"type\":\"" << EscapeJson(tool.type) << "\""
         << ",\"function\":{"
         << "\"name\":\"" << EscapeJson(tool.name) << "\""
         << ",\"description\":\"" << EscapeJson(tool.description) << "\""
         << ",\"parameters\":" << tool.parameters
         << "}}";
    }
    ss << "]";

    // Add tool_choice
    if (!request.tool_choice.empty()) {
      if (request.tool_choice == "auto" || request.tool_choice == "none" || request.tool_choice == "required") {
        ss << ",\"tool_choice\":\"" << request.tool_choice << "\"";
      } else {
        // Specific tool name
        ss << ",\"tool_choice\":{\"type\":\"function\",\"function\":{\"name\":\""
           << EscapeJson(request.tool_choice) << "\"}}";
      }
    }
  }

  ss << "}";
  return ss.str();
}

// ============================================================================
// Tool call parsing from OpenAI-compatible responses
// ============================================================================

std::vector<LLMToolCall> ParseToolCalls(const std::string& body) {
  std::vector<LLMToolCall> calls;
  // Find "tool_calls" array in the response
  auto tcPos = body.find("\"tool_calls\"");
  if (tcPos == std::string::npos) return calls;

  // Find the array start
  auto arrStart = body.find('[', tcPos);
  if (arrStart == std::string::npos) return calls;

  // Simple state machine to parse the tool_calls array
  size_t pos = arrStart + 1;
  int depth = 1;

  while (pos < body.size() && depth > 0) {
    // Find each object in the array
    auto objStart = body.find('{', pos);
    if (objStart == std::string::npos) break;

    // Find matching closing brace
    int objDepth = 0;
    size_t objEnd = objStart;
    while (objEnd < body.size()) {
      if (body[objEnd] == '{') objDepth++;
      else if (body[objEnd] == '}') {
        objDepth--;
        if (objDepth == 0) break;
      }
      objEnd++;
    }
    if (objDepth != 0) break;

    std::string obj = body.substr(objStart, objEnd - objStart + 1);

    LLMToolCall call;
    call.id = ExtractJsonString(obj, "id");

    // Navigate into the "function" object
    auto funcPos = obj.find("\"function\"");
    if (funcPos != std::string::npos) {
      std::string funcPart = obj.substr(funcPos);
      call.name = ExtractJsonString(funcPart, "name");
      call.arguments = ExtractJsonString(funcPart, "arguments");
    }

    if (!call.id.empty() && !call.name.empty()) {
      calls.push_back(std::move(call));
    }

    pos = objEnd + 1;

    // Skip past comma
    while (pos < body.size() && (body[pos] == ' ' || body[pos] == ',' ||
                                  body[pos] == '\t' || body[pos] == '\n')) {
      pos++;
    }

    // Check if we hit the closing bracket
    if (pos < body.size() && body[pos] == ']') break;
  }

  return calls;
}

std::string ExtractFinishReason(const std::string& body) {
  // Look for finish_reason in the response
  auto frPos = body.find("\"finish_reason\"");
  if (frPos == std::string::npos) return "stop";

  // Extract the value after the colon
  auto colonPos = body.find(':', frPos);
  if (colonPos == std::string::npos) return "stop";

  auto valStart = colonPos + 1;
  while (valStart < body.size() && (body[valStart] == ' ' || body[valStart] == '\t')) {
    valStart++;
  }

  if (valStart < body.size() && body[valStart] == '"') {
    valStart++;
    auto valEnd = body.find('"', valStart);
    if (valEnd != std::string::npos) {
      return body.substr(valStart, valEnd - valStart);
    }
  }

  return "stop";
}

// ============================================================================
// Shared helpers for reasoning_content providers (Z.ai, Alibaba, DeepSeek)
// ============================================================================

LLMMessage ParseResponseWithReasoning(const std::string& body,
                                       std::string* error) {
  auto choicesPos = body.find("\"choices\"");
  if (choicesPos == std::string::npos) {
    auto errMsg = ExtractJsonString(body, "message");
    if (error) *error = errMsg.empty() ? "No choices in response" : errMsg;
    return {};
  }

  LLMMessage msg;
  msg.role = "assistant";
  msg.content = ExtractJsonString(body.substr(choicesPos), "content");
  msg.thinking_content =
      ExtractJsonString(body.substr(choicesPos), "reasoning_content");
  return msg;
}

std::optional<LLMToken> ParseSSELineWithReasoning(
    const std::string& line) {

  // Check for usage stats (OpenAI-compatible providers send these in the
  // final SSE chunk, sometimes as a separate message with empty choices).
  auto ptStr = ExtractJsonNumber(line, "prompt_tokens");
  auto ctStr = ExtractJsonNumber(line, "completion_tokens");
  if (!ptStr.empty() || !ctStr.empty()) {
    LLMToken token;
    token.prompt_tokens = std::atoi(ptStr.c_str());
    token.completion_tokens = std::atoi(ctStr.c_str());
    return token;
  }

  // Check for reasoning_content (thinking mode)
  auto reasoningContent = ExtractJsonString(line, "reasoning_content");
  if (!reasoningContent.empty()) {
    LLMToken token;
    token.content = reasoningContent;
    token.is_thinking = true;
    return token;
  }

  // Standard content + finish reason
  auto frStop = line.find("\"finish_reason\":\"stop\"");
  auto frToolCalls = line.find("\"finish_reason\":\"tool_calls\"");
  bool isFinal = (frStop != std::string::npos ||
                  frToolCalls != std::string::npos);

  auto content = ExtractJsonString(line, "content");

  LLMToken token;
  token.content = content;
  token.is_final = isFinal;

  if (frStop != std::string::npos) {
    token.finish_reason = "stop";
  } else if (frToolCalls != std::string::npos) {
    token.finish_reason = "tool_calls";
    token.tool_calls = ParseToolCalls(line);
  }

  if (content.empty() && !isFinal) return std::nullopt;
  return token;
}

} // namespace openai_compat
} // namespace animus::kernel::llm
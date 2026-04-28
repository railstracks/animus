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
    ss << "{\"role\":\"" << EscapeJson(request.messages[i].role)
       << "\",\"content\":\"" << EscapeJson(request.messages[i].content)
       << "\"}";
  }
  ss << "]";

  ss << ",\"temperature\":" << request.temperature;

  if (request.max_tokens > 0) {
    ss << ",\"max_tokens\":" << request.max_tokens;
  }

  ss << ",\"stream\":" << (request.stream ? "true" : "false");
  ss << "}";
  return ss.str();
}

} // namespace openai_compat
} // namespace animus::kernel::llm

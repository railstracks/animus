#pragma once

#include "animus_kernel/tools/ToolRegistry.h"

#include <string>

namespace animus::kernel {

// ============================================================================
// CalculatorTool — deterministic arithmetic and unit conversion
//
// Recursive-descent expression parser. No external dependencies.
// Ticket 097.
// ============================================================================

class CalculatorTool : public IToolHandler {
public:
    ToolDefinition GetDefinition() const override;
    ToolResult Execute(const ToolCall& call) override;

private:
    // Expression parser (recursive descent)
    // Grammar:
    //   expr    → term (('+' | '-') term)*
    //   term    → factor (('*' | '/' | '%') factor)*
    //   factor  → power ('^' | '**') factor       // right-assoc
    //   power   → unary
    //   unary   → ('-' | '+') unary | primary
    //   primary → number | constant | func '(' args ')' | '(' expr ')'

    struct ParseContext {
        const std::string& src;
        size_t pos{0};
        int depth{0};

        explicit ParseContext(const std::string& s) : src(s) {}

        void skipWs() {
            while (pos < src.size() && std::isspace(static_cast<unsigned char>(src[pos]))) pos++;
        }

        bool match(char c) {
            skipWs();
            if (pos < src.size() && src[pos] == c) { pos++; return true; }
            return false;
        }

        bool matchStr(const std::string& s) {
            skipWs();
            if (pos + s.size() <= src.size()) {
                for (size_t i = 0; i < s.size(); i++) {
                    if (std::tolower(static_cast<unsigned char>(src[pos + i])) !=
                        std::tolower(static_cast<unsigned char>(s[i]))) return false;
                }
                // Check word boundary
                if (pos + s.size() < src.size()) {
                    char next = src[pos + s.size()];
                    if (std::isalnum(static_cast<unsigned char>(next)) || next == '_') return false;
                }
                pos += s.size();
                return true;
            }
            return false;
        }

        char peek() {
            skipWs();
            return pos < src.size() ? src[pos] : '\0';
        }
    };

    double EvalExpression(ParseContext& ctx, std::string& err);
    double EvalTerm(ParseContext& ctx, std::string& err);
    double EvalFactor(ParseContext& ctx, std::string& err);
    double EvalUnary(ParseContext& ctx, std::string& err);
    double EvalPrimary(ParseContext& ctx, std::string& err);
    double EvalFunction(const std::string& name, ParseContext& ctx, std::string& err);

    // Unit conversion
    double Convert(double value, const std::string& from, const std::string& to, std::string& err);
};

} // namespace animus::kernel

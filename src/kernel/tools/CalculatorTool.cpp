#include "animus_kernel/tools/CalculatorTool.h"

#include <json/json.h>
#include <json/writer.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <sstream>
#include <unordered_map>

namespace animus::kernel {

namespace {
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

std::string ToJson(const Json::Value& v) {
    Json::StreamWriterBuilder wb;
    wb.settings_["indentation"] = "";
    return Json::writeString(wb, v);
}

std::string FormatNum(double v) {
    // Trim trailing zeros
    if (std::isnan(v)) return "nan";
    if (std::isinf(v)) return v > 0 ? "inf" : "-inf";
    std::ostringstream ss;
    ss.precision(10);
    ss << v;
    return ss.str();
}
} // anonymous namespace

// ============================================================================
// Tool definition
// ============================================================================

ToolDefinition CalculatorTool::GetDefinition() const {
    ToolDefinition def;
    def.name = "calculator";
    def.description =
        "Deterministic calculator for arithmetic and mathematical expressions. "
        "Evaluates expressions with a real parser — no LLM math errors. "
        "Also supports unit conversion.";
    def.resultMode = ToolResultMode::deliver_to_model;

    def.parameters.push_back({
        "action", "string",
        "Operation: eval (evaluate expression) or convert (unit conversion)",
        true, "", {"eval", "convert"}
    });
    def.parameters.push_back({
        "expression", "string",
        "Mathematical expression to evaluate (for eval). "
        "Supports: + - * / % ^, parentheses, functions (sqrt, sin, cos, log, etc.), "
        "constants (pi, e, tau). Max 500 chars.",
        false
    });
    def.parameters.push_back({
        "value", "number",
        "Value to convert (for convert)",
        false
    });
    def.parameters.push_back({
        "from", "string",
        "Source unit for conversion (e.g. km, lbs, fahrenheit)",
        false
    });
    def.parameters.push_back({
        "to", "string",
        "Target unit for conversion (e.g. miles, kg, celsius)",
        false
    });

    return def;
}

// ============================================================================
// Expression parser — recursive descent
// ============================================================================

double CalculatorTool::EvalExpression(ParseContext& ctx, std::string& err) {
    if (++ctx.depth > 50) { err = "expression too deeply nested (max 50 levels)"; return 0; }

    double result = EvalTerm(ctx, err);
    if (!err.empty()) return 0;

    while (true) {
        ctx.skipWs();
        if (ctx.match('+')) {
            double rhs = EvalTerm(ctx, err);
            if (!err.empty()) return 0;
            result += rhs;
        } else if (ctx.match('-')) {
            double rhs = EvalTerm(ctx, err);
            if (!err.empty()) return 0;
            result -= rhs;
        } else {
            break;
        }
    }

    ctx.depth--;
    return result;
}

double CalculatorTool::EvalTerm(ParseContext& ctx, std::string& err) {
    double result = EvalFactor(ctx, err);
    if (!err.empty()) return 0;

    while (true) {
        ctx.skipWs();
        if (ctx.match('*')) {
            // Check it's not ** (handled in factor)
            if (ctx.peek() == '*') { ctx.pos--; break; }
            double rhs = EvalFactor(ctx, err);
            if (!err.empty()) return 0;
            result *= rhs;
        } else if (ctx.match('/')) {
            double rhs = EvalFactor(ctx, err);
            if (!err.empty()) return 0;
            if (rhs == 0.0) { err = "division by zero"; return 0; }
            result /= rhs;
        } else if (ctx.match('%')) {
            double rhs = EvalFactor(ctx, err);
            if (!err.empty()) return 0;
            if (rhs == 0.0) { err = "modulo by zero"; return 0; }
            result = std::fmod(result, rhs);
        } else {
            break;
        }
    }
    return result;
}

double CalculatorTool::EvalFactor(ParseContext& ctx, std::string& err) {
    double base = EvalUnary(ctx, err);
    if (!err.empty()) return 0;

    ctx.skipWs();
    // Check for ^ or **
    if (ctx.match('^') || ctx.matchStr("**")) {
        double exp = EvalFactor(ctx, err); // right-associative
        if (!err.empty()) return 0;
        return std::pow(base, exp);
    }
    return base;
}

double CalculatorTool::EvalUnary(ParseContext& ctx, std::string& err) {
    ctx.skipWs();
    if (ctx.match('-')) {
        return -EvalUnary(ctx, err);
    }
    if (ctx.match('+')) {
        return EvalUnary(ctx, err);
    }
    return EvalPrimary(ctx, err);
}

double CalculatorTool::EvalPrimary(ParseContext& ctx, std::string& err) {
    ctx.skipWs();

    if (ctx.pos >= ctx.src.size()) {
        err = "unexpected end of expression";
        return 0;
    }

    // Parenthesized expression
    if (ctx.match('(')) {
        double result = EvalExpression(ctx, err);
        if (!err.empty()) return 0;
        if (!ctx.match(')')) { err = "expected ')'"; return 0; }
        return result;
    }

    // Number
    if (std::isdigit(static_cast<unsigned char>(ctx.src[ctx.pos])) ||
        (ctx.src[ctx.pos] == '.' && ctx.pos + 1 < ctx.src.size() &&
         std::isdigit(static_cast<unsigned char>(ctx.src[ctx.pos + 1])))) {
        size_t start = ctx.pos;
        bool hasDot = false;
        bool hasE = false;
        while (ctx.pos < ctx.src.size()) {
            char c = ctx.src[ctx.pos];
            if (std::isdigit(static_cast<unsigned char>(c))) {
                ctx.pos++;
            } else if (c == '.' && !hasDot && !hasE) {
                hasDot = true;
                ctx.pos++;
            } else if ((c == 'e' || c == 'E') && !hasE) {
                hasE = true;
                ctx.pos++;
                if (ctx.pos < ctx.src.size() && (ctx.src[ctx.pos] == '+' || ctx.src[ctx.pos] == '-'))
                    ctx.pos++;
            } else {
                break;
            }
        }
        std::string numStr = ctx.src.substr(start, ctx.pos - start);
        try {
            return std::stod(numStr);
        } catch (...) {
            err = "invalid number: " + numStr;
            return 0;
        }
    }

    // Identifier — could be constant or function
    if (std::isalpha(static_cast<unsigned char>(ctx.src[ctx.pos])) || ctx.src[ctx.pos] == '_') {
        size_t start = ctx.pos;
        while (ctx.pos < ctx.src.size() &&
               (std::isalnum(static_cast<unsigned char>(ctx.src[ctx.pos])) || ctx.src[ctx.pos] == '_')) {
            ctx.pos++;
        }
        std::string name = ctx.src.substr(start, ctx.pos - start);
        std::transform(name.begin(), name.end(), name.begin(), ::tolower);

        // Check for function call
        ctx.skipWs();
        if (ctx.peek() == '(') {
            return EvalFunction(name, ctx, err);
        }

        // Constants
        if (name == "pi") return M_PI;
        if (name == "e") return M_E;
        if (name == "tau") return 2.0 * M_PI;

        err = "unknown identifier: '" + name + "'. Available constants: pi, e, tau";
        return 0;
    }

    err = "unexpected character: '" + std::string(1, ctx.src[ctx.pos]) + "'";
    return 0;
}

double CalculatorTool::EvalFunction(const std::string& name, ParseContext& ctx, std::string& err) {
    if (!ctx.match('(')) { err = "expected '(' after function name"; return 0; }

    // Collect arguments
    std::vector<double> args;
    if (ctx.peek() != ')') {
        do {
            double val = EvalExpression(ctx, err);
            if (!err.empty()) return 0;
            args.push_back(val);
            ctx.skipWs();
        } while (ctx.match(',') && ctx.peek() != ')');
    }
    if (!ctx.match(')')) { err = "expected ')' after function arguments"; return 0; }

    // Single-argument functions
    auto require1 = [&](const char* fn) -> double {
        if (args.size() != 1) { err = std::string(fn) + " expects 1 argument, got " + std::to_string(args.size()); return 0; }
        return args[0];
    };
    auto require2 = [&](const char* fn) -> std::pair<double,double> {
        if (args.size() != 2) { err = std::string(fn) + " expects 2 arguments, got " + std::to_string(args.size()); return {0,0}; }
        return {args[0], args[1]};
    };

    if (name == "abs") return std::abs(require1("abs"));
    if (name == "ceil") return std::ceil(require1("ceil"));
    if (name == "floor") return std::floor(require1("floor"));
    if (name == "sqrt") return std::sqrt(require1("sqrt"));
    if (name == "cbrt") return std::cbrt(require1("cbrt"));
    if (name == "log" || name == "ln") return std::log(require1("log"));
    if (name == "log10") return std::log10(require1("log10"));
    if (name == "log2") return std::log2(require1("log2"));
    if (name == "exp") return std::exp(require1("exp"));
    if (name == "sin") return std::sin(require1("sin"));
    if (name == "cos") return std::cos(require1("cos"));
    if (name == "tan") return std::tan(require1("tan"));
    if (name == "asin") return std::asin(require1("asin"));
    if (name == "acos") return std::acos(require1("acos"));
    if (name == "atan") return std::atan(require1("atan"));

    if (name == "round") {
        if (args.size() == 1) return std::round(args[0]);
        if (args.size() == 2) {
            double factor = std::pow(10, args[1]);
            return std::round(args[0] * factor) / factor;
        }
        err = "round expects 1 or 2 arguments";
        return 0;
    }

    if (name == "pow") { auto [a,b] = require2("pow"); return std::pow(a, b); }
    if (name == "atan2") { auto [a,b] = require2("atan2"); return std::atan2(a, b); }
    if (name == "gcd") {
        auto [a,b] = require2("gcd");
        long long ia = std::llround(a), ib = std::llround(b);
        if (ia < 0) ia = -ia;
        if (ib < 0) ib = -ib;
        while (ib) { long long t = ia % ib; ia = ib; ib = t; }
        return static_cast<double>(ia);
    }
    if (name == "lcm") {
        auto [a,b] = require2("lcm");
        long long ia = std::llround(a), ib = std::llround(b);
        if (ia == 0 || ib == 0) return 0;
        long long ga = std::abs(ia), gb = std::abs(ib);
        while (gb) { long long t = ga % gb; ga = gb; gb = t; }
        return static_cast<double>((std::abs(ia) / ga) * std::abs(ib));
    }
    if (name == "min") {
        if (args.empty()) { err = "min requires at least 1 argument"; return 0; }
        return *std::min_element(args.begin(), args.end());
    }
    if (name == "max") {
        if (args.empty()) { err = "max requires at least 1 argument"; return 0; }
        return *std::max_element(args.begin(), args.end());
    }
    if (name == "clamp") {
        if (args.size() != 3) { err = "clamp expects 3 arguments (value, min, max)"; return 0; }
        return std::clamp(args[0], args[1], args[2]);
    }

    err = "unknown function: '" + name + "'";
    return 0;
}

// ============================================================================
// Unit conversion
// ============================================================================

double CalculatorTool::Convert(double value, const std::string& from,
                                const std::string& to, std::string& err) {
    // Normalize
    auto lower = [](std::string s) {
        std::transform(s.begin(), s.end(), s.begin(), ::tolower);
        return s;
    };
    std::string f = lower(from);
    std::string t = lower(to);

    // Temperature (special — offset + scale)
    static const std::unordered_map<std::string, std::pair<double,double>> tempFactors = {
        {"celsius",    {1.0, 273.15}},
        {"c",          {1.0, 273.15}},
        {"fahrenheit", {5.0/9.0, 255.372222}},
        {"f",          {5.0/9.0, 255.372222}},
        {"kelvin",     {1.0, 0.0}},
        {"k",          {1.0, 0.0}},
    };

    if (tempFactors.count(f) && tempFactors.count(t)) {
        double kelvin = value * tempFactors.at(f).first + tempFactors.at(f).second;
        return (kelvin - tempFactors.at(t).second) / tempFactors.at(t).first;
    }

    // If one is temp and other isn't
    if (tempFactors.count(f) != tempFactors.count(t)) {
        err = "cannot convert between temperature and non-temperature units";
        return 0;
    }

    // Linear conversions (through base unit)
    // Category → {unit → factor_to_base}
    static const std::vector<std::pair<std::string,
        std::unordered_map<std::string, double>>> categories = {

        {"length", {
            {"m", 1.0}, {"meter", 1.0}, {"meters", 1.0},
            {"km", 1000.0}, {"kilometer", 1000.0},
            {"cm", 0.01}, {"centimeter", 0.01},
            {"mm", 0.001}, {"millimeter", 0.001},
            {"mile", 1609.344}, {"miles", 1609.344},
            {"yard", 0.9144}, {"yards", 0.9144}, {"yd", 0.9144},
            {"foot", 0.3048}, {"feet", 0.3048}, {"ft", 0.3048},
            {"inch", 0.0254}, {"inches", 0.0254}, {"in", 0.0254},
            {"nautical_mile", 1852.0}, {"nmi", 1852.0},
        }},

        {"weight", {
            {"kg", 1.0}, {"kilogram", 1.0}, {"kilos", 1.0},
            {"g", 0.001}, {"gram", 0.001}, {"grams", 0.001},
            {"lb", 0.45359237}, {"lbs", 0.45359237}, {"pound", 0.45359237}, {"pounds", 0.45359237},
            {"oz", 0.028349523125}, {"ounce", 0.028349523125}, {"ounces", 0.028349523125},
            {"ton", 1000.0}, {"tonne", 1000.0}, {"metric_ton", 1000.0},
            {"ton_us", 907.18474},
        }},

        {"time", {
            {"second", 1.0}, {"seconds", 1.0}, {"sec", 1.0}, {"s", 1.0},
            {"minute", 60.0}, {"minutes", 60.0}, {"min", 60.0},
            {"hour", 3600.0}, {"hours", 3600.0}, {"hr", 3600.0}, {"h", 3600.0},
            {"day", 86400.0}, {"days", 86400.0}, {"d", 86400.0},
            {"week", 604800.0}, {"weeks", 604800.0},
            {"month", 2592000.0}, {"months", 2592000.0},
            {"year", 31536000.0}, {"years", 31536000.0}, {"yr", 31536000.0},
        }},

        {"data", {
            {"byte", 1.0}, {"bytes", 1.0}, {"b", 1.0},
            {"kb", 1000.0}, {"kilobyte", 1000.0},
            {"mb", 1e6}, {"megabyte", 1e6},
            {"gb", 1e9}, {"gigabyte", 1e9},
            {"tb", 1e12}, {"terabyte", 1e12},
            {"kib", 1024.0}, {"kibibyte", 1024.0},
            {"mib", 1048576.0}, {"mebibyte", 1048576.0},
            {"gib", 1073741824.0}, {"gibibyte", 1073741824.0},
            {"tib", 1099511627776.0}, {"tebibyte", 1099511627776.0},
        }},

        {"speed", {
            {"mps", 1.0}, {"m/s", 1.0},
            {"kmh", 0.277778}, {"km/h", 0.277778}, {"kph", 0.277778},
            {"mph", 0.44704}, {"mi/h", 0.44704},
            {"knot", 0.514444}, {"kn", 0.514444},
        }},
    };

    for (const auto& [catName, units] : categories) {
        if (units.count(f) && units.count(t)) {
            double baseValue = value * units.at(f);
            return baseValue / units.at(t);
        }
        if (units.count(f) != units.count(t)) {
            err = "cannot convert between '" + from + "' and '" + to + "' (different categories)";
            return 0;
        }
    }

    err = "unknown unit: '" + from + "' or '" + to + "'";
    return 0;
}

// ============================================================================
// Execute
// ============================================================================

ToolResult CalculatorTool::Execute(const ToolCall& call) {
    ToolResult result;
    result.call_id = call.id;

    auto args = ParseArgs(call.arguments);
    if (args.isNull()) {
        result.success = false;
        result.error = "failed to parse arguments";
        return result;
    }

    std::string action = args.isMember("action") && args["action"].isString()
        ? args["action"].asString() : "eval";

    if (action == "eval") {
        if (!args.isMember("expression") || !args["expression"].isString()) {
            result.success = false;
            result.error = "expression is required for eval action";
            return result;
        }

        std::string expr = args["expression"].asString();
        if (expr.size() > 500) {
            result.success = false;
            result.error = "expression exceeds 500 character limit";
            return result;
        }

        ParseContext ctx(expr);
        std::string err;
        double val = EvalExpression(ctx, err);

        if (!err.empty()) {
            result.success = false;
            result.error = err;
            return result;
        }

        // Check for trailing characters
        ctx.skipWs();
        if (ctx.pos < expr.size()) {
            result.success = false;
            result.error = "unexpected trailing characters at position " +
                           std::to_string(ctx.pos) + ": '" + expr.substr(ctx.pos) + "'";
            return result;
        }

        Json::Value output(Json::objectValue);
        output["expression"] = expr;
        output["result"] = FormatNum(val);
        result.success = true;
        result.output = ToJson(output);
        return result;
    }

    if (action == "convert") {
        if (!args.isMember("value") || !args["value"].isNumeric()) {
            result.success = false;
            result.error = "value (number) is required for convert action";
            return result;
        }
        if (!args.isMember("from") || !args["from"].isString()) {
            result.success = false;
            result.error = "from (source unit) is required for convert action";
            return result;
        }
        if (!args.isMember("to") || !args["to"].isString()) {
            result.success = false;
            result.error = "to (target unit) is required for convert action";
            return result;
        }

        double value = args["value"].asDouble();
        std::string from = args["from"].asString();
        std::string to = args["to"].asString();
        std::string err;
        double converted = Convert(value, from, to, err);

        if (!err.empty()) {
            result.success = false;
            result.error = err;
            return result;
        }

        Json::Value output(Json::objectValue);
        output["value"] = value;
        output["from"] = from;
        output["to"] = to;
        output["result"] = FormatNum(converted);
        result.success = true;
        result.output = ToJson(output);
        return result;
    }

    result.success = false;
    result.error = "unknown action: " + action + " (supported: eval, convert)";
    return result;
}

} // namespace animus::kernel

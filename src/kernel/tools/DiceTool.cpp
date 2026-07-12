#include "animus_kernel/tools/DiceTool.h"

#include <json/json.h>
#include <json/writer.h>
#include <regex>
#include <sstream>

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
} // anonymous namespace

// ============================================================================
// Tool definition
// ============================================================================

ToolDefinition DiceTool::GetDefinition() const {
    ToolDefinition def;
    def.name = "dice";
    def.description =
        "Roll dice using standard notation (NdS±M). Supports seeded rolls "
        "for reproducible results. Returns raw numerical results.";
    def.resultMode = ToolResultMode::deliver_to_model;

    def.parameters.push_back({
        "action", "string",
        "The dice operation: roll",
        true, "", {"roll"}
    });
    def.parameters.push_back({
        "notation", "string",
        "Dice notation: NdS±M (e.g. d6, 2d10, 3d8+4, 1d20-2). N defaults to 1.",
        true
    });
    def.parameters.push_back({
        "count", "integer",
        "Number of times to roll the full expression (default 1)",
        false
    });
    def.parameters.push_back({
        "seed", "string",
        "Optional seed for reproducible rolls. Same seed + notation + count = same results.",
        false
    });

    return def;
}

// ============================================================================
// Notation parsing
// ============================================================================

DiceTool::DiceNotation DiceTool::ParseNotation(const std::string& notation) const {
    DiceNotation result;

    // ^(\d*)d(\d+)([+-]\d+)?$
    std::regex re(R"(^(\d*)d(\d+)([+-]\d+)?$)", std::regex::icase);
    std::smatch match;
    if (!std::regex_match(notation, match, re)) {
        result.error = "invalid notation: '" + notation + "'. Expected format: NdS±M (e.g. 2d6, d20, 3d8+4)";
        return result;
    }

    // N (count of dice) — empty means 1
    if (match[1].str().empty()) {
        result.count = 1;
    } else {
        result.count = std::stoi(match[1].str());
        if (result.count < 1 || result.count > 1000) {
            result.error = "dice count must be between 1 and 1000";
            return result;
        }
    }

    // S (sides per die)
    result.sides = std::stoi(match[2].str());
    if (result.sides < 1 || result.sides > 100000) {
        result.error = "dice sides must be between 1 and 100000";
        return result;
    }

    // ±M (modifier)
    if (match[3].matched && !match[3].str().empty()) {
        result.modifier = std::stoi(match[3].str());
    }

    result.valid = true;
    return result;
}

// ============================================================================
// Seed hashing (FNV-1a)
// ============================================================================

std::uint64_t DiceTool::HashSeed(const std::string& seed) const {
    std::uint64_t hash = 14695981039346656037ULL;
    for (char c : seed) {
        hash ^= static_cast<std::uint64_t>(static_cast<unsigned char>(c));
        hash *= 1099511628211ULL;
    }
    return hash;
}

// ============================================================================
// Execute
// ============================================================================

ToolResult DiceTool::Execute(const ToolCall& call) {
    ToolResult result;
    result.call_id = call.id;

    auto args = ParseArgs(call.arguments);
    if (args.isNull()) {
        result.success = false;
        result.error = "failed to parse arguments";
        return result;
    }

    std::string action = args.isMember("action") && args["action"].isString()
        ? args["action"].asString() : "roll";

    if (action != "roll") {
        result.success = false;
        result.error = "unknown action: " + action + " (supported: roll)";
        return result;
    }

    if (!args.isMember("notation") || !args["notation"].isString()) {
        result.success = false;
        result.error = "notation is required (e.g. '2d6', 'd20+5')";
        return result;
    }

    std::string notationStr = args["notation"].asString();
    auto notation = ParseNotation(notationStr);
    if (!notation.valid) {
        result.success = false;
        result.error = notation.error;
        return result;
    }

    int count = 1;
    if (args.isMember("count") && args["count"].isInt()) {
        count = args["count"].asInt();
        if (count < 1 || count > 10000) {
            result.success = false;
            result.error = "count must be between 1 and 10000";
            return result;
        }
    }

    // Set up RNG
    std::mt19937_64 rng;
    bool hasSeed = args.isMember("seed") && args["seed"].isString();
    std::string seedStr;
    if (hasSeed) {
        seedStr = args["seed"].asString();
        rng.seed(HashSeed(seedStr));
    } else {
        std::random_device rd;
        rng.seed(rd());
    }

    // Roll
    std::uniform_int_distribution<int> dist(1, notation.sides);

    Json::Value resultsArray(Json::arrayValue);

    for (int c = 0; c < count; c++) {
        Json::Value rollEntry(Json::objectValue);
        Json::Value rolls(Json::arrayValue);

        int sum = 0;
        for (int d = 0; d < notation.count; d++) {
            int roll = dist(rng);
            rolls.append(roll);
            sum += roll;
        }

        rollEntry["rolls"] = rolls;
        rollEntry["sum"] = sum;
        rollEntry["modifier"] = notation.modifier;
        rollEntry["total"] = sum + notation.modifier;
        resultsArray.append(rollEntry);
    }

    Json::Value output(Json::objectValue);
    output["notation"] = notationStr;
    output["results"] = resultsArray;
    output["count"] = count;
    if (hasSeed) {
        output["seed_used"] = seedStr;
    }

    result.success = true;
    result.output = ToJson(output);
    return result;
}

} // namespace animus::kernel

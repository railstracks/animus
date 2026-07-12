#pragma once

#include "animus_kernel/tools/ToolRegistry.h"
#include "animus_kernel/tools/HttpClient.h"

#include <string>
#include <map>

namespace Json { class Value; }

namespace animus::kernel {

class AgentConfigStore;

// ============================================================================
// EmailTool — agent-facing email tool backed by AgentMail
//
// Operations: list_inboxes, list_threads, get_thread, get_message,
// get_attachment, search, send, reply, forward, mark_read, delete,
// list_drafts, create_draft, send_draft
//
// Available in session_types = {"default"} (excluded from consolidation).
// Also available in email:chat auto-reply sessions.
// ============================================================================

class EmailTool : public IToolHandler {
public:
    explicit EmailTool(HttpClient& httpClient, AgentConfigStore* configStore);

    ToolDefinition GetDefinition() const override;
    ToolResult Execute(const ToolCall& call) override;

private:
    // --- AgentMail REST API helpers ---
    HttpClient::Response AgentMailGet(const std::string& inboxId,
                                       const std::string& path,
                                       const std::string& apiKey,
                                       const std::map<std::string, std::string>& query = {});
    HttpClient::Response AgentMailPost(const std::string& inboxId,
                                        const std::string& path,
                                        const std::string& body,
                                        const std::string& apiKey);
    HttpClient::Response AgentMailPut(const std::string& inboxId,
                                       const std::string& path,
                                       const std::string& body,
                                       const std::string& apiKey);
    HttpClient::Response AgentMailDelete(const std::string& inboxId,
                                          const std::string& path,
                                          const std::string& apiKey);

    // Resolve inbox_id + api_key from config store for a given account name.
    // Returns true if found, fills out params.
    bool ResolveAccount(const std::string& accountName,
                        std::string& outInboxId,
                        std::string& outApiKey);

    // Resolve by inbox_id when the agent passes an email address instead of channel name.
    bool ResolveAccountByInboxId(const std::string& inboxId,
                                 std::string& outInboxId,
                                 std::string& outApiKey);

    // Find the first AgentMail account in config store.
    bool FindFirstAgentMailAccount(std::string& outInboxId,
                                   std::string& outApiKey);

    // --- Action handlers ---
    std::string HandleListInboxes(const Json::Value& args);
    std::string HandleListThreads(const Json::Value& args);
    std::string HandleGetThread(const Json::Value& args);
    std::string HandleGetMessage(const Json::Value& args);
    std::string HandleGetAttachment(const Json::Value& args);
    std::string HandleSearch(const Json::Value& args);
    std::string HandleSend(const Json::Value& args);
    std::string HandleReply(const Json::Value& args);
    std::string HandleForward(const Json::Value& args);
    std::string HandleMarkRead(const Json::Value& args);
    std::string HandleDelete(const Json::Value& args);
    std::string HandleListDrafts(const Json::Value& args);
    std::string HandleCreateDraft(const Json::Value& args);
    std::string HandleSendDraft(const Json::Value& args);

    HttpClient& m_httpClient;
    AgentConfigStore* m_configStore;
};

} // namespace animus::kernel

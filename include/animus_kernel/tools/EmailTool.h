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

    // #211: pure client-side reply-recipient resolution — explicit `to`
    // arg wins; otherwise the anchor's envelope decides (own-sent anchor
    // -> its To:, the original recipient; received anchor -> its From:).
    // Returns false + error when unresolvable — never guesses. The old
    // server-side thread-ownership path silently self-addressed replies
    // anchored on the sender's own sent messages.
    // Pure: no HTTP, unit-testable against anchor JSON fixtures.
    static bool ResolveReplyRecipient(const Json::Value& args,
                                      const Json::Value& anchor,
                                      bool anchorFetched,
                                      const std::string& selfInboxId,
                                      std::string& outRecipient,
                                      std::string& outError);

    // --- #212: attachment support (pure, unit-testable) ---
    // Normalizes the optional attachments param for send: array of
    // {filename, content(base64), content_type?}. Rejects malformed
    // entries loudly. Absent param -> empty array, no error.
    static bool NormalizeAttachments(const Json::Value& args,
                                     Json::Value& outAttachments,
                                     std::string& outError);

    // Counts attachments on the sent message inside a thread-GET body.
    // Send responses can echo "attachments": 0 despite success (observed
    // live 2026-10-22) — the thread is the receipt. -1 = message absent.
    static int CountConfirmedAttachments(const Json::Value& threadBody,
                                         const std::string& messageId);

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
    std::string HandleListAttachments(const Json::Value& args);
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

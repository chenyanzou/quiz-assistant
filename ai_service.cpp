#include "ai_service.hpp"
#include "lib/json.hpp"
#include <curl/curl.h>
#include <iostream>
#include <sstream>
#include <stdexcept>

using json = nlohmann::json;

namespace ai {

// ============================================================
// Helper: callback for curl to write response data into a string
// ============================================================
static size_t writeCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t total = size * nmemb;
    static_cast<std::string*>(userp)->append(static_cast<char*>(contents), total);
    return total;
}

// ============================================================
// chatCompletion — core function
// ============================================================
std::string chatCompletion(const AIConfig& config,
    const std::string& system_prompt,
    const std::string& user_message)
{
    // --- Guard: empty base_url ---
    if (config.base_url.empty()) {
        return "AI服务未配置，请在AI设置中配置API密钥。";
    }

    // --- Build request body ---
    json requestBody;
    requestBody["model"] = config.model.empty() ? "gpt-3.5-turbo" : config.model;
    requestBody["messages"] = json::array({
        {{"role", "system"}, {"content", system_prompt}},
        {{"role", "user"},   {"content", user_message}}
    });
    requestBody["temperature"] = 0.7;
    requestBody["max_tokens"]  = 2048;

    std::string bodyStr = requestBody.dump();

    // --- Construct URL ---
    std::string url = config.base_url;
    // Remove trailing slash if present
    while (!url.empty() && url.back() == '/') {
        url.pop_back();
    }
    url += "/chat/completions";

    // --- Initialize curl ---
    CURL* curl = curl_easy_init();
    if (!curl) {
        return "AI服务调用失败：无法初始化网络连接。";
    }

    std::string responseStr;
    std::string errorBuffer(CURL_ERROR_SIZE, '\0');
    struct curl_slist* headers = nullptr;

    // Build authorization header
    std::string authHeader = "Authorization: Bearer " + config.api_key;

    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, authHeader.c_str());

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, bodyStr.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(bodyStr.size()));
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &responseStr);
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, &errorBuffer[0]);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 90L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 30L);

    // For development: allow self-signed certs if needed
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);

    // --- Perform request ---
    CURLcode res = curl_easy_perform(curl);

    // --- Check HTTP status code ---
    long httpCode = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);

    // --- Cleanup ---
    curl_easy_cleanup(curl);
    curl_slist_free_all(headers);

    // --- Error handling: curl failure ---
    if (res != CURLE_OK) {
        std::string errMsg(errorBuffer.c_str());
        if (errMsg.empty()) {
            errMsg = curl_easy_strerror(res);
        }
        return "AI服务网络错误: " + errMsg;
    }

    // --- Error handling: non-200 ---
    if (httpCode != 200) {
        std::ostringstream oss;
        oss << "AI服务返回错误 (HTTP " << httpCode << ")";
        if (!responseStr.empty()) {
            // Try to extract a readable error from the response
            try {
                json errJson = json::parse(responseStr);
                if (errJson.contains("error") && errJson["error"].is_object()) {
                    if (errJson["error"].contains("message")) {
                        oss << ": " << errJson["error"]["message"].get<std::string>();
                    }
                }
            } catch (...) {
                // If we can't parse, just include a truncated body
                std::string preview = responseStr.substr(0, 200);
                oss << ": " << preview;
            }
        }
        return oss.str();
    }

    // --- Parse response JSON ---
    json responseJson;
    try {
        responseJson = json::parse(responseStr);
    } catch (const json::parse_error& e) {
        return std::string("AI服务响应解析失败: ") + e.what();
    }

    // --- Extract content ---
    try {
        std::string content = responseJson["choices"][0]["message"]["content"].get<std::string>();
        return content;
    } catch (const json::exception& e) {
        return std::string("AI服务响应格式异常: ") + e.what();
    }
}

// ============================================================
// analyzeQuestion — detailed analysis of a question
// ============================================================
std::string analyzeQuestion(const AIConfig& config,
    const std::string& question_content,
    const std::string& options_json,
    const std::string& answer)
{
    const char* systemPrompt =
        "你是一个专业的题库解析助手。请对以下题目进行详细解析，包括解题思路、涉及的知识点、易错点分析。请用中文回复。";

    std::ostringstream userMsg;
    userMsg << "题目：" << question_content << "\n";
    userMsg << "选项：" << options_json << "\n";
    userMsg << "正确答案：" << answer << "\n";
    userMsg << "请进行详细解析。";

    return chatCompletion(config, systemPrompt, userMsg.str());
}

// ============================================================
// generateVariant — create a similar-but-different question
// ============================================================
std::string generateVariant(const AIConfig& config,
    const std::string& original_content,
    const std::string& original_answer,
    const std::string& question_type)
{
    const char* systemPrompt =
        "你是一个专业的出题助手。请根据原题生成一道同类型、同难度的变体题（举一反三）。要求：题干不同但考察相同知识点，给出标准答案。返回格式：\n"
        "题干：...\n"
        "选项：A. ... B. ... C. ... D. ...\n"
        "答案：...";

    std::ostringstream userMsg;
    userMsg << "原题题干：" << original_content << "\n";
    userMsg << "原题答案：" << original_answer << "\n";
    userMsg << "题目类型：" << question_type << "\n";
    userMsg << "请生成一道变体题。";

    return chatCompletion(config, systemPrompt, userMsg.str());
}

// ============================================================
// generateQuestion — create a new question on a given topic
// ============================================================
std::string generateQuestion(const AIConfig& config,
    const std::string& topic,
    const std::string& question_type,
    int difficulty)
{
    const char* systemPrompt =
        "你是一个专业的出题助手。请根据以下知识点和题型要求，生成一道新的考试题目。"
        "难度星级（1-5星）。返回格式：\n"
        "题干：...\n"
        "选项：(如适用)\n"
        "答案：...\n"
        "解析：...";

    std::ostringstream userMsg;
    userMsg << "知识点/主题：" << topic << "\n";
    userMsg << "题目类型：" << question_type << "\n";
    userMsg << "难度：" << difficulty << "星\n";
    userMsg << "请生成一道新题目。";

    return chatCompletion(config, systemPrompt, userMsg.str());
}

} // namespace ai

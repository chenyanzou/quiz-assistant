#include "ai_service.hpp"
#include "lib/json.hpp"
#include <curl/curl.h>
#include <iostream>
#include <sstream>
#include <stdexcept>

using json = nlohmann::json;

namespace ai {

namespace {

// ============================================================
// RAII wrappers for libcurl C handles
// ============================================================

struct CurlHandle {
    CURL* handle = nullptr;
    CurlHandle() : handle{curl_easy_init()} {}
    ~CurlHandle() { if (handle) curl_easy_cleanup(handle); }
    CurlHandle(const CurlHandle&) = delete;
    CurlHandle& operator=(const CurlHandle&) = delete;
    CurlHandle(CurlHandle&&) = delete;
    CurlHandle& operator=(CurlHandle&&) = delete;
    [[nodiscard]] auto get() const -> CURL* { return handle; }
    [[nodiscard]] operator bool() const { return handle != nullptr; }
};

struct CurlSlistGuard {
    curl_slist* list = nullptr;
    CurlSlistGuard() = default;
    ~CurlSlistGuard() { if (list) curl_slist_free_all(list); }
    CurlSlistGuard(const CurlSlistGuard&) = delete;
    CurlSlistGuard& operator=(const CurlSlistGuard&) = delete;
    void append(const char* str) {
        list = curl_slist_append(list, str);
    }
    [[nodiscard]] auto get() const -> curl_slist* { return list; }
};

// curl write callback — appends data to a std::string
auto writeCallback(void* contents, size_t size, size_t nmemb, void* userp) -> size_t {
    auto total = size * nmemb;
    static_cast<std::string*>(userp)->append(static_cast<char*>(contents), total);
    return total;
}

} // anonymous namespace

// ============================================================
// chatCompletion — core function
// ============================================================

auto chatCompletion(const AIConfig& config,
    const std::string& system_prompt,
    const std::string& user_message) -> std::string
{
    // Guard: empty base_url
    if (config.base_url.empty()) {
        return "AI服务未配置，请在AI设置中配置API密钥。";
    }

    // Build request body
    json requestBody{
        {"model", config.model.empty() ? "gpt-3.5-turbo" : config.model},
        {"messages", json::array({
            {{"role", "system"}, {"content", system_prompt}},
            {{"role", "user"},   {"content", user_message}}
        })},
        {"temperature", 0.7},
        {"max_tokens", 2048}
    };

    auto bodyStr = requestBody.dump();

    // Construct URL (strip trailing slash)
    auto url = config.base_url;
    while (!url.empty() && url.back() == '/') url.pop_back();
    url += "/chat/completions";

    // Initialize curl (RAII)
    CurlHandle curl;
    if (!curl) {
        return "AI服务调用失败：无法初始化网络连接。";
    }

    std::string responseStr;
    std::string errorBuffer(CURL_ERROR_SIZE, '\0');
    CurlSlistGuard headers;

    auto authHeader = "Authorization: Bearer " + config.api_key;
    headers.append("Content-Type: application/json");
    headers.append(authHeader.c_str());

    curl_easy_setopt(curl.get(), CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl.get(), CURLOPT_POST, 1L);
    curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDS, bodyStr.c_str());
    curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDSIZE, static_cast<long>(bodyStr.size()));
    curl_easy_setopt(curl.get(), CURLOPT_HTTPHEADER, headers.get());
    curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &responseStr);
    curl_easy_setopt(curl.get(), CURLOPT_ERRORBUFFER, &errorBuffer[0]);
    curl_easy_setopt(curl.get(), CURLOPT_TIMEOUT, 90L);
    curl_easy_setopt(curl.get(), CURLOPT_CONNECTTIMEOUT, 30L);
    curl_easy_setopt(curl.get(), CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl.get(), CURLOPT_SSL_VERIFYHOST, 0L);

    // Perform request
    auto res = curl_easy_perform(curl.get());

    long httpCode = 0;
    curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &httpCode);

    // Error: curl failure
    if (res != CURLE_OK) {
        std::string errMsg{errorBuffer.c_str()};
        if (errMsg.empty()) errMsg = curl_easy_strerror(res);
        return "AI服务网络错误: " + errMsg;
    }

    // Error: non-200 HTTP
    if (httpCode != 200) {
        std::ostringstream oss;
        oss << "AI服务返回错误 (HTTP " << httpCode << ")";
        if (!responseStr.empty()) {
            try {
                auto errJson = json::parse(responseStr);
                if (errJson.contains("error") && errJson["error"].is_object()) {
                    if (errJson["error"].contains("message")) {
                        oss << ": " << errJson["error"]["message"].get<std::string>();
                    }
                }
            } catch (...) {
                auto preview = responseStr.substr(0, 200);
                oss << ": " << preview;
            }
        }
        return oss.str();
    }

    // Parse response
    json responseJson;
    try {
        responseJson = json::parse(responseStr);
    } catch (const json::parse_error& e) {
        return std::string{"AI服务响应解析失败: "} + e.what();
    }

    try {
        return responseJson["choices"][0]["message"]["content"].get<std::string>();
    } catch (const json::exception& e) {
        return std::string{"AI服务响应格式异常: "} + e.what();
    }
}

// ============================================================
// Built-in prompt wrappers
// ============================================================

auto analyzeQuestion(const AIConfig& config,
    const std::string& question_content,
    const std::string& options_json,
    const std::string& answer) -> std::string
{
    constexpr auto systemPrompt =
        "你是一个专业的题库解析助手。请对以下题目进行详细解析，包括解题思路、涉及的知识点、易错点分析。请用中文回复。";

    std::ostringstream userMsg;
    userMsg << "题目：" << question_content << "\n"
            << "选项：" << options_json << "\n"
            << "正确答案：" << answer << "\n"
            << "请进行详细解析。";

    return chatCompletion(config, systemPrompt, userMsg.str());
}

auto generateVariant(const AIConfig& config,
    const std::string& original_content,
    const std::string& original_answer,
    const std::string& question_type) -> std::string
{
    constexpr auto systemPrompt =
        "你是一个专业的出题助手。请根据原题生成一道同类型、同难度的变体题（举一反三）。"
        "要求：题干不同但考察相同知识点，给出标准答案。返回格式：\n"
        "题干：...\n"
        "选项：A. ... B. ... C. ... D. ...\n"
        "答案：...";

    std::ostringstream userMsg;
    userMsg << "原题题干：" << original_content << "\n"
            << "原题答案：" << original_answer << "\n"
            << "题目类型：" << question_type << "\n"
            << "请生成一道变体题。";

    return chatCompletion(config, systemPrompt, userMsg.str());
}

auto generateQuestion(const AIConfig& config,
    const std::string& topic,
    const std::string& question_type,
    int difficulty) -> std::string
{
    constexpr auto systemPrompt =
        "你是一个专业的出题助手。请根据以下知识点和题型要求，生成一道新的考试题目。"
        "难度星级（1-5星）。返回格式：\n"
        "题干：...\n"
        "选项：(如适用)\n"
        "答案：...\n"
        "解析：...";

    std::ostringstream userMsg;
    userMsg << "知识点/主题：" << topic << "\n"
            << "题目类型：" << question_type << "\n"
            << "难度：" << difficulty << "星\n"
            << "请生成一道新题目。";

    return chatCompletion(config, systemPrompt, userMsg.str());
}

} // namespace ai

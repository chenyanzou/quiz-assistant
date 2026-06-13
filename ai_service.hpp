#pragma once
#include <string>

struct AIConfig {
    std::string provider;     // openai, kimi, deepseek, qwen, glm, custom
    std::string base_url;
    std::string api_key;
    std::string model;
};

namespace ai {

auto chatCompletion(const AIConfig& config,
    const std::string& system_prompt,
    const std::string& user_message) -> std::string;

// Built-in prompts
auto analyzeQuestion(const AIConfig& config,
    const std::string& question_content,
    const std::string& options_json,
    const std::string& answer) -> std::string;

auto generateVariant(const AIConfig& config,
    const std::string& original_content,
    const std::string& original_answer,
    const std::string& question_type) -> std::string;

auto generateQuestion(const AIConfig& config,
    const std::string& topic,
    const std::string& question_type,
    int difficulty) -> std::string;

} // namespace ai

#pragma once
#include <string>
#include <vector>
#include <optional>
#include "sqlite3.h"

namespace db {

struct Category {
    int id = 0;
    std::string name;
    std::string description;
    std::string created_at;
};

struct Question {
    int id = 0;
    int category_id = 0;
    std::string question_type; // single, multi, judge, fill, short
    std::string content;
    std::string options_json;  // JSON array for choices
    std::string answer;
    std::string analysis;
    int difficulty = 1;
    std::string source;
    std::string created_at;
};

struct WrongQuestion {
    int id = 0;
    int question_id = 0;
    std::string user_answer;
    int wrong_count = 0;
    std::string last_wrong_at;
    bool reviewed = false;
    // joined fields
    std::string content;
    std::string options_json;
    std::string answer;
    std::string question_type;
    int category_id = 0;
    std::string category_name;
};

struct QuizRecord {
    int id = 0;
    int category_id = 0;
    std::string mode;
    int total_questions = 0;
    int correct_count = 0;
    int wrong_count = 0;
    double score = 0.0;
    std::string created_at;
};

struct StatsItem {
    std::string category_name;
    int total_questions = 0;
    int completed = 0;
    double accuracy = 0.0;
};

class Database {
public:
    explicit Database(const std::string& path);
    ~Database();

    // Categories
    auto getCategories() -> std::vector<Category>;
    auto getCategory(int id) -> std::optional<Category>;
    auto addCategory(const std::string& name, const std::string& desc) -> int;
    auto deleteCategory(int id) -> bool;

    // Questions
    auto getQuestions(std::optional<int> category_id = {},
        std::optional<std::string> type = {},
        std::optional<std::string> search = {},
        int limit = 100, int offset = 0) -> std::vector<Question>;
    auto countQuestions(std::optional<int> category_id = {},
        std::optional<std::string> type = {},
        std::optional<std::string> search = {}) -> int;
    auto getQuestion(int id) -> std::optional<Question>;
    auto addQuestion(const Question& q) -> int;
    auto updateQuestion(int id, const Question& q) -> bool;
    auto deleteQuestion(int id) -> bool;

    // Quiz records
    auto addQuizRecord(const QuizRecord& r) -> int;
    auto getQuizRecords(int category_id, int limit = 10) -> std::vector<QuizRecord>;

    // Wrong questions
    auto addWrongAnswer(int question_id, const std::string& user_answer) -> void;
    auto getWrongQuestions(std::optional<int> category_id = {}) -> std::vector<WrongQuestion>;
    auto countWrongQuestions(std::optional<int> category_id = {}) -> int;
    auto deleteWrongQuestion(int id) -> bool;
    auto markWrongReviewed(int id) -> bool;

    // Stats
    auto getStats() -> std::vector<StatsItem>;

private:
    sqlite3* db_{nullptr};
    auto exec(const std::string& sql) -> void;
    auto initTables() -> void;
};

} // namespace db

#pragma once
#include <string>
#include <vector>
#include <map>
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
    Database(const std::string& path);
    ~Database();

    // Categories
    std::vector<Category> getCategories();
    std::optional<Category> getCategory(int id);
    int addCategory(const std::string& name, const std::string& desc);
    bool deleteCategory(int id);

    // Questions
    std::vector<Question> getQuestions(std::optional<int> category_id = {},
        std::optional<std::string> type = {},
        std::optional<std::string> search = {},
        int limit = 100, int offset = 0);
    int countQuestions(std::optional<int> category_id = {},
        std::optional<std::string> type = {},
        std::optional<std::string> search = {});
    std::optional<Question> getQuestion(int id);
    int addQuestion(const Question& q);
    bool updateQuestion(int id, const Question& q);
    bool deleteQuestion(int id);

    // Quiz records
    int addQuizRecord(const QuizRecord& r);
    std::vector<QuizRecord> getQuizRecords(int category_id, int limit = 10);

    // Wrong questions
    void addWrongAnswer(int question_id, const std::string& user_answer);
    std::vector<WrongQuestion> getWrongQuestions(std::optional<int> category_id = {});
    int countWrongQuestions(std::optional<int> category_id = {});
    bool deleteWrongQuestion(int id);
    bool markWrongReviewed(int id);

    // Stats
    std::vector<StatsItem> getStats();

private:
    sqlite3* db_;
    void exec(const std::string& sql);
    void initTables();
};

} // namespace db

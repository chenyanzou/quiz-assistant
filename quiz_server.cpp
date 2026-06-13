#include "httplib.h"
#include "json.hpp"
#include "database.hpp"
#include "ai_service.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <random>
#include <algorithm>
#include <ctime>
#include <cctype>
#include <set>
#include <chrono>

using json = nlohmann::json;
using namespace httplib;

// ============================================================
// Helpers
// ============================================================

static std::string readFile(const std::string& path) {
    std::ifstream f(path);
    if (!f) return "";
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static std::unique_ptr<db::Database> g_db;

// Case-insensitive character comparison
static int ciCharCompare(char a, char b) {
    return std::tolower(static_cast<unsigned char>(a))
        == std::tolower(static_cast<unsigned char>(b));
}

static std::string trim(const std::string& s) {
    size_t start = 0;
    while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start])))
        ++start;
    size_t end = s.size();
    while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1])))
        --end;
    return s.substr(start, end - start);
}

static bool answersMatch(const std::string& userAnswer, const std::string& correctAnswer) {
    auto tu = trim(userAnswer);
    auto tc = trim(correctAnswer);
    return std::equal(
        tu.begin(), tu.end(),
        tc.begin(),
        ciCharCompare);
}

// ============================================================
// Route setup
// ============================================================

static void setupRoutes(Server& svr) {
    // CORS helper
    auto cors = [](Response& res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Access-Control-Allow-Methods",
            "GET,POST,PUT,DELETE,OPTIONS");
        res.set_header("Access-Control-Allow-Headers", "*");
    };

    // Preflight handler for all /api/* routes
    svr.Options(R"(/api/.*)", [cors](const Request&, Response& res) {
        cors(res);
        res.status = 204;
    });

    // ========================================================
    // CATEGORIES
    // ========================================================

    svr.Get("/api/categories", [cors](const Request&, Response& res) {
        cors(res);
        try {
            auto cats = g_db->getCategories();
            json arr = json::array();
            for (auto& c : cats) {
                json obj;
                obj["id"] = c.id;
                obj["name"] = c.name;
                obj["description"] = c.description;
                obj["created_at"] = c.created_at;
                arr.push_back(obj);
            }
            res.set_content(arr.dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 500;
            res.set_content(
                json{{"error", e.what()}}.dump(), "application/json");
        }
    });

    svr.Post("/api/categories", [cors](const Request& req, Response& res) {
        cors(res);
        try {
            json body = json::parse(req.body);
            std::string name = body.value("name", "");
            std::string description = body.value("description", "");
            if (name.empty()) {
                res.status = 400;
                res.set_content(
                    json{{"error", "name is required"}}.dump(),
                    "application/json");
                return;
            }
            int id = g_db->addCategory(name, description);
            res.status = 201;
            res.set_content(json{{"id", id}}.dump(), "application/json");
        } catch (const json::parse_error&) {
            res.status = 400;
            res.set_content(
                json{{"error", "Invalid JSON"}}.dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 500;
            res.set_content(
                json{{"error", e.what()}}.dump(), "application/json");
        }
    });

    svr.Delete(R"(/api/categories/(\d+))", [cors](
        const Request& req, Response& res) {
        cors(res);
        try {
            int id = std::stoi(req.matches[1]);
            bool ok = g_db->deleteCategory(id);
            res.set_content(
                json{{"success", ok}}.dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 500;
            res.set_content(
                json{{"error", e.what()}}.dump(), "application/json");
        }
    });

    // ========================================================
    // QUESTIONS CRUD
    // ========================================================

    svr.Get("/api/questions", [cors](const Request& req, Response& res) {
        cors(res);
        try {
            // Parse optional query parameters
            std::optional<int> category_id;
            if (req.has_param("category_id")) {
                category_id = std::stoi(req.get_param_value("category_id"));
            }
            std::optional<std::string> type;
            if (req.has_param("type") && !req.get_param_value("type").empty()) {
                type = req.get_param_value("type");
            }
            std::optional<std::string> search;
            if (req.has_param("search") && !req.get_param_value("search").empty()) {
                search = req.get_param_value("search");
            }
            int limit = 100;
            if (req.has_param("limit")) {
                limit = std::stoi(req.get_param_value("limit"));
            }
            int offset = 0;
            if (req.has_param("offset")) {
                offset = std::stoi(req.get_param_value("offset"));
            }

            auto questions = g_db->getQuestions(category_id, type, search, limit, offset);
            int total = g_db->countQuestions(category_id, type, search);

            json arr = json::array();
            for (auto& q : questions) {
                json obj;
                obj["id"] = q.id;
                obj["category_id"] = q.category_id;
                obj["question_type"] = q.question_type;
                obj["content"] = q.content;
                // Parse options_json back into JSON array
                try {
                    if (!q.options_json.empty()) {
                        obj["options"] = json::parse(q.options_json);
                    } else {
                        obj["options"] = json::array();
                    }
                } catch (...) {
                    obj["options"] = q.options_json;
                }
                obj["answer"] = q.answer;
                obj["analysis"] = q.analysis;
                obj["difficulty"] = q.difficulty;
                obj["source"] = q.source;
                obj["created_at"] = q.created_at;
                arr.push_back(obj);
            }

            json result;
            result["questions"] = arr;
            result["total"] = total;
            res.set_content(result.dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 500;
            res.set_content(
                json{{"error", e.what()}}.dump(), "application/json");
        }
    });

    svr.Get(R"(/api/questions/(\d+))", [cors](
        const Request& req, Response& res) {
        cors(res);
        try {
            int id = std::stoi(req.matches[1]);
            auto q = g_db->getQuestion(id);
            if (!q) {
                res.status = 404;
                res.set_content(
                    json{{"error", "Question not found"}}.dump(),
                    "application/json");
                return;
            }
            json obj;
            obj["id"] = q->id;
            obj["category_id"] = q->category_id;
            obj["question_type"] = q->question_type;
            obj["content"] = q->content;
            try {
                if (!q->options_json.empty()) {
                    obj["options"] = json::parse(q->options_json);
                } else {
                    obj["options"] = json::array();
                }
            } catch (...) {
                obj["options"] = q->options_json;
            }
            obj["answer"] = q->answer;
            obj["analysis"] = q->analysis;
            obj["difficulty"] = q->difficulty;
            obj["source"] = q->source;
            obj["created_at"] = q->created_at;
            res.set_content(obj.dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 500;
            res.set_content(
                json{{"error", e.what()}}.dump(), "application/json");
        }
    });

    svr.Post("/api/questions", [cors](const Request& req, Response& res) {
        cors(res);
        try {
            json body = json::parse(req.body);

            db::Question q;
            q.category_id = body.value("category_id", 0);
            q.question_type = body.value("question_type", "single");
            q.content = body.value("content", "");
            if (body.contains("options")) {
                q.options_json = body["options"].dump();
            } else if (body.contains("options_json")) {
                q.options_json = body["options_json"].get<std::string>();
            }
            q.answer = body.value("answer", "");
            q.analysis = body.value("analysis", "");
            q.difficulty = body.value("difficulty", 1);
            q.source = body.value("source", "");

            if (q.content.empty()) {
                res.status = 400;
                res.set_content(
                    json{{"error", "content is required"}}.dump(),
                    "application/json");
                return;
            }

            int id = g_db->addQuestion(q);
            res.status = 201;
            res.set_content(json{{"id", id}}.dump(), "application/json");
        } catch (const json::parse_error&) {
            res.status = 400;
            res.set_content(
                json{{"error", "Invalid JSON"}}.dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 500;
            res.set_content(
                json{{"error", e.what()}}.dump(), "application/json");
        }
    });

    svr.Put(R"(/api/questions/(\d+))", [cors](
        const Request& req, Response& res) {
        cors(res);
        try {
            int id = std::stoi(req.matches[1]);
            json body = json::parse(req.body);

            db::Question q;
            q.id = id;
            q.category_id = body.value("category_id", 0);
            q.question_type = body.value("question_type", "single");
            q.content = body.value("content", "");
            if (body.contains("options")) {
                q.options_json = body["options"].dump();
            } else if (body.contains("options_json")) {
                q.options_json = body["options_json"].get<std::string>();
            }
            q.answer = body.value("answer", "");
            q.analysis = body.value("analysis", "");
            q.difficulty = body.value("difficulty", 1);
            q.source = body.value("source", "");

            bool ok = g_db->updateQuestion(id, q);
            res.set_content(
                json{{"success", ok}}.dump(), "application/json");
        } catch (const json::parse_error&) {
            res.status = 400;
            res.set_content(
                json{{"error", "Invalid JSON"}}.dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 500;
            res.set_content(
                json{{"error", e.what()}}.dump(), "application/json");
        }
    });

    svr.Delete(R"(/api/questions/(\d+))", [cors](
        const Request& req, Response& res) {
        cors(res);
        try {
            int id = std::stoi(req.matches[1]);
            bool ok = g_db->deleteQuestion(id);
            res.set_content(
                json{{"success", ok}}.dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 500;
            res.set_content(
                json{{"error", e.what()}}.dump(), "application/json");
        }
    });

    // ========================================================
    // IMPORT
    // ========================================================

    svr.Post("/api/questions/import", [cors](
        const Request& req, Response& res) {
        cors(res);
        try {
            json body = json::parse(req.body);

            int category_id = body.value("category_id", 0);
            std::string category_name = body.value("category_name", "");

            // Auto-create category if needed
            if (category_id == 0 && !category_name.empty()) {
                category_id = g_db->addCategory(category_name, "");
            }

            if (category_id == 0) {
                res.status = 400;
                res.set_content(
                    json{{"error",
                        "category_id or category_name is required"}}.dump(),
                    "application/json");
                return;
            }

            int imported = 0;
            int failed = 0;

            json questions_arr = body.value("questions", json::array());
            std::string source = body.value("source", "");

            for (auto& item : questions_arr) {
                try {
                    db::Question q;
                    q.category_id = category_id;
                    q.question_type = item.value("question_type", "single");
                    q.content = item.value("content", "");
                    if (item.contains("options")) {
                        q.options_json = item["options"].dump();
                    } else if (item.contains("options_json")) {
                        q.options_json = item["options_json"].get<std::string>();
                    }
                    q.answer = item.value("answer", "");
                    q.analysis = item.value("analysis", "");
                    q.difficulty = item.value("difficulty", 1);
                    q.source = source.empty()
                        ? item.value("source", "") : source;

                    if (!q.content.empty()) {
                        g_db->addQuestion(q);
                        ++imported;
                    } else {
                        ++failed;
                    }
                } catch (...) {
                    ++failed;
                }
            }

            json result;
            result["imported"] = imported;
            result["failed"] = failed;
            res.set_content(result.dump(), "application/json");
        } catch (const json::parse_error&) {
            res.status = 400;
            res.set_content(
                json{{"error", "Invalid JSON"}}.dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 500;
            res.set_content(
                json{{"error", e.what()}}.dump(), "application/json");
        }
    });

    // ========================================================
    // QUIZ
    // ========================================================

    svr.Post("/api/quiz/start", [cors](const Request& req, Response& res) {
        cors(res);
        try {
            json body = json::parse(req.body);

            std::string mode = body.value("mode", "sequential");
            int category_id = body.value("category_id", 0);
            int count = body.value("count", 10);
            bool wrong_only = body.value("wrong_only", false);

            std::vector<db::Question> questions;

            if (wrong_only) {
                // Get wrong questions and load their parent questions
                auto wrongs = g_db->getWrongQuestions(
                    category_id > 0
                        ? std::optional<int>(category_id)
                        : std::nullopt);
                for (auto& w : wrongs) {
                    auto parent = g_db->getQuestion(w.question_id);
                    if (parent) {
                        questions.push_back(*parent);
                    }
                }
                // Remove duplicates (same question wrong multiple times)
                std::vector<db::Question> unique;
                std::set<int> seen;
                for (auto& q : questions) {
                    if (seen.find(q.id) == seen.end()) {
                        seen.insert(q.id);
                        unique.push_back(q);
                    }
                }
                questions = std::move(unique);
            } else {
                questions = g_db->getQuestions(
                    category_id > 0
                        ? std::optional<int>(category_id)
                        : std::nullopt,
                    std::nullopt, std::nullopt, 10000, 0);
            }

            // Shuffle if random or exam mode
            if (mode == "random" || mode == "exam") {
                std::random_device rd;
                std::mt19937 g(rd());
                std::shuffle(questions.begin(), questions.end(), g);
            }

            // Limit count
            if (count > 0 && static_cast<int>(questions.size()) > count) {
                questions.resize(count);
            }

            // Build response — strip answer/analysis to prevent cheating
            json arr = json::array();
            for (auto& q : questions) {
                json obj;
                obj["id"] = q.id;
                obj["category_id"] = q.category_id;
                obj["question_type"] = q.question_type;
                obj["content"] = q.content;
                try {
                    if (!q.options_json.empty()) {
                        obj["options"] = json::parse(q.options_json);
                    } else {
                        obj["options"] = json::array();
                    }
                } catch (...) {
                    obj["options"] = q.options_json;
                }
                obj["difficulty"] = q.difficulty;
                // Intentionally NOT including answer or analysis
                arr.push_back(obj);
            }

            json result;
            result["quiz_id"] =
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch())
                    .count();
            result["questions"] = arr;
            result["total"] = arr.size();
            res.set_content(result.dump(), "application/json");
        } catch (const json::parse_error&) {
            res.status = 400;
            res.set_content(
                json{{"error", "Invalid JSON"}}.dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 500;
            res.set_content(
                json{{"error", e.what()}}.dump(), "application/json");
        }
    });

    svr.Post("/api/quiz/submit", [cors](const Request& req, Response& res) {
        cors(res);
        try {
            json body = json::parse(req.body);

            int question_id = body.value("question_id", 0);
            std::string user_answer = body.value("user_answer", "");
            int category_id = body.value("category_id", 0);
            std::string mode = body.value("mode", "sequential");

            if (question_id == 0) {
                res.status = 400;
                res.set_content(
                    json{{"error", "question_id is required"}}.dump(),
                    "application/json");
                return;
            }

            auto q = g_db->getQuestion(question_id);
            if (!q) {
                res.status = 404;
                res.set_content(
                    json{{"error", "Question not found"}}.dump(),
                    "application/json");
                return;
            }

            bool correct = answersMatch(user_answer, q->answer);

            if (!correct) {
                g_db->addWrongAnswer(question_id, user_answer);
            }

            // Record quiz attempt
            db::QuizRecord record;
            record.category_id = category_id;
            record.mode = mode;
            record.total_questions = 1;
            record.correct_count = correct ? 1 : 0;
            record.wrong_count = correct ? 0 : 1;
            record.score = correct ? 100.0 : 0.0;
            g_db->addQuizRecord(record);

            json result;
            result["correct"] = correct;
            result["correct_answer"] = q->answer;
            result["analysis"] = q->analysis;
            res.set_content(result.dump(), "application/json");
        } catch (const json::parse_error&) {
            res.status = 400;
            res.set_content(
                json{{"error", "Invalid JSON"}}.dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 500;
            res.set_content(
                json{{"error", e.what()}}.dump(), "application/json");
        }
    });

    // ========================================================
    // WRONG QUESTIONS
    // ========================================================

    svr.Get("/api/wrong", [cors](const Request& req, Response& res) {
        cors(res);
        try {
            std::optional<int> category_id;
            if (req.has_param("category_id")) {
                category_id = std::stoi(req.get_param_value("category_id"));
            }

            auto wrongs = g_db->getWrongQuestions(category_id);
            int total = g_db->countWrongQuestions(category_id);

            json arr = json::array();
            for (auto& w : wrongs) {
                json obj;
                obj["id"] = w.id;
                obj["question_id"] = w.question_id;
                obj["user_answer"] = w.user_answer;
                obj["wrong_count"] = w.wrong_count;
                obj["last_wrong_at"] = w.last_wrong_at;
                obj["reviewed"] = w.reviewed;
                obj["content"] = w.content;
                try {
                    if (!w.options_json.empty()) {
                        obj["options"] = json::parse(w.options_json);
                    } else {
                        obj["options"] = json::array();
                    }
                } catch (...) {
                    obj["options"] = w.options_json;
                }
                obj["answer"] = w.answer;
                obj["question_type"] = w.question_type;
                obj["category_id"] = w.category_id;
                obj["category_name"] = w.category_name;
                arr.push_back(obj);
            }

            json result;
            result["wrong_questions"] = arr;
            result["total"] = total;
            res.set_content(result.dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 500;
            res.set_content(
                json{{"error", e.what()}}.dump(), "application/json");
        }
    });

    svr.Delete(R"(/api/wrong/(\d+))", [cors](
        const Request& req, Response& res) {
        cors(res);
        try {
            int id = std::stoi(req.matches[1]);
            bool ok = g_db->deleteWrongQuestion(id);
            res.set_content(
                json{{"success", ok}}.dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 500;
            res.set_content(
                json{{"error", e.what()}}.dump(), "application/json");
        }
    });

    svr.Put(R"(/api/wrong/(\d+)/review)", [cors](
        const Request& req, Response& res) {
        cors(res);
        try {
            int id = std::stoi(req.matches[1]);
            bool ok = g_db->markWrongReviewed(id);
            res.set_content(
                json{{"success", ok}}.dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 500;
            res.set_content(
                json{{"error", e.what()}}.dump(), "application/json");
        }
    });

    // ========================================================
    // STATS
    // ========================================================

    svr.Get("/api/stats", [cors](const Request&, Response& res) {
        cors(res);
        try {
            auto stats = g_db->getStats();
            json arr = json::array();
            for (auto& s : stats) {
                json obj;
                obj["category_name"] = s.category_name;
                obj["total_questions"] = s.total_questions;
                obj["completed"] = s.completed;
                obj["accuracy"] = s.accuracy;
                arr.push_back(obj);
            }
            json result;
            result["stats"] = arr;
            res.set_content(result.dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 500;
            res.set_content(
                json{{"error", e.what()}}.dump(), "application/json");
        }
    });

    // ========================================================
    // AI
    // ========================================================

    svr.Post("/api/ai/analyze", [cors](const Request& req, Response& res) {
        cors(res);
        try {
            json body = json::parse(req.body);

            AIConfig config;
            config.provider = body.value("provider", "openai");
            config.base_url = body.value("base_url", "");
            config.api_key = body.value("api_key", "");
            config.model = body.value("model", "gpt-3.5-turbo");

            std::string content = body.value("content", "");
            std::string options;
            if (body.contains("options")) {
                options = body["options"].dump();
            } else if (body.contains("options_json")) {
                options = body["options_json"].get<std::string>();
            }
            std::string answer = body.value("answer", "");

            if (content.empty()) {
                res.status = 400;
                res.set_content(
                    json{{"error", "content is required"}}.dump(),
                    "application/json");
                return;
            }

            std::string analysis = ai::analyzeQuestion(
                config, content, options, answer);

            json result;
            result["analysis"] = analysis;
            res.set_content(result.dump(), "application/json");
        } catch (const json::parse_error&) {
            res.status = 400;
            res.set_content(
                json{{"error", "Invalid JSON"}}.dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 500;
            res.set_content(
                json{{"error", e.what()}}.dump(), "application/json");
        }
    });

    svr.Post("/api/ai/variant", [cors](const Request& req, Response& res) {
        cors(res);
        try {
            json body = json::parse(req.body);

            AIConfig config;
            config.provider = body.value("provider", "openai");
            config.base_url = body.value("base_url", "");
            config.api_key = body.value("api_key", "");
            config.model = body.value("model", "gpt-3.5-turbo");

            std::string original_content = body.value("original_content", "");
            std::string original_answer = body.value("original_answer", "");
            std::string question_type = body.value("question_type", "single");

            if (original_content.empty()) {
                res.status = 400;
                res.set_content(
                    json{{"error", "original_content is required"}}.dump(),
                    "application/json");
                return;
            }

            std::string variant = ai::generateVariant(
                config, original_content, original_answer, question_type);

            json result;
            result["variant"] = variant;
            res.set_content(result.dump(), "application/json");
        } catch (const json::parse_error&) {
            res.status = 400;
            res.set_content(
                json{{"error", "Invalid JSON"}}.dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 500;
            res.set_content(
                json{{"error", e.what()}}.dump(), "application/json");
        }
    });

    svr.Post("/api/ai/generate", [cors](const Request& req, Response& res) {
        cors(res);
        try {
            json body = json::parse(req.body);

            AIConfig config;
            config.provider = body.value("provider", "openai");
            config.base_url = body.value("base_url", "");
            config.api_key = body.value("api_key", "");
            config.model = body.value("model", "gpt-3.5-turbo");

            std::string topic = body.value("topic", "");
            std::string question_type = body.value("question_type", "single");
            int difficulty = body.value("difficulty", 1);

            if (topic.empty()) {
                res.status = 400;
                res.set_content(
                    json{{"error", "topic is required"}}.dump(),
                    "application/json");
                return;
            }

            std::string generated = ai::generateQuestion(
                config, topic, question_type, difficulty);

            json result;
            result["question"] = generated;
            res.set_content(result.dump(), "application/json");
        } catch (const json::parse_error&) {
            res.status = 400;
            res.set_content(
                json{{"error", "Invalid JSON"}}.dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 500;
            res.set_content(
                json{{"error", e.what()}}.dump(), "application/json");
        }
    });
}

// ============================================================
// Main
// ============================================================

int main(int argc, char* argv[]) {
    int port = 8080;
    std::string db_path = "data/quiz_app.db";

    if (argc > 1) {
        try {
            port = std::stoi(argv[1]);
        } catch (...) {
            std::cerr << "Invalid port argument, using default 8080" << std::endl;
        }
    }
    if (argc > 2) {
        db_path = argv[2];
    }

    std::cout << "Initializing database: " << db_path << std::endl;
    g_db = std::make_unique<db::Database>(db_path);

    Server svr;

    // Setup all API routes
    setupRoutes(svr);

    // Serve index.html at root
    svr.Get("/", [](const Request&, Response& res) {
        std::string html = readFile("index.html");
        if (html.empty()) {
            res.status = 404;
            res.set_content("<h1>index.html not found</h1>", "text/html");
            return;
        }
        res.set_content(html, "text/html");
    });

    std::cout << "Quiz Server running on http://0.0.0.0:" << port << std::endl;
    std::cout << "Open http://localhost:" << port << " in your browser" << std::endl;

    svr.listen("0.0.0.0", port);
    return 0;
}

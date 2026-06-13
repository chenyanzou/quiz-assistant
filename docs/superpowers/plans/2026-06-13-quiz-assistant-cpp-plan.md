# 智能题库刷题助手 C++ 版 — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Complete C++ quiz server + single-file HTML frontend with AI integration, ready for GitHub deployment.

**Architecture:** Single C++ executable (quiz_server.exe) serving REST API over HTTP via cpp-httplib, static file serving for index.html. SQLite3 for persistence. libcurl for AI API proxying. Single-file SPA frontend with Bootstrap 5.

**Tech Stack:** C++17, cpp-httplib (header-only), SQLite3 amalgamation, nlohmann/json (header-only), libcurl, Bootstrap 5 CDN, vanilla JS.

**Source files (5):**
- `quiz_server.cpp` — main() + all HTTP routes
- `database.hpp` — SQLite3 RAII wrapper
- `database.cpp` — SQLite3 implementation
- `ai_service.hpp` — AI multi-provider API proxy
- `index.html` — complete SPA frontend

---

### Task 0: Project Scaffolding & Library Downloads

**Files:**
- Create: `lib/httplib.h`
- Create: `lib/json.hpp`
- Create: `lib/sqlite3.h`
- Create: `lib/sqlite3.c`
- Create: `CMakeLists.txt`

- [ ] **Step 1: Download cpp-httplib**

Download from: `https://raw.githubusercontent.com/yhirose/cpp-httplib/master/httplib.h`
Save to: `lib/httplib.h`

- [ ] **Step 2: Download nlohmann/json**

Download from: `https://github.com/nlohmann/json/releases/latest/download/json.hpp`
Save to: `lib/json.hpp`

- [ ] **Step 3: Download SQLite3 amalgamation**

Download from: `https://www.sqlite.org/2024/sqlite-amalgamation-3450200.zip`
Extract `sqlite3.h` and `sqlite3.c` to `lib/`

- [ ] **Step 4: Create CMakeLists.txt**

```cmake
cmake_minimum_required(VERSION 3.14)
project(QuizServer VERSION 1.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(CURL REQUIRED)

add_library(sqlite3 lib/sqlite3.c)
target_include_directories(sqlite3 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/lib)

add_executable(quiz_server
    quiz_server.cpp
    database.cpp
    ai_service.cpp
)

target_include_directories(quiz_server PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/lib
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(quiz_server PRIVATE
    sqlite3
    CURL::libcurl
)

if(WIN32)
    target_link_libraries(quiz_server PRIVATE ws2_32 crypt32)
endif()

# Create data directory at build
file(MAKE_DIRECTORY ${CMAKE_SOURCE_DIR}/data)
```

- [ ] **Step 5: Commit scaffold**

---

### Task 1: Database Layer (database.hpp + database.cpp)

**Files:**
- Create: `database.hpp`
- Create: `database.cpp`

- [ ] **Step 1: Write database.hpp**

```cpp
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
```

- [ ] **Step 2: Write database.cpp**

Full implementation of all Database methods using SQLite3 C API. Key points:
- `initTables()` creates all 4 tables with proper schema
- `getQuestions()` supports filtering by category, type, search (LIKE on content)
- `addWrongAnswer()` upserts: increment count if exists, insert if new
- `getStats()` computes per-category accuracy using JOIN queries
- All methods use prepared statements for SQL injection prevention
- RAII via constructor/destructor (sqlite3_open / sqlite3_close)

- [ ] **Step 3: Commit database layer**

---

### Task 2: AI Service Module (ai_service.hpp + ai_service.cpp)

**Files:**
- Create: `ai_service.hpp`
- Create: `ai_service.cpp`

- [ ] **Step 1: Write ai_service.hpp**

```cpp
#pragma once
#include <string>
#include <map>

struct AIConfig {
    std::string provider;     // openai, kimi, deepseek, qwen, glm, custom
    std::string base_url;
    std::string api_key;
    std::string model;
};

namespace ai {

std::string chatCompletion(const AIConfig& config,
    const std::string& system_prompt,
    const std::string& user_message);

// Built-in prompts
std::string analyzeQuestion(const AIConfig& config,
    const std::string& question_content,
    const std::string& options_json,
    const std::string& answer);

std::string generateVariant(const AIConfig& config,
    const std::string& original_content,
    const std::string& original_answer,
    const std::string& question_type);

std::string generateQuestion(const AIConfig& config,
    const std::string& topic,
    const std::string& question_type,
    int difficulty);

} // namespace ai
```

- [ ] **Step 2: Write ai_service.cpp**

Implementation:
- `chatCompletion()`: Builds OpenAI-compatible JSON request with libcurl. Sets headers (Authorization, Content-Type). Sends POST to `{base_url}/chat/completions`. Parses response JSON with nlohmann/json, extracts `choices[0].message.content`.
- `analyzeQuestion()`: System prompt instructs AI to return detailed analysis of the question.
- `generateVariant()`: System prompt instructs AI to create a similar but different question of same type.
- `generateQuestion()`: System prompt instructs AI to create a new question on given topic.
- Error handling: curl failures, non-200 responses, malformed JSON responses.
- Timeout: 60 seconds for AI calls.

- [ ] **Step 3: Commit AI service**

---

### Task 3: Main Server (quiz_server.cpp)

**Files:**
- Create: `quiz_server.cpp`

- [ ] **Step 1: Write quiz_server.cpp — includes and helpers**

```cpp
#include "httplib.h"
#include "json.hpp"
#include "database.hpp"
#include "ai_service.hpp"
#include <iostream>
#include <fstream>
#include <sstream>

using json = nlohmann::json;
using namespace httplib;

// Global database instance
std::unique_ptr<db::Database> g_db;

// CORS middleware helper
void addCORS(Response& res) {
    res.set_header("Access-Control-Allow-Origin", "*");
    res.set_header("Access-Control-Allow-Methods", "GET,POST,PUT,DELETE,OPTIONS");
    res.set_header("Access-Control-Allow-Headers", "*");
}

// Parse JSON body helper
json parseBody(const Request& req, Response& res) {
    try { return json::parse(req.body); }
    catch (...) { res.status = 400; return json{{"error","Invalid JSON"}}; }
}

// Read file helper
std::string readFile(const std::string& path) {
    std::ifstream f(path);
    if (!f) return "";
    std::ostringstream ss; ss << f.rdbuf(); return ss.str();
}
```

- [ ] **Step 2: Write route handlers — Categories**

```cpp
void setupRoutes(Server& svr) {
    // OPTIONS handler for CORS preflight
    svr.Options(R"(/api/.*)", [](const Request&, Response& res) {
        addCORS(res); res.status = 204;
    });

    // === CATEGORIES ===
    svr.Get("/api/categories", [](const Request&, Response& res) {
        addCORS(res);
        auto cats = g_db->getCategories();
        json arr = json::array();
        for (auto& c : cats) {
            arr.push_back({{"id",c.id},{"name",c.name},
                {"description",c.description},{"created_at",c.created_at}});
        }
        res.set_content(arr.dump(), "application/json");
    });

    svr.Post("/api/categories", [](const Request& req, Response& res) {
        addCORS(res);
        auto j = parseBody(req, res);
        if (res.status == 400) return;
        int id = g_db->addCategory(j["name"], j.value("description",""));
        res.set_content(json{{"id",id}}.dump(), "application/json");
    });

    svr.Delete(R"(/api/categories/(\d+))", [](const Request& req, Response& res) {
        addCORS(res);
        int id = std::stoi(req.matches[1]);
        bool ok = g_db->deleteCategory(id);
        res.set_content(json{{"success",ok}}.dump(), "application/json");
    });
```

- [ ] **Step 3: Write route handlers — Questions CRUD**

```cpp
    // === QUESTIONS ===
    svr.Get("/api/questions", [](const Request& req, Response& res) {
        addCORS(res);
        std::optional<int> cat_id;
        if (req.has_param("category_id")) cat_id = std::stoi(req.get_param_value("category_id"));
        std::optional<std::string> type, search;
        if (req.has_param("type")) type = req.get_param_value("type");
        if (req.has_param("search")) search = req.get_param_value("search");
        int limit = req.has_param("limit") ? std::stoi(req.get_param_value("limit")) : 100;
        int offset = req.has_param("offset") ? std::stoi(req.get_param_value("offset")) : 0;

        auto questions = g_db->getQuestions(cat_id, type, search, limit, offset);
        int total = g_db->countQuestions(cat_id, type, search);

        json arr = json::array();
        for (auto& q : questions) {
            arr.push_back({
                {"id",q.id},{"category_id",q.category_id},{"question_type",q.question_type},
                {"content",q.content},{"options",json::parse(q.options_json.empty()?"[]":q.options_json)},
                {"answer",q.answer},{"analysis",q.analysis},{"difficulty",q.difficulty},
                {"source",q.source},{"created_at",q.created_at}
            });
        }
        res.set_content(json{{"questions",arr},{"total",total}}.dump(), "application/json");
    });

    svr.Get(R"(/api/questions/(\d+))", [](const Request& req, Response& res) {
        addCORS(res);
        auto q = g_db->getQuestion(std::stoi(req.matches[1]));
        if (!q) { res.status = 404; res.set_content("{\"error\":\"Not found\"}","application/json"); return; }
        json j = {{"id",q->id},{"category_id",q->category_id},{"question_type",q->question_type},
            {"content",q->content},{"options",json::parse(q->options_json.empty()?"[]":q->options_json)},
            {"answer",q->answer},{"analysis",q->analysis},{"difficulty",q->difficulty},
            {"source",q->source}};
        res.set_content(j.dump(), "application/json");
    });

    svr.Post("/api/questions", [](const Request& req, Response& res) {
        addCORS(res);
        auto j = parseBody(req, res); if (res.status==400) return;
        db::Question q;
        q.category_id = j["category_id"];
        q.question_type = j["question_type"];
        q.content = j["content"];
        q.options_json = j.value("options",json::array()).dump();
        q.answer = j["answer"];
        q.analysis = j.value("analysis","");
        q.difficulty = j.value("difficulty",1);
        q.source = j.value("source","manual");
        int id = g_db->addQuestion(q);
        res.set_content(json{{"id",id}}.dump(), "application/json");
    });

    svr.Put(R"(/api/questions/(\d+))", [](const Request& req, Response& res) {
        addCORS(res);
        auto j = parseBody(req, res); if (res.status==400) return;
        db::Question q;
        q.category_id = j["category_id"];
        q.question_type = j["question_type"];
        q.content = j["content"];
        q.options_json = j.value("options",json::array()).dump();
        q.answer = j["answer"];
        q.analysis = j.value("analysis","");
        q.difficulty = j.value("difficulty",1);
        bool ok = g_db->updateQuestion(std::stoi(req.matches[1]), q);
        res.set_content(json{{"success",ok}}.dump(), "application/json");
    });

    svr.Delete(R"(/api/questions/(\d+))", [](const Request& req, Response& res) {
        addCORS(res);
        bool ok = g_db->deleteQuestion(std::stoi(req.matches[1]));
        res.set_content(json{{"success",ok}}.dump(), "application/json");
    });
```

- [ ] **Step 4: Write route handlers — JSON Import**

```cpp
    // === IMPORT ===
    svr.Post("/api/questions/import", [](const Request& req, Response& res) {
        addCORS(res);
        auto j = parseBody(req, res); if (res.status==400) return;
        if (!j.contains("questions") || !j["questions"].is_array()) {
            res.status = 400;
            res.set_content("{\"error\":\"Missing questions array\"}","application/json");
            return;
        }
        int category_id = j.value("category_id", 0);
        std::string category_name = j.value("category_name", "");
        
        // Auto-create category if name provided
        if (category_id == 0 && !category_name.empty()) {
            category_id = g_db->addCategory(category_name, "Imported from JSON");
        }

        int imported = 0, failed = 0;
        for (auto& qj : j["questions"]) {
            try {
                db::Question q;
                q.category_id = qj.value("category_id", category_id);
                q.question_type = qj["question_type"];
                q.content = qj["content"];
                q.options_json = qj.value("options", json::array()).dump();
                q.answer = qj["answer"];
                q.analysis = qj.value("analysis", "");
                q.difficulty = qj.value("difficulty", 1);
                q.source = j.value("source", "import");
                g_db->addQuestion(q);
                imported++;
            } catch (...) { failed++; }
        }
        res.set_content(json{{"imported",imported},{"failed",failed}}.dump(), "application/json");
    });
```

- [ ] **Step 5: Write route handlers — Quiz Engine**

```cpp
    // === QUIZ ===
    svr.Post("/api/quiz/start", [](const Request& req, Response& res) {
        addCORS(res);
        auto j = parseBody(req, res); if (res.status==400) return;
        std::string mode = j.value("mode", "sequential");
        std::optional<int> cat_id;
        if (j.contains("category_id") && j["category_id"].is_number() && j["category_id"].get<int>() > 0) {
            cat_id = j["category_id"].get<int>();
        }
        int count = j.value("count", 10);
        bool wrong_only = j.value("wrong_only", false);
        
        std::vector<db::Question> questions;
        if (wrong_only) {
            auto wrongs = g_db->getWrongQuestions(cat_id);
            for (auto& w : wrongs) {
                auto q = g_db->getQuestion(w.question_id);
                if (q) questions.push_back(*q);
                if ((int)questions.size() >= count) break;
            }
        } else {
            questions = g_db->getQuestions(cat_id, {}, {}, count, 0);
        }
        if (mode == "random") {
            std::random_device rd;
            std::mt19937 g(rd());
            std::shuffle(questions.begin(), questions.end(), g);
        }

        json arr = json::array();
        for (auto& q : questions) {
            arr.push_back({
                {"id",q.id},{"category_id",q.category_id},{"question_type",q.question_type},
                {"content",q.content},{"options",json::parse(q.options_json.empty()?"[]":q.options_json)},
                {"difficulty",q.difficulty}
            });
        }
        res.set_content(json{{"quiz_id",std::time(nullptr)},
            {"questions",arr},{"total",questions.size()}}.dump(), "application/json");
    });

    svr.Post("/api/quiz/submit", [](const Request& req, Response& res) {
        addCORS(res);
        auto j = parseBody(req, res); if (res.status==400) return;
        int question_id = j["question_id"];
        std::string user_answer = j["user_answer"];
        int category_id = j.value("category_id", 0);
        
        auto q = g_db->getQuestion(question_id);
        if (!q) { res.status = 404; return; }

        // Simple answer comparison (case-insensitive, trim)
        auto trim = [](std::string s) {
            s.erase(0, s.find_first_not_of(" \t\n\r"));
            s.erase(s.find_last_not_of(" \t\n\r") + 1);
            return s;
        };
        auto lower = [](std::string s) {
            std::transform(s.begin(), s.end(), s.begin(), ::tolower);
            return s;
        };
        bool correct = lower(trim(user_answer)) == lower(trim(q->answer));

        if (!correct) {
            g_db->addWrongAnswer(question_id, user_answer);
        }

        // Record quiz attempt
        db::QuizRecord rec;
        rec.category_id = category_id;
        rec.mode = j.value("mode", "quiz");
        rec.total_questions = 1;
        rec.correct_count = correct ? 1 : 0;
        rec.wrong_count = correct ? 0 : 1;
        rec.score = correct ? 100.0 : 0.0;
        g_db->addQuizRecord(rec);

        res.set_content(json{
            {"correct",correct},
            {"correct_answer",q->answer},
            {"analysis",q->analysis}
        }.dump(), "application/json");
    });
```

- [ ] **Step 6: Write route handlers — Wrong Questions & Stats**

```cpp
    // === WRONG QUESTIONS ===
    svr.Get("/api/wrong", [](const Request& req, Response& res) {
        addCORS(res);
        std::optional<int> cat_id;
        if (req.has_param("category_id")) cat_id = std::stoi(req.get_param_value("category_id"));
        auto wrongs = g_db->getWrongQuestions(cat_id);
        json arr = json::array();
        for (auto& w : wrongs) {
            arr.push_back({
                {"id",w.id},{"question_id",w.question_id},{"user_answer",w.user_answer},
                {"wrong_count",w.wrong_count},{"last_wrong_at",w.last_wrong_at},
                {"reviewed",w.reviewed},{"content",w.content},
                {"options",json::parse(w.options_json.empty()?"[]":w.options_json)},
                {"answer",w.answer},{"question_type",w.question_type},
                {"category_id",w.category_id},{"category_name",w.category_name}
            });
        }
        int total = g_db->countWrongQuestions(cat_id);
        res.set_content(json{{"wrong_questions",arr},{"total",total}}.dump(), "application/json");
    });

    svr.Delete(R"(/api/wrong/(\d+))", [](const Request& req, Response& res) {
        addCORS(res);
        bool ok = g_db->deleteWrongQuestion(std::stoi(req.matches[1]));
        res.set_content(json{{"success",ok}}.dump(), "application/json");
    });

    svr.Put(R"(/api/wrong/(\d+)/review)", [](const Request& req, Response& res) {
        addCORS(res);
        bool ok = g_db->markWrongReviewed(std::stoi(req.matches[1]));
        res.set_content(json{{"success",ok}}.dump(), "application/json");
    });

    // === STATS ===
    svr.Get("/api/stats", [](const Request&, Response& res) {
        addCORS(res);
        auto stats = g_db->getStats();
        json arr = json::array();
        for (auto& s : stats) {
            arr.push_back({
                {"category_name",s.category_name},
                {"total_questions",s.total_questions},
                {"completed",s.completed},
                {"accuracy",s.accuracy}
            });
        }
        res.set_content(json{{"stats",arr}}.dump(), "application/json");
    });
```

- [ ] **Step 7: Write route handlers — AI Endpoints**

```cpp
    // === AI ===
    svr.Post("/api/ai/analyze", [](const Request& req, Response& res) {
        addCORS(res);
        auto j = parseBody(req, res); if (res.status==400) return;
        AIConfig config{j["provider"], j["base_url"], j["api_key"], j["model"]};
        std::string result = ai::analyzeQuestion(config,
            j["content"], j.value("options","").dump(), j["answer"]);
        res.set_content(json{{"analysis",result}}.dump(), "application/json");
    });

    svr.Post("/api/ai/variant", [](const Request& req, Response& res) {
        addCORS(res);
        auto j = parseBody(req, res); if (res.status==400) return;
        AIConfig config{j["provider"], j["base_url"], j["api_key"], j["model"]};
        std::string result = ai::generateVariant(config,
            j["content"], j["answer"], j["question_type"]);
        res.set_content(json{{"variant",result}}.dump(), "application/json");
    });

    svr.Post("/api/ai/generate", [](const Request& req, Response& res) {
        addCORS(res);
        auto j = parseBody(req, res); if (res.status==400) return;
        AIConfig config{j["provider"], j["base_url"], j["api_key"], j["model"]};
        std::string result = ai::generateQuestion(config,
            j["topic"], j["question_type"], j.value("difficulty",3));
        res.set_content(json{{"question",result}}.dump(), "application/json");
    });
}
```

- [ ] **Step 8: Write main() function**

```cpp
int main(int argc, char* argv[]) {
    int port = 8080;
    std::string db_path = "data/quiz_app.db";
    
    if (argc > 1) port = std::stoi(argv[1]);
    if (argc > 2) db_path = argv[2];

    std::cout << "Initializing database: " << db_path << std::endl;
    g_db = std::make_unique<db::Database>(db_path);

    Server svr;
    setupRoutes(svr);

    // Serve static files
    svr.set_mount_point("/", "./data");
    
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
```

- [ ] **Step 9: Commit quiz_server.cpp**

---

### Task 4: Build & Compilation Fixes

- [ ] **Step 1: Install build dependencies**

Check for g++/MinGW, cmake, libcurl-dev

- [ ] **Step 2: Configure CMake**

Run: `mkdir -p build && cd build && cmake .. -G "MinGW Makefiles"`

- [ ] **Step 3: Build**

Run: `cmake --build .`

- [ ] **Step 4: Fix any compilation errors**

Common issues: sqlite3.c compilation flags, libcurl linking on Windows, C++17 compat.

- [ ] **Step 5: Verify server starts**

Run: `./quiz_server.exe 8080` and check http://localhost:8080/api/categories

---

### Task 5: Single-File Frontend (index.html)

**Files:**
- Create: `index.html`

- [ ] **Step 1: Write HTML structure**

Complete single-file SPA with:
- Bootstrap 5 CDN CSS
- 7 tab navigation (Dashboard, Question Bank, Import, Quiz, Wrong Book, AI Assistant, Stats)
- Responsive layout

- [ ] **Step 2: Write JavaScript API client layer**

```javascript
const API_BASE = '';
async function api(path, options = {}) {
    const res = await fetch(API_BASE + path, {
        headers: { 'Content-Type': 'application/json', ...options.headers },
        ...options
    });
    return res.json();
}
```

- [ ] **Step 3: Write Dashboard page**

Show: category cards with question counts, recent accuracy, quick-start quiz button.

- [ ] **Step 4: Write Question Bank page**

Table/list with search, filter by category/type, add/edit/delete buttons, modal forms.

- [ ] **Step 5: Write Import page**

File upload (JSON), preview, import button with progress feedback.

- [ ] **Step 6: Write Quiz page**

Mode selector (sequential/random/exam/wrong-only), category selector, question count. Answer interface: radio for single, checkbox for multi, text for fill/short. Instant feedback (correct/wrong with answer revealed).

- [ ] **Step 7: Write Wrong Book page**

List wrong questions with original content, user answer vs correct answer, review button.

- [ ] **Step 8: Write AI Assistant page**

API config form (provider selector, base URL, API key, model). Tabs: Analyze (select question → get AI analysis), Variant (generate similar question), Generate (topic input → AI-generated question → save to bank).

- [ ] **Step 9: Write Stats page**

Per-category accuracy bars, total questions, completion rate.

- [ ] **Step 10: Add CSS styles**

Custom styles for clean UI, responsive design, quiz feedback animations.

- [ ] **Step 11: Commit index.html**

---

### Task 6: Extract Questions from PDF (OCR)

**Files:**
- Create: `data/sample_questions.json`

- [ ] **Step 1: Install OCR dependencies**

Run: `pip install pytesseract pdf2image`

- [ ] **Step 2: Install Tesseract OCR**

Download and install Tesseract-OCR for Windows

- [ ] **Step 3: Extract text from PDF pages**

Python script to convert PDF to images, OCR each page, collect text.

- [ ] **Step 4: Parse questions from OCR text**

Identify question patterns (题号、题型标记、选项 A/B/C/D、答案标记), structure into JSON.

- [ ] **Step 5: Create sample_questions.json**

At least 50+ questions from the PDF, properly structured.

- [ ] **Step 6: Commit sample_questions.json**

---

### Task 7: Update 项目规划.md

**Files:**
- Modify: `项目规划.md`

- [ ] **Step 1: Rewrite project doc**

Update all sections to reflect:
- Pure C++17 backend (no Python)
- Remove Python/Flask references
- New directory structure
- New tech stack
- Updated usage instructions (compile & run)
- Remove 华为云盘古 references, keep multi-AI provider support

- [ ] **Step 2: Commit updated doc**

---

### Task 8: End-to-End Testing & Fixes

- [ ] **Step 1: Start server and test all API endpoints**

Test all endpoints with curl or browser.

- [ ] **Step 2: Test frontend flows**

Open browser, test: import questions → browse → quiz → wrong book → stats → AI (demo mode if no API key).

- [ ] **Step 3: Fix any issues found**

- [ ] **Step 4: Final commit**

---

### Task 9: GitHub Deployment

- [ ] **Step 1: Create GitHub repository**

Via `gh` CLI or manually.

- [ ] **Step 2: Add .gitignore**

```
build/
*.exe
*.o
data/quiz_app.db
```

- [ ] **Step 3: Push to GitHub**

- [ ] **Step 4: Add README.md with build and usage instructions**

Include: dependencies, build steps, run instructions, API docs summary.

- [ ] **Step 5: Set up GitHub Pages (if applicable for demo)**

Or document how to run locally for public access.

- [ ] **Step 6: Commit and push**

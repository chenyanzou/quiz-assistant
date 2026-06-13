#include "database.hpp"
#include <iostream>
#include <stdexcept>
#include <cstring>

namespace db {

// ============================================================
// Helper: RAII prepared statement wrapper
// ============================================================
struct StmtGuard {
    sqlite3_stmt* stmt = nullptr;
    StmtGuard(sqlite3* db, const char* sql) {
        int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
        if (rc != SQLITE_OK) {
            std::cerr << "[DB] prepare failed: " << sqlite3_errmsg(db) << " SQL: " << sql << std::endl;
            stmt = nullptr;
        }
    }
    ~StmtGuard() {
        if (stmt) sqlite3_finalize(stmt);
    }
    sqlite3_stmt* get() const { return stmt; }
    operator bool() const { return stmt != nullptr; }
};

// Helper: read a nullable text column safely
static std::string colText(sqlite3_stmt* stmt, int col) {
    int type = sqlite3_column_type(stmt, col);
    if (type == SQLITE_NULL) return "";
    const char* text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, col));
    return text ? std::string(text) : std::string();
}

// Helper: bind text, handling empty string as NULL for optional columns
static void bindText(sqlite3_stmt* stmt, int idx, const std::string& val) {
    sqlite3_bind_text(stmt, idx, val.c_str(), -1, SQLITE_TRANSIENT);
}

// ============================================================
// Constructor / Destructor
// ============================================================
Database::Database(const std::string& path) : db_(nullptr) {
    int rc = sqlite3_open(path.c_str(), &db_);
    if (rc != SQLITE_OK || !db_) {
        std::string err = db_ ? sqlite3_errmsg(db_) : "unknown";
        throw std::runtime_error("Failed to open database: " + path + " - " + err);
    }
    // Enable WAL mode and foreign keys
    exec("PRAGMA journal_mode=WAL");
    exec("PRAGMA foreign_keys=ON");
    initTables();
}

Database::~Database() {
    if (db_) {
        sqlite3_close(db_);
        db_ = nullptr;
    }
}

void Database::exec(const std::string& sql) {
    char* errMsg = nullptr;
    int rc = sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK) {
        std::string err = errMsg ? errMsg : "unknown";
        std::cerr << "[DB] exec failed: " << err << " SQL: " << sql << std::endl;
        sqlite3_free(errMsg);
    }
}

// ============================================================
// Schema Initialization
// ============================================================
void Database::initTables() {
    exec(
        "CREATE TABLE IF NOT EXISTS categories ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  name VARCHAR(100) UNIQUE NOT NULL,"
        "  description TEXT,"
        "  created_at DATETIME DEFAULT CURRENT_TIMESTAMP"
        ")"
    );

    exec(
        "CREATE TABLE IF NOT EXISTS questions ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  category_id INTEGER REFERENCES categories(id),"
        "  question_type VARCHAR(20) CHECK(question_type IN ('single','multi','judge','fill','short')),"
        "  content TEXT NOT NULL,"
        "  options_json TEXT DEFAULT '[]',"
        "  answer TEXT NOT NULL,"
        "  analysis TEXT DEFAULT '',"
        "  difficulty INTEGER DEFAULT 1 CHECK(difficulty BETWEEN 1 AND 5),"
        "  source VARCHAR(200) DEFAULT 'manual',"
        "  created_at DATETIME DEFAULT CURRENT_TIMESTAMP"
        ")"
    );

    exec(
        "CREATE TABLE IF NOT EXISTS user_records ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  category_id INTEGER,"
        "  mode VARCHAR(20),"
        "  total_questions INTEGER,"
        "  correct_count INTEGER,"
        "  wrong_count INTEGER,"
        "  score FLOAT,"
        "  created_at DATETIME DEFAULT CURRENT_TIMESTAMP"
        ")"
    );

    exec(
        "CREATE TABLE IF NOT EXISTS wrong_questions ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  question_id INTEGER REFERENCES questions(id) UNIQUE,"
        "  user_answer TEXT,"
        "  wrong_count INTEGER DEFAULT 1,"
        "  last_wrong_at DATETIME DEFAULT CURRENT_TIMESTAMP,"
        "  reviewed BOOLEAN DEFAULT 0"
        ")"
    );
}

// ============================================================
// Categories
// ============================================================
std::vector<Category> Database::getCategories() {
    std::vector<Category> result;
    const char* sql = "SELECT id, name, description, created_at FROM categories ORDER BY id";
    StmtGuard stmt(db_, sql);
    if (!stmt) return result;

    while (sqlite3_step(stmt.get()) == SQLITE_ROW) {
        Category c;
        c.id = sqlite3_column_int(stmt.get(), 0);
        c.name = colText(stmt.get(), 1);
        c.description = colText(stmt.get(), 2);
        c.created_at = colText(stmt.get(), 3);
        result.push_back(std::move(c));
    }
    return result;
}

std::optional<Category> Database::getCategory(int id) {
    const char* sql = "SELECT id, name, description, created_at FROM categories WHERE id = ?";
    StmtGuard stmt(db_, sql);
    if (!stmt) return std::nullopt;

    sqlite3_bind_int(stmt.get(), 1, id);

    if (sqlite3_step(stmt.get()) == SQLITE_ROW) {
        Category c;
        c.id = sqlite3_column_int(stmt.get(), 0);
        c.name = colText(stmt.get(), 1);
        c.description = colText(stmt.get(), 2);
        c.created_at = colText(stmt.get(), 3);
        return c;
    }
    return std::nullopt;
}

int Database::addCategory(const std::string& name, const std::string& desc) {
    const char* sql = "INSERT INTO categories (name, description) VALUES (?, ?)";
    StmtGuard stmt(db_, sql);
    if (!stmt) return -1;

    bindText(stmt.get(), 1, name);
    bindText(stmt.get(), 2, desc);

    int rc = sqlite3_step(stmt.get());
    if (rc != SQLITE_DONE) {
        std::cerr << "[DB] addCategory failed: " << sqlite3_errmsg(db_) << std::endl;
        return -1;
    }
    return static_cast<int>(sqlite3_last_insert_rowid(db_));
}

bool Database::deleteCategory(int id) {
    const char* sql = "DELETE FROM categories WHERE id = ?";
    StmtGuard stmt(db_, sql);
    if (!stmt) return false;

    sqlite3_bind_int(stmt.get(), 1, id);
    int rc = sqlite3_step(stmt.get());
    if (rc != SQLITE_DONE) {
        std::cerr << "[DB] deleteCategory failed: " << sqlite3_errmsg(db_) << std::endl;
        return false;
    }
    return sqlite3_changes(db_) > 0;
}

// ============================================================
// Questions
// ============================================================
std::vector<Question> Database::getQuestions(
    std::optional<int> category_id,
    std::optional<std::string> type,
    std::optional<std::string> search,
    int limit, int offset)
{
    std::vector<Question> result;
    std::string sql = "SELECT id, category_id, question_type, content, options_json, "
                      "answer, analysis, difficulty, source, created_at "
                      "FROM questions WHERE 1=1";

    if (category_id.has_value()) sql += " AND category_id = ?";
    if (type.has_value())        sql += " AND question_type = ?";
    if (search.has_value())      sql += " AND content LIKE ?";
    sql += " ORDER BY id LIMIT ? OFFSET ?";

    StmtGuard stmt(db_, sql.c_str());
    if (!stmt) return result;

    int bindIdx = 1;
    if (category_id.has_value()) sqlite3_bind_int(stmt.get(), bindIdx++, category_id.value());
    if (type.has_value())        bindText(stmt.get(), bindIdx++, type.value());
    if (search.has_value()) {
        std::string pattern = "%" + search.value() + "%";
        sqlite3_bind_text(stmt.get(), bindIdx++, pattern.c_str(), -1, SQLITE_TRANSIENT);
    }
    sqlite3_bind_int(stmt.get(), bindIdx++, limit);
    sqlite3_bind_int(stmt.get(), bindIdx++, offset);

    while (sqlite3_step(stmt.get()) == SQLITE_ROW) {
        Question q;
        q.id = sqlite3_column_int(stmt.get(), 0);
        q.category_id = sqlite3_column_int(stmt.get(), 1);
        q.question_type = colText(stmt.get(), 2);
        q.content = colText(stmt.get(), 3);
        q.options_json = colText(stmt.get(), 4);
        q.answer = colText(stmt.get(), 5);
        q.analysis = colText(stmt.get(), 6);
        q.difficulty = sqlite3_column_int(stmt.get(), 7);
        q.source = colText(stmt.get(), 8);
        q.created_at = colText(stmt.get(), 9);
        result.push_back(std::move(q));
    }
    return result;
}

int Database::countQuestions(
    std::optional<int> category_id,
    std::optional<std::string> type,
    std::optional<std::string> search)
{
    std::string sql = "SELECT COUNT(*) FROM questions WHERE 1=1";
    if (category_id.has_value()) sql += " AND category_id = ?";
    if (type.has_value())        sql += " AND question_type = ?";
    if (search.has_value())      sql += " AND content LIKE ?";

    StmtGuard stmt(db_, sql.c_str());
    if (!stmt) return 0;

    int bindIdx = 1;
    if (category_id.has_value()) sqlite3_bind_int(stmt.get(), bindIdx++, category_id.value());
    if (type.has_value())        bindText(stmt.get(), bindIdx++, type.value());
    if (search.has_value()) {
        std::string pattern = "%" + search.value() + "%";
        sqlite3_bind_text(stmt.get(), bindIdx++, pattern.c_str(), -1, SQLITE_TRANSIENT);
    }

    if (sqlite3_step(stmt.get()) == SQLITE_ROW) {
        return sqlite3_column_int(stmt.get(), 0);
    }
    return 0;
}

std::optional<Question> Database::getQuestion(int id) {
    const char* sql = "SELECT id, category_id, question_type, content, options_json, "
                      "answer, analysis, difficulty, source, created_at "
                      "FROM questions WHERE id = ?";
    StmtGuard stmt(db_, sql);
    if (!stmt) return std::nullopt;

    sqlite3_bind_int(stmt.get(), 1, id);

    if (sqlite3_step(stmt.get()) == SQLITE_ROW) {
        Question q;
        q.id = sqlite3_column_int(stmt.get(), 0);
        q.category_id = sqlite3_column_int(stmt.get(), 1);
        q.question_type = colText(stmt.get(), 2);
        q.content = colText(stmt.get(), 3);
        q.options_json = colText(stmt.get(), 4);
        q.answer = colText(stmt.get(), 5);
        q.analysis = colText(stmt.get(), 6);
        q.difficulty = sqlite3_column_int(stmt.get(), 7);
        q.source = colText(stmt.get(), 8);
        q.created_at = colText(stmt.get(), 9);
        return q;
    }
    return std::nullopt;
}

int Database::addQuestion(const Question& q) {
    const char* sql = "INSERT INTO questions "
                      "(category_id, question_type, content, options_json, answer, "
                      " analysis, difficulty, source) "
                      "VALUES (?, ?, ?, ?, ?, ?, ?, ?)";
    StmtGuard stmt(db_, sql);
    if (!stmt) return -1;

    sqlite3_bind_int(stmt.get(), 1, q.category_id);
    bindText(stmt.get(), 2, q.question_type);
    bindText(stmt.get(), 3, q.content);
    bindText(stmt.get(), 4, q.options_json);
    bindText(stmt.get(), 5, q.answer);
    bindText(stmt.get(), 6, q.analysis);
    sqlite3_bind_int(stmt.get(), 7, q.difficulty);
    bindText(stmt.get(), 8, q.source);

    int rc = sqlite3_step(stmt.get());
    if (rc != SQLITE_DONE) {
        std::cerr << "[DB] addQuestion failed: " << sqlite3_errmsg(db_) << std::endl;
        return -1;
    }
    return static_cast<int>(sqlite3_last_insert_rowid(db_));
}

bool Database::updateQuestion(int id, const Question& q) {
    const char* sql = "UPDATE questions SET category_id=?, question_type=?, content=?, "
                      "options_json=?, answer=?, analysis=?, difficulty=?, source=? "
                      "WHERE id=?";
    StmtGuard stmt(db_, sql);
    if (!stmt) return false;

    sqlite3_bind_int(stmt.get(), 1, q.category_id);
    bindText(stmt.get(), 2, q.question_type);
    bindText(stmt.get(), 3, q.content);
    bindText(stmt.get(), 4, q.options_json);
    bindText(stmt.get(), 5, q.answer);
    bindText(stmt.get(), 6, q.analysis);
    sqlite3_bind_int(stmt.get(), 7, q.difficulty);
    bindText(stmt.get(), 8, q.source);
    sqlite3_bind_int(stmt.get(), 9, id);

    int rc = sqlite3_step(stmt.get());
    if (rc != SQLITE_DONE) {
        std::cerr << "[DB] updateQuestion failed: " << sqlite3_errmsg(db_) << std::endl;
        return false;
    }
    return sqlite3_changes(db_) > 0;
}

bool Database::deleteQuestion(int id) {
    const char* sql = "DELETE FROM questions WHERE id = ?";
    StmtGuard stmt(db_, sql);
    if (!stmt) return false;

    sqlite3_bind_int(stmt.get(), 1, id);
    int rc = sqlite3_step(stmt.get());
    if (rc != SQLITE_DONE) {
        std::cerr << "[DB] deleteQuestion failed: " << sqlite3_errmsg(db_) << std::endl;
        return false;
    }
    return sqlite3_changes(db_) > 0;
}

// ============================================================
// Quiz Records
// ============================================================
int Database::addQuizRecord(const QuizRecord& r) {
    const char* sql = "INSERT INTO user_records "
                      "(category_id, mode, total_questions, correct_count, wrong_count, score) "
                      "VALUES (?, ?, ?, ?, ?, ?)";
    StmtGuard stmt(db_, sql);
    if (!stmt) return -1;

    sqlite3_bind_int(stmt.get(), 1, r.category_id);
    bindText(stmt.get(), 2, r.mode);
    sqlite3_bind_int(stmt.get(), 3, r.total_questions);
    sqlite3_bind_int(stmt.get(), 4, r.correct_count);
    sqlite3_bind_int(stmt.get(), 5, r.wrong_count);
    sqlite3_bind_double(stmt.get(), 6, r.score);

    int rc = sqlite3_step(stmt.get());
    if (rc != SQLITE_DONE) {
        std::cerr << "[DB] addQuizRecord failed: " << sqlite3_errmsg(db_) << std::endl;
        return -1;
    }
    return static_cast<int>(sqlite3_last_insert_rowid(db_));
}

std::vector<QuizRecord> Database::getQuizRecords(int category_id, int limit) {
    std::vector<QuizRecord> result;
    const char* sql = "SELECT id, category_id, mode, total_questions, correct_count, "
                      "wrong_count, score, created_at "
                      "FROM user_records WHERE category_id = ? "
                      "ORDER BY created_at DESC LIMIT ?";
    StmtGuard stmt(db_, sql);
    if (!stmt) return result;

    sqlite3_bind_int(stmt.get(), 1, category_id);
    sqlite3_bind_int(stmt.get(), 2, limit);

    while (sqlite3_step(stmt.get()) == SQLITE_ROW) {
        QuizRecord r;
        r.id = sqlite3_column_int(stmt.get(), 0);
        r.category_id = sqlite3_column_int(stmt.get(), 1);
        r.mode = colText(stmt.get(), 2);
        r.total_questions = sqlite3_column_int(stmt.get(), 3);
        r.correct_count = sqlite3_column_int(stmt.get(), 4);
        r.wrong_count = sqlite3_column_int(stmt.get(), 5);
        r.score = sqlite3_column_double(stmt.get(), 6);
        r.created_at = colText(stmt.get(), 7);
        result.push_back(std::move(r));
    }
    return result;
}

// ============================================================
// Wrong Questions
// ============================================================
void Database::addWrongAnswer(int question_id, const std::string& user_answer) {
    const char* sql =
        "INSERT INTO wrong_questions (question_id, user_answer, wrong_count, last_wrong_at, reviewed) "
        "VALUES (?, ?, 1, CURRENT_TIMESTAMP, 0) "
        "ON CONFLICT(question_id) DO UPDATE SET "
        "  user_answer = excluded.user_answer, "
        "  wrong_count = wrong_questions.wrong_count + 1, "
        "  last_wrong_at = CURRENT_TIMESTAMP, "
        "  reviewed = 0";
    StmtGuard stmt(db_, sql);
    if (!stmt) return;

    sqlite3_bind_int(stmt.get(), 1, question_id);
    bindText(stmt.get(), 2, user_answer);

    int rc = sqlite3_step(stmt.get());
    if (rc != SQLITE_DONE) {
        std::cerr << "[DB] addWrongAnswer failed: " << sqlite3_errmsg(db_) << std::endl;
    }
}

std::vector<WrongQuestion> Database::getWrongQuestions(std::optional<int> category_id) {
    std::vector<WrongQuestion> result;
    std::string sql =
        "SELECT w.id, w.question_id, w.user_answer, w.wrong_count, w.last_wrong_at, "
        "       w.reviewed, "
        "       q.content, q.options_json, q.answer, q.question_type, q.category_id, "
        "       c.name AS category_name "
        "FROM wrong_questions w "
        "JOIN questions q ON q.id = w.question_id "
        "JOIN categories c ON c.id = q.category_id ";

    if (category_id.has_value()) {
        sql += "WHERE q.category_id = ? ";
    }
    sql += "ORDER BY w.last_wrong_at DESC";

    StmtGuard stmt(db_, sql.c_str());
    if (!stmt) return result;

    if (category_id.has_value()) {
        sqlite3_bind_int(stmt.get(), 1, category_id.value());
    }

    while (sqlite3_step(stmt.get()) == SQLITE_ROW) {
        WrongQuestion w;
        w.id = sqlite3_column_int(stmt.get(), 0);
        w.question_id = sqlite3_column_int(stmt.get(), 1);
        w.user_answer = colText(stmt.get(), 2);
        w.wrong_count = sqlite3_column_int(stmt.get(), 3);
        w.last_wrong_at = colText(stmt.get(), 4);
        w.reviewed = sqlite3_column_int(stmt.get(), 5) != 0;
        w.content = colText(stmt.get(), 6);
        w.options_json = colText(stmt.get(), 7);
        w.answer = colText(stmt.get(), 8);
        w.question_type = colText(stmt.get(), 9);
        w.category_id = sqlite3_column_int(stmt.get(), 10);
        w.category_name = colText(stmt.get(), 11);
        result.push_back(std::move(w));
    }
    return result;
}

int Database::countWrongQuestions(std::optional<int> category_id) {
    std::string sql =
        "SELECT COUNT(*) FROM wrong_questions w "
        "JOIN questions q ON q.id = w.question_id ";

    if (category_id.has_value()) {
        sql += "WHERE q.category_id = ?";
    }

    StmtGuard stmt(db_, sql.c_str());
    if (!stmt) return 0;

    if (category_id.has_value()) {
        sqlite3_bind_int(stmt.get(), 1, category_id.value());
    }

    if (sqlite3_step(stmt.get()) == SQLITE_ROW) {
        return sqlite3_column_int(stmt.get(), 0);
    }
    return 0;
}

bool Database::deleteWrongQuestion(int id) {
    const char* sql = "DELETE FROM wrong_questions WHERE id = ?";
    StmtGuard stmt(db_, sql);
    if (!stmt) return false;

    sqlite3_bind_int(stmt.get(), 1, id);
    int rc = sqlite3_step(stmt.get());
    if (rc != SQLITE_DONE) {
        std::cerr << "[DB] deleteWrongQuestion failed: " << sqlite3_errmsg(db_) << std::endl;
        return false;
    }
    return sqlite3_changes(db_) > 0;
}

bool Database::markWrongReviewed(int id) {
    const char* sql = "UPDATE wrong_questions SET reviewed = 1 WHERE id = ?";
    StmtGuard stmt(db_, sql);
    if (!stmt) return false;

    sqlite3_bind_int(stmt.get(), 1, id);
    int rc = sqlite3_step(stmt.get());
    if (rc != SQLITE_DONE) {
        std::cerr << "[DB] markWrongReviewed failed: " << sqlite3_errmsg(db_) << std::endl;
        return false;
    }
    return sqlite3_changes(db_) > 0;
}

// ============================================================
// Statistics
// ============================================================
std::vector<StatsItem> Database::getStats() {
    std::vector<StatsItem> result;

    // Per-category stats using subqueries:
    //  - total_questions: count of questions in that category
    //  - completed: count of unique quiz records for that category with score > 0
    //  - accuracy: AVG(correct_count / total_questions) across quiz records for that category
    const char* sql =
        "SELECT "
        "  c.name AS category_name,"
        "  COALESCE((SELECT COUNT(*) FROM questions WHERE category_id = c.id), 0),"
        "  COALESCE((SELECT COUNT(*) FROM user_records WHERE category_id = c.id AND score > 0), 0),"
        "  COALESCE("
        "    (SELECT AVG(CAST(correct_count AS REAL) / NULLIF(total_questions, 0)) "
        "     FROM user_records WHERE category_id = c.id AND total_questions > 0),"
        "    0.0"
        "  )"
        "FROM categories c "
        "ORDER BY c.name";

    StmtGuard stmt(db_, sql);
    if (!stmt) return result;

    while (sqlite3_step(stmt.get()) == SQLITE_ROW) {
        StatsItem s;
        s.category_name = colText(stmt.get(), 0);
        s.total_questions = sqlite3_column_int(stmt.get(), 1);
        s.completed = sqlite3_column_int(stmt.get(), 2);
        s.accuracy = sqlite3_column_double(stmt.get(), 3);
        result.push_back(std::move(s));
    }
    return result;
}

} // namespace db

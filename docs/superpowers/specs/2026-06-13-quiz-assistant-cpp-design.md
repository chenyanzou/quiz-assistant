# 智能题库刷题助手 — C++ 版设计文档

## 项目概述

基于 C++17 + cpp-httplib + SQLite3 的智能刷题系统，所有功能由一个可执行文件 `quiz_server.exe` 和一个 HTML 文件 `index.html` 提供。支持 JSON 题库导入、多模式刷题、AI 智能解析与举一反三、错题管理。

## 技术栈

| 层级 | 技术 | 说明 |
|------|------|------|
| 主后端 | C++17 + cpp-httplib | header-only HTTP 服务 |
| 数据库 | SQLite3 | C API，零配置 |
| AI 调用 | libcurl + nlohmann/json | 多服务商 OpenAI 兼容 |
| 前端 | Bootstrap 5 CDN + 原生 JS | 全功能 SPA 单文件 |

## 架构

```
index.html (SPA) → REST API → quiz_server.exe
                                  ├── HTTP Router (cpp-httplib)
                                  ├── Quiz Engine (刷题逻辑)
                                  ├── AI Service (libcurl)
                                  ├── Database Layer (SQLite3)
                                  └── JSON Parser (nlohmann/json)
```

## 目录结构

```
C++数据结构与算法编程作业/
├── quiz_server.cpp          # 主程序 + HTTP 路由
├── database.hpp             # SQLite3 封装
├── quiz_engine.hpp          # 刷题引擎
├── ai_service.hpp           # AI 多服务商 API
├── CMakeLists.txt           # 构建配置
├── lib/                     # 第三方库
│   ├── httplib.h            # cpp-httplib (header-only)
│   ├── json.hpp             # nlohmann/json (header-only)
│   └── sqlite3.h + sqlite3.c # SQLite3 amalgamation
├── data/
│   ├── sample_questions.json # 示例题库
│   └── quiz_app.db           # 运行时自动创建
├── index.html               # 单文件前端 SPA
└── 项目规划.md               # 项目规划文档
```

## API 路由

| 方法 | 路径 | 说明 |
|------|------|------|
| GET | /api/categories | 获取科目列表 |
| POST | /api/categories | 添加科目 |
| DELETE | /api/categories/:id | 删除科目 |
| GET | /api/questions | 题目列表(支持筛选) |
| POST | /api/questions | 手动添加题目 |
| PUT | /api/questions/:id | 编辑题目 |
| DELETE | /api/questions/:id | 删除题目 |
| POST | /api/questions/import | JSON 批量导入 |
| POST | /api/quiz/start | 开始刷题 |
| POST | /api/quiz/submit | 提交单题答案 |
| GET | /api/wrong | 错题列表 |
| DELETE | /api/wrong/:id | 移除错题 |
| PUT | /api/wrong/:id/review | 标记已复习 |
| GET | /api/stats | 学习统计 |
| POST | /api/ai/analyze | AI 解析题目 |
| POST | /api/ai/variant | 举一反三 |
| POST | /api/ai/generate | AI 智能出题 |

## 数据库设计

- categories(id, name, description, created_at)
- questions(id, category_id, type, content, options_json, answer, analysis, difficulty, source, created_at)
- user_records(id, category_id, mode, total, correct, wrong, score, created_at)
- wrong_questions(id, question_id, user_answer, wrong_count, last_wrong_at, reviewed)

## 前端页面（Tab 切换）

1. 首页仪表盘 — 科目概览、进度统计
2. 题库管理 — 浏览/搜索/CRUD
3. 题库导入 — JSON 文件上传
4. 刷题页 — 顺序/随机/考试
5. 错题本 — 错题回顾/重做
6. AI 助手 — 解析/变体/出题 + API 配置
7. 学习统计 — 正确率趋势

# 智能题库刷题助手 (C++ 版)

基于 **c17 cpp-httplib SQLite3** 的智能刷题系统。编译为一个可执行文件 + 一个 HTML 文件。

## 功能

- 📚 **题库管理** — JSON 导入、手动添加、搜索筛选（支持单选/多选/判断/填空/简答）
- ✍️ **多模式刷题** — 顺序刷题 / 随机刷题 / 模拟考试 / 错题重做
- 📝 **错题本** — 自动收集、标记复习、错题统计
- 🤖 **AI 智能助手** — 题目解析 / 举一反三 / AI 出题（支持 OpenAI、Kimi、DeepSeek、通义千问、智谱GLM、Ollama）
- 📊 **学习统计** — 正确率趋势、刷题历史
- 🌐 **单文件前端** — Bootstrap 5   Chart.js，响应式设计

## 技术栈

| 层级 | 技术 |
|------|------|
| c17, pcp -httplib 0.34, SQLite3 |
| AI 调用 | libcurl (OpenAI 兼容 API) |
| JSON | nlohmann/ JSON |
| 前端 | Bootstrap 5, Chart.js, 原生 JS |
| 构建 | g   / MinGW |

## 快速开始

### 编译

```bash
# 1. 编译 SQLite3 (C)
gcc -c lib/sqlite3.c -o build/sqlite3.o -w

# 2. 编译 C++ 源文件
g++ -std=c++17 -I. -Ilib -Ilib/curl -O2 -c database.cpp -o build/database.o
g++ -std=c++17 -I. -Ilib -Ilib/curl -O2 -c ai_service.cpp -o build/ai_service.o
g++ -std=c++17 -I. -Ilib -Ilib/curl -O2 -c quiz_server.cpp -o build/quiz_server.o

# 3. 链接
g++ -std=c++17 -O2 build/database.o build/ai_service.o build/quiz_server.o \
    build/sqlite3.o lib/libcurl.dll.a -lws2_32 -lcrypt32 -o quiz_server.exe
```

### 运行

”“bash
./quiz_server.exe          # 默认端口 8080
./quiz_server.exe 3000     # 自定义端口
```

浏览器打开 `http://localhost:8080`

### 导入示例题库

1. 点击「题库导入」标签页
2. 选择 `data/sample_questions.json`
3. 点击「导入题目」
4. 80 道入党积极分子考试题目即导入完成

## 目录结构

```
├── quiz_server.cpp      # 主程序 + HTTP 路由
├── database.hpp/cpp      # SQLite3 数据库层
├── ai_service.hpp/cpp    # AI 服务 (libcurl)
├── index.html            # 单文件前端 SPA
├── CMakeLists.txt        # 构建配置
├── lib/                  # 第三方库 (header-only)
│   ├── httplib.h        # cpp-httplib
《中华人民共和国中华人民共和国宪法》
│├──SQLite3 .h/ c# SQLite3合并
│   ├── libcurl.dll.a    # libcurl 导入库
│   └── curl/            # libcurl 头文件
├── data/
│   └── sample_questions.json  # 示例题库
└── 项目规划.md           # 项目文档
```

## API

| 方法 | 路径 | 说明 |
|------|------|------|
| GET | /api/categories | 科目列表 |
| POST | /api/categories | 添加科目 |
| DELETE | /api/categories/{id} | 删除科目 |
| GET | /api/questions | 题目列表 |
| POST | /api/questions | 添加题目 |
| PUT | /api/questions/{id} | 编辑题目 |
| DELETE | /api/questions/{id} | 删除题目 |
| POST | /api/questions/import | JSON 批量导入 |
| POST | /api/quiz/start | 开始刷题 |
| POST | /api/quiz/submit | 提交答案 |
| GET | /api/wrong | 错题列表 |
| DELETE | /api/wrong/{id} | 删除错题 |
| PUT | /api/wrong/{id}/review | 标记已复习 |
| GET | /api/stats | 学习统计 |
| POST | /api/ai/analyze | AI 解析 |
| POST | /api/ai/variant | AI 变体题 |
| POST | /api/ai/generate | AI 出题 |

## 许可证

与条款

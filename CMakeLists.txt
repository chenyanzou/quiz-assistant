cmake_minimum_required(VERSION 3.14)
project(QuizServer VERSION 1.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# libcurl (manual paths — import library generated from system DLL)
add_library(curl_import STATIC IMPORTED)
set_target_properties(curl_import PROPERTIES
    IMPORTED_LOCATION "${CMAKE_CURRENT_SOURCE_DIR}/lib/libcurl.dll.a"
    INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/lib/curl"
)

# SQLite3 amalgamation
add_library(sqlite3 ${CMAKE_CURRENT_SOURCE_DIR}/lib/sqlite3.c)
target_include_directories(sqlite3 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/lib)

# Main executable
add_executable(quiz_server
    quiz_server.cpp
    database.cpp
    ai_service.cpp
)

target_include_directories(quiz_server PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/lib
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/curl
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(quiz_server PRIVATE
    sqlite3
    curl_import
)

if(WIN32)
    target_link_libraries(quiz_server PRIVATE ws2_32 crypt32)
endif()

# Suppress SQLite warnings
set_target_properties(sqlite3 PROPERTIES COMPILE_FLAGS "-w")

# Create data directory
file(MAKE_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/data)

# --------------- Direct build commands (works around CMake path encoding issues) ---------------
# g++ -std=c++17 -I. -Ilib -Ilib/curl -O2 -c database.cpp -o build/database.o
# g++ -std=c++17 -I. -Ilib -Ilib/curl -O2 -c ai_service.cpp -o build/ai_service.o
# g++ -std=c++17 -I. -Ilib -Ilib/curl -O2 -c quiz_server.cpp -o build/quiz_server.o
# gcc  -c lib/sqlite3.c -o build/sqlite3.o -w
# g++ -std=c++17 -O2 build/database.o build/ai_service.o build/quiz_server.o \
#     build/sqlite3.o lib/libcurl.dll.a -lws2_32 -lcrypt32 -o quiz_server.exe

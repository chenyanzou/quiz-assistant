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

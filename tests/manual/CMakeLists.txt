# A1 预验证诊断程序（独立于主构建）：
#   cmake -B build-manual -S tests/manual -DCMAKE_PREFIX_PATH=/opt/homebrew/opt/qt
#   cmake --build build-manual
cmake_minimum_required(VERSION 3.21)
project(a1_backend_diag LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Quick Gui Core)

add_executable(a1_backend_diag a1_backend_diag.cpp)
target_link_libraries(a1_backend_diag PRIVATE Qt6::Quick Qt6::Gui Qt6::Core)

cmake_minimum_required(VERSION 3.15)
project(muduo VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 2. 【优化】不要硬编码 set(LIBS pthread)，使用官方标准方式查找线程库
# 这样可以自动处理 Linux/Windows/Mac 不同的线程旗标
# set(LIBRARY_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/lib)
# set(LIBS pthread)
find_package(Threads REQUIRED)

# 3. 引导子目录
add_subdirectory(src)
add_subdirectory(example)


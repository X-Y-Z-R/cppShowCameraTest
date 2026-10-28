cmake_minimum_required(VERSION 3.10)
project(cppShowCameraTest)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    add_compile_options("/utf-8")
endif()

# OpenCV 路径（与工程内其他模块保持一致）
set(OpenCV_DIR "H:/opencv/build/x64/vc16/lib")
find_package(OpenCV REQUIRED)

# 可执行文件
add_executable(cppShowCameraTest main.cpp)

# 头文件目录
target_include_directories(cppShowCameraTest PRIVATE ${OpenCV_INCLUDE_DIRS})

# 链接库
target_link_libraries(cppShowCameraTest ${OpenCV_LIBS})
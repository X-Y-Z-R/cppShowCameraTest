cmake_minimum_required(VERSION 3.16)
project(cppShowCameraTest LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

if(MSVC)
    add_compile_options("/utf-8")
endif()

# ============================================
# 第三方库路径（与工程内其他模块保持一致）
# ============================================
set(OpenCV_DIR "H:/opencv/build/x64/vc16/lib")

find_package(OpenCV REQUIRED)

# Qt5 / Qt6 均可，优先使用 Qt6（可通过 Qt5_DIR / Qt6_DIR 或 CMAKE_PREFIX_PATH 指定）
find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets)

# ============================================
# 可执行文件
# ============================================
add_executable(cppShowCameraTest
    main.cpp
    mainwindow.h
    mainwindow.cpp
    capturethread.h
    capturethread.cpp
    processorthread.h
    processorthread.cpp
    detector.h
    detector.cpp
    framequeue.h
    framequeue.cpp
    cameradevice.h
    cameradevice.cpp
    uitheme.h
    uitheme.cpp
)

target_include_directories(cppShowCameraTest PRIVATE ${OpenCV_INCLUDE_DIRS})

target_link_libraries(cppShowCameraTest PRIVATE
    Qt${QT_VERSION_MAJOR}::Widgets
    ${OpenCV_LIBS}
)

# DirectShow 设备枚举所需的系统库
if(WIN32)
    target_link_libraries(cppShowCameraTest PRIVATE
        strmiids
        ole32
        oleaut32
    )
endif()

# ============================================
# 运行时依赖部署
# ============================================
if(WIN32)
    # Qt 运行库：由 Qt 配置目录反推安装根目录，再查找 windeployqt
    if(QT_VERSION_MAJOR EQUAL 6)
        set(_qt_cmake_config_dir "${Qt6_DIR}")
    else()
        set(_qt_cmake_config_dir "${Qt5_DIR}")
    endif()
    get_filename_component(_qt_prefix "${_qt_cmake_config_dir}/../../.." ABSOLUTE)
    find_program(WINDEPLOYQT_EXECUTABLE windeployqt
                 PATHS "${_qt_prefix}/bin" NO_DEFAULT_PATH)

    if(NOT WINDEPLOYQT_EXECUTABLE)
        find_program(WINDEPLOYQT_EXECUTABLE windeployqt)
    endif()

    if(WINDEPLOYQT_EXECUTABLE)
        add_custom_command(TARGET cppShowCameraTest POST_BUILD
            COMMAND "${WINDEPLOYQT_EXECUTABLE}" --no-translations
                    "$<TARGET_FILE:cppShowCameraTest>"
            COMMENT "Deploying Qt runtime..."
        )
    else()
        message(WARNING "windeployqt not found, 请手动部署 Qt DLL")
    endif()

    # OpenCV 运行库
    set(OpenCV_BIN_DIR "H:/opencv/build/x64/vc16/bin")
    file(GLOB OPENCV_DLLS "${OpenCV_BIN_DIR}/opencv_world*.dll")
    if(OPENCV_DLLS)
        add_custom_command(TARGET cppShowCameraTest POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    ${OPENCV_DLLS}
                    "$<TARGET_FILE_DIR:cppShowCameraTest>"
            COMMENT "Copying OpenCV DLL..."
        )
    endif()
endif()

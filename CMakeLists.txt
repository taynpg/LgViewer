cmake_minimum_required(VERSION 3.21)
project(LgViewer VERSION 0.1.0 LANGUAGES CXX)

# 构建标记：dev / release（打包脚本通过 -DLG_BUILD_MARK= 传入）
set(LG_BUILD_MARK "dev" CACHE STRING "Build mark: dev or release")

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Widgets)

# ---------------------------------------------------------------- PCRE2 ------
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(PCRE2 QUIET IMPORTED_TARGET libpcre2-8)
endif()

if(NOT PCRE2_FOUND)
    find_path(PCRE2_INCLUDE_DIR NAMES pcre2.h)
    find_library(PCRE2_LIBRARY NAMES pcre2-8 libpcre2-8)
    if(NOT PCRE2_INCLUDE_DIR OR NOT PCRE2_LIBRARY)
        message(FATAL_ERROR
            "PCRE2 (8-bit) not found.\n"
            "MSYS2: pacman -S mingw-w64-ucrt-x86_64-pcre2\n"
            "Debian: apt install libpcre2-dev")
    endif()
    add_library(pcre2_8 INTERFACE)
    target_include_directories(pcre2_8 INTERFACE ${PCRE2_INCLUDE_DIR})
    target_link_libraries(pcre2_8 INTERFACE ${PCRE2_LIBRARY})
    set(PCRE2_TARGET pcre2_8)
else()
    set(PCRE2_TARGET PkgConfig::PCRE2)
endif()

# ---------------------------------------------------------------- sources ----
set(PROJECT_SOURCES
    src/main.cpp
    src/common.h
    src/common.cpp
    src/textencoding.h
    src/textencoding.cpp
    src/regexengine.h
    src/regexengine.cpp
    src/timeparse.h
    src/timeparse.cpp
    src/logpattern.h
    src/logpattern.cpp
    src/logdocument.h
    src/logdocument.cpp
    src/logview.h
    src/logview.cpp
    src/filterdialog.h
    src/filterdialog.cpp
    src/regexhelpdialog.h
    src/regexhelpdialog.cpp
    src/regexpromptdialog.h
    src/regexpromptdialog.cpp
    src/regextesterdialog.h
    src/regextesterdialog.cpp
    src/profiledialog.h
    src/profiledialog.cpp
    src/licensedialog.h
    src/licensedialog.cpp
    src/mainwindow.h
    src/mainwindow.cpp
    src/icons.h
    src/icons.cpp
    resources/app.qrc
)

if(WIN32)
    enable_language(RC)
    list(APPEND PROJECT_SOURCES resources/win.rc)
endif()

if(WIN32)
    add_executable(LgViewer WIN32 ${PROJECT_SOURCES})
else()
    add_executable(LgViewer ${PROJECT_SOURCES})
endif()

target_include_directories(LgViewer PRIVATE src)

# 版本号 + 构建标记，供「关于」与标题栏使用
target_compile_definitions(LgViewer PRIVATE
    LG_VERSION="${PROJECT_VERSION}"
    LG_BUILD_MARK="${LG_BUILD_MARK}"
)

target_link_libraries(LgViewer PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    ${PCRE2_TARGET}
)

if(MSVC)
    target_compile_options(LgViewer PRIVATE /utf-8 /W3)
else()
    target_compile_options(LgViewer PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()

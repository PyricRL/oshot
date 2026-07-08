cmake_minimum_required(VERSION 3.15)

project(oshot VERSION 0.4.6 LANGUAGES C CXX)
set(PROJECT_FULL_VERSION "${PROJECT_VERSION}${PROJECT_VERSION_SUFFIX}")

if(APPLE)
    enable_language(OBJC OBJCXX)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

add_compile_options($<$<CONFIG:Debug>:-fsanitize=address>)
add_compile_options($<$<CONFIG:Debug>:-fno-omit-frame-pointer>)
add_link_options($<$<CONFIG:Debug>:-fsanitize=address>)

# GCC/Clang default Release flags already include -O3; this just makes it
# explicit. Guarded because "-O3" is not a valid cl.exe switch and would
# otherwise spam D9002 (or hard-fail under /WX) on MSVC builds.
if(NOT MSVC)
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3")
endif()

# LTO helper
function(enable_lto target)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        target_compile_options(
            ${target}
            PRIVATE $<$<CONFIG:Release>:-flto=auto;-ffat-lto-objects>
        )
        target_link_options(${target} PRIVATE $<$<CONFIG:Release>:-flto=auto>)
    elseif(CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang")
        target_compile_options(${target} PRIVATE $<$<CONFIG:Release>:-flto>)
        target_link_options(${target} PRIVATE $<$<CONFIG:Release>:-flto>)
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        target_compile_options(
            ${target}
            PRIVATE $<$<CONFIG:Release>:-flto=thin>
        )
        target_link_options(${target} PRIVATE $<$<CONFIG:Release>:-flto=thin>)
    endif()
endfunction()

# https://github.com/libcpr/cpr/blob/5f475522597b8f3721e2440daddeced7a969f24c/CMakeLists.txt#L39
# NOTE: definitions are collected into OSHOT_OPTION_DEFINITIONS instead of
# using add_definitions(), which is directory-scoped and would otherwise leak
# into every target declared afterward - including vendored third-party
# libs (fmt, imgui, tray, clip, dylib, nvdialog, tiny-process-library).
# They're applied to just ${PROJECT_NAME} once it exists (see below).
set(OSHOT_OPTION_DEFINITIONS "")
macro(add_option OPTION_NAME OPTION_TEXT OPTION_DEFAULT DEFINE_OPTION)
    option(${OPTION_NAME} ${OPTION_TEXT} ${OPTION_DEFAULT})
    if(DEFINED ENV{${OPTION_NAME}})
        # Allow overriding the option through an environment variable
        set(${OPTION_NAME} $ENV{${OPTION_NAME}})
    endif()
    if(${OPTION_NAME} AND ${DEFINE_OPTION})
        list(APPEND OSHOT_OPTION_DEFINITIONS ${OPTION_NAME}=1)
    endif()
    message(STATUS "  ${OPTION_NAME}: ${${OPTION_NAME}}")
endmacro()

# Options
add_option(WINDOWS_CMD "Enable terminal support on Windows" OFF ON)
add_option(DISABLE_PLUGINS "Disable plugins support" OFF ON)

# -----------------------------
# Dependencies
# -----------------------------
find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)
find_package(glfw3 REQUIRED)

if(TARGET glfw::glfw)
    set(GLFW_TARGET glfw::glfw)
elseif(TARGET glfw)
    set(GLFW_TARGET glfw)
elseif(TARGET glfw3)
    set(GLFW_TARGET glfw3)
else()
    message(FATAL_ERROR "GLFW found but no usable CMake target was exported")
endif()

find_package(OpenGL REQUIRED)
pkg_check_modules(TESSERACT REQUIRED IMPORTED_TARGET tesseract)
pkg_check_modules(LEPTONICA REQUIRED IMPORTED_TARGET lept)
pkg_check_modules(ZBAR REQUIRED IMPORTED_TARGET zbar)

# Homebrew's lept.pc points Cflags at .../include/leptonica directly (its
# own convention is `#include <allheaders.h>`), but our code uses
# `#include <leptonica/allheaders.h>`, which needs the parent dir on the
# include path too.
message(STATUS "LEPTONICA_INCLUDE_DIRS before: ${LEPTONICA_INCLUDE_DIRS}")
foreach(_lept_dir ${LEPTONICA_INCLUDE_DIRS})
    get_filename_component(_lept_parent_dir "${_lept_dir}" DIRECTORY)
    list(APPEND LEPTONICA_INCLUDE_DIRS "${_lept_parent_dir}")
endforeach()
list(REMOVE_DUPLICATES LEPTONICA_INCLUDE_DIRS)
message(STATUS "LEPTONICA_INCLUDE_DIRS after: ${LEPTONICA_INCLUDE_DIRS}")

if(UNIX AND NOT APPLE)
    find_package(X11 REQUIRED)
    pkg_check_modules(APPINDICATOR appindicator3-0.1)
    if(NOT APPINDICATOR_FOUND)
        pkg_check_modules(APPINDICATOR REQUIRED ayatana-appindicator3-0.1)
    endif()
    pkg_check_modules(GIO REQUIRED gio-2.0)
    pkg_check_modules(XRANDR REQUIRED xrandr)
    pkg_check_modules(GTK REQUIRED gtk+-3.0)
    pkg_check_modules(DBUS REQUIRED dbus-1)
endif()

# -----------------------------
# Main executable
# -----------------------------
add_executable(
    ${PROJECT_NAME}
    src/main.cpp
    src/main_tool_opengl3.cpp
    src/libs/getopt_port/getopt.c
)
enable_lto(${PROJECT_NAME})

add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/version.h
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/scripts/generateVersion.sh
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/scripts/generateVersion.sh
    COMMENT "Generating version header"
)
add_custom_target(
    generate_version
    DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/version.h
)
add_dependencies(${PROJECT_NAME} generate_version)
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_BINARY_DIR})

target_compile_definitions(${PROJECT_NAME} PRIVATE $<$<CONFIG:Debug>:DEBUG=1>)
target_compile_options(
    ${PROJECT_NAME}
    PRIVATE
        -Wall
        -Wextra
        -pedantic
        -Wno-unused-parameter
        $<$<CONFIG:Debug>:-ggdb3;-fno-omit-frame-pointer>
)

# oshot_plugin PUBLIC-links oshot_common (see below), so linking oshot_plugin
# alone pulls in oshot_common's usage requirements transitively. This is
# deliberate: it ensures oshot and oshot_plugin.so resolve globals.cpp's
# externs (g_ss_tool, g_config, g_cache, g_plugins, g_current_plugin, ...)
# to the same symbols at the same address.
if(NOT DISABLE_PLUGINS)
    target_link_libraries(${PROJECT_NAME} PRIVATE oshot_plugin)
else()
    target_link_libraries(${PROJECT_NAME} PRIVATE oshot_common)
endif()

if(WIN32 AND NOT WINDOWS_CMD)
    set_target_properties(${PROJECT_NAME} PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

if(UNIX AND NOT APPLE)
    target_link_libraries(${PROJECT_NAME} PRIVATE X11::X11)
endif()

if(APPLE)
    target_sources(${PROJECT_NAME} PRIVATE src/main_tool_metal.mm)
    set(_rpath_origin "@loader_path")
else()
    set(_rpath_origin "$ORIGIN")
endif()

set_target_properties(
    ${PROJECT_NAME}
    PROPERTIES
        BUILD_RPATH "${_rpath_origin}/../lib;$<TARGET_FILE_DIR:imgui>"
        INSTALL_RPATH "${_rpath_origin}/../lib"
)

# -----------------------------
# Common library for shared code
# -----------------------------
add_library(
    oshot_common
    STATIC
    src/cache.cpp
    src/clipboard.cpp
    src/config.cpp
    src/globals.cpp
    src/screen_capture.cpp
    src/screenshot_tool.cpp
    src/text_extraction.cpp
    src/util.cpp
    src/libs/toml++/toml.cpp
    src/libs/tinyfiledialogs/tinyfiledialogs.c
)

if(OSHOT_OPTION_DEFINITIONS)
    target_compile_definitions(oshot_common PUBLIC ${OSHOT_OPTION_DEFINITIONS})
endif()

if(NOT DISABLE_PLUGINS)
    target_sources(oshot_common PRIVATE src/plugins.cpp)
endif()

enable_lto(oshot_common)

if(WIN32)
    target_link_libraries(oshot_common PRIVATE ws2_32 shcore d3d11 dxgi)
    if(MINGW)
        target_link_options(${PROJECT_NAME} PRIVATE -Wl,--export-all-symbols)
    endif()
elseif(APPLE)
    target_compile_options(
        oshot_common
        PRIVATE -fobjc-arc $<$<CXX_COMPILER_ID:AppleClang>:-stdlib=libc++>
    )
    target_link_options(
        oshot_common
        PRIVATE $<$<CXX_COMPILER_ID:AppleClang>:-stdlib=libc++>
    )
    target_link_libraries(
        oshot_common
        PRIVATE
            "-framework Metal"
            "-framework QuartzCore"
            "-framework CoreGraphics"
            "-framework Cocoa"
            "-framework IOKit"
            "-framework OpenGL"
    )
else()
    target_include_directories(
        oshot_common
        PRIVATE ${APPINDICATOR_INCLUDE_DIRS}
    )
    target_compile_options(oshot_common PRIVATE ${APPINDICATOR_CFLAGS_OTHER})

    target_link_libraries(oshot_common PRIVATE X11::X11)
    target_include_directories(
        oshot_common
        PRIVATE
        SYSTEM
        ${GIO_INCLUDE_DIRS}
        ${XRANDR_INCLUDE_DIRS}
    )
    target_link_libraries(
        oshot_common
        PRIVATE ${GIO_LIBRARIES} ${XRANDR_LIBRARIES}
    )
    target_compile_options(
        oshot_common
        PRIVATE ${GIO_CFLAGS_OTHER} ${XRANDR_CFLAGS_OTHER}
    )
endif()

target_link_libraries(
    oshot_common
    PUBLIC
        fmt
        imgui
        tray
        clip
        nvdialog
        tiny-process-library
        PkgConfig::TESSERACT
        PkgConfig::LEPTONICA
        PkgConfig::ZBAR
        OpenGL::GL
        ${GLFW_TARGET}
    PRIVATE Threads::Threads
)
if(NOT DISABLE_PLUGINS)
    target_link_libraries(oshot_common PUBLIC dylib)
endif()

target_compile_definitions(
    oshot_common
    PUBLIC VERSION="${PROJECT_VERSION}" TOML_HEADER_ONLY=0
)

# NOTE: PkgConfig::TESSERACT / PkgConfig::ZBAR already propagate their own
# include dirs, link dirs, and compile options as usage requirements via
# target_link_libraries() above.
# LEPTONICA_INCLUDE_DIRS is the one exception: it was patched earlier in this
# file to add the parent directory for Homebrew's lept.pc convention, which
# PkgConfig::LEPTONICA's own interface include dirs don't have.
target_include_directories(
    oshot_common
    PUBLIC include/ include/libs ${LEPTONICA_INCLUDE_DIRS}
)

target_compile_features(oshot_common PUBLIC cxx_std_20)

# -----------------------------
# oshot's plugin library
# -----------------------------
if(NOT DISABLE_PLUGINS)
    add_library(oshot_plugin SHARED src/plugins/oshot_plugins.cpp)

    target_compile_features(oshot_plugin PUBLIC cxx_std_20)

    target_link_libraries(oshot_plugin PUBLIC oshot_common)

    set_target_properties(
        oshot_plugin
        PROPERTIES
            VERSION 1
            SOVERSION 1
            WINDOWS_EXPORT_ALL_SYMBOLS ON
            POSITION_INDEPENDENT_CODE ON
    )
endif()

# -----------------------------
# fmt library
# -----------------------------

add_library(fmt STATIC src/libs/fmt/os.cc src/libs/fmt/format.cc)

enable_lto(fmt)

target_include_directories(fmt PUBLIC include/libs)
target_compile_features(fmt PUBLIC cxx_std_20)
target_compile_definitions(fmt PUBLIC FMT_HEADER_ONLY=0)

# -----------------------------
# imgui library
# -----------------------------

file(GLOB IMGUI_SOURCES src/libs/imgui/*.cpp)
if(APPLE)
    list(APPEND IMGUI_SOURCES src/libs/imgui/imgui_impl_metal.mm)
endif()

if(DISABLE_PLUGINS)
    add_library(imgui STATIC ${IMGUI_SOURCES})
else()
    add_library(imgui SHARED ${IMGUI_SOURCES})
endif()

enable_lto(imgui)

target_include_directories(imgui PUBLIC include/libs/imgui)
target_compile_definitions(
    imgui
    PUBLIC IMGUI_IMPL_OPENGL_LOADER_GLAD IMGUI_DISABLE_OBSOLETE_FUNCTIONS
)
target_link_libraries(imgui PUBLIC ${GLFW_TARGET})

if(APPLE)
    find_library(METAL_FRAMEWORK Metal REQUIRED)
    find_library(FOUNDATION_FRAMEWORK Foundation REQUIRED)
    find_library(QUARTZCORE_FRAMEWORK QuartzCore REQUIRED)
    target_link_libraries(
        imgui
        PUBLIC
            ${METAL_FRAMEWORK}
            ${FOUNDATION_FRAMEWORK}
            ${QUARTZCORE_FRAMEWORK}
    )
endif()

if(NOT DISABLE_PLUGINS)
    set_target_properties(
        imgui
        PROPERTIES
            VERSION 1.92.9
            SOVERSION 1
            WINDOWS_EXPORT_ALL_SYMBOLS ON
            POSITION_INDEPENDENT_CODE ON
    )
endif()

# -----------------------------
# cimgui library (C-ABI plugins only)
# -----------------------------

if(NOT DISABLE_PLUGINS)
    add_library(cimgui SHARED src/libs/cimgui/cimgui.cpp)

    target_include_directories(cimgui PUBLIC include/libs include/libs/cimgui)
    target_link_libraries(cimgui PUBLIC imgui)
    set_target_properties(
        cimgui
        PROPERTIES
            VERSION 1
            SOVERSION 1
            WINDOWS_EXPORT_ALL_SYMBOLS ON
            POSITION_INDEPENDENT_CODE ON
    )
endif()

# -----------------------------
# Tray library (integrated from its own CMakeLists.txt)
# -----------------------------

if(WIN32)
    list(APPEND TRAY_SOURCES src/libs/tray/tray_windows.cpp)
else()
    if(UNIX)
        if(APPLE)
            find_library(COCOA_FRAMEWORK Cocoa REQUIRED)
            list(APPEND TRAY_SOURCES src/libs/tray/tray_darwin.mm)
        else()
            list(APPEND TRAY_SOURCES src/libs/tray/tray_linux.cpp)
        endif()
    endif()
endif()

add_library(tray STATIC ${TRAY_SOURCES})

if(WIN32)
    target_compile_definitions(
        tray
        PRIVATE TRAY_WINAPI=1 WIN32_LEAN_AND_MEAN NOMINMAX
    )
else()
    if(UNIX)
        if(APPLE)
            target_compile_definitions(tray PRIVATE TRAY_APPKIT=1)
            target_link_libraries(tray PRIVATE ${COCOA_FRAMEWORK})
        else()
            target_compile_options(
                tray
                PRIVATE ${GTK_CFLAGS} ${GTK_CFLAGS_OTHER}
            )
            target_include_directories(
                tray
                PRIVATE ${GTK_INCLUDE_DIRS} ${APPINDICATOR_INCLUDE_DIRS}
            )
            target_link_directories(
                tray
                PRIVATE ${GTK_LIBRARY_DIRS} ${APPINDICATOR_LIBRARY_DIRS}
            )
            target_link_libraries(tray PRIVATE ${GTK_LIBRARIES})

            target_compile_options(
                tray
                PRIVATE ${APPINDICATOR_CFLAGS} ${APPINDICATOR_CFLAGS_OTHER}
            )
            target_compile_definitions(tray PRIVATE TRAY_APPINDICATOR=1)
            target_link_libraries(tray PRIVATE ${APPINDICATOR_LIBRARIES})
        endif()
    endif()
endif()

target_include_directories(tray PUBLIC include/libs)
target_compile_features(tray PRIVATE cxx_std_20)

# -----------------------------
# clip library (integrated from its own CMakeLists.txt)
# -----------------------------

add_library(clip STATIC src/libs/clip/clip.cpp src/libs/clip/image.cpp)

if(WIN32)
    target_sources(
        clip
        PRIVATE
            src/libs/clip/clip_win.cpp
            src/libs/clip/clip_win_bmp.cpp
            src/libs/clip/clip_win_wic.cpp
    )
elseif(APPLE)
    target_sources(clip PRIVATE src/libs/clip/clip_osx.mm)
else()
    target_sources(clip PRIVATE src/libs/clip/clip_none.cpp)
endif()

if(MSVC)
    target_compile_definitions(clip PRIVATE _CRT_SECURE_NO_WARNINGS)
    # MinGW requires the windowscodecs just because CLSIDs are defined
    # in the windowscodecs.a file instead of the wincodec.h file (?!)
elseif(MINGW)
    target_link_libraries(clip PUBLIC shlwapi windowscodecs)
endif()

target_include_directories(clip PUBLIC include/libs/clip)
target_compile_features(clip PRIVATE cxx_std_20)
target_compile_definitions(clip PUBLIC CLIP_ENABLE_IMAGE=1)

# -----------------------------
# dylib library (integrated from its own CMakeLists.txt)
# -----------------------------

if(NOT DISABLE_PLUGINS)
    file(GLOB DYLIB_SOURCES src/libs/dylib/*.cpp)
    add_library(dylib STATIC ${DYLIB_SOURCES})

    if(UNIX)
        target_link_libraries(dylib PRIVATE ${CMAKE_DL_LIBS})
    endif()

    target_include_directories(dylib PUBLIC include/libs/)
    target_compile_features(dylib PRIVATE cxx_std_20)
endif()

# -----------------------------
# nvdialog (integrated from its own CMakeLists.txt)
# -----------------------------

file(GLOB NVD_COMMON_SOURCES src/libs/nvdialog/*.c)

if(WIN32)
    file(GLOB NVD_BACKEND_SOURCES src/libs/nvdialog/backend/win32/*.c)
elseif(APPLE)
    file(GLOB NVD_BACKEND_SOURCES src/libs/nvdialog/backend/cocoa/*.m)
else()
    file(GLOB NVD_BACKEND_SOURCES src/libs/nvdialog/backend/gtk/*.c)
endif()

add_library(nvdialog STATIC ${NVD_COMMON_SOURCES} ${NVD_BACKEND_SOURCES})
enable_lto(nvdialog)

target_compile_definitions(
    nvdialog
    PUBLIC NVDIALOG_MAXBUF=4096 NVD_STATIC_LINKAGE=1
    PRIVATE NVD_EXPORT_SYMBOLS
)

target_include_directories(
    nvdialog
    PUBLIC include/libs/nvdialog/ include/libs
    PRIVATE src/libs/nvdialog/impl/
)

set_target_properties(nvdialog PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON)

if(WIN32)
    target_link_libraries(
        nvdialog
        PUBLIC comdlg32 shell32 user32 gdi32 ole32
    )
elseif(APPLE)
    target_compile_definitions(nvdialog PRIVATE NVD_USE_COCOA=1)
    target_compile_options(nvdialog PRIVATE -fno-objc-arc)
    target_link_libraries(
        nvdialog
        PRIVATE
            "-framework AppKit"
            "-framework Cocoa"
            "-framework Foundation"
            "-framework UserNotifications"
    )
else()
    target_compile_options(
        nvdialog
        PRIVATE
            ${GTK_CFLAGS}
            ${GTK_CFLAGS_OTHER}
            ${DBUS_CFLAGS}
            ${DBUS_CFLAGS_OTHER}
    )
    target_include_directories(nvdialog PRIVATE ${DBUS_INCLUDE_DIRS})
    target_link_directories(
        nvdialog
        PRIVATE ${GTK_LIBRARY_DIRS} ${DBUS_LIBRARY_DIRS}
    )
    target_link_libraries(nvdialog PRIVATE ${GTK_LIBRARIES} ${DBUS_LIBRARIES})
endif()

# -----------------------------
# tiny-process-library (integrated from its own CMakeLists.txt)
# -----------------------------

add_library(
    tiny-process-library
    STATIC
    "src/libs/tiny-process-library/process.cpp"
)
add_library(
    tiny-process-library::tiny-process-library
    ALIAS tiny-process-library
)

if(MSVC)
    target_compile_definitions(
        tiny-process-library
        PRIVATE _CRT_SECURE_NO_WARNINGS
    )
else()
    target_compile_options(tiny-process-library PRIVATE -Wall -Wextra)
    target_compile_features(tiny-process-library PRIVATE cxx_std_20)
endif()

if(WIN32)
    target_sources(
        tiny-process-library
        PRIVATE "src/libs/tiny-process-library/process_win.cpp"
    )
    # If compiled using MSYS2, use sh to run commands
    if(MSYS)
        target_compile_definitions(
            tiny-process-library
            PUBLIC MSYS_PROCESS_USE_SH
        )
    endif()
else()
    target_sources(
        tiny-process-library
        PRIVATE "src/libs/tiny-process-library/process_unix.cpp"
    )
endif()

target_link_libraries(tiny-process-library PUBLIC Threads::Threads)
target_include_directories(
    tiny-process-library
    PUBLIC include/libs/tiny-process-library
)
enable_lto(tiny-process-library)

# -----------------------------
# Install
# -----------------------------

include(GNUInstallDirs)

if(DISABLE_PLUGINS)
    install(TARGETS ${PROJECT_NAME} RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
else()
    install(
        TARGETS ${PROJECT_NAME} imgui cimgui oshot_plugin
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    )
endif()

install(
    FILES LICENSE
    DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/licenses/${PROJECT_NAME}
)

install(
    FILES oshot.desktop
    DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/applications
)

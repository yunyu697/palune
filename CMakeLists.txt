cmake_minimum_required(VERSION 3.21)

# 版本号唯一来源：仓库根目录 VERSION 文件。
# 规则：开发期为 1.0.0-alphaN；除第一段外任何段 <= 10，alpha 编号 <= 10。
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" PALUNE_VERSION LIMIT_COUNT 1)
string(STRIP "${PALUNE_VERSION}" PALUNE_VERSION)

project(Palune VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Widgets Quick QuickControls2 Network Test)
qt_standard_project_setup()

add_library(palune_core STATIC
    src/mpv/MpvClient.cpp
    src/mpv/MpvClient.h
    src/update/UpdateManifest.cpp
    src/update/UpdateManifest.h
    src/update/UpdateCrypto.cpp
    src/update/UpdateCrypto.h
    src/update/UpdateService.cpp
    src/update/UpdateService.h
    src/update/Version.cpp
    src/update/Version.h
    src/diag/Log.cpp
    src/diag/Log.h
)
target_include_directories(palune_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/libmpv/include
)
target_link_libraries(palune_core PUBLIC
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Quick Qt6::QuickControls2 Qt6::Network
)
target_compile_definitions(palune_core PUBLIC
    PALUNE_VERSION="${PALUNE_VERSION}"
    PALUNE_VERSION_NUMERIC_MAJOR=1
    PALUNE_VERSION_NUMERIC_MINOR=0
    PALUNE_VERSION_NUMERIC_PATCH=0
)
if(WIN32)
    target_link_libraries(palune_core PUBLIC user32 dwmapi shell32)
endif()

qt_add_executable(Palune
    src/app/main.cpp
    src/app/AppController.cpp
    src/app/AppController.h
    src/window/VideoWindow.cpp
    src/window/VideoWindow.h
    src/window/OverlayWindow.cpp
    src/window/OverlayWindow.h
    src/window/WindowController.cpp
    src/window/WindowController.h
    src/m0/SelfTest.cpp
    src/m0/SelfTest.h
)

qt_add_resources(Palune "app_resources"
    PREFIX "/"
    FILES
        qml/Main.qml
        qml/SettingsPanel.qml
        qml/UpdateSection.qml
        resources/trust/update.pub
        resources/trust/test-update.pub
)

if(WIN32)
    set_target_properties(Palune PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

target_link_libraries(Palune PRIVATE palune_core)

# 把 libmpv / libsodium 复制到可执行文件旁（运行时动态加载）。
# CI 环境没有本地二进制，缺文件时跳过，保证构建本身可通过。
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/libmpv/libmpv-2.dll")
    add_custom_command(TARGET Palune POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/third_party/libmpv/libmpv-2.dll"
            "$<TARGET_FILE_DIR:Palune>/libmpv-2.dll"
        COMMENT "复制 libmpv 运行时库"
    )
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/libsodium/libsodium.dll")
    add_custom_command(TARGET Palune POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/third_party/libsodium/libsodium.dll"
            "$<TARGET_FILE_DIR:Palune>/libsodium.dll"
        COMMENT "复制 libsodium 运行时库"
    )
endif()

enable_testing()

add_executable(tst_version tests/unit/tst_version.cpp)
target_link_libraries(tst_version PRIVATE palune_core Qt6::Test)
add_test(NAME version COMMAND tst_version)

add_executable(tst_manifest tests/unit/tst_manifest.cpp)
target_link_libraries(tst_manifest PRIVATE palune_core Qt6::Test)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/libsodium/libsodium.dll")
    add_custom_command(TARGET tst_manifest POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/third_party/libsodium/libsodium.dll"
            "$<TARGET_FILE_DIR:tst_manifest>/libsodium.dll"
    )
endif()
add_test(NAME manifest COMMAND tst_manifest)

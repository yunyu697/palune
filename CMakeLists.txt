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
    src/core/AppPaths.cpp
    src/core/AppPaths.h
    src/core/ProcessMetrics.cpp
    src/core/ProcessMetrics.h
    src/core/StartupClock.cpp
    src/core/StartupClock.h
    src/mpv/MpvClient.cpp
    src/mpv/MpvClient.h
    src/settings/Settings.cpp
    src/settings/Settings.h
    src/settings/Toml.cpp
    src/settings/Toml.h
    src/theme/LayoutHost.cpp
    src/theme/LayoutHost.h
    src/theme/ThemeRegistry.cpp
    src/theme/ThemeRegistry.h
    src/ui/UiStateStore.cpp
    src/ui/UiStateStore.h
    src/media/MediaLibraryService.cpp
    src/media/MediaLibraryService.h
    src/media/MediaServerAdapter.h
    src/media/Types.h
    src/update/UpdateManifest.cpp
    src/update/UpdateManifest.h
    src/update/UpdateCrypto.cpp
    src/update/UpdateCrypto.h
    src/update/UpdateService.cpp
    src/update/UpdateService.h
    src/update/Version.cpp
    src/update/Version.h
    src/diag/CrashBreadcrumb.cpp
    src/diag/CrashBreadcrumb.h
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
    target_link_libraries(palune_core PUBLIC user32 dwmapi shell32 psapi)
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
        qml/LibraryPanel.qml
        qml/tokens/Tokens.qml
        qml/components/PillButton.qml
        qml/components/SeekBar.qml
        qml/components/VolumeBar.qml
        qml/components/EmptyState.qml
        qml/shell/VideoHost.qml
        qml/layouts/LayoutA/LayoutA.qml
        qml/layouts/LayoutB/LayoutB.qml
        qml/layouts/LayoutC/LayoutC.qml
        qml/library/MediaLibraryEmpty.qml
        resources/trust/update.pub
        resources/trust/test-update.pub
)

if(WIN32)
    set_target_properties(Palune PROPERTIES WIN32_EXECUTABLE TRUE)
elseif(APPLE)
    # 必须是 bundle，macdeployqt 才会部署 Qt 运行时（否则会跳过运行时部署）。
    # 自定义 Info.plist：冻结 bundle id 与 palune:// 协议注册（M1 判据 13）。
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/packaging/macos/Info.plist.in"
        "${CMAKE_CURRENT_BINARY_DIR}/Info.plist"
        @ONLY
    )
    set_target_properties(Palune PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_BINARY_DIR}/Info.plist"
        MACOSX_BUNDLE_BUNDLE_NAME "Palune"
        MACOSX_BUNDLE_GUI_IDENTIFIER "app.palune.player"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PALUNE_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "1.0.0"
    )
endif()

target_link_libraries(Palune PRIVATE palune_core)

# 把 libmpv / libsodium 复制到可执行文件旁（运行时动态加载，文件名随平台不同）。
# 缺文件时跳过；CI 的常规构建没有本地二进制也能通过编译。
set(PALUNE_RUNTIME_LIBS "")
foreach(_rel IN ITEMS
        "third_party/libmpv/libmpv-2.dll"
        "third_party/libmpv/libmpv.dylib"
        "third_party/libmpv/libmpv.2.dylib"
        "third_party/libmpv/libmpv.so.2"
        "third_party/libsodium/libsodium.dll"
        "third_party/libsodium/libsodium.dylib"
        "third_party/libsodium/libsodium.so.23")
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${_rel}")
        list(APPEND PALUNE_RUNTIME_LIBS "${CMAKE_CURRENT_SOURCE_DIR}/${_rel}")
    endif()
endforeach()

function(palune_copy_runtime_libs target)
    foreach(_lib IN LISTS PALUNE_RUNTIME_LIBS)
        get_filename_component(_name "${_lib}" NAME)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${_lib}" "$<TARGET_FILE_DIR:${target}>/${_name}"
            COMMENT "复制运行时库 ${_name}")
    endforeach()
endfunction()

palune_copy_runtime_libs(Palune)

# ---------------------------------------------------------------------------
# 安装与打包（release 工作流使用：Windows NSIS/MSI、macOS dmg、Linux AppImage/deb/rpm）
# ---------------------------------------------------------------------------
include(GNUInstallDirs)

install(TARGETS Palune
    BUNDLE  DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
install(FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/README.md"
    "${CMAKE_CURRENT_SOURCE_DIR}/NOTICE.md"
    "${CMAKE_CURRENT_SOURCE_DIR}/VERSION"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/palune"
)

# libmpv / libsodium 是运行时 dlopen 加载的动态库，安装器不会自动带上，必须显式安装。
if(APPLE)
    set(PALUNE_RUNTIME_DIR "Palune.app/Contents/MacOS")
else()
    set(PALUNE_RUNTIME_DIR "${CMAKE_INSTALL_BINDIR}")
endif()
foreach(_lib
        "third_party/libmpv/libmpv-2.dll"
        "third_party/libmpv/libmpv.dylib"
        "third_party/libmpv/libmpv.2.dylib"
        "third_party/libmpv/libmpv.so.2"
        "third_party/libsodium/libsodium.dll"
        "third_party/libsodium/libsodium.dylib"
        "third_party/libsodium/libsodium.so.23")
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${_lib}")
        install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/${_lib}" DESTINATION "${PALUNE_RUNTIME_DIR}")
    endif()
endforeach()

# 把 Qt 运行时依赖部署进安装树（Windows 用 windeployqt，macOS 用 macdeployqt）。
qt_generate_deploy_app_script(TARGET Palune OUTPUT_SCRIPT PALUNE_DEPLOY_SCRIPT
    NO_UNSUPPORTED_PLATFORM_ERROR)
install(SCRIPT ${PALUNE_DEPLOY_SCRIPT})

# CPack：Windows 产出 NSIS 安装器与 MSI。
set(CPACK_PACKAGE_NAME "Palune")
set(CPACK_PACKAGE_VENDOR "Palune")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Palune 媒体播放器")
set(CPACK_PACKAGE_VERSION "${PALUNE_VERSION}")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "Palune")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/packaging/LICENSE.txt")

if(WIN32)
    set(CPACK_PACKAGE_FILE_NAME "Palune-${PALUNE_VERSION}-win-x64")
    set(CPACK_NSIS_DISPLAY_NAME "Palune")
    set(CPACK_NSIS_PACKAGE_NAME "Palune")
    set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
    set(CPACK_NSIS_INSTALLED_ICON_NAME "bin\\\\Palune.exe")
    set(CPACK_NSIS_MUI_FINISHPAGE_RUN "bin\\\\Palune.exe")
    # MSI 的 ProductVersion 必须是纯数字版本，alpha 后缀交给文件名表达。
    set(CPACK_WIX_PRODUCT_GUID "5B0D3C11-7E10-4E1C-9A46-2C6C2C0A0F01")
    set(CPACK_WIX_UPGRADE_GUID "9E3A2D41-1B3F-4E57-8C2B-6F0A7D1C2E34")
    set(CPACK_WIX_PROGRAM_MENU_FOLDER "Palune")
elseif(APPLE)
    set(CPACK_PACKAGE_FILE_NAME "Palune-${PALUNE_VERSION}-macos-arm64")
endif()

include(CPack)

enable_testing()

add_executable(tst_version tests/unit/tst_version.cpp)
target_link_libraries(tst_version PRIVATE palune_core Qt6::Test)
add_test(NAME version COMMAND tst_version)

add_executable(tst_manifest tests/unit/tst_manifest.cpp)
target_link_libraries(tst_manifest PRIVATE palune_core Qt6::Test)
palune_copy_runtime_libs(tst_manifest)
add_test(NAME manifest COMMAND tst_manifest)

add_executable(tst_settings tests/unit/tst_settings.cpp)
target_link_libraries(tst_settings PRIVATE palune_core Qt6::Test)
palune_copy_runtime_libs(tst_settings)
add_test(NAME settings COMMAND tst_settings)

add_executable(tst_theme tests/unit/tst_theme.cpp)
target_link_libraries(tst_theme PRIVATE palune_core Qt6::Test)
palune_copy_runtime_libs(tst_theme)
add_test(NAME theme COMMAND tst_theme)

add_executable(tst_media tests/unit/tst_media.cpp)
target_link_libraries(tst_media PRIVATE palune_core Qt6::Test)
palune_copy_runtime_libs(tst_media)
add_test(NAME media COMMAND tst_media)

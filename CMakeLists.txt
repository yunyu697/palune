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
    src/settings/Preferences.cpp
    src/settings/Preferences.h
    src/settings/Toml.cpp
    src/settings/Toml.h
    src/theme/LayoutHost.cpp
    src/theme/LayoutHost.h
    src/theme/ThemeRegistry.cpp
    src/theme/ThemeRegistry.h
    src/ui/UiStateStore.cpp
    src/ui/UiStateStore.h
    src/ui/CompactController.cpp
    src/ui/CompactController.h
    src/ui/ActionRegistry.cpp
    src/ui/ActionRegistry.h
    src/media/MediaLibraryService.cpp
    src/media/MediaLibraryService.h
    src/media/ThumbnailProvider.cpp
    src/media/ThumbnailProvider.h
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
    # ole32：ThumbnailProvider 用 IShellItemImageFactory（SHCreateItemFromParsingName）。
    target_link_libraries(palune_core PUBLIC user32 dwmapi shell32 psapi ole32)
endif()

qt_add_executable(Palune
    src/app/main.cpp
    src/app/AppController.cpp
    src/app/AppController.h
    src/app/ShotContexts.cpp
    src/app/ShotContexts.h
    src/window/VideoWindow.cpp
    src/window/VideoWindow.h
    src/window/OverlayWindow.cpp
    src/window/OverlayWindow.h
    src/window/WindowController.cpp
    src/window/WindowController.h
    src/window/SettingsWindow.cpp
    src/window/SettingsWindow.h
    src/m0/SelfTest.cpp
    src/m0/SelfTest.h
)

qt_add_resources(Palune "app_resources"
    PREFIX "/"
    FILES
        qml/Main.qml
        qml/SettingsPanel.qml
        qml/SettingsWindow.qml
        qml/UpdateSection.qml
        qml/LibraryPanel.qml
        qml/tokens/Tokens.qml
        qml/components/PillButton.qml
        qml/components/SeekBar.qml
        qml/components/VolumeBar.qml
        qml/components/EmptyState.qml
        qml/components/WindowButtons.qml
        qml/components/SettingRow.qml
        qml/components/SettingSwitch.qml
        qml/components/SettingCycle.qml
        qml/components/SettingStepper.qml
        qml/components/OutlinedText.qml
        qml/shell/VideoHost.qml
        qml/layouts/LayoutA/LayoutA.qml
        qml/layouts/LayoutB/LayoutB.qml
        qml/layouts/LayoutC/LayoutC.qml
        qml/library/MediaLibraryEmpty.qml
        resources/trust/update.pub
        resources/trust/test-update.pub
        i18n/palune_en_US.qm
        packaging/palune-256.png
)

if(WIN32)
    set_target_properties(Palune PROPERTIES WIN32_EXECUTABLE TRUE)
    # 资源里的图标给 exe 用（任务栏/资源管理器），否则 Windows 显示默认空白图标。
    target_sources(Palune PRIVATE packaging/palune.rc)
elseif(APPLE)
    # 品牌图标：与 Windows 同一份 SVG 源导出的 .icns，装进 .app/Contents/Resources。
    set_source_files_properties(resources/brand/palune.icns PROPERTIES
        MACOSX_PACKAGE_LOCATION "Resources")
    target_sources(Palune PRIVATE resources/brand/palune.icns)
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
        MACOSX_BUNDLE_ICON_FILE "palune.icns"
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
# 许可说明里承诺「安装目录下的 NOTICE.md」，所以 bin 旁也放一份。
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/NOTICE.md" DESTINATION "${CMAKE_INSTALL_BINDIR}")

# MSVC 运行时 vcruntime140 / msvcp140 必须随包安装，否则干净机器双击 0xC0000135。
# windeployqt 的 --compiler-runtime 只部署 vc_redist.x64.exe（还要再跑一次安装器），
# 这里直接从 VC Redist 目录取 CRT DLL，确定性更高。
set(PALUNE_MSVC_CRT_DLLS "")
if(MSVC AND WIN32)
    set(_palune_vc_root "")
    if(DEFINED ENV{VCINSTALLDIR} AND EXISTS "$ENV{VCINSTALLDIR}/Redist")
        set(_palune_vc_root "$ENV{VCINSTALLDIR}")
    endif()
    if(NOT _palune_vc_root AND CMAKE_CXX_COMPILER)
        set(_palune_probe "${CMAKE_CXX_COMPILER}")
        foreach(_i RANGE 7)
            get_filename_component(_palune_probe "${_palune_probe}" DIRECTORY)
            if(EXISTS "${_palune_probe}/Redist")
                set(_palune_vc_root "${_palune_probe}")
                break()
            endif()
        endforeach()
    endif()
    if(_palune_vc_root)
        file(GLOB _palune_crt
            "${_palune_vc_root}/Redist/MSVC/*/x64/Microsoft.VC*.CRT/*.dll")
        list(SORT _palune_crt)
        foreach(_name IN ITEMS vcruntime140.dll vcruntime140_1.dll msvcp140.dll
                               msvcp140_1.dll msvcp140_2.dll concrt140.dll)
            foreach(_file IN LISTS _palune_crt)
                get_filename_component(_base "${_file}" NAME)
                if(_base STREQUAL _name)
                    list(APPEND PALUNE_MSVC_CRT_DLLS "${_file}")
                endif()
            endforeach()
        endforeach()
    endif()
    if(PALUNE_MSVC_CRT_DLLS)
        message(STATUS "MSVC 运行时随安装包部署：${_palune_vc_root}")
        install(FILES ${PALUNE_MSVC_CRT_DLLS} DESTINATION "${CMAKE_INSTALL_BINDIR}")
    else()
        message(WARNING "未找到 MSVC 运行时（VCINSTALLDIR / 编译器路径），"
                        "干净机器可能缺 vcruntime140.dll")
    endif()
endif()

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

# 把 Qt 运行时依赖部署进安装树。
# Windows 不用 qt_generate_deploy_app_script：它生成的脚本对含空格路径（"Program Files"、
# 用户名带空格）不加引号，cpack 的 install 阶段会直接失败；统一改用下面的 windeployqt。
# 非 Windows 仍走官方脚本（macOS 的 macdeployqt 由它调用）。
if(NOT WIN32)
    # Qt 6.9 的 macdeployqt 已移除 --compiler-runtime 参数，显式传入会直接报
    # "Unknown argument"；编译器运行库由系统提供，不需要也不应再传。
    qt_generate_deploy_app_script(TARGET Palune OUTPUT_SCRIPT PALUNE_DEPLOY_SCRIPT
        NO_UNSUPPORTED_PLATFORM_ERROR)
    install(SCRIPT ${PALUNE_DEPLOY_SCRIPT})
endif()

# Windows：windeployqt 一次补齐 Qt DLL、platforms 插件与 QML 模块。
# MSVC 运行时由上面的 install(FILES) 直接部署；这里用 --no-compiler-runtime 避免
# 把 vc_redist.x64.exe 塞进 bin。--qmldir 让 QtQuick.Controls.Basic 等 QML 模块进安装树。
if(WIN32 AND WINDEPLOYQT_EXECUTABLE)
    install(CODE "
        set(_palune_bin \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}\")
        message(STATUS \"windeployqt --qmldir -> \${_palune_bin}\")
        execute_process(
            COMMAND \"${WINDEPLOYQT_EXECUTABLE}\" --qmldir \"${CMAKE_CURRENT_SOURCE_DIR}/qml\"
                    --no-compiler-runtime --dir \"\${_palune_bin}\" \"\${_palune_bin}/Palune.exe\"
            RESULT_VARIABLE _palune_wdq_result)
        if(NOT _palune_wdq_result EQUAL 0)
            message(FATAL_ERROR \"windeployqt 部署 QML 模块失败：\${_palune_wdq_result}\")
        endif()
    ")
endif()

# CPack：Windows 产出 NSIS 安装器与 MSI。
set(CPACK_PACKAGE_NAME "Palune")
set(CPACK_PACKAGE_VENDOR "Palune")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Palune 媒体播放器")
set(CPACK_PACKAGE_VERSION "${PALUNE_VERSION}")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "Palune")
# 许可页必须给 RTF：中文纯文本在 NSIS/WiX 里会按代码页解码成乱码。
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/packaging/LICENSE.rtf")

if(WIN32)
    set(CPACK_PACKAGE_FILE_NAME "Palune-${PALUNE_VERSION}-win-x64")
    set(CPACK_NSIS_DISPLAY_NAME "Palune")
    set(CPACK_NSIS_PACKAGE_NAME "Palune")
    set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
    set(CPACK_NSIS_INSTALLED_ICON_NAME "bin\\\\Palune.exe")
    set(CPACK_NSIS_MUI_FINISHPAGE_RUN "bin\\\\Palune.exe")
    # 安装向导与卸载器图标用同一份品牌 ico（与 exe 图标同源，见 resources/brand）。
    set(CPACK_NSIS_MUI_ICON "${CMAKE_CURRENT_SOURCE_DIR}/resources/brand/palune.ico")
    set(CPACK_NSIS_MUI_UNIICON "${CMAKE_CURRENT_SOURCE_DIR}/resources/brand/palune.ico")
    # CPack 默认不为 bin 下的可执行文件建快捷方式，必须显式创建；
    # 开始菜单文件夹里同时放主程序和卸载项，桌面再放一份主程序。
    # NSIS 字符串用单引号，避免 CPackConfig.cmake 里双引号未转义导致语法错误。
    set(CPACK_NSIS_CREATE_ICONS_EXTRA
"CreateDirectory '$SMPROGRAMS\\\\Palune'
CreateShortCut '$SMPROGRAMS\\\\Palune\\\\Palune.lnk' '$INSTDIR\\\\bin\\\\Palune.exe'
CreateShortCut '$SMPROGRAMS\\\\Palune\\\\Uninstall Palune.lnk' '$INSTDIR\\\\uninstall.exe'
CreateShortCut '$DESKTOP\\\\Palune.lnk' '$INSTDIR\\\\bin\\\\Palune.exe'")
    set(CPACK_NSIS_DELETE_ICONS_EXTRA
"Delete '$SMPROGRAMS\\\\Palune\\\\Palune.lnk'
Delete '$SMPROGRAMS\\\\Palune\\\\Uninstall Palune.lnk'
RMDir '$SMPROGRAMS\\\\Palune'
Delete '$DESKTOP\\\\Palune.lnk'")
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

add_executable(tst_core tests/unit/tst_core.cpp)
target_link_libraries(tst_core PRIVATE palune_core Qt6::Test)
add_test(NAME core COMMAND tst_core)

add_executable(tst_actions tests/unit/tst_actions.cpp)
target_link_libraries(tst_actions PRIVATE palune_core Qt6::Test)
add_test(NAME actions COMMAND tst_actions)

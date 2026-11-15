# 单个 target 的 POST_BUILD 依赖部署（Windows）。
#
# 由 CMakeLists.txt 以脚本模式调用，而不是在构建图里直接串命令，原因是：
# 所有 exe 输出到同一个目录，多个 target 并行构建时它们的 POST_BUILD 会同时跑
# windeployqt 并往同一批文件写，Windows 上会出现 sharing violation。这里用
# file(LOCK) 把「部署」串行化，既能保证每个 target 都真的执行过部署，
# 又不会因为并发写同一目录而偶发失败。
#
# 用法：
#   cmake -DEXE=<exe> -DDEST=<dir> -DWINDEPLOYQT=<path> [-DQML_SRC_DIR=<qml>]
#         -DQT_CONF=<qt.conf> -DCRT="<dll;dll>" -DRUNTIME_LIBS="<dll;dll>"
#         [-DTEST_LIB=<Qt6Test.dll>] -DLOCK=<lockfile> -P deploy_qt_runtime.cmake

if(NOT DEFINED EXE OR NOT EXISTS "${EXE}")
    message(FATAL_ERROR "deploy_qt_runtime: 缺少可执行文件 EXE=${EXE}")
endif()
if(NOT DEFINED DEST)
    get_filename_component(DEST "${EXE}" DIRECTORY)
endif()
file(MAKE_DIRECTORY "${DEST}")

# 同目录所有 target 共用一把锁：并发 POST_BUILD 会排队，不会同时写 DLL。
# 注意：GUARD FUNCTION 只在函数上下文里有效，-P 脚本模式没有函数上下文，
# 会直接报 "error locking file"（本机实测）。这里不写 GUARD：锁持有到本进程
# 结束，正好是「一个 target 部署完再下一个」；结尾显式 RELEASE。
if(DEFINED LOCK)
    file(LOCK "${LOCK}" TIMEOUT 600 RESULT_VARIABLE _lock_rv)
    if(_lock_rv)
        message(FATAL_ERROR "deploy_qt_runtime: 获取部署锁失败（${_lock_rv}）：${LOCK}")
    endif()
endif()

get_filename_component(_exe_name "${EXE}" NAME)

# 1) 先补「windeployqt 不认识」的文件，避免它解析依赖时找不到同目录的 Qt6Test.dll。
foreach(_lib IN LISTS RUNTIME_LIBS)
    if(EXISTS "${_lib}")
        get_filename_component(_name "${_lib}" NAME)
        execute_process(COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${_lib}" "${DEST}/${_name}"
                        RESULT_VARIABLE _r)
        if(NOT _r EQUAL 0)
            message(FATAL_ERROR "deploy_qt_runtime: 复制 ${_name} 失败（${_r}）")
        endif()
    endif()
endforeach()
if(DEFINED TEST_LIB AND EXISTS "${TEST_LIB}")
    execute_process(COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${TEST_LIB}" "${DEST}/Qt6Test.dll"
                    RESULT_VARIABLE _r)
    if(NOT _r EQUAL 0)
        message(FATAL_ERROR "deploy_qt_runtime: 复制 Qt6Test.dll 失败（${_r}）")
    endif()
endif()
foreach(_crt IN LISTS CRT)
    if(EXISTS "${_crt}")
        get_filename_component(_name "${_crt}" NAME)
        execute_process(COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${_crt}" "${DEST}/${_name}"
                        RESULT_VARIABLE _r)
        if(NOT _r EQUAL 0)
            message(FATAL_ERROR "deploy_qt_runtime: 复制 CRT ${_name} 失败（${_r}）")
        endif()
    endif()
endforeach()
if(DEFINED QT_CONF AND EXISTS "${QT_CONF}")
    execute_process(COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${QT_CONF}" "${DEST}/qt.conf"
                    RESULT_VARIABLE _r)
    if(NOT _r EQUAL 0)
        message(FATAL_ERROR "deploy_qt_runtime: 复制 qt.conf 失败（${_r}）")
    endif()
endif()

# 2) windeployqt：按该 exe 自己的导入表部署 Qt DLL + platforms 插件。
#    Palune 额外带 --qmldir，把 QML 模块一起部署。
if(NOT DEFINED WINDEPLOYQT OR NOT EXISTS "${WINDEPLOYQT}")
    message(FATAL_ERROR "deploy_qt_runtime: 找不到 windeployqt（${WINDEPLOYQT}）")
endif()
set(_args --no-compiler-runtime --dir "${DEST}")
if(DEFINED QML_SRC_DIR AND EXISTS "${QML_SRC_DIR}")
    list(APPEND _args --qmldir "${QML_SRC_DIR}")
endif()
list(APPEND _args "${EXE}")
execute_process(COMMAND "${WINDEPLOYQT}" ${_args}
                RESULT_VARIABLE _wdq
                OUTPUT_VARIABLE _out
                ERROR_VARIABLE _err)
if(NOT _wdq EQUAL 0)
    message(FATAL_ERROR "deploy_qt_runtime: windeployqt 部署 ${_exe_name} 失败（${_wdq}）\n${_err}")
endif()

# 3) 立即自检：Qt6Core 与 qt.conf 必须已经落到同目录，缺了就当场失败，
#    而不是等用户双击时由 csrss 弹一个我们读不到正文的系统错误框。
#    Palune（带 --qmldir）额外要求 platforms 插件与 QtQuick 模块。
set(_must Qt6Core.dll qt.conf)
if(DEFINED QML_SRC_DIR AND EXISTS "${QML_SRC_DIR}")
    list(APPEND _must platforms/qwindows.dll qml/QtQuick/qmldir)
endif()
foreach(_rel IN LISTS _must)
    if(NOT EXISTS "${DEST}/${_rel}")
        message(FATAL_ERROR "deploy_qt_runtime: ${_exe_name} 部署后仍缺 ${_rel}")
    endif()
endforeach()

if(DEFINED LOCK)
    file(LOCK "${LOCK}" RELEASE RESULT_VARIABLE _lock_rv)
endif()

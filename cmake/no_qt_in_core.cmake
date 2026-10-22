# core 层无 Qt 红线检查：grep 到 Qt 头直接构建失败
set(SCLD_QLIST
    "QtCore" "QtGui" "QtWidgets" "QtQuick" "QtNetwork" "QtWebSocket"
    "QWebSocket" "Q_OBJECT" "QObject" "QString" "QVector")

file(GLOB_RECURSE SCLD_CORE_FILES
    "${CORE_DIR}/foundation/*"
    "${CORE_DIR}/core/*"
    "${CORE_DIR}/service/*")

set(SCLD_FOUND 0)
foreach(f ${SCLD_CORE_FILES})
    if(f MATCHES "\\.hpp$|\\.cpp$|\\.h$")
        file(READ ${f} content)
        foreach(q ${SCLD_QLIST})
            if(content MATCHES "#include <${q}")
                message(FATAL_ERROR "core 红线破坏: ${f} 引用了 Qt 头 <${q}>")
            endif()
        endforeach()
    endif()
endforeach()
message(STATUS "core 无 Qt 依赖检查通过")

QT += core network sql
CONFIG += console c++17
CONFIG -= app_bundle

# 中文源码需显式 UTF-8 编译（MSVC 需要）
msvc: QMAKE_CXXFLAGS += /utf-8

TARGET = LicenseServer
TEMPLATE = app

SOURCES += \
    server/main.cpp \
    server/LicenseDb.cpp \
    server/LicenseHttpServer.cpp

HEADERS += \
    server/LicenseDb.h \
    server/LicenseHttpServer.h

# 编译开关：两个平台共用同一套源码，仅数据库访问层按平台条件编译
#   Windows -> SQLite（QSQLITE，本地文件，免部署数据库服务）
#   Linux   -> MySQL（QMYSQL，需目标 Qt 已构建该驱动插件 + 系统安装 libmysqlclient）
win32 {
    DEFINES += QDV_DB_SQLITE
}
unix:!macx {
    DEFINES += QDV_DB_MYSQL
}

# --- Windows 资源：exe 图标 + 版本信息 ---
# 图标由 tools/icons/make_icons.py 生成；qmake 会据此自动合成 .rc 并编进 exe，
# 装完以后的开始菜单/桌面快捷方式和资源管理器都取这里的图标。
win32 {
    RC_ICONS = $$PWD/../resources/icons/license_server.ico
    VERSION = 1.0.2
    QMAKE_TARGET_COMPANY = QDVision
    QMAKE_TARGET_PRODUCT = LicenseServer
    QMAKE_TARGET_DESCRIPTION = QDVision License Server
}

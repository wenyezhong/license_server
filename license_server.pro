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
#!/usr/bin/env bash
# Linux 版 build_server.bat：编译 license_server（MySQL 版）
# 用法：
#   ./build_server.sh                 # 使用 PATH 中的 qmake
#   QMAKE=/opt/Qt5.14.2/bin/qmake ./build_server.sh   # 指定 Qt 安装路径

set -e

QMAKE="${QMAKE:-qmake}"
cd "$(dirname "$0")/license_server"

echo "====CONFIG===="
"$QMAKE" license_server.pro
if [ $? -ne 0 ]; then echo "QMAKE_FAILED"; exit 1; fi

echo "====BUILD===="
make -j"$(nproc)"
if [ $? -ne 0 ]; then echo "BUILD_FAILED"; exit 1; fi

echo "BUILD_OK"

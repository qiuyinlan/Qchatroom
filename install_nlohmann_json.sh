#!/bin/bash

# 安装 nlohmann/json 库的脚本
echo "正在安装 nlohmann/json 库..."

# 检查系统类型
if [[ "$OSTYPE" == "linux-gnu"* ]]; then
    # Linux 系统
    if command -v apt-get &> /dev/null; then
        # Ubuntu/Debian
        sudo apt-get update
        sudo apt-get install -y nlohmann-json3-dev
        echo "nlohmann/json 安装完成！"
    elif command -v yum &> /dev/null; then
        # CentOS/RHEL
        sudo yum install -y nlohmann-json-devel
        echo "nlohmann/json 安装完成！"
    else
        echo "请手动安装 nlohmann/json 库"
        echo "Ubuntu/Debian: sudo apt-get install nlohmann-json3-dev"
        echo "CentOS/RHEL: sudo yum install nlohmann-json-devel"
    fi
elif [[ "$OSTYPE" == "darwin"* ]]; then
    # macOS 系统
    if command -v brew &> /dev/null; then
        brew install nlohmann-json
        echo "nlohmann/json 安装完成！"
    else
        echo "请先安装 Homebrew，然后运行: brew install nlohmann-json"
    fi
else
    echo "不支持的操作系统，请手动安装 nlohmann/json 库"
fi

echo "安装脚本执行完成！" 
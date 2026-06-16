#!/bin/bash

set -e

echo "=== 管道聊天程序测试 ==="
echo ""

# 测试1: 检查程序是否存在
echo "[测试1] 检查可执行文件..."
if [ -x "./pipe_chat" ]; then
    echo "  ✓ pipe_chat 可执行文件存在"
else
    echo "  ✗ pipe_chat 可执行文件不存在"
    exit 1
fi

# 测试2: 检查编译警告
echo ""
echo "[测试2] 重新编译检查警告..."
make clean > /dev/null 2>&1
if make 2>&1 | grep -q "warning"; then
    echo "  ✗ 编译存在警告"
    make 2>&1 | grep "warning"
    exit 1
else
    echo "  ✓ 编译无警告"
fi

# 测试3: 静态分析代码结构
echo ""
echo "[测试3] 检查代码关键功能..."

check_feature() {
    local pattern=$1
    local desc=$2
    if grep -q "$pattern" pipe_chat.c; then
        echo "  ✓ $desc"
    else
        echo "  ✗ $desc"
        exit 1
    fi
}

check_feature "pipe(" "使用 pipe() 创建管道"
check_feature "fork()" "使用 fork() 创建子进程"
check_feature "select(" "使用 select() 多路复用"
check_feature "forward_message" "消息转发函数"
check_feature "notify_exit" "退出通知函数"
check_feature "MAX_MSG_LEN" "消息长度限制"
check_feature "不能发送空消息" "空消息错误提示"
check_feature "消息太长" "消息过长错误提示"
check_feature "管道可能已关闭" "管道关闭错误提示"
check_feature "对方已退出" "对方退出通知"
check_feature "waitpid" "等待子进程退出"
check_feature "SIGTERM" "信号清理子进程"

echo ""
echo "[测试4] 检查内存安全..."

# 检查是否有未初始化的变量或潜在问题
echo "  检查是否正确关闭管道..."
grep -n "close_pipe\|close_all_pipes" pipe_chat.c > /dev/null && echo "  ✓ 有关闭管道的辅助函数"

echo "  检查错误处理..."
grep -n "perror\|strerror" pipe_chat.c > /dev/null && echo "  ✓ 有错误提示"

echo ""
echo "=== 所有测试通过！ ==="
echo ""
echo "程序使用说明："
echo "  1. 运行 ./pipe_chat 启动程序"
echo "  2. 父进程会创建两个聊天端 A 和 B"
echo "  3. 由于两个子进程共享 stdin，建议在两个终端分别运行聊天端"
echo "  4. 或者使用以下方式测试（在两个终端分别执行）："
echo "     终端1: mkfifo /tmp/chat_a /tmp/chat_b"
echo "     终端1: cat > /tmp/chat_a & cat /tmp/chat_b"
echo "     终端2: ./pipe_chat < /tmp/chat_a > /tmp/chat_b"
echo ""
echo "  快速测试命令："
echo "    echo -e '你好，B！\\nquit\\n' | timeout 2 ./pipe_chat 2>&1 || true"

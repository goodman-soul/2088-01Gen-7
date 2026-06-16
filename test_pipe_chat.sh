#!/bin/bash

PASS=0
FAIL=0
STATIC_PASS=0
STATIC_FAIL=0
RUNTIME_PASS=0
RUNTIME_FAIL=0

echo "=== 管道聊天程序测试 ==="
echo ""

check_static() {
    local desc=$1
    local pattern=$2
    if grep -q "$pattern" pipe_chat.c; then
        echo "  ✓ $desc"
        PASS=$((PASS + 1))
        STATIC_PASS=$((STATIC_PASS + 1))
    else
        echo "  ✗ $desc"
        FAIL=$((FAIL + 1))
        STATIC_FAIL=$((STATIC_FAIL + 1))
    fi
}

check_runtime() {
    local desc=$1
    local condition=$2
    if eval "$condition"; then
        echo "  ✓ $desc"
        PASS=$((PASS + 1))
        RUNTIME_PASS=$((RUNTIME_PASS + 1))
    else
        echo "  ✗ $desc"
        cat "$OUTPUT_LOG" 2>/dev/null | head -30
        FAIL=$((FAIL + 1))
        RUNTIME_FAIL=$((RUNTIME_FAIL + 1))
    fi
}

run_chat() {
    local input="$1"
    OUTPUT_LOG=$(mktemp)
    printf '%b' "$input" | ./pipe_chat > "$OUTPUT_LOG" 2>&1
    local exit_code=$?
    return $exit_code
}

# ========== 静态测试 ==========
echo "[静态测试]"

echo "  [1.1] 检查可执行文件..."
if [ -x "./pipe_chat" ]; then
    echo "    ✓ pipe_chat 可执行文件存在"
    PASS=$((PASS + 1))
    STATIC_PASS=$((STATIC_PASS + 1))
else
    echo "    ✗ pipe_chat 可执行文件不存在，尝试编译..."
    if make > /dev/null 2>&1 && [ -x "./pipe_chat" ]; then
        echo "    ✓ 编译成功"
        PASS=$((PASS + 1))
        STATIC_PASS=$((STATIC_PASS + 1))
    else
        echo "    ✗ 编译失败"
        FAIL=$((FAIL + 1))
        STATIC_FAIL=$((STATIC_FAIL + 1))
        exit 1
    fi
fi

echo "  [1.2] 重新编译检查警告..."
make clean > /dev/null 2>&1
BUILD_OUTPUT=$(make 2>&1)
if echo "$BUILD_OUTPUT" | grep -qi "warning"; then
    echo "    ✗ 编译存在警告"
    echo "$BUILD_OUTPUT" | grep -i "warning"
    FAIL=$((FAIL + 1))
    STATIC_FAIL=$((STATIC_FAIL + 1))
else
    echo "    ✓ 编译无警告"
    PASS=$((PASS + 1))
    STATIC_PASS=$((STATIC_PASS + 1))
fi

echo "  [1.3] 检查代码关键功能..."
check_static "使用 pipe() 创建管道" "pipe("
check_static "使用 fork() 创建子进程" "fork()"
check_static "使用 select() 多路复用" "select("
check_static "消息转发函数存在" "forward_message"
check_static "退出通知函数存在" "notify_exit"
check_static "消息长度限制宏定义" "MAX_MSG_LEN"
check_static "空消息错误提示" "不能发送空消息"
check_static "消息过长错误提示" "消息太长"
check_static "管道关闭错误提示" "管道可能已关闭"
check_static "对方退出通知" "对方已退出"
check_static "父进程正确关闭 to_child.read_fd" "close(self->to_child.read_fd)"
check_static "父进程正确关闭 from_child.write_fd" "close(self->from_child.write_fd)"

echo ""
echo "  静态测试结果: $STATIC_PASS/$((STATIC_PASS + STATIC_FAIL)) 通过"

# ========== 运行时测试 ==========
echo ""
echo "[运行时测试]"

echo "  [2.1] 测试程序正常启动（无 select 失败错误）..."
run_chat 'quit\nquit\n'
check_runtime "无 'select 失败' 错误" "! grep -q 'select 失败' '$OUTPUT_LOG'"
check_runtime "程序输出启动信息" "grep -q '正在启动管道聊天程序' '$OUTPUT_LOG'"
check_runtime "聊天端 A 启动成功" "grep -q '聊天端 A.*已启动' '$OUTPUT_LOG'"
check_runtime "聊天端 B 启动成功" "grep -q '聊天端 B.*已启动' '$OUTPUT_LOG'"
check_runtime "程序正常退出" "grep -q '程序已正常退出' '$OUTPUT_LOG'"
rm -f "$OUTPUT_LOG"

echo "  [2.2] 测试空消息错误提示..."
run_chat '\nquit\nquit\n'
check_runtime "正确提示'不能发送空消息'" "grep -q '不能发送空消息' '$OUTPUT_LOG'"
rm -f "$OUTPUT_LOG"

echo "  [2.3] 测试长消息错误提示..."
LONG_MSG=$(python3 -c "print('a' * 300)" 2>/dev/null || printf 'a%.0s' $(seq 1 300))
run_chat "${LONG_MSG}\nquit\nquit\n"
check_runtime "正确提示'消息太长'" "grep -q '消息太长' '$OUTPUT_LOG'"
rm -f "$OUTPUT_LOG"

echo "  [2.4] 测试消息转发功能..."
run_chat 'HelloFromA\nquit\nquit\n'
check_runtime "父进程转发消息" "grep -q '转发:' '$OUTPUT_LOG'"
check_runtime "包含 A -> B 方向" "grep -q 'A -> B' '$OUTPUT_LOG'"
rm -f "$OUTPUT_LOG"

echo "  [2.5] 测试一方退出后另一方收到通知..."
run_chat 'quit\nquit\n'
check_runtime "父进程发送退出通知" "grep -q '通知聊天端' '$OUTPUT_LOG'"
check_runtime "系统通知包含'聊天端.*已退出'" "grep -q '\[系统\] 聊天端.*已退出' '$OUTPUT_LOG'"
check_runtime "检测到断开连接" "grep -q '已断开连接' '$OUTPUT_LOG'"
rm -f "$OUTPUT_LOG"

echo "  [2.6] 测试 Ctrl+D (EOF) 退出..."
run_chat ''
check_runtime "正确检测输入关闭" "grep -q '输入已关闭' '$OUTPUT_LOG'"
check_runtime "程序正常退出" "grep -q '程序已正常退出' '$OUTPUT_LOG'"
rm -f "$OUTPUT_LOG"

echo ""
echo "  运行时测试结果: $RUNTIME_PASS/$((RUNTIME_PASS + RUNTIME_FAIL)) 通过"

# ========== 总结 ==========
echo ""
echo "========================================"
echo "  测试总结: $PASS/$((PASS + FAIL)) 通过"
echo "   - 静态测试: $STATIC_PASS/$((STATIC_PASS + STATIC_FAIL))"
echo "   - 运行时测试: $RUNTIME_PASS/$((RUNTIME_PASS + RUNTIME_FAIL))"
echo "========================================"

if [ $FAIL -eq 0 ]; then
    echo ""
    echo "🎉 所有测试通过！"
    exit 0
else
    echo ""
    echo "❌ 有 $FAIL 个测试失败"
    exit 1
fi

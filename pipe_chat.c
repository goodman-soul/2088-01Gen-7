#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/wait.h>
#include <sys/select.h>
#include <errno.h>
#include <signal.h>

#define MAX_MSG_LEN 256
#define BUF_SIZE (MAX_MSG_LEN + 1)

typedef struct {
    int read_fd;
    int write_fd;
} Pipe;

typedef struct {
    pid_t pid;
    Pipe to_child;
    Pipe from_child;
    char name;
    int alive;
} ChatEnd;

void close_pipe(Pipe *p) {
    if (p->read_fd != -1) close(p->read_fd);
    if (p->write_fd != -1) close(p->write_fd);
    p->read_fd = p->write_fd = -1;
}

void close_all_pipes(ChatEnd *ends) {
    close_pipe(&ends[0].to_child);
    close_pipe(&ends[0].from_child);
    close_pipe(&ends[1].to_child);
    close_pipe(&ends[1].from_child);
}

int create_pipe(Pipe *p) {
    int fds[2];
    if (pipe(fds) == -1) {
        perror("创建管道失败");
        return -1;
    }
    p->read_fd = fds[0];
    p->write_fd = fds[1];
    return 0;
}

void chat_end_process(ChatEnd *self, ChatEnd *peer) {
    char buf[BUF_SIZE];
    fd_set readfds;
    int max_fd;

    close(self->to_child.write_fd);
    close(self->from_child.read_fd);
    close(peer->to_child.read_fd);
    close(peer->to_child.write_fd);
    close(peer->from_child.read_fd);
    close(peer->from_child.write_fd);

    printf("[聊天端 %c] 已启动，输入消息发送给对方（输入 'quit' 退出）:\n", self->name);
    fflush(stdout);

    while (1) {
        FD_ZERO(&readfds);
        FD_SET(STDIN_FILENO, &readfds);
        FD_SET(self->to_child.read_fd, &readfds);
        max_fd = (STDIN_FILENO > self->to_child.read_fd) ? STDIN_FILENO : self->to_child.read_fd;

        int ret = select(max_fd + 1, &readfds, NULL, NULL, NULL);
        if (ret == -1) {
            if (errno == EINTR) continue;
            fprintf(stderr, "[聊天端 %c] 错误: select 失败: %s\n", self->name, strerror(errno));
            break;
        }

        if (FD_ISSET(STDIN_FILENO, &readfds)) {
            if (!fgets(buf, BUF_SIZE, stdin)) {
                fprintf(stderr, "[聊天端 %c] 提示: 输入已关闭，正在退出...\n", self->name);
                break;
            }

            size_t len = strlen(buf);
            if (len > 0 && buf[len - 1] == '\n') {
                buf[len - 1] = '\0';
                len--;
            }

            if (len == 0) {
                fprintf(stderr, "[聊天端 %c] 错误: 不能发送空消息\n", self->name);
                continue;
            }

            if (len > MAX_MSG_LEN - 1) {
                fprintf(stderr, "[聊天端 %c] 错误: 消息太长（最大 %d 字符），当前 %zu 字符\n",
                        self->name, MAX_MSG_LEN - 1, len);
                continue;
            }

            if (strcmp(buf, "quit") == 0) {
                printf("[聊天端 %c] 正在退出...\n", self->name);
                break;
            }

            ssize_t written = write(self->from_child.write_fd, buf, len + 1);
            if (written == -1) {
                fprintf(stderr, "[聊天端 %c] 错误: 发送消息失败: %s\n", self->name, strerror(errno));
                break;
            }
        }

        if (FD_ISSET(self->to_child.read_fd, &readfds)) {
            ssize_t n = read(self->to_child.read_fd, buf, BUF_SIZE);
            if (n == -1) {
                if (errno == EINTR) continue;
                fprintf(stderr, "[聊天端 %c] 错误: 读取消息失败: %s\n", self->name, strerror(errno));
                break;
            }
            if (n == 0) {
                fprintf(stderr, "[聊天端 %c] 提示: 对方已退出，聊天结束\n", self->name);
                break;
            }
            buf[n] = '\0';
            printf("[聊天端 %c] 收到: %s\n", self->name, buf);
            fflush(stdout);
        }
    }

    close(self->to_child.read_fd);
    close(self->from_child.write_fd);
    exit(0);
}

int spawn_chat_end(ChatEnd *self, ChatEnd *peer) {
    if (create_pipe(&self->to_child) == -1 || create_pipe(&self->from_child) == -1) {
        return -1;
    }

    fflush(stdout);
    fflush(stderr);
    pid_t pid = fork();
    if (pid == -1) {
        perror("创建子进程失败");
        close_pipe(&self->to_child);
        close_pipe(&self->from_child);
        return -1;
    }

    if (pid == 0) {
        chat_end_process(self, peer);
    }

    self->pid = pid;
    self->alive = 1;
    close(self->to_child.read_fd);
    close(self->from_child.write_fd);
    self->to_child.read_fd = -1;
    self->from_child.write_fd = -1;

    return 0;
}

int forward_message(ChatEnd *from, ChatEnd *to) {
    char buf[BUF_SIZE];
    ssize_t n = read(from->from_child.read_fd, buf, BUF_SIZE);

    if (n == -1) {
        if (errno == EINTR) return 0;
        fprintf(stderr, "[父进程] 错误: 从聊天端 %c 读取失败: %s\n",
                from->name, strerror(errno));
        return -1;
    }

    if (n == 0) {
        fprintf(stderr, "[父进程] 提示: 聊天端 %c 已断开连接\n", from->name);
        return -1;
    }

    buf[n] = '\0';
    size_t len = strlen(buf);

    if (len == 0) {
        fprintf(stderr, "[父进程] 错误: 收到来自聊天端 %c 的空消息，已丢弃\n", from->name);
        return 0;
    }

    if (len > MAX_MSG_LEN - 1) {
        fprintf(stderr, "[父进程] 错误: 来自聊天端 %c 的消息过长（%zu 字符），已丢弃\n",
                from->name, len);
        return 0;
    }

    printf("[父进程] 转发: %c -> %c: %s\n", from->name, to->name, buf);
    fflush(stdout);

    ssize_t written = write(to->to_child.write_fd, buf, len + 1);
    if (written == -1) {
        fprintf(stderr, "[父进程] 错误: 转发给聊天端 %c 失败: %s（管道可能已关闭）\n",
                to->name, strerror(errno));
        return -1;
    }

    return 0;
}

void notify_exit(ChatEnd *leaver, ChatEnd *peer) {
    char notify[BUF_SIZE];
    snprintf(notify, BUF_SIZE, "[系统] 聊天端 %c 已退出", leaver->name);
    fprintf(stderr, "[父进程] 通知聊天端 %c: %s\n", peer->name, notify);

    ssize_t written = write(peer->to_child.write_fd, notify, strlen(notify) + 1);
    if (written == -1) {
        fprintf(stderr, "[父进程] 警告: 无法向聊天端 %c 发送退出通知: %s\n",
                peer->name, strerror(errno));
    }
}

void cleanup_and_exit(ChatEnd *ends, int code) {
    close_all_pipes(ends);
    if (ends[0].alive) kill(ends[0].pid, SIGTERM);
    if (ends[1].alive) kill(ends[1].pid, SIGTERM);
    waitpid(ends[0].pid, NULL, 0);
    waitpid(ends[1].pid, NULL, 0);
    exit(code);
}

int main() {
    ChatEnd ends[2];
    ends[0].name = 'A';
    ends[0].alive = 0;
    ends[0].to_child.read_fd = ends[0].to_child.write_fd = -1;
    ends[0].from_child.read_fd = ends[0].from_child.write_fd = -1;

    ends[1].name = 'B';
    ends[1].alive = 0;
    ends[1].to_child.read_fd = ends[1].to_child.write_fd = -1;
    ends[1].from_child.read_fd = ends[1].from_child.write_fd = -1;

    printf("[父进程] 正在启动管道聊天程序...\n");
    printf("[父进程] 创建聊天端 A...\n");
    if (spawn_chat_end(&ends[0], &ends[1]) == -1) {
        fprintf(stderr, "[父进程] 错误: 无法创建聊天端 A\n");
        cleanup_and_exit(ends, 1);
    }

    printf("[父进程] 创建聊天端 B...\n");
    if (spawn_chat_end(&ends[1], &ends[0]) == -1) {
        fprintf(stderr, "[父进程] 错误: 无法创建聊天端 B\n");
        cleanup_and_exit(ends, 1);
    }

    printf("[父进程] 聊天已启动！A 和 B 可以开始聊天了。\n");
    printf("[父进程] 提示: 任意一方输入 'quit' 或 Ctrl+D 退出。\n\n");
    fflush(stdout);

    fd_set readfds;
    int max_fd;

    while (ends[0].alive && ends[1].alive) {
        FD_ZERO(&readfds);
        int fds[2] = {ends[0].from_child.read_fd, ends[1].from_child.read_fd};
        max_fd = 0;

        for (int i = 0; i < 2; i++) {
            if (ends[i].alive && fds[i] != -1) {
                FD_SET(fds[i], &readfds);
                if (fds[i] > max_fd) max_fd = fds[i];
            }
        }

        int ret = select(max_fd + 1, &readfds, NULL, NULL, NULL);
        if (ret == -1) {
            if (errno == EINTR) continue;
            fprintf(stderr, "[父进程] 错误: select 失败: %s\n", strerror(errno));
            break;
        }

        for (int i = 0; i < 2; i++) {
            if (ends[i].alive && FD_ISSET(fds[i], &readfds)) {
                int peer_idx = (i == 0) ? 1 : 0;
                int result = forward_message(&ends[i], &ends[peer_idx]);
                if (result != 0) {
                    ends[i].alive = 0;
                    if (ends[peer_idx].alive) {
                        notify_exit(&ends[i], &ends[peer_idx]);
                    }
                    break;
                }
            }
        }
    }

    printf("\n[父进程] 聊天会话结束，正在清理...\n");

    for (int i = 0; i < 2; i++) {
        if (ends[i].to_child.write_fd != -1) {
            close(ends[i].to_child.write_fd);
            ends[i].to_child.write_fd = -1;
        }
        if (ends[i].from_child.read_fd != -1) {
            close(ends[i].from_child.read_fd);
            ends[i].from_child.read_fd = -1;
        }
    }

    for (int i = 0; i < 2; i++) {
        if (ends[i].pid > 0) {
            waitpid(ends[i].pid, NULL, 0);
            ends[i].alive = 0;
        }
    }

    close_all_pipes(ends);
    printf("[父进程] 程序已正常退出。\n");
    return 0;
}

// ============================================================================
//  server.cpp —— 聊天室服务器（配合 ../client.cpp 使用）
//  协议：一条消息 = UTF-8 文本 + '\0'；连上后第一帧是昵称，之后每帧是聊天内容
//  模型：单线程 + select()，一条线程即可同时服务多个客户端
// ============================================================================

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define FD_SETSIZE 256
#include <WinSock2.h>
#include <WS2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#define CLOSE_SOCKET closesocket
using socket_t = SOCKET;
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#include <csignal>
#include <cerrno>
#define CLOSE_SOCKET close
using socket_t = int;
#endif

#include <cstdio>
#include <string>
#include <vector>

constexpr int PORT       = 2022;         // 与 client.cpp 的 QUN_LIAO_PORT 一致
constexpr int BUF_SIZE   = 4096;
constexpr int MAX_BUFFER = 64 * 1024;    // 半包缓存上限，防止恶意客户端撑爆内存

struct Client {
    socket_t    sock;
    std::string name;
    std::string buf;                     // 已收到、但还没凑成完整一帧的数据
    bool        logged = false;
};

std::vector<Client> g_clients;

// 发送一帧：内容 + '\0'
void sendAll(socket_t sock, const std::string& text) {
    std::string packet = text;
    packet.push_back('\0');
    send(sock, packet.data(), static_cast<int>(packet.size()), 0);
}

// 广播给所有客户端，同时在服务器控制台打印一份
void broadcast(const std::string& text, socket_t exceptSock = (socket_t)-1) {
    for (const auto& c : g_clients)
        if (c.sock != exceptSock) sendAll(c.sock, text);
    printf("%s\n", text.c_str());
    fflush(stdout);
}

void removeClient(size_t index) {
    Client& c = g_clients[index];
    if (c.logged) broadcast("*** " + c.name + " 离开了聊天室 ***", c.sock);
    CLOSE_SOCKET(c.sock);
    g_clients.erase(g_clients.begin() + index);
}

// 处理一帧完整消息：第一帧是昵称，之后都是聊天内容
void handleMessage(size_t index, std::string line) {
    Client& c = g_clients[index];

    if (c.logged) {
        if (!line.empty()) broadcast(c.name + ": " + line, c.sock);  // 不回显给自己
        return;
    }

    // 第一帧：登录昵称（客户端的 scanf_s 已把长度限制在 31 字节内）
    if (line.empty()) line = "游客";

    c.name   = line;
    c.logged = true;
    printf("[登录] %s\n", c.name.c_str());
    broadcast("*** " + c.name + " 进入了聊天室 ***");
}

// 读取一次数据，返回 false 表示连接已断开
bool readClient(size_t index) {
    char raw[BUF_SIZE];
    int n = recv(g_clients[index].sock, raw, sizeof(raw), 0);
    if (n <= 0) return false;

    Client& c = g_clients[index];
    c.buf.append(raw, n);
    if (c.buf.size() > MAX_BUFFER) return false;

    size_t pos;
    while ((pos = c.buf.find('\0')) != std::string::npos) {
        handleMessage(index, c.buf.substr(0, pos));
        c.buf.erase(0, pos + 1);
    }
    return true;
}

int main() {
#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        printf("WSAStartup 失败\n");
        return 1;
    }
    SetConsoleOutputCP(CP_UTF8);         // 控制台按 UTF-8 显示中文
#else
    signal(SIGPIPE, SIG_IGN);            // 写已断开的连接时别直接杀掉进程
#endif

    socket_t listenSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listenSock == (socket_t)-1) {    // Windows 的 SOCKET 是无符号数，不能用 < 0 判断
        printf("创建套接字失败\n");
        return 1;
    }

    int on = 1;
    setsockopt(listenSock, SOL_SOCKET, SO_REUSEADDR,
               reinterpret_cast<const char*>(&on), sizeof(on));

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);   // 本机所有网卡
    addr.sin_port        = htons(PORT);

    if (bind(listenSock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        printf("bind 失败：端口 %d 是不是被占用了？\n", PORT);
        return 1;
    }
    if (listen(listenSock, SOMAXCONN) < 0) {
        printf("listen 失败\n");
        return 1;
    }

    printf("聊天室服务器已启动，监听端口 %d，等待客户端连接...\n", PORT);
    fflush(stdout);

    for (;;) {
        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(listenSock, &readSet);

        socket_t maxFd = listenSock;
        for (const auto& c : g_clients) {
            FD_SET(c.sock, &readSet);
            if (c.sock > maxFd) maxFd = c.sock;
        }

        if (select(static_cast<int>(maxFd) + 1, &readSet, nullptr, nullptr, nullptr) < 0) {
#ifdef _WIN32
            printf("select 失败: %d\n", WSAGetLastError());
            break;
#else
            if (errno == EINTR) continue;
            printf("select 失败\n");
            break;
#endif
        }

        // 1. 有新客户端连进来
        if (FD_ISSET(listenSock, &readSet)) {
            sockaddr_in peer{};
#ifdef _WIN32
            int len = sizeof(peer);
#else
            socklen_t len = sizeof(peer);
#endif
            socket_t s = accept(listenSock,
                                reinterpret_cast<sockaddr*>(&peer), &len);
            if (s != (socket_t)-1) {
                g_clients.push_back(Client{s, "", "", false});
                char ip[INET_ADDRSTRLEN] = {};
                inet_ntop(AF_INET, &peer.sin_addr, ip, sizeof(ip));
                printf("[连接] %s:%d\n", ip, ntohs(peer.sin_port));
                fflush(stdout);
            }
        }

        // 2. 已有客户端发来数据；倒序遍历，删除元素不影响还没处理的下标
        for (size_t i = g_clients.size(); i-- > 0;) {
            if (!FD_ISSET(g_clients[i].sock, &readSet)) continue;
            if (!readClient(i)) removeClient(i);
        }
    }

    for (const auto& c : g_clients) CLOSE_SOCKET(c.sock);
    CLOSE_SOCKET(listenSock);
#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
}

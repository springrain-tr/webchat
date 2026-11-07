# webchat — 控制台聊天室

一个用 C++ 写的局域网控制台聊天室，包含服务端与 Windows 客户端：

- **server**：单线程 + `select()`，一条线程同时服务多个客户端，Windows / Linux 均可编译。
- **client**：Windows 控制台界面，自绘聊天区、分割线和输入行，支持中文输入与显示（统一 UTF-8），昵称登录、断线提示。

## 目录结构

```
webchat/
├─ webchat.slnx              # Visual Studio 解决方案（client + server）
├─ .gitignore
├─ build/                    # 构建产物，已被 git 忽略
└─ src/
   ├─ client/                # Windows 客户端
   │  ├─ client.cpp
   │  ├─ client.vcxproj
   │  └─ client.vcxproj.filters
   └─ server/                # 跨平台服务端
      ├─ server.cpp
      ├─ server.vcxproj
      └─ Makefile
```

## 编译

### Windows（Visual Studio）

用 Visual Studio 打开 `webchat.slnx`，选择 `x64` 平台与 `Debug` / `Release` 配置，生成即可。产物在：

```
build/bin/x64/Debug/client.exe
build/bin/x64/Debug/server.exe
```

也可以在开发者命令提示符里用 MSBuild：

```bat
msbuild webchat.slnx /p:Configuration=Release /p:Platform=x64
```

### Linux / MSYS2（服务端）

服务端不依赖 Windows API，用 g++ 直接编译：

```bash
cd src/server
make          # 产物：build/bin/server
make run      # 编译并运行
make clean
```

## 运行

先启动服务端，再启动一个或多个客户端：

```bash
./build/bin/server                      # 默认监听 2022 端口
./build/bin/x64/Debug/client.exe        # 默认连接 127.0.0.1:2022
```

客户端启动后输入昵称进入聊天室，回车发送消息，退格按整字删除中文。

- 客户端要连其它机器：改 `src/client/client.cpp` 顶部的 `SERVER_IP`。
- 换端口：同时改客户端 `QUN_LIAO_PORT` 与服务端 `PORT`。

## 通信协议

一条消息 = **UTF-8 文本 + `'\0'`**，没有消息头，边界靠结尾的 `'\0'` 划分。

1. 客户端连上后发送的第一帧是昵称。
2. 之后每一帧都是聊天内容，服务端原样转给其他客户端（不回显给发送者）。
3. 客户端在本地把收到的帧按控制台列宽截断成一行显示，中文按 2 列计算。
4. 服务端对半包做缓存（上限 64 KB），读满一帧才处理；客户端断开时广播离开消息。

## 已知限制

- 服务端是单线程 `select()` 模型，连接数受 `FD_SETSIZE`（当前 256）限制。
- 聊天记录不做持久化，也不做用户认证，只适合局域网内学习/实验。

## 许可

未指定许可证。如需开源分发，请补充 `LICENSE` 文件。

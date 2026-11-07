#include <stdio.h>
#include <string>
#include <conio.h>

#include <WinSock2.h>

#pragma comment(lib, "ws2_32.lib")

#define SERVER_IP	"127.0.0.1"
#define QUN_LIAO_PORT 2022

SOCKET serverSocket;	//网络套接字
sockaddr_in sockAddr;	//网络地址
char nickName[32];		//昵称

HANDLE g_hOut;			//控制台输出句柄
HANDLE g_hIn;			//控制台输入句柄
int g_width = 110;		//窗口宽度（列）
int g_height = 36;		//窗口高度（行）
int g_left = 0;			//窗口左上角在缓冲区中的坐标
int g_top = 0;
int g_chatRows = 33;	//聊天区行数：0 ~ g_chatRows-1
int g_lineRow = 33;		//分割线所在行
int g_inputRow = 34;	//输入行
int g_nextRow = 0;		//下一条消息写到聊天区的第几行
std::wstring g_input;	//当前正在输入的内容（宽字符，中文按字符删除）
int g_inputCol = 1;		//输入光标所在的列

HANDLE hMutex;	//互斥锁

//void GBKToUTF8(string& strGBK) {};

void GBKToUTF8(std::string& strGBK)
 {
	     std::string strOutUTF8 = "";
	     WCHAR * str1;
	     int n = MultiByteToWideChar(CP_ACP, 0, strGBK.c_str(), -1, NULL, 0);
	     str1 = new WCHAR[n];
	     MultiByteToWideChar(CP_ACP, 0, strGBK.c_str(), -1, str1, n);
	     n = WideCharToMultiByte(CP_UTF8, 0, str1, -1, NULL, 0, NULL, NULL);
	     char* str2 = new char[n];
	     WideCharToMultiByte(CP_UTF8, 0, str1, -1, str2, n, NULL, NULL);
	     strOutUTF8 = str2;
	     delete[]str1;
	     str1 = NULL;
	     delete[]str2;
	     str2 = NULL;
		 strGBK = strOutUTF8;
	 }

bool init() {
	// 1.网络服务的初始化
	WSADATA data;
	int ret = WSAStartup(MAKEWORD(1, 1), &data);
	if (ret != 0) {
		return false;
	}

	// 2.网络套接字
	serverSocket = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);

	//3.物理地址
	sockAddr.sin_family = PF_INET;
	sockAddr.sin_addr.S_un.S_addr = inet_addr(SERVER_IP);
	sockAddr.sin_port = htons(QUN_LIAO_PORT);	//	端口号

	hMutex = CreateMutex(0, 0, "console");

	return true;
}

void login() {
	printf("  欢迎进入聊天室\n\n");
	printf("    昵称: ");
	scanf_s("%s", nickName, sizeof(nickName));

	while (getchar() != '\n');  //清空输入缓冲区

	std::string name = nickName;
	send(serverSocket, name.c_str(), strlen(name.c_str()) + 1, 0);
}

void gotoxy(int x, int y) {
	COORD pos = { (SHORT)(g_left + x), (SHORT)(g_top + y) };
	SetConsoleCursorPosition(g_hOut, pos);
}

//用字符填满一整行：不经过 printf，不会触发自动换行和滚动
void fillRow(int row, char ch) {
	DWORD written = 0;
	COORD pos = { (SHORT)g_left, (SHORT)(g_top + row) };
	FillConsoleOutputCharacterA(g_hOut, ch, g_width, pos, &written);
	FillConsoleOutputAttribute(g_hOut, 7, g_width, pos, &written);
}

//宽字符串转 UTF-8：发送和显示都统一用 UTF-8
std::string WideToUTF8(const std::wstring& w) {
	if (w.empty()) return std::string();
	int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), NULL, 0, NULL, NULL);
	std::string s(n, '\0');
	WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, NULL, NULL);
	return s;
}

//一个宽字符在控制台占几列：中日韩文字和全角符号占 2 列
int wcharWidth(wchar_t ch) {
	unsigned int c = (unsigned int)ch;
	if (c >= 0x1100 && (c <= 0x115F || (c >= 0x2E80 && c <= 0xA4CF) ||
		(c >= 0xAC00 && c <= 0xD7A3) || (c >= 0xF900 && c <= 0xFAFF) ||
		(c >= 0xFE30 && c <= 0xFE4F) || (c >= 0xFF00 && c <= 0xFF60) ||
		(c >= 0xFFE0 && c <= 0xFFE6))) {
		return 2;
	}
	return 1;
}

int strWidth(const std::wstring& s) {
	int w = 0;
	for (wchar_t ch : s) w += wcharWidth(ch);
	return w;
}

//重画输入行：清空 -> ">" -> 输入内容，光标放到内容末尾
void showInput() {
	WaitForSingleObject(hMutex, INFINITE);

	fillRow(g_inputRow, ' ');
	gotoxy(0, g_inputRow);
	printf(">");
	std::string text = WideToUTF8(g_input);
	printf("%s", text.c_str());

	g_inputCol = 1 + strWidth(g_input);
	gotoxy(g_inputCol, g_inputRow);

	ReleaseMutex(hMutex);
}

//读取一行输入：支持中文，退格按整字删除，回车结束
std::string readLine() {
	g_input.clear();
	showInput();

	while (1) {
		INPUT_RECORD rec;
		DWORD read = 0;
		if (!ReadConsoleInputW(g_hIn, &rec, 1, &read) || read == 0) continue;
		if (rec.EventType != KEY_EVENT || !rec.Event.KeyEvent.bKeyDown) continue;

		wchar_t c = rec.Event.KeyEvent.uChar.UnicodeChar;
		if (c == 0) continue;					//方向键、功能键等

		if (c == L'\r' || c == L'\n') {		//回车：结束这一行
			return WideToUTF8(g_input);
		}
		if (c == 8 || c == 127) {				//退格：中文一次删掉整个字
			if (!g_input.empty()) {
				g_input.pop_back();
				showInput();
			}
			continue;
		}
		if (c < 0x20) continue;					//其它控制字符不显示

		if (strWidth(g_input) + wcharWidth(c) > g_width - 2) continue;
		g_input.push_back(c);
		showInput();
	}
}

//按控制台列宽截断 UTF-8 文本：汉字算 2 列，换行等控制字符换成空格
void fitText(const char* s, int maxCells, std::string& out) {
	int cells = 0;
	const unsigned char* p = (const unsigned char*)s;
	while (*p && cells < maxCells) {
		unsigned int cp = *p;
		int n = 1;
		if (cp >= 0xF0) { cp &= 0x07; n = 4; }
		else if (cp >= 0xE0) { cp &= 0x0F; n = 3; }
		else if (cp >= 0xC0) { cp &= 0x1F; n = 2; }
		for (int i = 1; i < n && p[i]; ++i)
			cp = (cp << 6) | (p[i] & 0x3F);

		if (cp < 0x20) {					//换行、制表符等会打乱布局
			out.push_back(' ');
			++p;
			++cells;
			continue;
		}
		int w = (cp >= 0x2E80 || (cp >= 0x1100 && cp <= 0x115F)) ? 2 : 1;
		if (cells + w > maxCells) break;
		out.append((const char*)p, n);
		cells += w;
		p += n;
	}
}

void uiInit() {
	g_hOut = GetStdHandle(STD_OUTPUT_HANDLE);
	g_hIn  = GetStdHandle(STD_INPUT_HANDLE);

	//打开 VT 转义序列，颜色才能生效
	DWORD mode = 0;
	GetConsoleMode(g_hOut, &mode);
	SetConsoleMode(g_hOut, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);

	//按窗口实际大小布局，不再假设窗口一定是 110x36
	CONSOLE_SCREEN_BUFFER_INFO info;
	GetConsoleScreenBufferInfo(g_hOut, &info);
	g_width  = info.srWindow.Right - info.srWindow.Left + 1;
	g_height = info.srWindow.Bottom - info.srWindow.Top + 1;
	if (g_height < 5) g_height = 5;
	if (g_width < 10) g_width = 10;

	//尽量把缓冲区缩成窗口大小；终端不允许也没关系，后面不会写到窗口外
	COORD size = { (SHORT)g_width, (SHORT)g_height };
	SetConsoleScreenBufferSize(g_hOut, size);

	system("cls");
	GetConsoleScreenBufferInfo(g_hOut, &info);
	g_left = info.srWindow.Left;
	g_top  = info.srWindow.Top;

	g_chatRows = g_height - 3;
	g_lineRow  = g_height - 3;
	g_inputRow = g_height - 2;
	g_nextRow  = 0;

	fillRow(g_lineRow, '-');		//这一行之后只由 printMsg 负责重画
	gotoxy(1, g_inputRow);
}

void printMsg(const char* msg) {
	//上锁(申请互斥锁)
	//如果没申请到就一直等待，直到等到为止
	WaitForSingleObject(hMutex, INFINITE);

	if (g_nextRow >= g_chatRows) {
		//聊天区满了：只把聊天区整体上移一行，分割线原地不动
		SMALL_RECT src = { (SHORT)g_left, (SHORT)(g_top + 1),
			(SHORT)(g_left + g_width - 1), (SHORT)(g_top + g_chatRows - 1) };
		COORD dest = { (SHORT)g_left, (SHORT)g_top };
		CHAR_INFO fillChar;
		fillChar.Char.AsciiChar = ' ';
		fillChar.Attributes = 7;
		ScrollConsoleScreenBufferA(g_hOut, &src, NULL, dest, &fillChar);
		g_nextRow = g_chatRows - 1;
	}

	fillRow(g_nextRow, ' ');		//清掉这一行，避免长消息留下残影
	gotoxy(0, g_nextRow);

	static int color = 31;
	std::string text;
	fitText(msg, g_width - 1, text);	//按列宽截断，保证只占一行
	printf("\033[0;%d;40m%s\033[0m", color++, text.c_str());
	if (color > 36) {
		color = 31;
	}

	g_nextRow++;
	gotoxy(g_inputCol, g_inputRow);

	//释放锁
	ReleaseMutex(hMutex);
}

DWORD WINAPI threadFuncRecv(LPVOID pram) {
	char buff[4096];
	while (1) {
		int ret = recv(serverSocket, buff, sizeof(buff), 0);
		if (ret <= 0) {
			printf("服务器关闭或故障!\n");
			break;
		}
		if (ret >= (int)sizeof(buff)) ret = sizeof(buff) - 1;
		buff[ret] = 0;		//补齐字符串结尾

		//打印接收到的信息
		printMsg(buff);
	}


	return NULL;
}

int main(void) {
	SetConsoleOutputCP(CP_UTF8); // 设置控制台输出为 UTF-8
	SetConsoleCP(CP_UTF8); // 设置控制台输入为 UTF-8
	setvbuf(stdout, NULL, _IONBF, 0); // 不走缓冲区，printf 立即输出

	if (!init()) {
		printf("初始化失败！\n");
		return -1;
	}

	int ret = connect(serverSocket, (SOCKADDR*)&sockAddr, sizeof(sockAddr));
	if (ret != 0) {
		printf("连接服务器失败，请检查网络连接!\n");
		return -2;
	}

	//登录聊天室
	login();

	uiInit();  //初始化界面ui

	HANDLE hThread = CreateThread(0, 0, threadFuncRecv, 0, 0, 0);
	CloseHandle(hThread);	//对线程没有影响

	//编辑信息
	while (1) {
		std::string text = readLine();
		if (text.empty()) {
			continue;
		}

		//清空输入行
		WaitForSingleObject(hMutex, INFINITE);
		g_inputCol = 1;
		fillRow(g_inputRow, ' ');
		ReleaseMutex(hMutex);

		//把用户自己说的话，输出到聊天室
		std::string line = "【LocalHost@" + std::string(nickName) + "】" + text;
		printMsg(line.c_str());

		//发送编辑信息（UTF-8 + '\0'，和服务器协议一致）
		send(serverSocket, text.c_str(), (int)text.size() + 1, 0);
	}

	getchar();
	return 0;
}
#include <iostream>
#include <WS2tcpip.h>

#pragma comment(lib, "WS2_32")

constexpr short SERVER_PORT = 6487;

class EXP_OVER {
public:
	EXP_OVER(bool is_recv) : _is_recv(is_recv)
	{
		ZeroMemory(&_over, sizeof(_over));

		_wsabuf[0].buf = _buffer;
		_wsabuf[0].len = sizeof(_buffer);
	}
	//private:
	WSAOVERLAPPED _over;
	bool _is_recv;
	char _buffer[1024];
	WSABUF _wsabuf[1];
};

char recv_buffer[1024]{};
WSABUF recv_wsabuf[1]{};

char send_buffer[1024]{};
WSABUF send_wsabuf[1]{};


SOCKET c_socket;

void error_display(const char* msg, int err_no)
{
	WCHAR* lpMsgBuf;
	FormatMessage(
		FORMAT_MESSAGE_ALLOCATE_BUFFER |
		FORMAT_MESSAGE_FROM_SYSTEM,
		NULL, err_no,
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		(LPTSTR)&lpMsgBuf, 0, NULL);
	std::cout << msg;
	std::wcout << L" 에러 " << lpMsgBuf << std::endl;
	while (true); // 디버깅 용
	LocalFree(lpMsgBuf);
}

void do_recv(SOCKET s, EXP_OVER* o)
{
	DWORD recv_flag = 0;
	ZeroMemory(&o->_over, sizeof(WSAOVERLAPPED));
	//o->hEvent = IO_READ;
	auto ret = WSARecv(s, o->_wsabuf, 1, NULL, &recv_flag, &o->_over, NULL);
	if (0 != ret) {
		auto err_no = WSAGetLastError();
		if (WSA_IO_PENDING != err_no) {
			error_display("WSARecv : ", err_no);
			exit(-1);
		}
	}
}

int main()
{
	std::wcout.imbue(std::locale("korean"));

	WSADATA WSAData{};
	auto res = WSAStartup(MAKEWORD(2, 0), &WSAData);

	SOCKET s_socket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, 0, 0, WSA_FLAG_OVERLAPPED);
	if (s_socket <= 0) std::cout << "ERROR" << "원인\n";
	else std::cout << "Socket Created.\n";

	SOCKADDR_IN addr{};
	addr.sin_family = AF_INET;
	addr.sin_port = htons(SERVER_PORT);
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	bind(s_socket, reinterpret_cast<sockaddr*>(&addr), sizeof(SOCKADDR_IN));
	listen(s_socket, SOMAXCONN);
	INT addr_size = sizeof(SOCKADDR_IN);
	c_socket = WSAAccept(s_socket, reinterpret_cast<sockaddr*>(&addr), &addr_size,
		nullptr, NULL);

	HANDLE hIOCP = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, NULL, 0);
	HANDLE h = CreateIoCompletionPort(reinterpret_cast<HANDLE>(c_socket), hIOCP, 3, 0);

	if (NULL == h) {
		auto err_no = WSAGetLastError();
		error_display("엄", err_no);
		exit(-1);
	}

	EXP_OVER recv_over(true);
	EXP_OVER send_over(false);

	do_recv(c_socket, &recv_over);

	while (true) {
		DWORD io_size;
		WSAOVERLAPPED* o;
		ULONG_PTR key;
		BOOL ret = GetQueuedCompletionStatus(hIOCP, &io_size, &key, &o, INFINITE);
		EXP_OVER* eo = reinterpret_cast<EXP_OVER*>(o);
		if (eo->_is_recv == true) {
			eo->_buffer[io_size] = 0;
			std::cout << "From Client: " << eo->_buffer << std::endl;


			memcpy(send_over._buffer, eo->_buffer, io_size);
			send_over._wsabuf[0].len = io_size;
			ZeroMemory(&send_over._over, sizeof(send_over._over));
			DWORD size_sent;
			WSASend(c_socket, send_over._wsabuf, 1, &size_sent, 0, &send_over._over, 0);
		}
		else {
			do_recv(c_socket, &recv_over);
		}
	}


	closesocket(c_socket);
	closesocket(s_socket);
	WSACleanup();
}
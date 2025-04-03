#include <iostream>
#include <unordered_map>
#include <WS2tcpip.h>
#include <MSWSock.h>

#pragma comment(lib, "WS2_32")
#pragma comment(lib, "MSWSock")

constexpr short SERVER_PORT = 6487;

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


enum IO_OP { IO_RECV, IO_SEND, IO_ACCEPT };

HANDLE g_hIOCP;

class EXP_OVER {
public:
	EXP_OVER(IO_OP op) : _io_op(op)
	{
		ZeroMemory(&_over, sizeof(_over));

		_wsabuf[0].buf = _buffer;
		_wsabuf[0].len = sizeof(_buffer);
	}
	//private:
	WSAOVERLAPPED	_over;
	IO_OP			_io_op;
	SOCKET			_accept_socket;
	unsigned char	_buffer[1024];
	WSABUF			_wsabuf[1];
};

class SESSION;

std::unordered_map<long long, SESSION> g_users;

class SESSION {
public:
	SESSION() {
		std::cout << "DEFAULT SESSION CONSTRUCTOR CALLED!\n";
		exit(-1);
	}
	SESSION(long long session_id, SOCKET s) : _id(session_id), _c_socket(s)
	{
		do_recv();
	}
	~SESSION()
	{
		closesocket(_c_socket);
	}

	void do_send(unsigned char* buff)
	{
		EXP_OVER* o = new EXP_OVER(IO_SEND);
		const unsigned char packet_size = buff[0];
		memcpy(o->_buffer, buff, packet_size);
		o->_wsabuf[0].len = packet_size;
		DWORD size_sent;
		WSASend(_c_socket, o->_wsabuf, 1, &size_sent, 0, &(o->_over), NULL);
	}

	void do_recv()
	{
		DWORD recv_flag = 0;
		ZeroMemory(&_recv_over._over, sizeof(_recv_over._over));
		auto ret = WSARecv(_c_socket, _recv_over._wsabuf, 1, NULL, &recv_flag, &_recv_over._over, NULL);
		if (0 != ret) {
			auto err_no = WSAGetLastError();
			if (WSA_IO_PENDING != err_no) {
				error_display("WSARecv : ", err_no);
				exit(-1);
			}
		}
	}

	void process_packet(unsigned char* p)
	{

	}

	SOCKET			_c_socket;
	long long		_id;

	EXP_OVER		_recv_over{ IO_RECV };
	unsigned char	_remained;

	short			_x, _y;
	std::string		_name;

};

void do_accept(SOCKET s_socket, EXP_OVER* accept_over)
{
	SOCKET c_socket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, 0, 0, WSA_FLAG_OVERLAPPED);
	accept_over->_accept_socket = c_socket;
	CreateIoCompletionPort(reinterpret_cast<HANDLE>(c_socket), g_hIOCP, 3, 0);
	AcceptEx(s_socket, c_socket, accept_over->_buffer, 0,
		sizeof(SOCKADDR_IN) + 16, sizeof(SOCKADDR_IN) + 16, NULL, &accept_over->_over);
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

	g_hIOCP = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, NULL, 0);
	CreateIoCompletionPort(reinterpret_cast<HANDLE>(s_socket), g_hIOCP, -1, 0);


	EXP_OVER accept_over(IO_ACCEPT);
	do_accept(s_socket, &accept_over);

	int new_id = 0;
	while (true) {
		DWORD io_size;
		WSAOVERLAPPED* o;
		ULONG_PTR key;
		BOOL ret = GetQueuedCompletionStatus(g_hIOCP, &io_size, &key, &o, INFINITE);
		EXP_OVER* eo = reinterpret_cast<EXP_OVER*>(o);
		switch (eo->_io_op) {
		case IO_ACCEPT:
			g_users.try_emplace(new_id, new_id, eo->_accept_socket);
			++new_id;
			do_accept(s_socket, &accept_over);
			break;
		case IO_SEND:
			delete eo;
			break;
		case IO_RECV:
			SESSION& user = g_users[key];
			
			unsigned char* p = eo->_buffer;;
			int data_size = io_size + user._remained;

			while (p < eo->_buffer + data_size) {
				unsigned char packet_size = *p;
				if (p + packet_size > eo->_buffer + data_size)
					break;
				user.process_packet(p);
				p += packet_size;
			}
			if (
				p < eo->_buffer + data_size) {
				user._remained = eo->_buffer + data_size - p;
				memcpy(p, eo->_buffer, user._remained);
			}
			else
				user._remained = 0;

			user.do_recv();
			break;
		}
	}


	closesocket(c_socket);
	closesocket(s_socket);
	WSACleanup();
}
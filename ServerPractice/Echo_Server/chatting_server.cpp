#include <iostream>
#include <WS2tcpip.h>
#include <unordered_map>

#pragma comment(lib, "WS2_32")

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
	std::wcout << L" ���� " << lpMsgBuf << std::endl;
	while (true); // ����� ��
	LocalFree(lpMsgBuf);
}

void CALLBACK g_recv_callback(DWORD, DWORD, LPWSAOVERLAPPED, DWORD);
void CALLBACK g_send_callback(DWORD, DWORD, LPWSAOVERLAPPED, DWORD);
//std::unordered_map<WSAOVERLAPPED*, int> g_over_2_user;

class EXP_OVER {
public:
	EXP_OVER(long long id, char* message) : _id(id)
	{
		ZeroMemory(&_send_over, sizeof(_send_over));

		auto packet_size = 2 + strlen(message);
		if (packet_size > 255) {
			std::cout << "MESSAGE TOO LONG";
			exit(-1);
		}
		_send_buffer[0] = static_cast<unsigned char>(packet_size);
		_send_buffer[1] = static_cast<unsigned char>(_id);
		_send_wsabuf[0].buf = _send_buffer;
		_send_wsabuf[0].len = static_cast<ULONG>(packet_size);
		strcpy_s(_send_buffer + 2, sizeof(_send_buffer) - 2, message);
	}
//private:
	WSAOVERLAPPED _send_over;
	long long _id;
	char _send_buffer[1024];
	WSABUF _send_wsabuf[1];
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
		_recv_wsabuf[0].len = sizeof(_recv_buffer);
		_recv_wsabuf[0].buf = _recv_buffer;

		_recv_over.hEvent = reinterpret_cast<HANDLE>(session_id);

		do_recv();
	}
	~SESSION()
	{
		closesocket(_c_socket);
	}
	void recv_callback(DWORD err, DWORD num_bytes, LPWSAOVERLAPPED p_over, DWORD flag)
	{
		_recv_buffer[num_bytes] = 0;
		std::cout << "From Client[" << _id << "] :" << _recv_buffer << std::endl;

		for (auto& u : g_users) {
			u.second.do_send(_id, _recv_buffer);
		}
		do_recv();
	}

	void do_send(long long id, char* message)
	{
		EXP_OVER* o = new EXP_OVER(id, message);
		DWORD size_sent;
		WSASend(_c_socket, o->_send_wsabuf, 1, &size_sent, 0, &(o->_send_over), g_send_callback);
	}
private:
	SOCKET _c_socket;
	long long _id;

	WSAOVERLAPPED _recv_over;
	char _recv_buffer[1024];
	WSABUF _recv_wsabuf[1];


	void do_recv()
	{
		DWORD recv_flag = 0;
		ZeroMemory(&_recv_over, sizeof(_recv_over));
		_recv_over.hEvent = reinterpret_cast<HANDLE>(_id);
		auto ret = WSARecv(_c_socket, _recv_wsabuf, 1, NULL, &recv_flag, &_recv_over, g_recv_callback);
		if (0 != ret) {
			auto err_no = WSAGetLastError();
			if (WSA_IO_PENDING != err_no) {
				error_display("WSARecv : ", err_no);
				exit(-1);
			}
		}
	}
};



void CALLBACK g_send_callback(DWORD err, DWORD num_bytes, LPWSAOVERLAPPED p_over, DWORD flag)
{
	EXP_OVER* o = reinterpret_cast<EXP_OVER*>(p_over);
	delete o;
}

void CALLBACK g_recv_callback(DWORD err, DWORD num_bytes, LPWSAOVERLAPPED p_over, DWORD flag)
{
	auto my_id = reinterpret_cast<long long>(p_over->hEvent);
	g_users[my_id].recv_callback(err, num_bytes, p_over, flag);
}

int main()
{
	std::wcout.imbue(std::locale("korean"));

	WSADATA WSAData{};
	auto res = WSAStartup(MAKEWORD(2, 0), &WSAData);

	SOCKET s_socket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, 0, 0, WSA_FLAG_OVERLAPPED);
	if (s_socket <= 0) std::cout << "ERROR" << "����\n";
	else std::cout << "Socket Created.\n";

	SOCKADDR_IN addr{};
	addr.sin_family = AF_INET;
	addr.sin_port = htons(SERVER_PORT);
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	bind(s_socket, reinterpret_cast<sockaddr*>(&addr), sizeof(SOCKADDR_IN));
	listen(s_socket, SOMAXCONN);
	INT addr_size = sizeof(SOCKADDR_IN);

	long long client_id = 0;
	while (true) {
		auto c_socket = WSAAccept(s_socket, reinterpret_cast<sockaddr*>(&addr), &addr_size,
			nullptr, NULL);
		g_users.try_emplace(client_id, client_id, c_socket);

		++client_id;
	}

	closesocket(s_socket);
	WSACleanup();
}
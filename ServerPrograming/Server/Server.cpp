#include <iostream>
#include <WS2tcpip.h>
#include <map>

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
	std::wcout << L" 에러 " << lpMsgBuf << std::endl;
	while (true); // 디버깅 용
	LocalFree(lpMsgBuf);
}

void CALLBACK g_recv_callback(DWORD, DWORD, LPWSAOVERLAPPED, DWORD);
void CALLBACK g_send_callback(DWORD, DWORD, LPWSAOVERLAPPED, DWORD);


class SESSION;
class EXP_OVER;
std::map<long long, SESSION> g_users;

class EXP_OVER {
public:
	EXP_OVER();

	WSAOVERLAPPED _send_over;
	char _send_buffer[1024];
	WSABUF _send_wsabuf[1];
};

class SESSION {
public:
	SESSION()
	{
		std::cout << "DEFAULT SEESION CONSTRUCTOR CALLED\n";
		exit(-1);
	}
	SESSION(long long id, SOCKET s) : _id(id), _c_socket(s)
	{
		_recv_wsabuf[0].len = sizeof(_recv_buffer);
		_recv_wsabuf[0].buf = _recv_buffer;

		do_recv();
	}
	~SESSION()
	{
		closesocket(_c_socket);
	}

	void recv_callback(DWORD err, DWORD num_bytes, LPWSAOVERLAPPED p_over, DWORD flag);
	void do_send();
	
	float				_ClientPos[2] = { 62.5f, 62.5f };
private:
	SOCKET				_c_socket;
	long long			_id;

	WSAOVERLAPPED		_recv_over;
	char				_recv_buffer[128];
	WSABUF				_recv_wsabuf[1];


	void do_recv();
};



void CALLBACK g_recv_callback(DWORD err, DWORD num_bytes, LPWSAOVERLAPPED p_over, DWORD flag)
{
	auto my_id = reinterpret_cast<long long>(p_over->hEvent);
	if (0 != err || 0 == num_bytes) {
		std::cout << "Client" << my_id << " - logOut!" << std::endl;
		g_users.erase(my_id);
		for (auto& u : g_users) {
			u.second.do_send();
		}
		return;
	}

	g_users[my_id].recv_callback(err, num_bytes, p_over, flag);
}

void CALLBACK g_send_callback(DWORD err, DWORD num_bytes, LPWSAOVERLAPPED p_over, DWORD flag)
{
	EXP_OVER* o = reinterpret_cast<EXP_OVER*>(p_over);
	delete o;
}

int main()
{
	WSADATA WSAData{};
	auto res = WSAStartup(MAKEWORD(2, 2), &WSAData);

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

	while (true) {
		if (10 > g_users.size()) {
			auto c_socket = WSAAccept(s_socket, reinterpret_cast<sockaddr*>(&addr), &addr_size, nullptr, NULL);
			for (long long i = 0; i < 10; ++i) {
				if (g_users.find(i) == g_users.end()) {
					g_users.try_emplace(i, i, c_socket);
					std::cout << "Client" << i <<  " Accept" << std::endl;
					for (auto& u : g_users) {
						u.second.do_send();
					}
					break;
				}
			}
		}
	}
	closesocket(s_socket);
	WSACleanup();
}

EXP_OVER::EXP_OVER()
{
	ZeroMemory(&_send_over, sizeof(_send_over));
	ZeroMemory(_send_buffer, sizeof(_send_buffer));

	int index{};
	for (int i = 0; i < 10; ++i) {
		_send_buffer[index] = static_cast<unsigned char>(i);
		if (g_users.find(i) != g_users.end()) {
			_send_buffer[index + 1] = static_cast<unsigned char>(1);
			memcpy(reinterpret_cast<void*> (&_send_buffer[index + 2]), reinterpret_cast<void*>(g_users[i]._ClientPos), sizeof(float) * 2);
		}
		else {
			_send_buffer[index + 1] = static_cast<unsigned char>(0);
		}
		index += 10;
	}

	_send_wsabuf[0].buf = _send_buffer;
	_send_wsabuf[0].len = 100;
}

void SESSION::do_send()
{
	EXP_OVER* o = new EXP_OVER;
	DWORD size_sent;
	WSASend(_c_socket, o->_send_wsabuf, 1, &size_sent, 0, &(o->_send_over), g_send_callback);
}

void SESSION::do_recv()
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

void SESSION::recv_callback(DWORD err, DWORD num_bytes, LPWSAOVERLAPPED p_over, DWORD flag)
{
	if (_recv_buffer[0] == 'w') {
		if (_ClientPos[1] < 437.0f)
			_ClientPos[1] += 125.0f;
	}
	else if (_recv_buffer[0] == 'a') {
		if (_ClientPos[0] > -437.0f)
			_ClientPos[0] -= 125.0f;
	}
	else if (_recv_buffer[0] == 's') {
		if (_ClientPos[1] > -437.0f)
			_ClientPos[1] -= 125.0f;
	}
	else if (_recv_buffer[0] == 'd') {
		if (_ClientPos[0] < 437.0f)
			_ClientPos[0] += 125.0f;
	}

	for (auto& u : g_users) {
		u.second.do_send();
	}

	do_recv();
}
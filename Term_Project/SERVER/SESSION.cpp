#include "SESSION.h"

extern concurrency::concurrent_unordered_map<long long, std::atomic<std::shared_ptr<SESSION>>> g_users;

SESSION::SESSION(long long id, SOCKET s)
	: _id(id), _socket(s)
{
	do_recv();
}

SESSION::~SESSION()
{
	closesocket(_socket);
}

void SESSION::process_packet(char* packet)
{
	switch(packet[1]) {
	case C2S_P_LOGIN: {
		cs_packet_login* p = reinterpret_cast<cs_packet_login*>(packet);
		strcpy_s(_name, p->name);
	}
		break;
	case C2S_P_MOVE: {

	}
		break;
	}
}

void SESSION::do_recv()
{
	DWORD flag = 0;
	ZeroMemory(&_ex_over._over, sizeof(_ex_over._over));
	_ex_over._wsabuf.buf = _ex_over._send_buf + _remained;
	_ex_over._wsabuf.len = BUF_SIZE - _remained;
	WSARecv(_socket, &_ex_over._wsabuf, 1, 0, &flag, &_ex_over._over, 0);
}
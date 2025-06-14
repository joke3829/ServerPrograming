#pragma once
#include "stdafx.h"
#include "SESSION.h"

class CServer {
public:
	void SetUp();

	void worker_thread();

	void Run();
protected:
	void ReadySocket();
	void ReadyIOCP();
	void ReadyNPC();

	SOCKET			_s_socket;
	SOCKET			_c_socket;
	HANDLE			_h_iocp;
	EX_OVER			_ex_over;

	std::atomic<long long> _uid{ 0 };
};


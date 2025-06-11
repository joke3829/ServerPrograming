#include "SERVER.h"

concurrency::concurrent_unordered_map<long long, std::atomic<std::shared_ptr<SESSION>>> g_users;

std::mutex g_sl;
std::array<std::array<std::unordered_set<long long>, MAP_WIDTH / SECTOR_SIZE>, MAP_HEIGHT / SECTOR_SIZE> g_sector;

void CServer::SetUp()
{
	ReadySocket();

	// NPC Ready

	ReadyIOCP();
}

void CServer::worker_thread()
{
	while (true) {
		DWORD num_bytes;
		ULONG_PTR key;
		WSAOVERLAPPED* over{ nullptr };
		BOOL ret = GetQueuedCompletionStatus(_h_iocp, &num_bytes, &key, &over, INFINITE);
		EX_OVER* ex_over = reinterpret_cast<EX_OVER*>(over);
		if (ret == FALSE) {
			if (ex_over->_comp_type == OP_ACCEPT) std::cout << "Accept Error" << std::endl;
			else {
				std::cout << "잘못됐어요" << std::endl;
				std::shared_ptr<SESSION> pp = g_users.at(key);
				if (nullptr != pp) pp->disconnect();
				g_users.at(key) = nullptr;
				if (ex_over->_comp_type == OP_SEND)
					delete ex_over;
			}
			continue;
		}
		if (0 == num_bytes && (ex_over->_comp_type == OP_RECV || ex_over->_comp_type == OP_SEND)) {
			std::shared_ptr<SESSION> pp = g_users.at(key);
			if (nullptr != pp) pp->disconnect();
			g_users.at(key) = nullptr;
			std::cout << "이상해요" << std::endl;
			if (ex_over->_comp_type == OP_SEND)
				delete ex_over;
			continue;
		}

		switch (ex_over->_comp_type) {
		case OP_ACCEPT:{
			long long client_id = _uid++;
			
			if (client_id < MAX_USER) {
				CreateIoCompletionPort(reinterpret_cast<HANDLE>(_c_socket), _h_iocp, client_id, 0);
				std::shared_ptr<SESSION> p = std::make_shared<SESSION>(client_id, _c_socket);
				g_users.insert(std::make_pair(client_id, p));
			}
			else {
				std::cout << "User Max" << std::endl;
				closesocket(_c_socket);
			}
			_c_socket = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
			//ZeroMemory(&_ex_over._over, sizeof(_ex_over._over));
			AcceptEx(_s_socket, _c_socket, _ex_over._send_buf, 0, sizeof(SOCKADDR_IN) + 16, sizeof(SOCKADDR_IN) + 16, 0, &_ex_over._over);
		}
			break;
		case OP_SEND: {
			delete ex_over;
		}
					break;
		case OP_RECV: {
			std::shared_ptr<SESSION> player = g_users.at(key);
			if (nullptr == player)
				break;
			int remained = num_bytes + player->getRemained();
			char* p = ex_over->_send_buf;
			while (remained > 0) {
				int packet_size = p[0];
				if (packet_size <= remained) {
					player->process_packet(p);
					p = p + packet_size;
					remained = remained - packet_size;
				}
				else
					break;
			}
			player->setRemained(remained);
			if (remained > 0)
				memcpy(ex_over->_send_buf, p, remained);
			player->do_recv();
		}
					break;
		}
	}
}

void CServer::Run()
{
	int num_thread = std::thread::hardware_concurrency();
	std::vector<std::thread> threads;

	for (int i = 0; i < num_thread; ++i)
		threads.emplace_back(&CServer::worker_thread, this);

	for (auto& t : threads)
		t.join();
	closesocket(_s_socket);
}

void CServer::ReadySocket()
{
	_s_socket = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
	SOCKADDR_IN server_addr{};
	server_addr.sin_addr.S_un.S_addr = INADDR_ANY;
	server_addr.sin_port = htons(GAME_PORT);
	server_addr.sin_family = AF_INET;
	bind(_s_socket, reinterpret_cast<sockaddr*>(&server_addr), sizeof(server_addr));
	listen(_s_socket, SOMAXCONN);
}

void CServer::ReadyIOCP()
{
	_h_iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, NULL, 0);
	CreateIoCompletionPort(reinterpret_cast<HANDLE>(_s_socket), _h_iocp, std::numeric_limits<unsigned long long>::max() - 1, 0);

	_c_socket = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
	_ex_over._comp_type = OP_ACCEPT;
	AcceptEx(_s_socket, _c_socket, _ex_over._send_buf, 0, sizeof(SOCKADDR_IN) + 16, sizeof(SOCKADDR_IN) + 16, NULL, &_ex_over._over);
}
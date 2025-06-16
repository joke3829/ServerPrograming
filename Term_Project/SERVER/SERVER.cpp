#include "SERVER.h"

concurrency::concurrent_unordered_map<long long, std::atomic<std::shared_ptr<SESSION>>> g_users;

std::mutex g_sl;
std::array<std::array<std::unordered_set<long long>, MAP_WIDTH / SECTOR_SIZE>, MAP_HEIGHT / SECTOR_SIZE> g_sector;

std::array<std::array<bool, MAP_WIDTH>, MAP_HEIGHT> g_obstacles;	// use [y][x]

thread_local SQLHENV henv;
thread_local SQLHDBC hdbc;
thread_local SQLHSTMT hstmt = 0;
thread_local SQLRETURN retcode;
thread_local SQLWCHAR szName[MAX_ID_LENGTH];
thread_local SQLINTEGER db_x, db_y, db_max_hp, db_hp, db_level, db_exp;
thread_local SQLLEN cbName = 0, cb_db_x = 0, cb_db_y = 0, cb_db_max_hp = 0, cb_db_hp = 0, cb_db_level = 0, cb_db_exp = 0;

extern std::mutex g_tl;
extern std::priority_queue<event_type> g_eventq;

void CServer::SetUp()
{
	ReadySocket();	

	// Read Map
	std::ifstream inFile{ "map.bin", std::ios::binary };
	for (int i = 0; i < MAP_WIDTH; ++i) {
		inFile.read(reinterpret_cast<char*>(g_obstacles[i].data()), 2000);
	}

	// NPC Ready
	ReadyNPC();

	ReadyIOCP();
}

void CServer::worker_thread()
{
	retcode = SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &henv);

	if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO) {
		retcode = SQLSetEnvAttr(henv, SQL_ATTR_ODBC_VERSION, (SQLPOINTER*)SQL_OV_ODBC3, 0);
		if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO) {
			retcode = SQLAllocHandle(SQL_HANDLE_DBC, henv, &hdbc);

			if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO) {
				SQLSetConnectAttr(hdbc, SQL_LOGIN_TIMEOUT, (SQLPOINTER)5, 0);
				retcode = SQLConnect(hdbc, (SQLWCHAR*)L"2020184003_TermProject", SQL_NTS, (SQLWCHAR*)NULL, 0, NULL, 0);
				if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO) {
					retcode = SQLAllocHandle(SQL_HANDLE_STMT, hdbc, &hstmt);
					// DataBase Connect SUCCESS

				}
			}
		}
	}


	while (true) {
		DWORD num_bytes;
		ULONG_PTR key;
		WSAOVERLAPPED* over{ nullptr };
		BOOL ret = GetQueuedCompletionStatus(_h_iocp, &num_bytes, &key, &over, INFINITE);
		EX_OVER* ex_over = reinterpret_cast<EX_OVER*>(over);
		if (ret == FALSE) {
			if (ex_over->_comp_type == OP_ACCEPT) std::cout << "Accept Error" << std::endl;
			else {
				std::cout << key << "client disconnet" << std::endl;
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
			std::cout << key << "client disconnet" << std::endl;
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
			if (!g_users.count(key))
				break;
			std::shared_ptr<SESSION> player = g_users.at(key);
			if (nullptr == player)
				break;
			int remained = num_bytes + player->getRemained();
			char* p = ex_over->_send_buf;
			while (remained > 0) {
				unsigned char p0 = static_cast<unsigned char>(p[0]);
				int packet_size = p0;
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
		case OP_PL_HEAL: {
			std::shared_ptr<SESSION> player = g_users.at(key);
			if (nullptr == player)
				break;
			player->do_heal_and_send();
			delete ex_over;
		}
					   break;
		case OP_NPC_AI: {
			std::shared_ptr<SESSION> npc = g_users.at(key);
			if (nullptr == npc)
				break;
			npc->_ll.lock();

			lua_getglobal(npc->_lua_machine, "npcAI");
			lua_pcall(npc->_lua_machine, 0, 0, 0);

			npc->_ll.unlock();
			delete ex_over;
		}
						break;
		}
	}

	SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
	SQLDisconnect(hdbc);
	SQLFreeHandle(SQL_HANDLE_DBC, hdbc);
	SQLFreeHandle(SQL_HANDLE_ENV, henv);
}

void CServer::Timer_thread()
{
	do {
		do {
			g_tl.lock();
			if (g_eventq.empty() == true) {
				g_tl.unlock();
				break;
			}
			auto& k = g_eventq.top();
			if (k.wakeup_time > std::chrono::high_resolution_clock::now()) {
				g_tl.unlock();
				break;
			}

			switch (k.event_id) {
			case PL_HEAL: {
				EX_OVER* o = new EX_OVER;
				o->_comp_type = OP_PL_HEAL;
				PostQueuedCompletionStatus(_h_iocp, 1, k.obj_id, &o->_over);
				g_eventq.emplace(event_type{ k.obj_id, std::chrono::high_resolution_clock::now() + std::chrono::seconds(5), PL_HEAL, 0 });
				g_tl.unlock();
			}
				break;
			case EV_NPC_AI: {
				std::shared_ptr<SESSION> npc = g_users.at(k.obj_id);
				if (nullptr == npc) {
					g_tl.unlock();
					break;
				}
				if (npc->_alive && npc->_revive_term < std::chrono::high_resolution_clock::now()) {
					EX_OVER* o = new EX_OVER;
					o->_comp_type = OP_NPC_AI;
					PostQueuedCompletionStatus(_h_iocp, 1, k.obj_id, &o->_over);
					g_eventq.emplace(event_type{ k.obj_id, std::chrono::high_resolution_clock::now() + std::chrono::milliseconds(500), EV_NPC_AI, k.target_id });
				}
				g_tl.unlock();
				break;
			}
				//break;
			}

			g_tl.lock();
			g_eventq.pop();
			g_tl.unlock();
		} while (true);
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	} while (true);
}

void CServer::Run()
{
	int num_thread = std::thread::hardware_concurrency();
	std::vector<std::thread> threads;

	for (int i = 0; i < num_thread; ++i)
		threads.emplace_back(&CServer::worker_thread, this);

	std::thread timer_thread{ &CServer::Timer_thread, this };
	timer_thread.join();

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

void CServer::ReadyNPC()
{
	long long id = MAX_USER;
	for (int cnt = 0; cnt < 80000; ++cnt) {	// NPC_PEACE_FIX
		std::shared_ptr<SESSION> p = std::make_shared<SESSION>();
		p->_id = id++;
		sprintf_s(p->_name, "PMan%d", p->_id - MAX_USER);

		short x, y, dir;
		switch (cnt / 10000) {
		case 0: {
			p->_level = 1;
			p->_max_hp = 100;
			p->_hp = 100;
			p->_attack = 5;
			p->_in_section = 0;
			x = rand() % 598;
			y = rand() % 598;
			dir = rand() % 2;
			while (g_obstacles[y][x]) {
				switch (dir) {
				case 0:
					++x;
					break;
				case 1:
					++y;
					break;
				}
			}
		}
			  break;
		case 1: {
			p->_level = 1;
			p->_max_hp = 100;
			p->_hp = 100;
			p->_attack = 5;
			p->_in_section = 1;
			x = rand() % 598 + 1402;
			y = rand() % 598 + 1402;
			dir = rand() % 2;
			while (g_obstacles[y][x]) {
				switch (dir) {
				case 0:
					--x;
					break;
				case 1:
					--y;
					break;
				}
			}
		}
			  break;
		case 2: {
			p->_level = 3;
			p->_max_hp = 160;
			p->_hp = 160;
			p->_attack = 5;
			p->_in_section = 2;
			x = rand() % 598 + 1402;
			y = rand() % 598;
			dir = rand() % 2;
			while (g_obstacles[y][x]) {
				switch (dir) {
				case 0:
					--x;
					break;
				case 1:
					++y;
					break;
				}
			}
		}
			  break;
		case 3: {
			p->_level = 3;
			p->_max_hp = 160;
			p->_hp = 160;
			p->_attack = 5;
			p->_in_section = 3;
			x = rand() % 598;
			y = rand() % 598 + 1402;
			dir = rand() % 2;
			while (g_obstacles[y][x]) {
				switch (dir) {
				case 0:
					++x;
					break;
				case 1:
					--y;
					break;
				}
			}
		}
			  break;
		case 4: {
			p->_level = 2;
			p->_max_hp = 130;
			p->_hp = 130;
			p->_attack = 5;
			p->_in_section = 4;
			x = rand() % 794 + 602;
			y = rand() % 598;
			dir = rand() % 2;
			while (g_obstacles[y][x]) {
				switch (dir) {
				case 0:
					++x;
					break;
				case 1:
					++y;
					break;
				}
			}
		}
			  break;
		case 5: {
			p->_level = 2;
			p->_max_hp = 130;
			p->_hp = 130;
			p->_attack = 5;
			p->_in_section = 5;
			x = rand() % 794 + 602;
			y = rand() % 598 + 1402;
			dir = rand() % 2;
			while (g_obstacles[y][x]) {
				switch (dir) {
				case 0:
					--x;
					break;
				case 1:
					--y;
					break;
				}
			}
		}
			  break;
		case 6: {
			p->_level = 4;
			p->_max_hp = 190;
			p->_hp = 190;
			p->_attack = 5;
			p->_in_section = 6;
			x = rand() % 598;
			y = rand() % 794 + 602;
			dir = rand() % 2;
			while (g_obstacles[y][x]) {
				switch (dir) {
				case 0:
					++x;
					break;
				case 1:
					++y;
					break;
				}
			}
		}
			  break;
		case 7: {
			p->_level = 4;
			p->_max_hp = 190;
			p->_hp = 190;
			p->_attack = 5;
			p->_in_section = 7;
			x = rand() % 598 + 1402;
			y = rand() % 794 + 602;
			dir = rand() % 2;
			while (g_obstacles[y][x]) {
				switch (dir) {
				case 0:
					--x;
					break;
				case 1:
					--y;
					break;
				}
			}
		}
			  break;
		}
		

		p->_x = x; p->_y = y;
		p->_ix = x; p->_iy = y;

		p->_sector_coord[0] = p->_y / SECTOR_SIZE;
		p->_sector_coord[1] = p->_x / SECTOR_SIZE;

		g_sector[p->_sector_coord[0]][p->_sector_coord[1]].insert(p->_id);

		p->_state = ST_INGAME;
		p->_npc_type = NPC_PEACE_FIX;

		// Peace Fix만의 lua 추가(필요하면)
		p->_lua_machine = luaL_newstate();
		luaL_openlibs(p->_lua_machine);
		luaL_loadfile(p->_lua_machine, "npc_ai.lua");
		lua_pcall(p->_lua_machine, 0, 0, 0);

		lua_getglobal(p->_lua_machine, "set_myid");
		lua_pushnumber(p->_lua_machine, p->_id);
		lua_pcall(p->_lua_machine, 1, 0, 0);

		lua_getglobal(p->_lua_machine, "set_state");
		lua_pushnumber(p->_lua_machine, 0);
		lua_pcall(p->_lua_machine, 1, 0, 0);

		lua_register(p->_lua_machine, "API_CheckUser", API_CheckUser);
		lua_register(p->_lua_machine, "API_Roming", API_Roming);
		lua_register(p->_lua_machine, "API_Chase_target", API_Chase_target);

		g_users.insert(std::make_pair(p->_id, p));
	}
	for (int cnt = 0; cnt < 40000; ++cnt) {	// NPC_PEACE_ROMING
		std::shared_ptr<SESSION> p = std::make_shared<SESSION>();
		p->_id = id++;
		sprintf_s(p->_name, "PMan%d", p->_id - MAX_USER);

		short x, y, dir;
		switch (cnt / 10000) {
		case 0: {
			p->_level = 1;
			p->_max_hp = 100;
			p->_hp = 100;
			p->_attack = 5;
			p->_in_section = 0;
			x = rand() % 598;
			y = rand() % 598;
			dir = rand() % 2;
			while (g_obstacles[y][x]) {
				switch (dir) {
				case 0:
					++x;
					break;
				case 1:
					++y;
					break;
				}
			}
		}
			  break;
		case 1: {
			p->_level = 1;
			p->_max_hp = 100;
			p->_hp = 100;
			p->_attack = 5;
			p->_in_section = 1;
			x = rand() % 598 + 1402;
			y = rand() % 598 + 1402;
			dir = rand() % 2;
			while (g_obstacles[y][x]) {
				switch (dir) {
				case 0:
					--x;
					break;
				case 1:
					--y;
					break;
				}
			}
		}
			  break;
		case 2: {
			p->_level = 3;
			p->_max_hp = 160;
			p->_hp = 160;
			p->_attack = 5;
			p->_in_section = 2;
			x = rand() % 598 + 1402;
			y = rand() % 598;
			dir = rand() % 2;
			while (g_obstacles[y][x]) {
				switch (dir) {
				case 0:
					--x;
					break;
				case 1:
					++y;
					break;
				}
			}
		}
			  break;
		case 3: {
			p->_level = 3;
			p->_max_hp = 160;
			p->_hp = 160;
			p->_attack = 5;
			p->_in_section = 3;
			x = rand() % 598;
			y = rand() % 598 + 1402;
			dir = rand() % 2;
			while (g_obstacles[y][x]) {
				switch (dir) {
				case 0:
					++x;
					break;
				case 1:
					--y;
					break;
				}
			}
		}
			  break;
		case 4: {
			p->_level = 2;
			p->_max_hp = 130;
			p->_hp = 130;
			p->_attack = 5;
			p->_in_section = 4;
			x = rand() % 794 + 602;
			y = rand() % 598;
			dir = rand() % 2;
			while (g_obstacles[y][x]) {
				switch (dir) {
				case 0:
					++x;
					break;
				case 1:
					++y;
					break;
				}
			}
		}
			  break;
		case 5: {
			p->_level = 2;
			p->_max_hp = 130;
			p->_hp = 130;
			p->_attack = 5;
			p->_in_section = 5;
			x = rand() % 794 + 602;
			y = rand() % 598 + 1402;
			dir = rand() % 2;
			while (g_obstacles[y][x]) {
				switch (dir) {
				case 0:
					--x;
					break;
				case 1:
					--y;
					break;
				}
			}
		}
			  break;
		case 6: {
			p->_level = 4;
			p->_max_hp = 190;
			p->_hp = 190;
			p->_attack = 5;
			p->_in_section = 6;
			x = rand() % 598;
			y = rand() % 794 + 602;
			dir = rand() % 2;
			while (g_obstacles[y][x]) {
				switch (dir) {
				case 0:
					++x;
					break;
				case 1:
					++y;
					break;
				}
			}
		}
			  break;
		case 7: {
			p->_level = 4;
			p->_max_hp = 190;
			p->_hp = 190;
			p->_attack = 5;
			p->_in_section = 7;
			x = rand() % 598 + 1402;
			y = rand() % 794 + 602;
			dir = rand() % 2;
			while (g_obstacles[y][x]) {
				switch (dir) {
				case 0:
					--x;
					break;
				case 1:
					--y;
					break;
				}
			}
		}
			  break;
		}


		p->_x = x; p->_y = y;
		p->_ix = x; p->_iy = y;

		p->_sector_coord[0] = p->_y / SECTOR_SIZE;
		p->_sector_coord[1] = p->_x / SECTOR_SIZE;

		g_sector[p->_sector_coord[0]][p->_sector_coord[1]].insert(p->_id);

		p->_state = ST_INGAME;

		p->_max_hp = p->_hp = 100;
		p->_npc_type = NPC_PEACE_ROMING;
		// Peace Roming만의 lua 추가(필요하면)
		p->_lua_machine = luaL_newstate();
		luaL_openlibs(p->_lua_machine);
		luaL_loadfile(p->_lua_machine, "npc_ai.lua");
		lua_pcall(p->_lua_machine, 0, 0, 0);

		lua_getglobal(p->_lua_machine, "set_myid");
		lua_pushnumber(p->_lua_machine, p->_id);
		lua_pcall(p->_lua_machine, 1, 0, 0);

		lua_getglobal(p->_lua_machine, "set_state");
		lua_pushnumber(p->_lua_machine, 2);
		lua_pcall(p->_lua_machine, 1, 0, 0);

		lua_register(p->_lua_machine, "API_CheckUser", API_CheckUser);
		lua_register(p->_lua_machine, "API_Roming", API_Roming);
		lua_register(p->_lua_machine, "API_Chase_target", API_Chase_target);

		g_users.insert(std::make_pair(p->_id, p));
	}
	for (int cnt = 0; cnt < 60000; ++cnt) {	// NPC_AGRO_FIX
		std::shared_ptr<SESSION> p = std::make_shared<SESSION>();
		p->_id = id++;
		sprintf_s(p->_name, "AMan%d", p->_id - MAX_USER);

		short x, y, dir;
		switch (cnt / 10000) {
		case 0: {
			p->_level = 1;
			p->_max_hp = 100;
			p->_hp = 100;
			p->_attack = 5;
			p->_in_section = 0;
			x = rand() % 598;
			y = rand() % 598;
			dir = rand() % 2;
			while (g_obstacles[y][x]) {
				switch (dir) {
				case 0:
					++x;
					break;
				case 1:
					++y;
					break;
				}
			}
		}
			  break;
		case 1: {
			p->_level = 1;
			p->_max_hp = 100;
			p->_hp = 100;
			p->_attack = 5;
			p->_in_section = 1;
			x = rand() % 598 + 1402;
			y = rand() % 598 + 1402;
			dir = rand() % 2;
			while (g_obstacles[y][x]) {
				switch (dir) {
				case 0:
					--x;
					break;
				case 1:
					--y;
					break;
				}
			}
		}
			  break;
		case 2: {
			p->_level = 3;
			p->_max_hp = 160;
			p->_hp = 160;
			p->_attack = 5;
			p->_in_section = 2;
			x = rand() % 598 + 1402;
			y = rand() % 598;
			dir = rand() % 2;
			while (g_obstacles[y][x]) {
				switch (dir) {
				case 0:
					--x;
					break;
				case 1:
					++y;
					break;
				}
			}
		}
			  break;
		case 3: {
			p->_level = 3;
			p->_max_hp = 160;
			p->_hp = 160;
			p->_attack = 5;
			p->_in_section = 3;
			x = rand() % 598;
			y = rand() % 598 + 1402;
			dir = rand() % 2;
			while (g_obstacles[y][x]) {
				switch (dir) {
				case 0:
					++x;
					break;
				case 1:
					--y;
					break;
				}
			}
		}
			  break;
		case 4: {
			p->_level = 2;
			p->_max_hp = 130;
			p->_hp = 130;
			p->_attack = 5;
			p->_in_section = 4;
			x = rand() % 794 + 602;
			y = rand() % 598;
			dir = rand() % 2;
			while (g_obstacles[y][x]) {
				switch (dir) {
				case 0:
					++x;
					break;
				case 1:
					++y;
					break;
				}
			}
		}
			  break;
		case 5: {
			p->_level = 2;
			p->_max_hp = 130;
			p->_hp = 130;
			p->_attack = 5;
			p->_in_section = 5;
			x = rand() % 794 + 602;
			y = rand() % 598 + 1402;
			dir = rand() % 2;
			while (g_obstacles[y][x]) {
				switch (dir) {
				case 0:
					--x;
					break;
				case 1:
					--y;
					break;
				}
			}
		}
			  break;
		case 6: {
			p->_level = 4;
			p->_max_hp = 190;
			p->_hp = 190;
			p->_attack = 5;
			p->_in_section = 6;
			x = rand() % 598;
			y = rand() % 794 + 602;
			dir = rand() % 2;
			while (g_obstacles[y][x]) {
				switch (dir) {
				case 0:
					++x;
					break;
				case 1:
					++y;
					break;
				}
			}
		}
			  break;
		case 7: {
			p->_level = 4;
			p->_max_hp = 190;
			p->_hp = 190;
			p->_attack = 5;
			p->_in_section = 7;
			x = rand() % 598 + 1402;
			y = rand() % 794 + 602;
			dir = rand() % 2;
			while (g_obstacles[y][x]) {
				switch (dir) {
				case 0:
					--x;
					break;
				case 1:
					--y;
					break;
				}
			}
		}
			  break;
		}


		p->_x = x; p->_y = y;
		p->_ix = x; p->_iy = y;

		p->_sector_coord[0] = p->_y / SECTOR_SIZE;
		p->_sector_coord[1] = p->_x / SECTOR_SIZE;

		g_sector[p->_sector_coord[0]][p->_sector_coord[1]].insert(p->_id);

		p->_state = ST_INGAME;

		p->_max_hp = p->_hp = 100;
		p->_npc_type = NPC_AGRO_FIX;
		// Agro Fix만의 lua 추가(필요하면)

		p->_lua_machine = luaL_newstate();
		luaL_openlibs(p->_lua_machine);
		luaL_loadfile(p->_lua_machine, "npc_ai.lua");
		lua_pcall(p->_lua_machine, 0, 0, 0);

		lua_getglobal(p->_lua_machine, "set_myid");
		lua_pushnumber(p->_lua_machine, p->_id);
		lua_pcall(p->_lua_machine, 1, 0, 0);

		lua_getglobal(p->_lua_machine, "set_state");
		lua_pushnumber(p->_lua_machine, 0);
		lua_pcall(p->_lua_machine, 1, 0, 0);

		lua_register(p->_lua_machine, "API_CheckUser", API_CheckUser);
		lua_register(p->_lua_machine, "API_Roming", API_Roming);
		lua_register(p->_lua_machine, "API_Chase_target", API_Chase_target);

		g_users.insert(std::make_pair(p->_id, p));
	}
	for (int cnt = 0; cnt < 20000; ++cnt) {	// NPC_AGRO_ROMING
		std::shared_ptr<SESSION> p = std::make_shared<SESSION>();
		p->_id = id++;
		sprintf_s(p->_name, "AMan%d", p->_id - MAX_USER);

		short x, y, dir;
		switch (cnt / 10000) {
		case 0: {
			p->_level = 1;
			p->_max_hp = 100;
			p->_hp = 100;
			p->_attack = 5;
			p->_in_section = 0;
			x = rand() % 598;
			y = rand() % 598;
			dir = rand() % 2;
			while (g_obstacles[y][x]) {
				switch (dir) {
				case 0:
					++x;
					break;
				case 1:
					++y;
					break;
				}
			}
		}
			  break;
		case 1: {
			p->_level = 1;
			p->_max_hp = 100;
			p->_hp = 100;
			p->_attack = 5;
			p->_in_section = 1;
			x = rand() % 598 + 1402;
			y = rand() % 598 + 1402;
			dir = rand() % 2;
			while (g_obstacles[y][x]) {
				switch (dir) {
				case 0:
					--x;
					break;
				case 1:
					--y;
					break;
				}
			}
		}
			  break;
		case 2: {
			p->_level = 3;
			p->_max_hp = 160;
			p->_hp = 160;
			p->_attack = 5;
			p->_in_section = 2;
			x = rand() % 598 + 1402;
			y = rand() % 598;
			dir = rand() % 2;
			while (g_obstacles[y][x]) {
				switch (dir) {
				case 0:
					--x;
					break;
				case 1:
					++y;
					break;
				}
			}
		}
			  break;
		case 3: {
			p->_level = 3;
			p->_max_hp = 160;
			p->_hp = 160;
			p->_attack = 5;
			p->_in_section = 3;
			x = rand() % 598;
			y = rand() % 598 + 1402;
			dir = rand() % 2;
			while (g_obstacles[y][x]) {
				switch (dir) {
				case 0:
					++x;
					break;
				case 1:
					--y;
					break;
				}
			}
		}
			  break;
		case 4: {
			p->_level = 2;
			p->_max_hp = 130;
			p->_hp = 130;
			p->_attack = 5;
			p->_in_section = 4;
			x = rand() % 794 + 602;
			y = rand() % 598;
			dir = rand() % 2;
			while (g_obstacles[y][x]) {
				switch (dir) {
				case 0:
					++x;
					break;
				case 1:
					++y;
					break;
				}
			}
		}
			  break;
		case 5: {
			p->_level = 2;
			p->_max_hp = 130;
			p->_hp = 130;
			p->_attack = 5;
			p->_in_section = 5;
			x = rand() % 794 + 602;
			y = rand() % 598 + 1402;
			dir = rand() % 2;
			while (g_obstacles[y][x]) {
				switch (dir) {
				case 0:
					--x;
					break;
				case 1:
					--y;
					break;
				}
			}
		}
			  break;
		case 6: {
			p->_level = 4;
			p->_max_hp = 190;
			p->_hp = 190;
			p->_attack = 5;
			p->_in_section = 6;
			x = rand() % 598;
			y = rand() % 794 + 602;
			dir = rand() % 2;
			while (g_obstacles[y][x]) {
				switch (dir) {
				case 0:
					++x;
					break;
				case 1:
					++y;
					break;
				}
			}
		}
			  break;
		case 7: {
			p->_level = 4;
			p->_max_hp = 190;
			p->_hp = 190;
			p->_attack = 5;
			p->_in_section = 7;
			x = rand() % 598 + 1402;
			y = rand() % 794 + 602;
			dir = rand() % 2;
			while (g_obstacles[y][x]) {
				switch (dir) {
				case 0:
					--x;
					break;
				case 1:
					--y;
					break;
				}
			}
		}
			  break;
		}


		p->_x = x; p->_y = y;
		p->_ix = x; p->_iy = y;

		p->_sector_coord[0] = p->_y / SECTOR_SIZE;
		p->_sector_coord[1] = p->_x / SECTOR_SIZE;

		g_sector[p->_sector_coord[0]][p->_sector_coord[1]].insert(p->_id);

		p->_state = ST_INGAME;

		p->_max_hp = p->_hp = 100;
		p->_npc_type = NPC_AGRO_ROMING;
		// Agro Roming만의 lua 추가(필요하면)

		p->_lua_machine = luaL_newstate();
		luaL_openlibs(p->_lua_machine);
		luaL_loadfile(p->_lua_machine, "npc_ai.lua");
		lua_pcall(p->_lua_machine, 0, 0, 0);

		lua_getglobal(p->_lua_machine, "set_myid");
		lua_pushnumber(p->_lua_machine, p->_id);
		lua_pcall(p->_lua_machine, 1, 0, 0);

		lua_getglobal(p->_lua_machine, "set_state");
		lua_pushnumber(p->_lua_machine, 2);
		lua_pcall(p->_lua_machine, 1, 0, 0);

		lua_register(p->_lua_machine, "API_CheckUser", API_CheckUser);
		lua_register(p->_lua_machine, "API_Roming", API_Roming);
		lua_register(p->_lua_machine, "API_Chase_target", API_Chase_target);

		g_users.insert(std::make_pair(p->_id, p));
	}

/*for (long long id = MAX_USER; id < MAX_USER + NUM_MONSTER; ++id) {
	std::shared_ptr<SESSION> p = std::make_shared<SESSION>();
	p->_id = id;
	sprintf_s(p->_name, "PMan%d", p->_id);

	short x, y;
	x = rand() % 2000;
	y = rand() % 2000;

	p->_x = x; p->_y = y;

	p->_sector_coord[0] = p->_y / SECTOR_SIZE;
	p->_sector_coord[1] = p->_x / SECTOR_SIZE;

	g_sector[p->_sector_coord[0]][p->_sector_coord[1]].insert(id);

	p->_state = ST_INGAME;

	p->_max_hp = p->_hp = 100;

	// Peace Fix만의 lua 추가(필요하면)

	g_users.insert(std::make_pair(id, p));
}*/
	std::cout << "NPC_Ready" << std::endl;
}


int API_Roming(lua_State* L)
{
	long long uid = (long long)lua_tointeger(L, -1);
	lua_pop(L, 2);
	std::shared_ptr<SESSION> p = g_users.at(uid);
	if (nullptr == p) return 1;
	int nextstate = p->do_npc_move();

	lua_pushnumber(L, nextstate);
	return 1;
}

int API_Chase_target(lua_State* L)
{
	long long target_id = (long long)lua_tointeger(L, -1);
	long long uid = (long long)lua_tointeger(L, -2);
	lua_pop(L, 3);

	std::shared_ptr<SESSION> p = g_users.at(uid);
	if (nullptr == p) return 1;
	int nextstate = p->do_npc_chase(target_id);

	lua_pushnumber(L, 3);
	return 1;
}

int API_CheckUser(lua_State* L)
{
	long long uid = (long long)lua_tointeger(L, -1);
	lua_pop(L, 2);
	std::shared_ptr<SESSION> p = g_users.at(uid);
	if (nullptr == p) return 1;
	int ret = p->do_check_near_user();

	if (ret == -1) 
		lua_pushnumber(L, 0);
	else {
		lua_getglobal(L, "set_target");
		lua_pushnumber(L, ret);
		lua_pcall(L, 1, 0, 0);
		lua_pushnumber(L, 3);
	}

	return 1;
}
#include <iostream>
#include <array>
#include <WS2tcpip.h>
#include <MSWSock.h>
#include <thread>
#include <vector>
#include <mutex>
#include <unordered_set>
#include <chrono>
#include <queue>
#include <concurrent_unordered_map.h>
#include "include/lua.hpp"
#include "protocol.h"

#pragma comment(lib, "WS2_32.lib")
#pragma comment(lib, "MSWSock.lib")
#pragma comment(lib, "lua54.lib")
using namespace std;

constexpr int VIEW_RANGE = 5;

std::atomic<long long> g_uid = 0;
std::atomic<long long> g_npc_id = MAX_USER;

// sector의 크기는 20 x 20
// 맵의 크기가 2000 x 2000 이기에 총 1000개의 sector를 가진다.
constexpr int SECTOR_SIZE = 20;


enum COMP_TYPE { OP_ACCEPT, OP_RECV, OP_SEND, OP_NPC_MOVE, OP_AI_CHAT };
class OVER_EXP {
public:
	WSAOVERLAPPED _over;
	WSABUF _wsabuf;
	char _send_buf[BUF_SIZE];
	COMP_TYPE _comp_type;
	int _ai_target_obj;
	OVER_EXP()
	{
		_wsabuf.len = BUF_SIZE;
		_wsabuf.buf = _send_buf;
		_comp_type = OP_RECV;
		ZeroMemory(&_over, sizeof(_over));
	}
	OVER_EXP(char* packet)
	{
		_wsabuf.len = packet[0];
		_wsabuf.buf = _send_buf;
		ZeroMemory(&_over, sizeof(_over));
		_comp_type = OP_SEND;
		memcpy(_send_buf, packet, packet[0]);
	}
};

enum S_STATE { ST_FREE, ST_ALLOC, ST_INGAME };
enum EVENT_TYPE { EV_MOVE, EV_HEAL, EV_ATTACK };
class SESSION {
	OVER_EXP _recv_over;

public:
	mutex _s_lock;
	S_STATE _state;
	long long _id;
	SOCKET _socket;
	short	x, y;
	char	_name[NAME_SIZE];
	int		_prev_remain;
	unordered_set <long long> _view_list;
	mutex	_vl;
	long long		last_move_time;
	short			move_cnt{};

	atomic<bool>	_is_active;

	short _sector_coord[2] = { 0, 0 };

	lua_State* _L;
	std::mutex _lualock;
public:
	SESSION()
	{
		_id = -1;
		_socket = 0;
		x = y = 0;
		_name[0] = 0;
		_state = ST_FREE;
		_prev_remain = 0;
	}

	~SESSION() {}

	void do_recv()
	{
		DWORD recv_flag = 0;
		memset(&_recv_over._over, 0, sizeof(_recv_over._over));
		_recv_over._wsabuf.len = BUF_SIZE - _prev_remain;
		_recv_over._wsabuf.buf = _recv_over._send_buf + _prev_remain;
		WSARecv(_socket, &_recv_over._wsabuf, 1, 0, &recv_flag,
			&_recv_over._over, 0);
	}

	void do_send(void* packet)
	{
		OVER_EXP* sdata = new OVER_EXP{ reinterpret_cast<char*>(packet) };
		WSASend(_socket, &sdata->_wsabuf, 1, 0, 0, &sdata->_over, 0);
	}
	void send_login_info_packet()
	{
		SC_LOGIN_INFO_PACKET p;
		p.id = _id;
		p.size = sizeof(SC_LOGIN_INFO_PACKET);
		p.type = SC_LOGIN_INFO;
		p.x = x;
		p.y = y;
		do_send(&p);
	}
	void send_move_packet_mine();
	void send_move_packet(long long c_id);
	void send_move_packet(std::shared_ptr<SESSION> pl);

	void send_add_player_packet_mine();
	void send_add_player_packet(int c_id);
	void send_add_player_packet(std::shared_ptr<SESSION> pl);

	void send_chat_packet(int c_id, const char* mess);
	void send_remove_player_packet(int c_id)
	{
		_vl.lock();
		if (_view_list.count(c_id))
			_view_list.erase(c_id);
		else {
			_vl.unlock();
			return;
		}
		_vl.unlock();
		SC_REMOVE_OBJECT_PACKET p;
		p.id = c_id;
		p.size = sizeof(p);
		p.type = SC_REMOVE_OBJECT;
		do_send(&p);
	}
	void wakeup(long long waker);
};

struct event_type {
	long long obj_id;
	chrono::high_resolution_clock::time_point wakeup_time;
	EVENT_TYPE event_id;
	int target_id;

	constexpr bool operator < (const event_type& _Left) const
	{
		return (wakeup_time > _Left.wakeup_time);
	}
};

priority_queue<event_type> timer_queue;
mutex timer_lock;

HANDLE h_iocp;
concurrency::concurrent_unordered_map <long long, std::atomic<std::shared_ptr<SESSION>>> clients;

std::mutex g_sl;
std::array<std::array<std::unordered_set<long long>, W_WIDTH / SECTOR_SIZE>, W_HEIGHT / SECTOR_SIZE> g_Sector;

SOCKET g_s_socket, g_c_socket;
OVER_EXP g_a_over;

bool is_pc(long long object_id)
{
	return object_id < MAX_USER;
}

bool is_npc(long long object_id)
{
	return !is_pc(object_id);
}

bool can_see(long long from, long long to)
{
	std::shared_ptr<SESSION> p1 = clients.at(from);
	std::shared_ptr<SESSION> p2 = clients.at(to);
	if (p1 == nullptr || p2 == nullptr)
		return false;
	if (abs(p1->x - p2->x) > VIEW_RANGE) return false;
	return abs(p1->y - p2->y) <= VIEW_RANGE;
}

bool can_see(std::shared_ptr<SESSION> from, std::shared_ptr<SESSION> to)
{
	if (from == nullptr || to == nullptr)
		return false;
	if (abs(from->x - to->x) > VIEW_RANGE) return false;
	return abs(from->y - to->y) <= VIEW_RANGE;
}

void SESSION::send_move_packet_mine()
{
	SC_MOVE_OBJECT_PACKET p;
	p.id = _id;
	p.size = sizeof(SC_MOVE_OBJECT_PACKET);
	p.type = SC_MOVE_OBJECT;
	p.x = x;
	p.y = y;
	p.move_time = last_move_time;
	do_send(&p);
}

void SESSION::send_move_packet(long long c_id)
{
	std::shared_ptr<SESSION> pl = clients.at(c_id);
	if (nullptr == pl) return;
	SC_MOVE_OBJECT_PACKET p;
	p.id = c_id;
	p.size = sizeof(SC_MOVE_OBJECT_PACKET);
	p.type = SC_MOVE_OBJECT;
	p.x = pl->x;
	p.y = pl->y;
	p.move_time = pl->last_move_time;
	do_send(&p);
}

void SESSION::send_move_packet(std::shared_ptr<SESSION> pl)
{
	if (nullptr == pl) return;
	SC_MOVE_OBJECT_PACKET p;
	p.id = pl->_id;
	p.size = sizeof(SC_MOVE_OBJECT_PACKET);
	p.type = SC_MOVE_OBJECT;
	p.x = pl->x;
	p.y = pl->y;
	p.move_time = pl->last_move_time;
	do_send(&p);
}

void SESSION::send_add_player_packet_mine()
{
	SC_ADD_OBJECT_PACKET add_packet;
	add_packet.id = _id;
	strcpy_s(add_packet.name, _name);
	add_packet.size = sizeof(add_packet);
	add_packet.type = SC_ADD_OBJECT;
	add_packet.x = x;
	add_packet.y = y;
	_vl.lock();
	_view_list.insert(_id);
	_vl.unlock();
	do_send(&add_packet);
}

void SESSION::send_add_player_packet(int c_id)
{
	std::shared_ptr<SESSION> pl = clients.at(c_id);
	if (nullptr == pl) return;
	SC_ADD_OBJECT_PACKET add_packet;
	add_packet.id = c_id;
	strcpy_s(add_packet.name, pl->_name);
	add_packet.size = sizeof(add_packet);
	add_packet.type = SC_ADD_OBJECT;
	add_packet.x = pl->x;
	add_packet.y = pl->y;
	_vl.lock();
	_view_list.insert(c_id);
	_vl.unlock();
	do_send(&add_packet);
}

void SESSION::send_add_player_packet(std::shared_ptr<SESSION> pl)
{
	if (pl == nullptr) return;
	SC_ADD_OBJECT_PACKET add_packet;
	add_packet.id = pl->_id;
	strcpy_s(add_packet.name, pl->_name);
	add_packet.size = sizeof(add_packet);
	add_packet.type = SC_ADD_OBJECT;
	add_packet.x = pl->x;
	add_packet.y = pl->y;
	_vl.lock();
	_view_list.insert(pl->_id);
	_vl.unlock();
	do_send(&add_packet);
}

void SESSION::send_chat_packet(int p_id, const char* mess)
{
	SC_CHAT_PACKET packet;
	packet.id = p_id;
	packet.size = sizeof(packet);
	packet.type = SC_CHAT;
	strcpy_s(packet.mess, mess);
	do_send(&packet);
}

void SESSION::wakeup(long long waker)
{
	OVER_EXP* exover = new OVER_EXP;
	exover->_comp_type = OP_AI_CHAT;
	exover->_ai_target_obj = waker;
	PostQueuedCompletionStatus(h_iocp, 1, _id, &exover->_over);

	using namespace chrono;

	if (false == _is_active) {
		bool old_b = false;
		bool new_b = true;
		if (std::atomic_compare_exchange_strong(&_is_active, &old_b, new_b)) {
			timer_lock.lock();
			timer_queue.emplace(event_type{ _id, high_resolution_clock::now() + 1s,
				EV_MOVE, 0 });
			timer_lock.unlock();
		}
	}
}

void process_packet(std::shared_ptr<SESSION> user, long long c_id, char* packet)
{
	switch (packet[1]) {
	case CS_LOGIN: {
		CS_LOGIN_PACKET* p = reinterpret_cast<CS_LOGIN_PACKET*>(packet);
		strcpy_s(user->_name, p->name);
		short x, y, sx, sy;
		user->x = x = rand() % W_WIDTH;
		user->y = y = rand() % W_HEIGHT;
		user->_sector_coord[0] = sx = y / SECTOR_SIZE;
		user->_sector_coord[1] = sy = x / SECTOR_SIZE;
		{
			lock_guard<mutex> ll{ user->_s_lock };
			user->_state = ST_INGAME;
		}
		std::unordered_set<long long> su_list;
		g_sl.lock();
		g_Sector[sx][sy].insert(user->_id);
		for (short i = sx - 1; i <= sx + 1; ++i) {
			if (i < 0 || i >= W_HEIGHT / SECTOR_SIZE) continue;
			for (short j = sy - 1; j <= sy + 1; ++j) {
				if (j < 0 || j >= W_WIDTH / SECTOR_SIZE) continue;
				for (long long id : g_Sector[i][j])
					su_list.insert(id);
			}
		}
		g_sl.unlock();

		user->send_login_info_packet();
		for (long long id : su_list) {
			std::shared_ptr<SESSION> ply = clients.at(id);
			if (nullptr == ply) continue;
			{
				lock_guard<mutex> ll(ply->_s_lock);
				if (ST_INGAME != ply->_state) continue;
			}
			if (false == can_see(user, ply))
				continue;
			if (is_pc(ply->_id)) ply->send_add_player_packet_mine();
			else ply->wakeup(c_id);
			user->send_add_player_packet(ply);
		}
		break;
	}
	case CS_MOVE: {
		CS_MOVE_PACKET* p = reinterpret_cast<CS_MOVE_PACKET*>(packet);
		user->last_move_time = p->move_time;
		short x = user->x;
		short y = user->y;
		switch (p->direction) {
		case 0: if (y > 0) y--; break;
		case 1: if (y < W_HEIGHT - 1) y++; break;
		case 2: if (x > 0) x--; break;
		case 3: if (x < W_WIDTH - 1) x++; break;
		}
		user->x = x;
		user->y = y;
		short sx = y / SECTOR_SIZE;	// row
		short sy = x / SECTOR_SIZE;	// col
		if (user->_sector_coord[0] != sx || user->_sector_coord[1] != sy) {
			g_sl.lock();
			switch (p->direction) {
			case 0:
				g_Sector[sx + 1][sy].erase(user->_id);
				g_Sector[sx][sy].insert(user->_id);
				break;
			case 1:
				g_Sector[sx - 1][sy].erase(user->_id);
				g_Sector[sx][sy].insert(user->_id);
				break;
			case 2:
				g_Sector[sx][sy + 1].erase(user->_id);
				g_Sector[sx][sy].insert(user->_id);
				break;
			case 3:
				g_Sector[sx][sy - 1].erase(user->_id);
				g_Sector[sx][sy].insert(user->_id);
				break;
			}
			g_sl.unlock();
			user->_sector_coord[0] = sx;
			user->_sector_coord[1] = sy;
		}



		unordered_set<long long> near_list;
		user->_vl.lock();
		unordered_set<long long> old_vlist = user->_view_list;
		user->_vl.unlock();

		std::unordered_set<long long> su_list;
		g_sl.lock();
		g_Sector[sx][sy].insert(user->_id);
		for (short i = sx - 1; i <= sx + 1; ++i) {
			if (i < 0 || i >= W_HEIGHT / SECTOR_SIZE) continue;
			for (short j = sy - 1; j <= sy + 1; ++j) {
				if (j < 0 || j >= W_WIDTH / SECTOR_SIZE) continue;
				for (long long id : g_Sector[i][j])
					su_list.insert(id);
			}
		}
		g_sl.unlock();

		for (long long id : su_list) {
			std::shared_ptr<SESSION> ply = clients.at(id);
			if (nullptr == ply)
				continue;
			if (ply->_state != ST_INGAME) continue;
			if (can_see(user, ply))
				near_list.insert(ply->_id);
		}


		user->send_move_packet_mine();

		for (auto& pl : near_list) {
			std::shared_ptr<SESSION> cpl = clients.at(pl);
			if (nullptr == cpl)
				continue;
			//auto& cpl = clients[pl];
			if (is_pc(pl)) {
				cpl->_vl.lock();
				if (cpl->_view_list.count(c_id)) {
					cpl->_vl.unlock();
					cpl->send_move_packet(user);
				}
				else {
					cpl->_vl.unlock();
					cpl->send_add_player_packet(user);
				}
			}
			else {
				cpl->wakeup(c_id);
			}

			if (old_vlist.count(pl) == 0)
				user->send_add_player_packet(cpl);
		}

		for (auto& pl : old_vlist)
			if (0 == near_list.count(pl)) {
				user->send_remove_player_packet(pl);
				if (is_pc(pl)) {
					std::shared_ptr<SESSION> pp = clients.at(pl);
					if (nullptr != pp)
						pp->send_remove_player_packet(c_id);
				}
			}
	}
				break;
	}
}

void disconnect(long long c_id)
{
	std::shared_ptr<SESSION> p = clients.at(c_id);
	if (p == nullptr)
		return;

	p->_vl.lock();
	unordered_set <long long> vl = p->_view_list;
	p->_vl.unlock();
	for (auto& p_id : vl) {
		if (is_npc(p_id)) continue;
		std::shared_ptr<SESSION> pl = clients.at(p_id);
		if (pl == nullptr)
			continue;
		{
			lock_guard<mutex> ll(pl->_s_lock);
			if (ST_INGAME != pl->_state) continue;
		}
		if (pl->_id == c_id) continue;
		pl->send_remove_player_packet(c_id);
	}

	g_sl.lock();
	g_Sector[p->_sector_coord[0]][p->_sector_coord[1]].erase(c_id);
	g_sl.unlock();

	closesocket(p->_socket);
	clients.at(c_id) = nullptr;
}

int API_get_x(lua_State* L)
{
	int user_id =
		(int)lua_tointeger(L, -1);
	lua_pop(L, 2);
	std::shared_ptr<SESSION> p = clients.at(user_id);
	if (p == nullptr) return 1;
	int x = p->x;
	lua_pushnumber(L, x);
	return 1;
}

int API_get_y(lua_State* L)
{
	int user_id =
		(int)lua_tointeger(L, -1);
	lua_pop(L, 2);
	std::shared_ptr<SESSION> p = clients.at(user_id);
	if (p == nullptr) return 1;
	int y = p->y;
	lua_pushnumber(L, y);
	return 1;
}

int API_SendMessage(lua_State* L)
{
	int my_id = (int)lua_tointeger(L, -3);
	int user_id = (int)lua_tointeger(L, -2);
	char* mess = (char*)lua_tostring(L, -1);

	lua_pop(L, 3);
	std::shared_ptr<SESSION> p = clients.at(user_id);
	if (p == nullptr) return 0;
	p->send_chat_packet(my_id, mess);
	return 0;
}

int API_move_dir(lua_State* L)
{
	int my_id = (int)lua_tointeger(L, -3);
	int direction = (int)lua_tointeger(L, -2);
	bool run = (bool)lua_toboolean(L, -1);

	lua_pop(L, 3);

	std::shared_ptr<SESSION> npc = clients.at(my_id);

	std::unordered_set<long long> su_list;
	short sx = npc->_sector_coord[0], sy = npc->_sector_coord[1];
	g_sl.lock();
	for (short i = sx - 1; i <= sx + 1; ++i) {
		if (i < 0 || i >= W_HEIGHT / SECTOR_SIZE) continue;
		for (short j = sy - 1; j <= sy + 1; ++j) {
			if (j < 0 || j >= W_WIDTH / SECTOR_SIZE) continue;
			for (long long id : g_Sector[i][j])
				su_list.insert(id);
		}
	}
	g_sl.unlock();

	unordered_set<long long> old_vl;
	for (long long uid : su_list) {
		std::shared_ptr<SESSION> p_ob = clients.at(uid);
		if (p_ob == nullptr) continue;
		if (ST_INGAME != p_ob->_state) continue;
		if (true == is_npc(p_ob->_id)) continue;
		if (true == can_see(npc, p_ob))
			old_vl.insert(p_ob->_id);
	}

	int x = npc->x;
	int y = npc->y;

	if (false == run)
		direction = rand() % 4;
	switch (direction) {
	case 0: if (x < (W_WIDTH - 1)) x++; break;	// right
	case 1: if (x > 0) x--; break;				// left
	case 2: if (y < (W_HEIGHT - 1)) y++; break;	// down
	case 3:if (y > 0) y--; break;				// up
	}

	sx = y / SECTOR_SIZE; sy = x / SECTOR_SIZE;	// row, col
	if (npc->_sector_coord[0] != sx || npc->_sector_coord[1] != sy) {
		g_sl.lock();
		switch (direction) {
		case 0:
			g_Sector[sx][sy - 1].erase(npc->_id);
			g_Sector[sx][sy].insert(npc->_id);
			break;
		case 1:
			g_Sector[sx][sy + 1].erase(npc->_id);
			g_Sector[sx][sy].insert(npc->_id);
			break;
		case 2:
			g_Sector[sx - 1][sy].erase(npc->_id);
			g_Sector[sx][sy].insert(npc->_id);
			break;
		case 3:
			g_Sector[sx + 1][sy].erase(npc->_id);
			g_Sector[sx][sy].insert(npc->_id);
			break;
		}
		g_sl.unlock();
		npc->_sector_coord[0] = sx;
		npc->_sector_coord[1] = sy;
	}
	npc->x = x;
	npc->y = y;

	unordered_set<long long> new_vl;
	unordered_set<long long> new_su_list;
	g_sl.lock();
	for (short i = sx - 1; i <= sx + 1; ++i) {
		if (i < 0 || i >= W_HEIGHT / SECTOR_SIZE) continue;
		for (short j = sy - 1; j <= sy + 1; ++j) {
			if (j < 0 || j >= W_WIDTH / SECTOR_SIZE) continue;
			for (long long id : g_Sector[i][j])
				new_su_list.insert(id);
		}
	}
	g_sl.unlock();

	for (long long uid : new_su_list) {
		std::shared_ptr<SESSION> p_ob = clients.at(uid);
		if (p_ob == nullptr) continue;
		if (ST_INGAME != p_ob->_state) continue;
		if (true == is_npc(p_ob->_id)) continue;
		if (true == can_see(npc, p_ob))
			new_vl.insert(p_ob->_id);
	}

	for (auto pl : new_vl) {
		std::shared_ptr<SESSION> p = clients.at(pl);
		if (nullptr == p) continue;
		if (0 == old_vl.count(pl)) {
			// 플레이어의 시야에 등장
			p->send_add_player_packet(npc);
		}
		else {
			// 플레이어가 계속 보고 있음.
			p->send_move_packet(npc);
		}
	}

	///vvcxxccxvvdsvdvds
	for (auto pl : old_vl) {
		std::shared_ptr<SESSION> p = clients.at(pl);
		if (nullptr == p) continue;
		if (0 == new_vl.count(pl)) {
			p->_vl.lock();
			if (0 != p->_view_list.count(npc->_id)) {
				p->_vl.unlock();
				p->send_remove_player_packet(npc->_id);
			}
			else {
				p->_vl.unlock();
			}
		}
	}

	using namespace chrono;
	long long current_time = duration_cast<milliseconds>
		(system_clock::now().time_since_epoch()).count();
	npc->last_move_time = current_time;

	if (new_vl.size() != 0)
		npc->move_cnt = 0;

	if (npc->move_cnt > 3) {
		npc->_is_active = false;
		npc->move_cnt = 0;
	}

	if (npc->_is_active) {
		++npc->move_cnt;
		timer_lock.lock();
		timer_queue.emplace(event_type{ my_id, high_resolution_clock::now() + 1s, EV_MOVE, 0 });
		timer_lock.unlock();
	}

	return 1;
}

void do_npc_random_move(int npc_id)
{
	std::shared_ptr<SESSION> npc = clients.at(npc_id);
	npc->_lualock.lock();
	auto L = npc->_L;
	lua_getglobal(L, "event_npc_move");
	lua_pcall(L, 0, 0, 0);
	npc->_lualock.unlock();
}

void worker_thread(HANDLE h_iocp)
{
	while (true) {
		DWORD num_bytes;
		ULONG_PTR key;
		WSAOVERLAPPED* over = nullptr;
		BOOL ret = GetQueuedCompletionStatus(h_iocp, &num_bytes, &key, &over, INFINITE);
		OVER_EXP* ex_over = reinterpret_cast<OVER_EXP*>(over);
		if (FALSE == ret) {
			if (ex_over->_comp_type == OP_ACCEPT) std::cout << "Accept Error";
			else {
				std::cout << "GQCS Error on client[" << key << "]\n";
				disconnect(static_cast<long long>(key));
				if (ex_over->_comp_type == OP_SEND) delete ex_over;
				continue;
			}
		}

		if ((0 == num_bytes) && ((ex_over->_comp_type == OP_RECV) || (ex_over->_comp_type == OP_SEND))) {
			disconnect(static_cast<long long>(key));
			if (ex_over->_comp_type == OP_SEND) delete ex_over;
			continue;
		}

		switch (ex_over->_comp_type) {
		case OP_ACCEPT: {
			long long client_id = g_uid++;

			if (client_id < MAX_USER) {
				CreateIoCompletionPort(reinterpret_cast<HANDLE>(g_c_socket),
					h_iocp, client_id, 0);
				std::shared_ptr<SESSION> p = std::make_shared<SESSION>();
				p->_id = client_id;
				p->_socket = g_c_socket;
				clients.insert(std::make_pair(client_id, p));
				p->do_recv();
				g_c_socket = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
			}
			else {
				std::cout << "Max user exceeded.\n";
			}
			ZeroMemory(&g_a_over._over, sizeof(g_a_over._over));
			int addr_size = sizeof(SOCKADDR_IN);
			AcceptEx(g_s_socket, g_c_socket, g_a_over._send_buf, 0, addr_size + 16, addr_size + 16, 0, &g_a_over._over);
			break;
		}
		case OP_RECV: {
			std::shared_ptr<SESSION> user = clients.at(key);
			if (user == nullptr)
				break;
			int remain_data = num_bytes + user->_prev_remain;
			char* p = ex_over->_send_buf;
			while (remain_data > 0) {
				int packet_size = p[0];
				if (packet_size <= remain_data) {
					process_packet(user, static_cast<long long>(key), p);
					p = p + packet_size;
					remain_data = remain_data - packet_size;
				}
				else break;
			}
			user->_prev_remain = remain_data;
			if (remain_data > 0) {
				memcpy(ex_over->_send_buf, p, remain_data);
			}
			user->do_recv();
			break;
		}
		case OP_SEND:
			delete ex_over;
			break;
		case OP_NPC_MOVE: {
			using namespace chrono;

			long long npc_id = static_cast<long long>(key);
			do_npc_random_move(npc_id);
			delete ex_over;
		}
						break;
		case OP_AI_CHAT: {
			std::shared_ptr<SESSION> npc = clients.at(key);
			npc->_lualock.lock();
			auto L = npc->_L;
			lua_getglobal(L, "event_player_move");
			lua_pushnumber(L, ex_over->_ai_target_obj);
			lua_pcall(L, 1, 0, 0);
			npc->_lualock.unlock();
			delete ex_over;
		}
			break;
		}
	}
}

void InitializeNPC()
{
	using namespace chrono;
	cout << "NPC intialize begin.\n";
	for (long long i = MAX_USER; i < MAX_USER + MAX_NPC; ++i) {
		long long new_npc_id = i;
		std::shared_ptr<SESSION> p = std::make_shared<SESSION>();
		short x, y;
		p->x = x = rand() % W_WIDTH;
		p->y = y = rand() % W_HEIGHT;
		p->_sector_coord[0] = y / SECTOR_SIZE;
		p->_sector_coord[1] = x / SECTOR_SIZE;
		g_Sector[p->_sector_coord[0]][p->_sector_coord[1]].insert(new_npc_id);

		p->_id = new_npc_id;
		sprintf_s(p->_name, "NPC%d", i);
		p->_state = ST_INGAME;
		p->_is_active = false;

		auto L = p->_L = luaL_newstate();
		luaL_openlibs(L);
		luaL_loadfile(L, "npc.lua");
		lua_pcall(L, 0, 0, 0);

		lua_getglobal(L, "set_uid");
		lua_pushnumber(L, new_npc_id);
		lua_pcall(L, 1, 0, 0);

		lua_register(L, "API_SendMessage", API_SendMessage);
		lua_register(L, "API_get_x", API_get_x);
		lua_register(L, "API_get_y", API_get_y);
		lua_register(L, "API_move_dir", API_move_dir);

		clients.insert(std::make_pair(new_npc_id, p));
	}
	cout << "NPC initialize end.\n";
}

void do_timer()
{
	using namespace chrono;
	do {
		do {
			timer_lock.lock();
			if (timer_queue.empty() == true) {
				timer_lock.unlock();
				break;
			}
			auto& k = timer_queue.top();
			if (k.wakeup_time > high_resolution_clock::now()) {
				timer_lock.unlock();
				break;
			}
			timer_lock.unlock();

			switch (k.event_id) {
			case EV_MOVE:
				OVER_EXP* o = new OVER_EXP;
				o->_comp_type = OP_NPC_MOVE;
				PostQueuedCompletionStatus(h_iocp, 1, k.obj_id, &o->_over);
				break;
			}

			timer_lock.lock();
			timer_queue.pop();
			timer_lock.unlock();
		} while (true);
		this_thread::sleep_for(chrono::milliseconds(10));
	} while (true);
}

int main()
{
	WSADATA WSAData;
	WSAStartup(MAKEWORD(2, 2), &WSAData);
	g_s_socket = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
	SOCKADDR_IN server_addr;
	memset(&server_addr, 0, sizeof(server_addr));
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(PORT_NUM);
	server_addr.sin_addr.S_un.S_addr = INADDR_ANY;
	bind(g_s_socket, reinterpret_cast<sockaddr*>(&server_addr), sizeof(server_addr));
	listen(g_s_socket, SOMAXCONN);
	SOCKADDR_IN cl_addr;
	int addr_size = sizeof(cl_addr);

	InitializeNPC();

	h_iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, 0, 0, 0);
	CreateIoCompletionPort(reinterpret_cast<HANDLE>(g_s_socket), h_iocp, 9999, 0);
	g_c_socket = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
	g_a_over._comp_type = OP_ACCEPT;
	AcceptEx(g_s_socket, g_c_socket, g_a_over._send_buf, 0, addr_size + 16, addr_size + 16, 0, &g_a_over._over);

	vector <thread> worker_threads;
	int num_threads = std::thread::hardware_concurrency();
	for (int i = 0; i < num_threads; ++i)
		worker_threads.emplace_back(worker_thread, h_iocp);

	thread timer_thread{ do_timer };
	timer_thread.join();
	for (auto& th : worker_threads)
		th.join();
	closesocket(g_s_socket);
	WSACleanup();
}

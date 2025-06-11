#include "SESSION.h"

extern concurrency::concurrent_unordered_map<long long, std::atomic<std::shared_ptr<SESSION>>> g_users;
extern std::mutex g_sl;
extern std::array<std::array<std::unordered_set<long long>, MAP_WIDTH / SECTOR_SIZE>, MAP_HEIGHT / SECTOR_SIZE> g_sector;

bool can_see(short x1, short y1, short x2, short y2)
{
	if (abs(x1 - x2) > VIEW_RANGE) return false;
	return abs(y1 - y2) <= VIEW_RANGE;
}

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
		_x = 0;//rand() % MAP_WIDTH;
		_y = 0;// rand() % MAP_HEIGHT;
		{
			std::lock_guard<std::mutex> ll{ _s_lock };
			_state = ST_INGAME;
		}
		_sector_coord[0] = _y / SECTOR_SIZE; _sector_coord[1] = _x / SECTOR_SIZE;

		std::unordered_set<long long> su_list;
		g_sl.lock();
		g_sector[_sector_coord[0]][_sector_coord[1]].insert(_id);
		for (short i = _sector_coord[0] - 1; i <= _sector_coord[0] + 1; ++i) {
			if (i < 0 || i >= MAP_HEIGHT / SECTOR_SIZE) continue;
			for (short j = _sector_coord[1] - 1; j <= _sector_coord[1] + 1; ++j) {
				if (j < 0 || j >= MAP_WIDTH / SECTOR_SIZE) continue;
				for (long long id : g_sector[i][j])
					su_list.insert(id);
			}
		}
		g_sl.unlock();

		do_send_avatar_info();
		for (long long id : su_list) {
			std::shared_ptr<SESSION> ply = g_users.at(id);
			if (nullptr == ply) continue;
			{
				std::lock_guard<std::mutex> ll(ply->_s_lock);
				if (ST_INGAME != ply->_state) continue;
			}
			if (ply->_id == _id) continue;
			if (false == can_see(_x, _y, ply->_x, ply->_y))
				continue;
			//if (is_pc(ply->_id)) 
			//else ply->wakeup(c_id);
			ply->do_send_player_enter(_id);
			do_send_player_enter(ply->_id);
		}
		/*for (auto& c : g_users) {
			std::shared_ptr<SESSION> cp = c.second;
			if (nullptr == cp)
				continue;
			if (cp->_id == _id)
				continue;
			cp->do_send_player_enter(_id);

			do_send_player_enter(cp->_id);
		}*/
	}
		break;
	case C2S_P_MOVE: {
		cs_packet_move* p = reinterpret_cast<cs_packet_move*>(packet);
		switch (p->direction) {
		case 0: if (_y > 0) _y--; break;
		case 1: if (_y < MAP_HEIGHT - 1) _y++; break;
		case 2: if (_x > 0) _x--; break;
		case 3: if (_x < MAP_WIDTH - 1) _x++; break;
		}
		short sx = _y / SECTOR_SIZE;	// row
		short sy = _x / SECTOR_SIZE;	// col
		if (_sector_coord[0] != sx || _sector_coord[1] != sy) {
			g_sl.lock();
			switch (p->direction) {
			case 0:
				g_sector[sx + 1][sy].erase(_id);
				g_sector[sx][sy].insert(_id);
				break;
			case 1:
				g_sector[sx - 1][sy].erase(_id);
				g_sector[sx][sy].insert(_id);
				break;
			case 2:
				g_sector[sx][sy + 1].erase(_id);
				g_sector[sx][sy].insert(_id);
				break;
			case 3:
				g_sector[sx][sy - 1].erase(_id);
				g_sector[sx][sy].insert(_id);
				break;
			}
			g_sl.unlock();
			_sector_coord[0] = sx;
			_sector_coord[1] = sy;
		}

		std::unordered_set<long long> near_list;
		_vl.lock();
		std::unordered_set<long long> old_vlist = _view_list;
		_vl.unlock();

		std::unordered_set<long long> su_list;
		g_sl.lock();
		for (short i = sx - 1; i <= sx + 1; ++i) {
			if (i < 0 || i >= MAP_HEIGHT / SECTOR_SIZE) continue;
			for (short j = sy - 1; j <= sy + 1; ++j) {
				if (j < 0 || j >= MAP_WIDTH / SECTOR_SIZE) continue;
				for (long long id : g_sector[i][j])
					su_list.insert(id);
			}
		}
		g_sl.unlock();

		for (long long id : su_list) {
			std::shared_ptr<SESSION> ply = g_users.at(id);
			if (nullptr == ply)
				continue;
			if (ply->_id == _id) continue;
			if (ply->_state != ST_INGAME) continue;
			if (can_see(_x, _y, ply->_x, ply->_y))
				near_list.insert(ply->_id);
		}

		do_send_move();

		for (auto& pl : near_list) {
			std::shared_ptr<SESSION> cpl = g_users.at(pl);
			if (nullptr == cpl)
				continue;
			//if (is_pc(pl)) {
				cpl->_vl.lock();
				if (cpl->_view_list.count(_id)) {
					cpl->_vl.unlock();
					cpl->do_send_move(_id);
				}
				else {
					cpl->_vl.unlock();
					cpl->do_send_player_enter(_id);
				}
			/*}
			else {
				cpl->wakeup(c_id);
			}*/

				if (old_vlist.count(pl) == 0)
					do_send_player_enter(pl);
		}

		for (auto& pl : old_vlist) {
			if (0 == near_list.count(pl)) {
				do_send_player_leave(pl);
				//if (is_pc(pl)) {
					std::shared_ptr<SESSION> pp = g_users.at(pl);
					if (nullptr != pp)
						pp->do_send_player_leave(_id);
				//}
			}
		}

		/*for (auto& c : g_users) {
			std::shared_ptr<SESSION> cp = c.second;
			if (nullptr == cp)
				continue;
			if (cp->_id == _id)
				continue;
			cp->do_send_move(_id);
		}*/
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

void SESSION::do_send_avatar_info(short max_hp, short hp, short level, int exp)
{
	sc_packet_avatar_info p;
	p.size = sizeof(sc_packet_avatar_info);
	p.type = S2C_P_AVATAR_INFO;
	p.id = _id;
	p.x = _x; p.y = _y;
	p.max_hp = max_hp;
	p.hp = hp;
	p.level = level;
	p.exp = exp;
	do_send(&p);
}

void SESSION::do_send_player_enter(long long id, int type)
{
	std::shared_ptr<SESSION> pl = g_users.at(id);
	if (nullptr == pl) return;
	sc_packet_enter p;
	p.size = sizeof(sc_packet_enter);
	p.type = S2C_P_ENTER;
	p.id = id;
	strcpy_s(p.name, pl->_name);
	p.o_type = static_cast<char>(type);
	p.x = pl->_x; p.y = pl->_y;
	_vl.lock();
	_view_list.insert(id);
	_vl.unlock();
	do_send(&p);
}

void SESSION::do_send_player_leave(long long id)
{
	_vl.lock();
	if (_view_list.count(id))
		_view_list.erase(id);
	else {
		_vl.unlock();
		return;
	}
	_vl.unlock();
	sc_packet_leave p;
	p.size = sizeof(p);
	p.type = S2C_P_LEAVE;
	p.id = id;
	do_send(&p);
}

void SESSION::do_send_move()
{
	sc_packet_move p;
	p.size = sizeof(p);
	p.type = S2C_P_MOVE;
	p.id = _id;
	p.x = _x; p.y = _y;
	do_send(&p);
}

void SESSION::do_send_move(long long id)
{
	std::shared_ptr<SESSION> pl = g_users.at(id);
	if (nullptr == pl) return;
	sc_packet_move p;
	p.size = sizeof(p);
	p.type = S2C_P_MOVE;
	p.id = pl->_id;
	p.x = pl->_x; p.y = pl->_y;
	do_send(&p);
}

void SESSION::do_send(void* packet)
{
	EX_OVER* ex_over = new EX_OVER{ reinterpret_cast<char*>(packet) };
	WSASend(_socket, &ex_over->_wsabuf, 1, 0, 0, &ex_over->_over, 0);
}

void SESSION::disconnect()
{
	_vl.lock();
	std::unordered_set <long long> vl = _view_list;
	_vl.unlock();
	for (auto& p_id : vl) {
		//if (is_npc(p_id)) continue;
		std::shared_ptr<SESSION> pl = g_users.at(p_id);
		if (pl == nullptr)
			continue;
		{
			std::lock_guard<std::mutex> ll(pl->_s_lock);
			if (ST_INGAME != pl->_state) continue;
		}
		if (pl->_id == _id) continue;
		pl->do_send_player_leave(_id);
	}

	g_sl.lock();
	g_sector[_sector_coord[0]][_sector_coord[1]].erase(_id);
	g_sl.unlock();
}
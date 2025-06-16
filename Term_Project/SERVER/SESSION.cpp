#include "SESSION.h"

extern concurrency::concurrent_unordered_map<long long, std::atomic<std::shared_ptr<SESSION>>> g_users;
extern std::mutex g_sl;
extern std::array<std::array<std::unordered_set<long long>, MAP_WIDTH / SECTOR_SIZE>, MAP_HEIGHT / SECTOR_SIZE> g_sector;

extern std::array<std::array<bool, MAP_WIDTH>, MAP_HEIGHT> g_obstacles;

extern thread_local SQLHENV henv;
extern thread_local SQLHDBC hdbc;
extern thread_local SQLHSTMT hstmt;
extern thread_local SQLRETURN retcode;
extern thread_local SQLWCHAR szName[MAX_ID_LENGTH];
extern thread_local SQLINTEGER db_x, db_y, db_max_hp, db_hp, db_level, db_exp;
extern thread_local SQLLEN cbName, cb_db_x, cb_db_y, cb_db_max_hp, cb_db_hp, cb_db_level, cb_db_exp;

std::mutex g_tl;
std::priority_queue<event_type> g_eventq;

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

		std::wstring sqlcommand{ L"EXEC Get_User_Info " };
		std::string sName{ p->name };

		for (int i = 0; i < MAX_USER; ++i) {
			if (g_users.count(i)) {
				std::shared_ptr<SESSION> cl = g_users.at(i);
				if (cl != nullptr) {
					if (!strcmp(cl->_name, p->name)) {
						do_send_login_fail(1);
						return;
					}
				}
			}
		}

		if (sName.size() >= 20) {
			do_send_login_fail(2);
			return;
		}
		for (auto& c : sName) {
			if (false == isalpha(c) && false == isdigit(c)) {
				do_send_login_fail(2);
				return;
			}
		}

		std::wstring wName{ sName.begin(), sName.end() };
		sqlcommand += wName;
		retcode = SQLExecDirect(hstmt, (SQLWCHAR*)sqlcommand.data(), SQL_NTS);
		if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO) {
			retcode = SQLBindCol(hstmt, 1, SQL_C_LONG, &db_max_hp, 4, &cb_db_max_hp);
			retcode = SQLBindCol(hstmt, 2, SQL_C_LONG, &db_hp, 4, &cb_db_hp);
			retcode = SQLBindCol(hstmt, 3, SQL_C_LONG, &db_level , 4, &cb_db_level);
			retcode = SQLBindCol(hstmt, 4, SQL_C_LONG, &db_exp, 4, &cb_db_exp);
			retcode = SQLBindCol(hstmt, 5, SQL_C_LONG, &db_x, 4, &cb_db_x);
			retcode = SQLBindCol(hstmt, 6, SQL_C_LONG, &db_y, 4, &cb_db_y);
			retcode = SQLFetch(hstmt);
			SQLCloseCursor(hstmt);
			if (retcode != SQL_SUCCESS && retcode != SQL_SUCCESS_WITH_INFO) {	// Create New ID
				sqlcommand = L"EXEC Insert_New_User ";

				strcpy_s(_name, p->name);
				_x = (rand() % 500) + 750;
				_y = (rand() % 500) + 750;

				_max_hp = 100; _hp = 100;
				_level = 1; _exp = 0;
				_need_exp = 100 * pow(2, _level - 1);
				_attack = 30;

				sqlcommand = sqlcommand + std::to_wstring(_x) + L", " + std::to_wstring(_y) + L", " + wName;
				retcode = SQLExecDirect(hstmt, (SQLWCHAR*)sqlcommand.data(), SQL_NTS);
				SQLCloseCursor(hstmt);
			}
			else {
				strcpy_s(_name, p->name);
				_x = db_x;
				_y = db_y;

				_max_hp = db_max_hp; _hp = db_hp;
				_level = db_level; _exp = db_exp;
				_need_exp = 100 * pow(2, _level - 1);
				_attack = 30 + (_level * 15);
			}
		}


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

		g_tl.lock();
		g_eventq.emplace(event_type{ _id, std::chrono::high_resolution_clock::now() + std::chrono::seconds(5), PL_HEAL, 0 });
		g_tl.unlock();
	}
		break;
	case C2S_P_MOVE: {
		cs_packet_move* p = reinterpret_cast<cs_packet_move*>(packet);

		if (!(_x > 600 && _x <= 1400 && _y > 600 && _y <= 1400)) {
			if (_move_term >= std::chrono::high_resolution_clock::now())
				break;
		}

		short x = _x, y = _y;
		switch (p->direction) {
		case MOVE_UP: if (y > 0) y--; break;
		case MOVE_DOWN: if (y < MAP_HEIGHT - 1) y++; break;
		case MOVE_LEFT: if (x > 0) x--; break;
		case MOVE_RIGHT: if (x < MAP_WIDTH - 1) x++; break;
		}

		if (g_obstacles[y][x])
			break;

		_x = x; _y = y;
		_move_time = p->move_time;
		short sx = _y / SECTOR_SIZE;	// row
		short sy = _x / SECTOR_SIZE;	// col
		if (_sector_coord[0] != sx || _sector_coord[1] != sy) {
			g_sl.lock();
			g_sector[_sector_coord[0]][_sector_coord[1]].erase(_id);
			g_sector[sx][sy].insert(_id);
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
		_move_term = std::chrono::high_resolution_clock::now() + std::chrono::milliseconds(500);

		/*char str[] = "You Move";
		do_send_chat_packet(str);*/

		for (auto& pl : near_list) {
			std::shared_ptr<SESSION> cpl = g_users.at(pl);
			if (nullptr == cpl)
				continue;
			if (is_pc(pl)) {
				cpl->_vl.lock();
				if (cpl->_view_list.count(_id)) {
					cpl->_vl.unlock();
					cpl->do_send_move(_id);
				}
				else {
					cpl->_vl.unlock();
					cpl->do_send_player_enter(_id);
				}
			}
			else {
				cpl->wake_up(_id);
				/*if (_x == cpl->_x && _y == cpl->_y) {
					_hp -= 10;
					do_send_stat();
				}*/
			}

				if (old_vlist.count(pl) == 0)
					do_send_player_enter(pl);
		}

		for (auto& pl : old_vlist) {
			if (0 == near_list.count(pl)) {
				do_send_player_leave(pl);
				if (is_pc(pl)) {
					std::shared_ptr<SESSION> pp = g_users.at(pl);
					if (nullptr != pp)
						pp->do_send_player_leave(_id);
				}
			}
		}

	}
		break;
	case C2S_P_CHAT: {
		cs_packet_chat* p = reinterpret_cast<cs_packet_chat*>(packet);
		_vl.lock();
		std::unordered_set<long long> view = _view_list;
		_vl.unlock();

		do_send_chat_packet(p->message, _id);
		for (long long pid : view) {
			std::shared_ptr<SESSION> ply = g_users.at(pid);
			if (nullptr == ply) continue;
			if (ply->_state != ST_INGAME) continue;
			ply->do_send_chat_packet(p->message, _id);
		}
	}
		break;
	case C2S_P_WARP: {
		cs_packet_warp* p = reinterpret_cast<cs_packet_warp*>(packet);
		_cl.lock();
		switch (p->zone) {
		case 0:
			_x = (rand() % 500) + 750;
			_y = (rand() % 500) + 750;
			break;
		case 1:
			if (rand() % 2) {
				_x = (rand() % 5) + 604;
				_y = (rand() % 5) + 604;
			}
			else {
				_x = (rand() % 5) + 1392;
				_y = (rand() % 5) + 1392;
			}
			break;
		case 2:
			if (rand() % 2) {
				_x = (rand() % 5) + 998;
				_y = (rand() % 5) + 604;
			}
			else {
				_x = (rand() % 5) + 998;
				_y = (rand() % 5) + 1392;
			}
			break;
		case 3:
			if (rand() % 2) {
				_x = (rand() % 5) + 1392;
				_y = (rand() % 5) + 604;
			}
			else {
				_x = (rand() % 5) + 604;
				_y = (rand() % 5) + 1392;
			}
			break;
		case 4:
			if (rand() % 2) {
				_x = (rand() % 5) + 604;
				_y = (rand() % 5) + 998;
			}
			else {
				_x = (rand() % 5) + 1392;
				_y = (rand() % 5) + 998;
			}
			break;
		}
		_cl.unlock();

		short sx = _y / SECTOR_SIZE;	// row
		short sy = _x / SECTOR_SIZE;	// col
		if (_sector_coord[0] != sx || _sector_coord[1] != sy) {
			g_sl.lock();
			g_sector[_sector_coord[0]][_sector_coord[1]].erase(_id);
			g_sector[sx][sy].insert(_id);
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
		/*char str[] = "You Move";
		do_send_chat_packet(str);*/

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
	}
		break;
	case C2S_P_ATTACK: {
		auto time = std::chrono::high_resolution_clock::now();
		if (_attack_term < time) {
			std::unordered_set<long long> su_list;
			g_sl.lock();
			for (short i = _sector_coord[0] - 1; i <= _sector_coord[0] + 1; ++i) {
				if (i < 0 || i >= MAP_HEIGHT / SECTOR_SIZE) continue;
				for (short j = _sector_coord[1] - 1; j <= _sector_coord[1] + 1; ++j) {
					if (j < 0 || j >= MAP_WIDTH / SECTOR_SIZE) continue;
					for (long long id : g_sector[i][j])
						su_list.insert(id);
				}
			}
			g_sl.unlock();

			for (long long oid : su_list) {
				std::shared_ptr<SESSION> o = g_users.at(oid);
				if (nullptr == o) continue;
				if (is_pc(oid)) continue;
				auto t = std::chrono::high_resolution_clock::now();
				if (o->_revive_term < t) {
					if (attack_check(_x, _y, o->_x, o->_y)) {
						o->_hp -= _attack;
						std::string pname{ o->_name };
						std::string message;
						message = "You gave " + std::to_string(_attack) + "damage to " + pname;
						do_send_chat_packet(message.data());
						if (o->_hp <= 0) {
							o->_cl.lock();
							o->_x = o->_ix; o->_y = o->_iy;
							o->_cl.unlock();

							std::unordered_set<long long> npc_su_list;
							g_sl.lock();
							for (short i = o->_sector_coord[0] - 1; i <= o->_sector_coord[0] + 1; ++i) {
								if (i < 0 || i >= MAP_HEIGHT / SECTOR_SIZE) continue;
								for (short j = o->_sector_coord[1] - 1; j <= o->_sector_coord[1] + 1; ++j) {
									if (j < 0 || j >= MAP_WIDTH / SECTOR_SIZE) continue;
									for (long long id : g_sector[i][j])
										npc_su_list.insert(id);
								}
							}
							g_sl.unlock();

							for (long long pid : npc_su_list) {
								if (is_pc(pid)) {
									std::shared_ptr<SESSION> pp = g_users.at(pid);
									if (nullptr == pp) continue;
									if (pp->_state != ST_INGAME) continue;
									if (can_see(o->_x, o->_y, pp->_x, pp->_y))
										pp->do_send_move(o->_id);
								}
							}
							o->_revive_term = t + std::chrono::seconds(30);
							o->_alive = false;

							o->_ll.lock();
							
							lua_getglobal(o->_lua_machine, "set_state");
							if(o->_npc_type == NPC_AGRO_ROMING || o->_npc_type == NPC_PEACE_ROMING)
								lua_pushnumber(o->_lua_machine, 2);
							else
								lua_pushnumber(o->_lua_machine, 0);
							lua_pcall(o->_lua_machine, 1, 0, 0);

							o->_ll.unlock();

							short exp;
							if(o->_npc_type == NPC_PEACE_FIX)
								exp = o->_level * o->_level * 2;
							else
								exp = o->_level * o->_level * 4;

							std::string systemchat;
							systemchat = "you killed " + std::string(o->_name) + " got " + std::to_string(exp) + "exp";
							
							_exp += exp;
							if (_need_exp <= _exp) {
								_level += 1;
								_exp = 0;
								_need_exp = 100 * pow(2, _level - 1);
								_attack = 30 + (_level * 15);
							}
							do_send_chat_packet(systemchat.data());
							do_send_stat();
						}
						else {
							if (o->_npc_type == NPC_PEACE_FIX || o->_npc_type == NPC_PEACE_ROMING) {
								o->_ll.lock();
								lua_getglobal(o->_lua_machine, "set_state");
								lua_pushnumber(o->_lua_machine, 3);
								lua_pcall(o->_lua_machine, 1, 0, 0);

								lua_getglobal(o->_lua_machine, "set_target");
								lua_pushnumber(o->_lua_machine, _id);
								lua_pcall(o->_lua_machine, 1, 0, 0);
								o->_ll.unlock();
							}
						}
					}
				}
				else {
					if (attack_check(_x, _y, o->_x, o->_y)) {
						char tt[] = "Reviving";
						do_send_chat_packet(tt, o->_id);
					}
				}
			}
			_attack_term = time + std::chrono::seconds(1);
		}
	}
		break;
	case C2S_P_TELEPORT: {
		int zone = rand() % 9;
		switch (zone) {
		case 0: {
			_x = rand() % 598;
			_y = rand() % 598;
			while (g_obstacles[_y][_x])
				++_x;
		}
			  break;
		case 1: {
			_x = rand() % 598 + 1402;
			_y = rand() % 598 + 1402;
			while (g_obstacles[_y][_x])
					--_x;
		}
			  break;
		case 2: {
			_x = rand() % 598 + 1402;
			_y = rand() % 598;
			while (g_obstacles[_y][_x])
					--_x;
		}
			  break;
		case 3: {
			_x = rand() % 598;
			_y = rand() % 598 + 1402;
			while (g_obstacles[_y][_x]) 
					++_x;
		}
			  break;
		case 4: {
			_x = rand() % 794 + 602;
			_y = rand() % 598;
			while (g_obstacles[_y][_x]) 
					++_x;
		}
			  break;
		case 5: {
			_x = rand() % 794 + 602;
			_y = rand() % 598 + 1402;
			while (g_obstacles[_y][_x])
					--_x;
		}
			  break;
		case 6: {
			_x = rand() % 598;
			_y = rand() % 794 + 602;
			while (g_obstacles[_y][_x]) 
					++_x;
		}
			  break;
		case 7: {
			_x = rand() % 598 + 1402;
			_y = rand() % 794 + 602;
			while (g_obstacles[_y][_x]) 
					--_x;
		}
			  break;
		}
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

void SESSION::do_send_avatar_info()
{
	sc_packet_avatar_info p;
	p.size = sizeof(sc_packet_avatar_info);
	p.type = S2C_P_AVATAR_INFO;
	p.id = _id;
	p.x = _x; p.y = _y;
	p.max_hp = _max_hp;
	p.hp = _hp;
	p.level = _level;
	p.exp = _exp;
	do_send(&p);
}

void SESSION::do_send_login_fail(short reason)
{
	sc_packet_login_fail p;
	p.size = sizeof(p);
	p.type = S2C_P_LOGIN_FAIL;
	p.id = _id;
	p.reason = static_cast<char>(reason);
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

void SESSION::do_send_chat_packet(char* str, long long id)
{
	sc_packet_chat p;
	p.size = sizeof(p);
	p.type = S2C_P_CHAT;
	p.id = id;
	strcpy_s(p.message, str);
	do_send(&p);
}

void SESSION::do_send_stat()
{
	sc_packet_stat_change p;
	p.size = sizeof(p);
	p.type = S2C_P_STAT_CHANGE;
	p.id = _id;
	p.max_hp = _max_hp;
	p.hp = _hp;
	p.level = _level;
	p.exp = _exp;
	do_send(&p);
}

void SESSION::do_heal_and_send()
{
	short m = _max_hp;
	short hp = _hp;
	hp += m / 10;
	if (hp > m)
		hp = m;
	_hp = hp;
	do_send_stat();
}

void SESSION::do_send_move()
{
	sc_packet_move p;
	p.size = sizeof(p);
	p.type = S2C_P_MOVE;
	p.id = _id;
	p.x = _x; p.y = _y;
	p.move_time = _move_time;
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


	std::wstring sqlcommand{ L"EXEC save_user_data " };
	std::string sName{ _name };
	std::wstring wName{ sName.begin(), sName.end() };
	sqlcommand = sqlcommand + std::to_wstring(_hp) + L", " + std::to_wstring(_level) + L", " +
		std::to_wstring(_exp) + L", " + std::to_wstring(_x) + L", " + std::to_wstring(_y) +
		L", " + wName;
	retcode = SQLExecDirect(hstmt, (SQLWCHAR*)sqlcommand.data(), SQL_NTS);
	SQLCloseCursor(hstmt);
}

void SESSION::wake_up(long long id)
{
	if (_revive_term >= std::chrono::high_resolution_clock::now())
		return;
	bool talive = _alive;
	if (false == talive) {
		bool expected = false;
		bool new_b = true;
		if (std::atomic_compare_exchange_strong(&_alive, &expected, new_b)) {
			// timer 추가
			g_tl.lock();
			g_eventq.emplace(event_type{ _id, std::chrono::high_resolution_clock::now() + std::chrono::milliseconds(500),
				EV_NPC_AI, id });
			g_tl.unlock();
		}
	}
}

int SESSION::do_npc_move()
{
	int ret = 2;

	std::unordered_set<long long> su_list;
	g_sl.lock();
	for (short i = _sector_coord[0] - 1; i <= _sector_coord[0] + 1; ++i) {
		if (i < 0 || i >= MAP_HEIGHT / SECTOR_SIZE) continue;
		for (short j = _sector_coord[1] - 1; j <= _sector_coord[1] + 1; ++j) {
			if (j < 0 || j >= MAP_WIDTH / SECTOR_SIZE) continue;
			for (long long id : g_sector[i][j])
				su_list.insert(id);
		}
	}
	g_sl.unlock();

	std::unordered_set<long long> old_vl;
	for (auto& pid : su_list) {
		std::shared_ptr<SESSION> p_ob = g_users.at(pid);
		if (p_ob == nullptr) continue;
		if (ST_INGAME != p_ob->_state) continue;
		if (false == is_pc(p_ob->_id)) continue;
		if (true == can_see(_x, _y, p_ob->_x, p_ob->_y))
			old_vl.insert(p_ob->_id);
	}

	int dir = rand() % 4;
	_cl.lock();
	short x = _x;
	short y = _y;
	_cl.unlock();
	switch (dir) {
	case 0: if (y > 0) y--; break;
	case 1: if (y < MAP_HEIGHT - 1) y++; break;
	case 2: if (x > 0) x--; break;
	case 3: if (x < MAP_WIDTH - 1) x++; break;
	}

	if (g_obstacles[y][x])
		return ret;

	_cl.lock();
	_x = x; _y = y;
	_cl.unlock();

	short sx = _y / SECTOR_SIZE;	// row
	short sy = _x / SECTOR_SIZE;	// col

	bool change_sector = false;
	if (_sector_coord[0] != sx || _sector_coord[1] != sy) {
		g_sl.lock();
		g_sector[_sector_coord[0]][_sector_coord[1]].erase(_id);
		g_sector[sx][sy].insert(_id);
		g_sl.unlock();
		_sector_coord[0] = sx;
		_sector_coord[1] = sy;
		change_sector = true;
	}

	if (change_sector) {
		su_list.clear();
		g_sl.lock();
		for (short i = _sector_coord[0] - 1; i <= _sector_coord[0] + 1; ++i) {
			if (i < 0 || i >= MAP_HEIGHT / SECTOR_SIZE) continue;
			for (short j = _sector_coord[1] - 1; j <= _sector_coord[1] + 1; ++j) {
				if (j < 0 || j >= MAP_WIDTH / SECTOR_SIZE) continue;
				for (long long id : g_sector[i][j])
					su_list.insert(id);
			}
		}
		g_sl.unlock();
	}
	

	std::unordered_set<long long> new_vl;
	for (auto& pid : su_list) {
		std::shared_ptr<SESSION> p_ob = g_users.at(pid);
		if (p_ob == nullptr) continue;
		if (ST_INGAME != p_ob->_state) continue;
		if (false == is_pc(p_ob->_id)) continue;
		if (true == can_see(_x, _y, p_ob->_x, p_ob->_y))
			new_vl.insert(p_ob->_id);
	}

	if (new_vl.size() == 0)
		++_no_near;

	bool onceb = false;
	for (auto pl : new_vl) {
		_no_near = 0;
		std::shared_ptr<SESSION> p = g_users.at(pl);
		if (nullptr == p) continue;
		if (0 == old_vl.count(pl)) {
			// 플레이어의 시야에 등장
			p->do_send_player_enter(_id, 1);
		}
		else {
			// 플레이어가 계속 보고 있음.
			p->do_send_move(_id);
		}

		if (!onceb && _npc_type == NPC_AGRO_ROMING) {
			if (can_see5(_x, _y, p->_x, p->_y)) {
				lua_getglobal(_lua_machine, "set_target");
				lua_pushnumber(_lua_machine, p->_id);
				lua_pcall(_lua_machine, 1, 0, 0);

				ret = 3;
				onceb = true;
			}
		}
	}

	for (auto pl : old_vl) {
		std::shared_ptr<SESSION> p = g_users.at(pl);
		if (nullptr == p) continue;
		if (0 == new_vl.count(pl)) {
			p->_vl.lock();
			if (0 != p->_view_list.count(_id)) {
				p->_vl.unlock();
				p->do_send_player_leave(_id);
			}
			else {
				p->_vl.unlock();
			}
		}
	}
	if (_no_near >= 6) {
		_alive = false;
		if (_npc_type == NPC_AGRO_ROMING || _npc_type == NPC_PEACE_ROMING)
			ret = 2;
		else
			ret = 0;
	}
	
	return ret;
}

int SESSION::do_check_near_user()
{
	int ret = -1;
	if (_npc_type == NPC_AGRO_FIX) {
		std::unordered_set<long long> su_list;
		g_sl.lock();
		for (short i = _sector_coord[0] - 1; i <= _sector_coord[0] + 1; ++i) {
			if (i < 0 || i >= MAP_HEIGHT / SECTOR_SIZE) continue;
			for (short j = _sector_coord[1] - 1; j <= _sector_coord[1] + 1; ++j) {
				if (j < 0 || j >= MAP_WIDTH / SECTOR_SIZE) continue;
				for (long long id : g_sector[i][j])
					su_list.insert(id);
			}
		}
		g_sl.unlock();

		for (auto& pid : su_list) {
			std::shared_ptr<SESSION> p_ob = g_users.at(pid);
			if (p_ob == nullptr) continue;
			if (ST_INGAME != p_ob->_state) continue;
			if (false == is_pc(p_ob->_id)) continue;
			if (true == can_see5(_x, _y, p_ob->_x, p_ob->_y))
				return pid;

		}

		return ret;
	}
	else
		return ret;
}

int SESSION::do_npc_chase(long long id)
{
	std::shared_ptr<SESSION> target = g_users.at(id);
	if (target == nullptr) {
		if (_npc_type == NPC_AGRO_ROMING || _npc_type == NPC_PEACE_ROMING)
			return 2;
		else
			return 0;
	}


	auto t = std::chrono::high_resolution_clock::now();
	if (_path_trace_term < t) {
		std::priority_queue<NODE*> openq;
		std::vector<std::vector<bool>> opencheck(2000, std::vector<bool>(2000, false));
		std::vector<std::vector<bool>> closelist(2000, std::vector<bool>(2000, false));
		std::list<NODE*> AllNode;

		short goal_x = target->_x;
		short goal_y = target->_y;

		if (section_where(goal_x, goal_y) != _in_section) {
			if (_npc_type == NPC_AGRO_ROMING || _npc_type == NPC_PEACE_ROMING)
				return 2;
			else
				return 0;
		}

		openq.emplace(new NODE(_x, _y, 0, 0, nullptr));
		opencheck[_y][_x] = true;

		int huri{};

		while (!_path.empty())
			_path.pop();

		while (!openq.empty()) {
			NODE* node = openq.top();
			openq.pop();
			if (node->_x == goal_x && node->_y == goal_y) {	// 탐색 성공
				for (NODE* p = node->_parent; p != nullptr; p = p->_parent) {
					_path.push(NODE{ p->_x, p->_y, p->_h, p->_g, nullptr });
				}
				delete node;
				while (!openq.empty()) {
					NODE* p = openq.top();
					openq.pop();
					if (p) delete p;
				}
				for (NODE* p : AllNode)
					if (p) delete p;
				break;
			}
			else {
				short x = node->_x; short y = node->_y;
				switch (_in_section) {
				case 0: {
					if (!(y - 1 < 0)) {
						if (!g_obstacles[y - 1][x]) {
							huri = ((goal_x - x) * (goal_x - x)) + ((goal_y - (y - 1)) * (goal_y - (y - 1)));
							NODE* n = new NODE(x, y - 1, huri, node->_g + 1, node);
							if (closelist[y - 1][x] || opencheck[y - 1][x]) delete n;
							else openq.push(n);
						}
					}
					if (!(y + 1 > 597)) {
						if (!g_obstacles[y + 1][x]) {
							huri = ((goal_x - x) * (goal_x - x)) + ((goal_y - (y + 1)) * (goal_y - (y + 1)));
							NODE* n = new NODE(x, y + 1, huri, node->_g + 1, node);
							if (closelist[y + 1][x] || opencheck[y + 1][x]) delete n;
							else openq.push(n);
						}
					}
					if (!(x - 1 < 0)) {
						if (!g_obstacles[y][x - 1]) {
							huri = ((goal_x - (x - 1)) * (goal_x - (x - 1))) + ((goal_y - y) * (goal_y - y));
							NODE* n = new NODE(x - 1, y, huri, node->_g + 1, node);
							if (closelist[y][x - 1] || opencheck[y][x - 1]) delete n;
							else openq.push(n);
						}
					}
					if (!(x + 1 > 597)) {
						if (!g_obstacles[y][x + 1]) {
							huri = ((goal_x - (x + 1)) * (goal_x - (x + 1))) + ((goal_y - y) * (goal_y - y));
							NODE* n = new NODE(x + 1, y, huri, node->_g + 1, node);
							if (closelist[y][x + 1] || opencheck[y][x + 1]) delete n;
							else openq.push(n);
						}
					}
				}
					  break;
				case 1: {
					if (!(y - 1 < 1402)) {
						if (!g_obstacles[y - 1][x]) {
							huri = ((goal_x - x) * (goal_x - x)) + ((goal_y - (y - 1)) * (goal_y - (y - 1)));
							NODE* n = new NODE(x, y - 1, huri, node->_g + 1, node);
							if (closelist[y - 1][x] || opencheck[y - 1][x]) delete n;
							else openq.push(n);
						}
					}
					if (!(y + 1 > 1999)) {
						if (!g_obstacles[y + 1][x]) {
							huri = ((goal_x - x) * (goal_x - x)) + ((goal_y - (y + 1)) * (goal_y - (y + 1)));
							NODE* n = new NODE(x, y + 1, huri, node->_g + 1, node);
							if (closelist[y + 1][x] || opencheck[y + 1][x]) delete n;
							else openq.push(n);
						}
					}
					if (!(x - 1 < 1402)) {
						if (!g_obstacles[y][x - 1]) {
							huri = ((goal_x - (x - 1)) * (goal_x - (x - 1))) + ((goal_y - y) * (goal_y - y));
							NODE* n = new NODE(x - 1, y, huri, node->_g + 1, node);
							if (closelist[y][x - 1] || opencheck[y][x - 1]) delete n;
							else openq.push(n);
						}
					}
					if (!(x + 1 > 1999)) {
						if (!g_obstacles[y][x + 1]) {
							huri = ((goal_x - (x + 1)) * (goal_x - (x + 1))) + ((goal_y - y) * (goal_y - y));
							NODE* n = new NODE(x + 1, y, huri, node->_g + 1, node);
							if (closelist[y][x + 1] || opencheck[y][x + 1]) delete n;
							else openq.push(n);
						}
					}
				}
					  break;
				case 2: {
					if (!(y - 1 < 0)) {
						if (!g_obstacles[y - 1][x]) {
							huri = ((goal_x - x) * (goal_x - x)) + ((goal_y - (y - 1)) * (goal_y - (y - 1)));
							NODE* n = new NODE(x, y - 1, huri, node->_g + 1, node);
							if (closelist[y - 1][x] || opencheck[y - 1][x]) delete n;
							else openq.push(n);
						}
					}
					if (!(y + 1 > 597)) {
						if (!g_obstacles[y + 1][x]) {
							huri = ((goal_x - x) * (goal_x - x)) + ((goal_y - (y + 1)) * (goal_y - (y + 1)));
							NODE* n = new NODE(x, y + 1, huri, node->_g + 1, node);
							if (closelist[y + 1][x] || opencheck[y + 1][x]) delete n;
							else openq.push(n);
						}
					}
					if (!(x - 1 < 1402)) {
						if (!g_obstacles[y][x - 1]) {
							huri = ((goal_x - (x - 1)) * (goal_x - (x - 1))) + ((goal_y - y) * (goal_y - y));
							NODE* n = new NODE(x - 1, y, huri, node->_g + 1, node);
							if (closelist[y][x - 1] || opencheck[y][x - 1]) delete n;
							else openq.push(n);
						}
					}
					if (!(x + 1 > 1999)) {
						if (!g_obstacles[y][x + 1]) {
							huri = ((goal_x - (x + 1)) * (goal_x - (x + 1))) + ((goal_y - y) * (goal_y - y));
							NODE* n = new NODE(x + 1, y, huri, node->_g + 1, node);
							if (closelist[y][x + 1] || opencheck[y][x + 1]) delete n;
							else openq.push(n);
						}
					}
				}
					  break;
				case 3: {
					if (!(y - 1 < 1402)) {
						if (!g_obstacles[y - 1][x]) {
							huri = ((goal_x - x) * (goal_x - x)) + ((goal_y - (y - 1)) * (goal_y - (y - 1)));
							NODE* n = new NODE(x, y - 1, huri, node->_g + 1, node);
							if (closelist[y - 1][x] || opencheck[y - 1][x]) delete n;
							else openq.push(n);
						}
					}
					if (!(y + 1 > 1999)) {
						if (!g_obstacles[y + 1][x]) {
							huri = ((goal_x - x) * (goal_x - x)) + ((goal_y - (y + 1)) * (goal_y - (y + 1)));
							NODE* n = new NODE(x, y + 1, huri, node->_g + 1, node);
							if (closelist[y + 1][x] || opencheck[y + 1][x]) delete n;
							else openq.push(n);
						}
					}
					if (!(x - 1 < 0)) {
						if (!g_obstacles[y][x - 1]) {
							huri = ((goal_x - (x - 1)) * (goal_x - (x - 1))) + ((goal_y - y) * (goal_y - y));
							NODE* n = new NODE(x - 1, y, huri, node->_g + 1, node);
							if (closelist[y][x - 1] || opencheck[y][x - 1]) delete n;
							else openq.push(n);
						}
					}
					if (!(x + 1 > 597)) {
						if (!g_obstacles[y][x + 1]) {
							huri = ((goal_x - (x + 1)) * (goal_x - (x + 1))) + ((goal_y - y) * (goal_y - y));
							NODE* n = new NODE(x + 1, y, huri, node->_g + 1, node);
							if (closelist[y][x + 1] || opencheck[y][x + 1]) delete n;
							else openq.push(n);
						}
					}
				}
					  break;
				case 4: {
					if (!(y - 1 < 0)) {
						if (!g_obstacles[y - 1][x]) {
							huri = ((goal_x - x) * (goal_x - x)) + ((goal_y - (y - 1)) * (goal_y - (y - 1)));
							NODE* n = new NODE(x, y - 1, huri, node->_g + 1, node);
							if (closelist[y - 1][x] || opencheck[y - 1][x]) delete n;
							else openq.push(n);
						}
					}
					if (!(y + 1 > 597)) {
						if (!g_obstacles[y + 1][x]) {
							huri = ((goal_x - x) * (goal_x - x)) + ((goal_y - (y + 1)) * (goal_y - (y + 1)));
							NODE* n = new NODE(x, y + 1, huri, node->_g + 1, node);
							if (closelist[y + 1][x] || opencheck[y + 1][x]) delete n;
							else openq.push(n);
						}
					}
					if (!(x - 1 < 602)) {
						if (!g_obstacles[y][x - 1]) {
							huri = ((goal_x - (x - 1)) * (goal_x - (x - 1))) + ((goal_y - y) * (goal_y - y));
							NODE* n = new NODE(x - 1, y, huri, node->_g + 1, node);
							if (closelist[y][x - 1] || opencheck[y][x - 1]) delete n;
							else openq.push(n);
						}
					}
					if (!(x + 1 > 1395)) {
						if (!g_obstacles[y][x + 1]) {
							huri = ((goal_x - (x + 1)) * (goal_x - (x + 1))) + ((goal_y - y) * (goal_y - y));
							NODE* n = new NODE(x + 1, y, huri, node->_g + 1, node);
							if (closelist[y][x + 1] || opencheck[y][x + 1]) delete n;
							else openq.push(n);
						}
					}
				}
					  break;
				case 5: {
					if (!(y - 1 < 1402)) {
						if (!g_obstacles[y - 1][x]) {
							huri = ((goal_x - x) * (goal_x - x)) + ((goal_y - (y - 1)) * (goal_y - (y - 1)));
							NODE* n = new NODE(x, y - 1, huri, node->_g + 1, node);
							if (closelist[y - 1][x] || opencheck[y - 1][x]) delete n;
							else openq.push(n);
						}
					}
					if (!(y + 1 > 1999)) {
						if (!g_obstacles[y + 1][x]) {
							huri = ((goal_x - x) * (goal_x - x)) + ((goal_y - (y + 1)) * (goal_y - (y + 1)));
							NODE* n = new NODE(x, y + 1, huri, node->_g + 1, node);
							if (closelist[y + 1][x] || opencheck[y + 1][x]) delete n;
							else openq.push(n);
						}
					}
					if (!(x - 1 < 602)) {
						if (!g_obstacles[y][x - 1]) {
							huri = ((goal_x - (x - 1)) * (goal_x - (x - 1))) + ((goal_y - y) * (goal_y - y));
							NODE* n = new NODE(x - 1, y, huri, node->_g + 1, node);
							if (closelist[y][x - 1] || opencheck[y][x - 1]) delete n;
							else openq.push(n);
						}
					}
					if (!(x + 1 > 1395)) {
						if (!g_obstacles[y][x + 1]) {
							huri = ((goal_x - (x + 1)) * (goal_x - (x + 1))) + ((goal_y - y) * (goal_y - y));
							NODE* n = new NODE(x + 1, y, huri, node->_g + 1, node);
							if (closelist[y][x + 1] || opencheck[y][x + 1]) delete n;
							else openq.push(n);
						}
					}
				}
					  break;
				case 6: {
					if (!(y - 1 < 602)) {
						if (!g_obstacles[y - 1][x]) {
							huri = ((goal_x - x) * (goal_x - x)) + ((goal_y - (y - 1)) * (goal_y - (y - 1)));
							NODE* n = new NODE(x, y - 1, huri, node->_g + 1, node);
							if (closelist[y - 1][x] || opencheck[y - 1][x]) delete n;
							else openq.push(n);
						}
					}
					if (!(y + 1 > 1395)) {
						if (!g_obstacles[y + 1][x]) {
							huri = ((goal_x - x) * (goal_x - x)) + ((goal_y - (y + 1)) * (goal_y - (y + 1)));
							NODE* n = new NODE(x, y + 1, huri, node->_g + 1, node);
							if (closelist[y + 1][x] || opencheck[y + 1][x]) delete n;
							else openq.push(n);
						}
					}
					if (!(x - 1 < 0)) {
						if (!g_obstacles[y][x - 1]) {
							huri = ((goal_x - (x - 1)) * (goal_x - (x - 1))) + ((goal_y - y) * (goal_y - y));
							NODE* n = new NODE(x - 1, y, huri, node->_g + 1, node);
							if (closelist[y][x - 1] || opencheck[y][x - 1]) delete n;
							else openq.push(n);
						}
					}
					if (!(x + 1 > 597)) {
						if (!g_obstacles[y][x + 1]) {
							huri = ((goal_x - (x + 1)) * (goal_x - (x + 1))) + ((goal_y - y) * (goal_y - y));
							NODE* n = new NODE(x + 1, y, huri, node->_g + 1, node);
							if (closelist[y][x + 1] || opencheck[y][x + 1]) delete n;
							else openq.push(n);
						}
					}
				}
					  break;
				case 7: {
					if (!(y - 1 < 602)) {
						if (!g_obstacles[y - 1][x]) {
							huri = ((goal_x - x) * (goal_x - x)) + ((goal_y - (y - 1)) * (goal_y - (y - 1)));
							NODE* n = new NODE(x, y - 1, huri, node->_g + 1, node);
							if (closelist[y - 1][x] || opencheck[y - 1][x]) delete n;
							else openq.push(n);
						}
					}
					if (!(y + 1 > 1395)) {
						if (!g_obstacles[y + 1][x]) {
							huri = ((goal_x - x) * (goal_x - x)) + ((goal_y - (y + 1)) * (goal_y - (y + 1)));
							NODE* n = new NODE(x, y + 1, huri, node->_g + 1, node);
							if (closelist[y + 1][x] || opencheck[y + 1][x]) delete n;
							else openq.push(n);
						}
					}
					if (!(x - 1 < 1402)) {
						if (!g_obstacles[y][x - 1]) {
							huri = ((goal_x - (x - 1)) * (goal_x - (x - 1))) + ((goal_y - y) * (goal_y - y));
							NODE* n = new NODE(x - 1, y, huri, node->_g + 1, node);
							if (closelist[y][x - 1] || opencheck[y][x - 1]) delete n;
							else openq.push(n);
						}
					}
					if (!(x + 1 > 1999)) {
						if (!g_obstacles[y][x + 1]) {
							huri = ((goal_x - (x + 1)) * (goal_x - (x + 1))) + ((goal_y - y) * (goal_y - y));
							NODE* n = new NODE(x + 1, y, huri, node->_g + 1, node);
							if (closelist[y][x + 1] || opencheck[y][x + 1]) delete n;
							else openq.push(n);
						}
					}
				}
					  break;
				}
				opencheck[node->_y][node->_x] = false;
				closelist[node->_y][node->_x] = true;
				AllNode.push_back(node);
			}
		}
		_path_trace_term = t + std::chrono::seconds(3);

		if (_path.size() == 0) {// 탐색 실패
			if (_npc_type == NPC_AGRO_ROMING || _npc_type == NPC_PEACE_ROMING)
				return 2;
			else
				return 0;
		}
	}

	if (!_path.empty()) {
		auto& n = _path.top();
		_path.pop();

		std::unordered_set<long long> su_list;
		g_sl.lock();
		for (short i = _sector_coord[0] - 1; i <= _sector_coord[0] + 1; ++i) {
			if (i < 0 || i >= MAP_HEIGHT / SECTOR_SIZE) continue;
			for (short j = _sector_coord[1] - 1; j <= _sector_coord[1] + 1; ++j) {
				if (j < 0 || j >= MAP_WIDTH / SECTOR_SIZE) continue;
				for (long long id : g_sector[i][j])
					su_list.insert(id);
			}
		}
		g_sl.unlock();

		std::unordered_set<long long> old_vl;
		for (auto& pid : su_list) {
			std::shared_ptr<SESSION> p_ob = g_users.at(pid);
			if (p_ob == nullptr) continue;
			if (ST_INGAME != p_ob->_state) continue;
			if (false == is_pc(p_ob->_id)) continue;
			if (true == can_see(_x, _y, p_ob->_x, p_ob->_y))
				old_vl.insert(p_ob->_id);
		}


		_cl.lock();
		_x = n._x; _y = n._y;
		_cl.unlock();

		short sx = _y / SECTOR_SIZE;	// row
		short sy = _x / SECTOR_SIZE;	// col

		bool change_sector = false;
		if (_sector_coord[0] != sx || _sector_coord[1] != sy) {
			g_sl.lock();
			g_sector[_sector_coord[0]][_sector_coord[1]].erase(_id);
			g_sector[sx][sy].insert(_id);
			g_sl.unlock();
			_sector_coord[0] = sx;
			_sector_coord[1] = sy;
			change_sector = true;
		}

		if (change_sector) {
			su_list.clear();
			g_sl.lock();
			for (short i = _sector_coord[0] - 1; i <= _sector_coord[0] + 1; ++i) {
				if (i < 0 || i >= MAP_HEIGHT / SECTOR_SIZE) continue;
				for (short j = _sector_coord[1] - 1; j <= _sector_coord[1] + 1; ++j) {
					if (j < 0 || j >= MAP_WIDTH / SECTOR_SIZE) continue;
					for (long long id : g_sector[i][j])
						su_list.insert(id);
				}
			}
			g_sl.unlock();
		}


		std::unordered_set<long long> new_vl;
		for (auto& pid : su_list) {
			std::shared_ptr<SESSION> p_ob = g_users.at(pid);
			if (p_ob == nullptr) continue;
			if (ST_INGAME != p_ob->_state) continue;
			if (false == is_pc(p_ob->_id)) continue;
			if (true == can_see(_x, _y, p_ob->_x, p_ob->_y)) {
				new_vl.insert(p_ob->_id);
				if (_x == p_ob->_x && _y == p_ob->_y) {
					p_ob->_hp -= _attack;
					std::string syschat;
					syschat = "You " + std::to_string(_attack) + " damaged from " + _name;
					p_ob->do_send_chat_packet(syschat.data());
					p_ob->do_send_stat();
					if (p_ob->_hp <= 0) {
						p_ob->_hp = p_ob->_max_hp;
						p_ob->_exp /= 2;
						p_ob->_x = (rand() % 500) + 750;
						p_ob->_y = (rand() % 500) + 750;
						p_ob->do_send_move();
					}
				}
			}
		}

		if (new_vl.size() == 0)
			++_no_near;
		for (auto pl : new_vl) {
			_no_near = 0;
			std::shared_ptr<SESSION> p = g_users.at(pl);
			if (nullptr == p) continue;
			if (0 == old_vl.count(pl)) {
				// 플레이어의 시야에 등장
				p->do_send_player_enter(_id, 1);
			}
			else {
				// 플레이어가 계속 보고 있음.
				p->do_send_move(_id);
			}

		}

		for (auto pl : old_vl) {
			std::shared_ptr<SESSION> p = g_users.at(pl);
			if (nullptr == p) continue;
			if (0 == new_vl.count(pl)) {
				p->_vl.lock();
				if (0 != p->_view_list.count(_id)) {
					p->_vl.unlock();
					p->do_send_player_leave(_id);
				}
				else {
					p->_vl.unlock();
				}
			}
		}

		if (_no_near >= 6) {
			_alive = false;
			if (_npc_type == NPC_AGRO_ROMING || _npc_type == NPC_PEACE_ROMING)
				return 2;
			else
				return 0;
		}
		return 3;

	}
	if (_npc_type == NPC_AGRO_ROMING || _npc_type == NPC_PEACE_ROMING)
		return 2;
	else
		return 0;
}
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

		short sx = _y / SECTOR_SIZE;	// row
		short sy = _x / SECTOR_SIZE;	// col
		if (_sector_coord[0] != sx || _sector_coord[1] != sy) {
			g_sl.lock();
			switch (p->direction) {
			case MOVE_UP:
				g_sector[sx + 1][sy].erase(_id);
				g_sector[sx][sy].insert(_id);
				break;
			case MOVE_DOWN:
				g_sector[sx - 1][sy].erase(_id);
				g_sector[sx][sy].insert(_id);
				break;
			case MOVE_LEFT:
				g_sector[sx][sy + 1].erase(_id);
				g_sector[sx][sy].insert(_id);
				break;
			case MOVE_RIGHT:
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
				if (_x == cpl->_x && _y == cpl->_y) {
					_hp -= 10;
					do_send_stat();
				}
			}

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
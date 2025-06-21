#pragma once
#include "stdafx.h"

struct NODE {
	short _x;
	short _y;

	int _h;		// ÈÞ¸®½ºÆ½ °ª
	int _g;	

	NODE* _parent;
	NODE(short x, short y, int h, int g, NODE* parent)
		: _x(x), _y(y), _h(h), _g(g), _parent(parent)
	{
	}
	constexpr bool operator < (const NODE* _Left) const
	{
		return ( _h + _g > _Left->_h + _Left->_g);
	}
};

enum EVENT_TYPE { PL_HEAL, EV_NPC_AI };

struct event_type {
	long long obj_id;
	std::chrono::high_resolution_clock::time_point wakeup_time;
	EVENT_TYPE event_id;
	long long target_id;

	constexpr bool operator < (const event_type& _Left) const
	{
		return (wakeup_time > _Left.wakeup_time);
	}
};


enum COMP_TYPE { OP_ACCEPT, OP_RECV, OP_SEND, OP_PL_HEAL, OP_NPC_AI };
class EX_OVER {
public:
	EX_OVER()
	{
		_wsabuf.len = BUF_SIZE;
		_wsabuf.buf = _send_buf;
		_comp_type = OP_RECV;
		ZeroMemory(&_over, sizeof(_over));
	}

	EX_OVER(char* p)
	{
		unsigned char p0 = static_cast<unsigned char>(p[0]);
		_wsabuf.len = p0;
		_wsabuf.buf = _send_buf;
		ZeroMemory(&_over, sizeof(_over));
		_comp_type = OP_SEND;
		memcpy(_send_buf, p, p0);
	}

	WSAOVERLAPPED			_over;
	WSABUF					_wsabuf;
	char					_send_buf[BUF_SIZE]{};
	COMP_TYPE				_comp_type;
};

class SESSION {
public:
	SESSION() {};
	SESSION(long long id, SOCKET s);
	~SESSION();

	int getRemained() const { return _remained; }
	char* getName() { return _name; }
	short* getSCoord() { return _sector_coord; }
	std::mutex& getvl() { return _vl; }
	std::unordered_set<long long>& getviewlist() { return _view_list; }

	void setRemained(int remained) { _remained = remained; }

	void process_packet(char* packet);
	void do_recv();

	void do_send_avatar_info();
	void do_send_login_fail(short reason = 0);

	void do_send_player_enter(long long id, int type = 0);
	void do_send_player_leave(long long id);

	void do_send_chat_packet(char* str, long long id = -1);

	void do_send_stat();
	void do_heal_and_send();

	void do_send_move();
	void do_send_move(long long id);
	void do_send(void* packet);

	void disconnect();

	void wake_up(long long id = -1);
	int do_npc_move();
	int do_check_near_user();
	int do_npc_chase(long long id);
public:
	long long				_id{};
	SOCKET					_socket;

	EX_OVER					_ex_over;
	int						_remained{};

	char					_name[MAX_ID_LENGTH];

	std::mutex				_cl;		
	short					_x, _y;

	short					_ix, _iy;	// ÃÖÃÊ À§Ä¡

	std::atomic<short>		_hp;
	short					_level, _exp, _max_hp;
	short					_need_exp;
	short					_attack;

	std::mutex				_s_lock;
	S_STATE					_state;

	std::mutex						_vl;
	std::unordered_set<long long>	_view_list;

	short					_sector_coord[2] = { 0, 0 };

	std::chrono::high_resolution_clock::time_point _attack_term;
	std::chrono::high_resolution_clock::time_point _move_term;
	std::chrono::high_resolution_clock::time_point _revive_term;
	std::chrono::high_resolution_clock::time_point _path_trace_term;

	std::atomic<bool>								_alive = false;
	short											_npc_type;
	short											_in_section;
	
	std::mutex										_ll;
	lua_State*										_lua_machine;

	std::stack<NODE>								_path;
	short											_no_near{};

	unsigned	_move_time;
};


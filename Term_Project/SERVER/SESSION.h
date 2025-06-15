#pragma once
#include "stdafx.h"

enum EVENT_TYPE { PL_HEAL };

struct event_type {
	long long obj_id;
	std::chrono::high_resolution_clock::time_point wakeup_time;
	EVENT_TYPE event_id;
	int target_id;

	constexpr bool operator < (const event_type& _Left) const
	{
		return (wakeup_time > _Left.wakeup_time);
	}
};


enum COMP_TYPE { OP_ACCEPT, OP_RECV, OP_SEND, OP_PL_HEAL };
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


public:
	long long				_id{};
	SOCKET					_socket;

	EX_OVER					_ex_over;
	int						_remained{};

	char					_name[MAX_ID_LENGTH];
	short					_x, _y;

	std::atomic<short>		_hp;
	short					_level, _exp, _max_hp;
	short					_need_exp;
	short					_attack;

	std::mutex				_s_lock;
	S_STATE					_state;

	std::mutex						_vl;
	std::unordered_set<long long>	_view_list;

	short					_sector_coord[2] = { 0, 0 };
};


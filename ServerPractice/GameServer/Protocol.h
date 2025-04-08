#pragma once

constexpr char S2C_P_AVATAR_INFO = 1;
constexpr char S2C_P_MOVE = 2;
constexpr char S2C_P_ENTER = 3;
constexpr char S2C_P_LEAVE = 4;
constexpr char C2S_P_LOGIN = 5;
constexpr char C2S_P_MOVE = 6;

constexpr char MAX_ID_LENGTH = 20;

constexpr char MOVE_UP = 1;
constexpr char MOVE_DOWN = 2;
constexpr char MOVE_LEFT = 3;
constexpr char MOVE_RIGHT = 4;

constexpr unsigned short MAP_HEIGHT = 8;
constexpr unsigned short MAP_WIDTH = 8;

#pragma pack(push, 1)		// int가 4의 배수가 아니면 오류날 수 있음

struct sc_packet_avatar_info {
	unsigned char	size;
	char		type;
	long long	id;
	short		x, y;
	short		hp;
	short		level;
	int			exp;
};
struct sc_packet_move {
	unsigned char	size;
	char			type;
	long long		id;
	short			x, y;
};

struct sc_packet_enter {
	unsigned char	size;
	char			type;
	long long		id;
	char			name[MAX_ID_LENGTH];
	char			o_type;
	short			x, y;
};
struct sc_packet_leave {
	unsigned char	size;
	char			type;
	long long		id;
};

struct cs_packet_login {
	unsigned char	size;
	char	type;
	char	name[MAX_ID_LENGTH];
};
struct cs_packet_move {
	unsigned char	size;
	char	type;
	char	direction;
};

#pragma pack(pop)
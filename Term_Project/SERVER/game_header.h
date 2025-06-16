#pragma once

constexpr short GAME_PORT = 3000;
constexpr short BUF_SIZE = 1024;

constexpr short MAX_CHAT_LENGTH = 128;

constexpr int  MAX_USER = 100000;
constexpr int  NUM_MONSTER = 200000;

constexpr char S2C_P_AVATAR_INFO = 1;
constexpr char S2C_P_MOVE = 2;
constexpr char S2C_P_ENTER = 3;
constexpr char S2C_P_LEAVE = 4;
constexpr char C2S_P_LOGIN = 5;
constexpr char C2S_P_MOVE = 6;
constexpr char S2C_P_CHAT = 7;
constexpr char S2C_P_STAT_CHANGE = 8;
constexpr char S2C_P_LOGIN_FAIL = 9;
constexpr char C2S_P_ATTACK = 10;
constexpr char C2S_P_CHAT = 11;
constexpr char C2S_P_TELEPORT = 12;		// ���� �׽�Ʈ �� ��
										// ���۸����� HOTSPOT�� �����ϱ� ���� 
										// RANDOM TELEPORT�� �� ���
constexpr char C2S_P_WARP = 13;

constexpr char MAX_ID_LENGTH = 20;

constexpr char MOVE_UP = 1;
constexpr char MOVE_DOWN = 2;
constexpr char MOVE_LEFT = 3;
constexpr char MOVE_RIGHT = 4;

constexpr unsigned short MAP_HEIGHT = 2000;		//2000
constexpr unsigned short MAP_WIDTH = 2000;

#pragma pack (push, 1)

struct sc_packet_avatar_info {
	unsigned char size;
	char type;
	long long  id;
	short x, y;
	short max_hp;
	short hp;
	short level;
	int   exp;
};

struct sc_packet_move {
	unsigned char size;
	char type;
	long long id;
	short x, y;
	unsigned int move_time;
};

struct sc_packet_enter {
	unsigned char size;
	char type;
	long long  id;
	char name[MAX_ID_LENGTH];
	char o_type;						// 0 : PLAYER
										// 1...  : NPC��  
	short x, y;
};

struct sc_packet_leave {
	unsigned char size;
	char type;
	long long  id;
};

struct sc_packet_chat {
	unsigned char size;
	char type;
	long long  id;						// �޼����� ���� Object�� ID
										// -1 => SYSTEM MESSAGE
										//       ���� �޼��� ������ ���
	char message[MAX_CHAT_LENGTH];		// NULL terminated ���ڿ�
};

struct sc_packet_stat_change {
	unsigned char size;
	char type;
	long long  id;
	short max_hp;
	short hp;
	short level;
	int   exp;
};

struct sc_packet_login_fail {
	unsigned char size;
	char type;
	long long  id;
	char reason;			// 0 : �˼� ���� ����
							// 1 : �ٸ� Ŭ���̾�Ʈ���� �����
							// 2 : �������� ID (Ư������, 20�� �̻�)
							// 3 : ������ ������ �ʹ� ����
};

struct cs_packet_login {
	unsigned char  size;
	char  type;
	char  name[MAX_ID_LENGTH];
};

struct cs_packet_move {
	unsigned char  size;
	char  type;
	char  direction;

	unsigned move_time;
};

struct cs_packet_attack {
	unsigned char  size;
	char  type;
};

struct cs_packet_chat {
	unsigned char  size;
	char  type;
	char  message[MAX_CHAT_LENGTH];		// NULL terminated ���ڿ�
};

struct cs_packet_teleport {
	unsigned char  size;
	char  type;
};

struct cs_packet_warp {
	unsigned char size;
	char type;
	char zone;
};

#pragma pack (pop)


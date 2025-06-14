#include <SFML/Graphics.hpp>
#include <SFML/Network.hpp>
#include <iostream>
#include <unordered_map>
#include <Windows.h>
#include <chrono>
#include <string>
#include <list>
#include <array>
#include <fstream>
using namespace std;

#include "..\..\SERVER\game_header.h"

sf::TcpSocket s_socket;

constexpr auto SCREEN_WIDTH = 20;		// 16
constexpr auto SCREEN_HEIGHT = 20;

constexpr auto TILE_WIDTH = 65;			// 65
constexpr auto TILE_RECT_SIZE = 53;

constexpr auto WINDOW_WIDTH = SCREEN_WIDTH * TILE_RECT_SIZE;   // size of window
constexpr auto WINDOW_HEIGHT = SCREEN_WIDTH * TILE_RECT_SIZE;

int g_left_x;
int g_top_y;
int g_myid;

sf::RenderWindow* g_window;
sf::Font g_font;

sf::Text g_Systemmess;
chrono::system_clock::time_point g_sysmess_time;

sf::Text g_myChatMess;
sf::Text g_ChatModState;
bool g_chat;

char chatbuf[MAX_CHAT_LENGTH];
int chatlen = 0;

std::list<std::string> chatlog;
std::array<std::array<bool, MAP_WIDTH>, MAP_HEIGHT> g_obstacles;

sf::Text g_myStat;

class OBJECT {
private:
	bool m_showing;
	sf::Sprite m_sprite;

	sf::Text m_name;
	sf::Text m_chat;
	chrono::system_clock::time_point m_mess_end_time;
public:
	int id;
	int m_x, m_y;
	char name[MAX_ID_LENGTH];

	short max_hp{};
	short hp{};
	short level{};
	int exp{};
	int need_exp{};

	OBJECT(sf::Texture& t, int x, int y, int x2, int y2) {
		m_showing = false;
		m_sprite.setTexture(t);
		m_sprite.setTextureRect(sf::IntRect(x, y, x2, y2));
		float scale_size = static_cast<float>(TILE_RECT_SIZE) / static_cast<float>(x2);
		m_sprite.setScale(scale_size, scale_size);
		set_name("NONAME");
		m_mess_end_time = chrono::system_clock::now();
	}
	OBJECT() {
		m_showing = false;
	}
	void show()
	{
		m_showing = true;
	}
	void hide()
	{
		m_showing = false;
	}

	void a_move(int x, int y) {
		m_sprite.setPosition((float)x, (float)y);
	}

	void a_draw() {
		g_window->draw(m_sprite);
	}

	void move(int x, int y) {
		m_x = x;
		m_y = y;
	}
	void draw() {
		if (false == m_showing) return;
		float rx = (m_x - g_left_x) * TILE_RECT_SIZE + 1;
		float ry = (m_y - g_top_y) * TILE_RECT_SIZE + 1;
		m_sprite.setPosition(rx, ry);
		g_window->draw(m_sprite);
		auto size = m_name.getGlobalBounds();
		if (m_mess_end_time < chrono::system_clock::now()) {
			m_name.setPosition(rx + 32 - size.width / 2, ry - 10);
			g_window->draw(m_name);
		}
		else {
			m_chat.setPosition(rx + 32 - size.width / 2, ry - 10);
			g_window->draw(m_chat);
		}
	}
	void set_name(const char str[]) {
		m_name.setFont(g_font);
		m_name.setString(str);
		if (id < MAX_USER) m_name.setFillColor(sf::Color(255, 255, 255));
		else m_name.setFillColor(sf::Color(255, 255, 0));
		m_name.setStyle(sf::Text::Bold);
	}

	void set_chat(const char str[]) {
		m_chat.setFont(g_font);
		m_chat.setString(str);
		m_chat.setFillColor(sf::Color(255, 255, 255));
		m_chat.setStyle(sf::Text::Bold);
		m_mess_end_time = chrono::system_clock::now() + chrono::seconds(3);
	}
};

OBJECT avatar;
unordered_map <int, OBJECT> players;

OBJECT white_tile;
OBJECT black_tile;
OBJECT obstacle_tile;

sf::Texture* board;
sf::Texture* pieces;
sf::Texture* txtobstacle;

void client_initialize()
{
	board = new sf::Texture;
	pieces = new sf::Texture;
	txtobstacle = new sf::Texture;
	board->loadFromFile("chessmap.bmp");
	pieces->loadFromFile("chess2.png");
	txtobstacle->loadFromFile("brick.jpg");
	if (false == g_font.loadFromFile("cour.ttf")) {
		cout << "Font Loading Error!\n";
		exit(-1);
	}
	white_tile = OBJECT{ *board, 5, 5, TILE_WIDTH, TILE_WIDTH };
	black_tile = OBJECT{ *board, 69, 5, TILE_WIDTH, TILE_WIDTH };
	obstacle_tile = OBJECT{ *txtobstacle, 0, 0, 400, 400 };
	avatar = OBJECT{ *pieces, 128, 0, 64, 64 };
	avatar.move(4, 4);

	g_myChatMess.setFont(g_font);
	g_myChatMess.setFillColor(sf::Color(255, 255, 255));
	g_myChatMess.setStyle(sf::Text::Bold);

	g_Systemmess.setFont(g_font);
	g_Systemmess.setFillColor(sf::Color(255, 255, 255));
	g_Systemmess.setStyle(sf::Text::Bold);

	g_ChatModState.setFont(g_font);
	g_ChatModState.setString("Chat Mod On!");
	g_ChatModState.setFillColor(sf::Color(255, 255, 255));
	g_ChatModState.setStyle(sf::Text::Bold);

	g_myStat.setFont(g_font);
	g_myStat.setFillColor(sf::Color(255, 255, 255));
	g_myStat.setStyle(sf::Text::Bold);

	std::ifstream inFile{ "map.bin", std::ios::binary };
	for (int i = 0; i < MAP_WIDTH; ++i) {
		inFile.read(reinterpret_cast<char*>(g_obstacles[i].data()), 2000);
	}
}

void client_finish()
{
	players.clear();
	delete board;
	delete pieces;
	delete txtobstacle;
}

void ProcessPacket(char* ptr)
{
	static bool first_time = true;
	switch (ptr[1])
	{
	case S2C_P_AVATAR_INFO:
	{
		sc_packet_avatar_info * packet = reinterpret_cast<sc_packet_avatar_info*>(ptr);
		g_myid = packet->id;
		avatar.id = g_myid;
		avatar.max_hp = packet->max_hp;
		avatar.hp = packet->hp;
		avatar.level = packet->level;
		avatar.exp = packet->exp;
		avatar.need_exp = static_cast<int>(static_cast<float>(avatar.level) * 1.5);
		avatar.move(packet->x, packet->y);
		g_left_x = packet->x - SCREEN_WIDTH / 2;
		g_top_y = packet->y - SCREEN_HEIGHT / 2;
		avatar.show();
	}
	break;
	case S2C_P_LOGIN_FAIL:
	{
		sc_packet_login_fail* packet = reinterpret_cast<sc_packet_login_fail*>(ptr);
		switch (packet->reason) {
		case 0:
			std::cout << "알 수 없는 이유로 접속에 실패했습니다." << std::endl;
			break;
		case 1:
			std::cout << "이미 로그인 중입니다." << std::endl;
			break;
		case 2:
			std::cout << "부적절한 ID입니다." << std::endl;
			break;
		case 3:
			std::cout << "현재 동접자가 너무 많습니다." << std::endl;
			break;
		}
		char e;
		std::cin >> e;
		exit(0);
	}
	break;
	case S2C_P_ENTER:
	{
		sc_packet_enter* my_packet = reinterpret_cast<sc_packet_enter*>(ptr);
		int id = my_packet->id;

		if (id == g_myid) {
			avatar.move(my_packet->x, my_packet->y);
			g_left_x = my_packet->x - SCREEN_WIDTH / 2;
			g_top_y = my_packet->y - SCREEN_HEIGHT / 2;
			avatar.show();
		}
		else if (id < MAX_USER) {
			players[id] = OBJECT{ *pieces, 0, 0, 64, 64 };
			players[id].id = id;
			players[id].move(my_packet->x, my_packet->y);
			strcpy_s(players[id].name, my_packet->name);
			players[id].set_name(my_packet->name);
			players[id].show();
		}
		else {
			players[id] = OBJECT{ *pieces, 256, 0, 64, 64 };
			players[id].id = id;
			players[id].move(my_packet->x, my_packet->y);
			players[id].set_name(my_packet->name);
			players[id].show();
		}
		break;
	}
	case S2C_P_MOVE:
	{
		sc_packet_move* my_packet = reinterpret_cast<sc_packet_move*>(ptr);
		int other_id = my_packet->id;
		if (other_id == g_myid) {
			avatar.move(my_packet->x, my_packet->y);
			g_left_x = my_packet->x - SCREEN_WIDTH/2;
			g_top_y = my_packet->y - SCREEN_HEIGHT/2;
		}
		else {
			players[other_id].move(my_packet->x, my_packet->y);
		}
		break;
	}

	case S2C_P_LEAVE:
	{
		sc_packet_leave* my_packet = reinterpret_cast<sc_packet_leave*>(ptr);
		int other_id = my_packet->id;
		if (other_id == g_myid) {
			avatar.hide();
		}
		else {
			players.erase(other_id);
		}
		break;
	}
	case S2C_P_CHAT:
	{
		sc_packet_chat* my_packet = reinterpret_cast<sc_packet_chat*>(ptr);
		int other_id = my_packet->id;
		if (-1 != other_id) {
			if (other_id == g_myid) {
				avatar.set_chat(my_packet->message);
				std::string ch = avatar.name;
				ch = ch + ": " + my_packet->message;
				chatlog.push_front(ch);
			}
			else {
				players[other_id].set_chat(my_packet->message);
				std::string ch = players[other_id].name;
				ch = ch + ": " + my_packet->message;
				chatlog.push_front(ch);
			}
		}
		else
			chatlog.emplace_front(my_packet->message);

		while (chatlog.size() > 10) {
			chatlog.pop_back();
		}

		break;
	}
	default:
		printf("Unknown PACKET type [%d]\n", ptr[1]);
	}
}

void process_data(char* net_buf, size_t io_byte)
{
	char* ptr = net_buf;
	static size_t in_packet_size = 0;
	static size_t saved_packet_size = 0;
	static char packet_buffer[BUF_SIZE];

	while (0 != io_byte) {
		if (0 == in_packet_size) in_packet_size = static_cast<unsigned char>(ptr[0]);
		if (io_byte + saved_packet_size >= in_packet_size) {
			memcpy(packet_buffer + saved_packet_size, ptr, in_packet_size - saved_packet_size);
			ProcessPacket(packet_buffer);
			ptr += in_packet_size - saved_packet_size;
			io_byte -= in_packet_size - saved_packet_size;
			in_packet_size = 0;
			saved_packet_size = 0;
		}
		else {
			memcpy(packet_buffer + saved_packet_size, ptr, io_byte);
			saved_packet_size += io_byte;
			io_byte = 0;
		}
	}
}

void client_main()
{
	char net_buf[BUF_SIZE];
	size_t	received;

	auto recv_result = s_socket.receive(net_buf, BUF_SIZE, received);
	if (recv_result == sf::Socket::Error)
	{
		wcout << L"Recv 에러!";
		exit(-1);
	}
	if (recv_result == sf::Socket::Disconnected) {
		wcout << L"Disconnected\n";
		exit(-1);
	}
	if (recv_result != sf::Socket::NotReady)
		if (received > 0) process_data(net_buf, received);

	for (int i = 0; i < SCREEN_WIDTH; ++i)
		for (int j = 0; j < SCREEN_HEIGHT; ++j)
		{
			int tile_x = i + g_left_x;
			int tile_y = j + g_top_y;
			if ((tile_x < 0) || (tile_y < 0)) continue;
			if (g_obstacles[tile_y][tile_x]) {
				obstacle_tile.a_move(TILE_RECT_SIZE * i, TILE_RECT_SIZE * j);
				obstacle_tile.a_draw();
			}
			else {
				if (0 == (tile_x / 3 + tile_y / 3) % 2) {
					white_tile.a_move(TILE_RECT_SIZE * i, TILE_RECT_SIZE * j);
					white_tile.a_draw();
				}
				else
				{
					black_tile.a_move(TILE_RECT_SIZE * i, TILE_RECT_SIZE * j);
					black_tile.a_draw();
				}
			}
		}
	avatar.draw();
	for (auto& pl : players) pl.second.draw();
	sf::Text text;
	text.setFont(g_font);
	char buf[100];
	sprintf_s(buf, "(%d, %d)", avatar.m_x, avatar.m_y);
	text.setString(buf);
	g_window->draw(text);

	char statbuf[100];
	sprintf_s(statbuf, "HP: %d / %d\nLEVEL: %d\nEXP: %d / %d", avatar.hp, avatar.max_hp, avatar.level, avatar.exp, avatar.need_exp);
	g_myStat.setString(statbuf);
	g_myStat.setPosition(0, 50);
	g_window->draw(g_myStat);

	/*if (g_sysmess_time >= chrono::system_clock::now()) {
		g_Systemmess.setPosition(0, 50);
		g_window->draw(g_Systemmess);
	}*/
	if (g_chat) {
		g_myChatMess.setString(chatbuf);
		g_myChatMess.setPosition(0, 180);
		g_window->draw(g_myChatMess);

		g_ChatModState.setPosition(0, 150);
		g_window->draw(g_ChatModState);
	}

	int y = 210;
	for (auto& c : chatlog) {
		g_Systemmess.setString(c);
		g_Systemmess.setPosition(0, y);
		g_window->draw(g_Systemmess);
		y += 30;
	}
}

void send_packet(void *packet)
{
	unsigned char *p = reinterpret_cast<unsigned char *>(packet);
	size_t sent = 0;
	s_socket.send(packet, p[0], sent);
}

int main()
{
	wcout.imbue(locale("korean"));
	sf::Socket::Status status = s_socket.connect("127.0.0.1", GAME_PORT);
	s_socket.setBlocking(false);

	if (status != sf::Socket::Done) {
		wcout << L"서버와 연결할 수 없습니다.\n";
		exit(-1);
	}

	client_initialize();
	cs_packet_login p;
	p.size = sizeof(p);
	p.type = C2S_P_LOGIN;

	std::string playerName{};
	std::cout << "Input Your Name(ID): ";
	std::getline(std::cin, playerName);
	strcpy_s(avatar.name, playerName.data());
	avatar.set_name(avatar.name);
	strcpy_s(p.name, avatar.name);
	send_packet(&p);

	sf::RenderWindow window(sf::VideoMode(WINDOW_WIDTH, WINDOW_HEIGHT), "2D CLIENT");
	g_window = &window;

	while (window.isOpen())
	{
		sf::Event event;
		while (window.pollEvent(event))
		{
			if (event.type == sf::Event::Closed)
				window.close();

			if (event.type == sf::Event::TextEntered && g_chat) {
				char ch = static_cast<char>(event.text.unicode);
				if (event.text.unicode >= 32 && event.text.unicode <= 126) {
					if (chatlen < MAX_CHAT_LENGTH - 1) {
						chatbuf[chatlen++] = ch;
						chatbuf[chatlen] = '\0'; 
					}
				}
				// backspace
				else if (event.text.unicode == 8 && chatlen > 0) {
					chatbuf[--chatlen] = '\0';
				}
			}
			if (event.type == sf::Event::KeyPressed) {
				int direction = -1;
				switch (event.key.code) {
					if (!g_chat) {
				case sf::Keyboard::Left:
					direction = 2;
					break;
				case sf::Keyboard::Right:
					direction = 3;
					break;
				case sf::Keyboard::Up:
					direction = 0;
					break;
				case sf::Keyboard::Down:
					direction = 1;
					break;
				case sf::Keyboard::Num1:{
					cs_packet_warp p;
					p.size = sizeof(p);
					p.type = C2S_P_WARP;
					p.zone = 1;
					send_packet(&p);
				}
					break;
				case sf::Keyboard::Num2: {
					cs_packet_warp p;
					p.size = sizeof(p);
					p.type = C2S_P_WARP;
					p.zone = 2;
					send_packet(&p);
				}
									   break;
				case sf::Keyboard::Num3: {
					cs_packet_warp p;
					p.size = sizeof(p);
					p.type = C2S_P_WARP;
					p.zone = 3;
					send_packet(&p);
				}
									   break;
				case sf::Keyboard::Num4: {
					cs_packet_warp p;
					p.size = sizeof(p);
					p.type = C2S_P_WARP;
					p.zone = 4;
					send_packet(&p);
				}
									   break;
				case sf::Keyboard::Num9: {
					cs_packet_warp p;
					p.size = sizeof(p);
					p.type = C2S_P_WARP;
					p.zone = 0;
					send_packet(&p);
				}
									   break;
					}
				case sf::Keyboard::Escape:
					window.close();
					break;
				case sf::Keyboard::Return:
					if (g_chat && chatlen != 0) {
						cs_packet_chat p;
						p.size = sizeof(p);
						p.type = C2S_P_CHAT;
						strcpy_s(p.message, chatbuf);
						send_packet(&p);
						chatlen = 0;
						chatbuf[0] = '\0';
					}
					g_chat = !g_chat;
					break;
				}
				if (-1 != direction) {
					cs_packet_move p;
					p.size = sizeof(p);
					p.type = C2S_P_MOVE;
					p.direction = direction;
					send_packet(&p);
				}
			}
		}

		window.clear();
		client_main();
		window.display();
	}
	client_finish();

	return 0;
}
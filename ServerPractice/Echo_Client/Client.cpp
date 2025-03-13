#include <iostream>
#include <WS2tcpip.h>

#pragma comment(lib, "WS2_32")

constexpr short SERVER_PORT = 6487;
constexpr char SERVER_ADDR[] = "127.0.0.1";

int main()
{
	WSADATA WSAData{};
	auto res = WSAStartup(MAKEWORD(2, 0), &WSAData);

	SOCKET c_socket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, 0, 0, 0);
	SOCKADDR_IN addr{};
	addr.sin_family = AF_INET;
	addr.sin_port = htons(SERVER_PORT);
	inet_pton(AF_INET, SERVER_ADDR, &addr.sin_addr);
	WSAConnect(c_socket, reinterpret_cast<sockaddr*>(&addr), sizeof(SOCKADDR_IN), nullptr, nullptr, nullptr, nullptr);

	while (true) {
		char buffer[1024]{};
		std::cout << "Input : ";
		std::cin.getline(buffer, sizeof(buffer));

		WSABUF wsabuf[1]{};
		wsabuf[0].buf = buffer;
		wsabuf[0].len = static_cast<ULONG>(strlen(buffer));
		DWORD size_sent;
		WSASend(c_socket, wsabuf, 1, &size_sent, 0, nullptr, nullptr);

		char recv_buffer[1024]{};
		WSABUF recv_wsabuf[1]{};
		recv_wsabuf[0].buf = recv_buffer;
		recv_wsabuf[0].len = sizeof(recv_buffer);
		DWORD recv_bytes{};
		DWORD recv_flag{};
		WSARecv(c_socket, recv_wsabuf, 1, &recv_bytes, &recv_flag, nullptr, nullptr);
		recv_buffer[recv_bytes] = 0;

		std::cout << "From Server : " << recv_buffer << std::endl;
	}
	closesocket(c_socket);
	WSACleanup();
}
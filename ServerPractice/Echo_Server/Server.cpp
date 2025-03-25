#include <iostream>
#include <WS2tcpip.h>

#pragma comment(lib, "WS2_32")

constexpr short SERVER_PORT = 6487;

class SESSION {
	WSAOVERLAPPED recv_over;
	WSAOVERLAPPED recv_over;
	char recv_buffer[1024]{};
	WSABUF recv_wsabuf[1]{};

	char send_buffer[1024]{};
	WSABUF send_wsabuf[1]{};
	WSAOVERLAPPED send_over;

	SOCKET c_socket;
};

WSAOVERLAPPED recv_over;
char recv_buffer[1024]{};
WSABUF recv_wsabuf[1]{};

char send_buffer[1024]{};
WSABUF send_wsabuf[1]{};
WSAOVERLAPPED send_over;

SOCKET c_socket;

void error_display(const char* msg, int err_no)
{
	WCHAR* lpMsgBuf;
	FormatMessage(
		FORMAT_MESSAGE_ALLOCATE_BUFFER |
		FORMAT_MESSAGE_FROM_SYSTEM,
		NULL, err_no,
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		(LPTSTR)&lpMsgBuf, 0, NULL);
	std::cout << msg;
	std::wcout << L" ���� " << lpMsgBuf << std::endl;
	while (true); // ����� ��
	LocalFree(lpMsgBuf);
}

void CALLBACK recv_callback(DWORD, DWORD, LPWSAOVERLAPPED, DWORD);

void CALLBACK send_callback(DWORD err, DWORD num_bytes, LPWSAOVERLAPPED p_over, DWORD flag)
{
	recv_wsabuf[0].len = sizeof(recv_buffer);
	recv_wsabuf[0].buf = recv_buffer;
	DWORD recv_flag = 0;
	ZeroMemory(&recv_over, sizeof(recv_over));
	auto ret = WSARecv(c_socket, recv_wsabuf, 1, NULL, &recv_flag, &recv_over, recv_callback);
	if (0 != ret) {
		auto err_no = WSAGetLastError();
		if (WSA_IO_PENDING != err_no) {
			error_display("WSARecv : ", err_no);
			exit(-1);
		}
	}
}

void CALLBACK recv_callback(DWORD err, DWORD num_bytes, LPWSAOVERLAPPED p_over, DWORD flag)
{
	recv_buffer[num_bytes] = 0;
	std::cout << "From Client: " << recv_buffer << std::endl;

	
	memcpy(send_buffer, recv_buffer, num_bytes);


	send_wsabuf[0].buf = send_buffer;
	send_wsabuf[0].len = num_bytes;

	ZeroMemory(&send_over, sizeof(send_over));
	DWORD size_sent;
	WSASend(c_socket, send_wsabuf, 1, &size_sent, 0, &send_over, send_callback);
}

int main()
{
	std::wcout.imbue(std::locale("korean"));

	WSADATA WSAData{};
	auto res = WSAStartup(MAKEWORD(2, 0), &WSAData);

	SOCKET s_socket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, 0, 0, WSA_FLAG_OVERLAPPED);
	if (s_socket <= 0) std::cout << "ERROR" << "����\n";
	else std::cout << "Socket Created.\n";

	SOCKADDR_IN addr{};
	addr.sin_family = AF_INET;
	addr.sin_port = htons(SERVER_PORT);
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	bind(s_socket, reinterpret_cast<sockaddr*>(&addr), sizeof(SOCKADDR_IN));
	listen(s_socket, SOMAXCONN);
	INT addr_size = sizeof(SOCKADDR_IN);
	c_socket = WSAAccept(s_socket, reinterpret_cast<sockaddr*>(&addr), &addr_size,
		nullptr, NULL);

	send_callback(0, 0, 0, 0);
	while (true) SleepEx(0, TRUE);

		
	closesocket(c_socket);
	closesocket(s_socket);
	WSACleanup();
}
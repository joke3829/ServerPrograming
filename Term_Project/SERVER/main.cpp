#include "SERVER.h"

CServer g_GameServer;

int main()
{
	WSADATA WSAData;
	int ret = WSAStartup(MAKEWORD(2, 2), &WSAData);
	g_GameServer.SetUp();
	g_GameServer.Run();
	WSACleanup();
}
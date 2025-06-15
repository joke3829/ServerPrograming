#pragma once

#include <iostream>
#include <numeric>
#include <vector>
#include <array>
#include <unordered_set>
#include <unordered_map>
#include <string>
#include <fstream>
#include <queue>
#include <chrono>

#define NOMINMAX
#include <WS2tcpip.h>
#include <MSWSock.h>
#include <thread>
#include <mutex>
#include <concurrent_unordered_map.h>
#include <sqlext.h>

#include "game_header.h"

#pragma comment(lib, "WS2_32.lib")
#pragma comment(lib, "MSWSock.lib")

constexpr int VIEW_RANGE = 7;
constexpr int SECTOR_SIZE = 20;

constexpr short NPC_PEACE_FIX = 0;
constexpr short NPC_PEACE_ROMING = 1;
constexpr short NPC_AGRO_FIX = 2;
constexpr short NPC_AGRO_ROMING = 3;

enum S_STATE { ST_FREE, ST_ALLOC, ST_INGAME };

bool is_pc(long long id);

short getNPCType(long long id);
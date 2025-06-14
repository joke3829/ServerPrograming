#pragma once

#include <iostream>
#include <numeric>
#include <vector>
#include <array>
#include <unordered_set>
#include <unordered_map>
#include <string>
#include <fstream>

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

enum S_STATE { ST_FREE, ST_ALLOC, ST_INGAME };
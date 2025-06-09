#pragma once

#include <iostream>
#include <numeric>
#include <vector>

#define NOMINMAX
#include <WS2tcpip.h>
#include <MSWSock.h>
#include <thread>
#include <mutex>
#include <concurrent_unordered_map.h>

#include "game_header.h"

#pragma comment(lib, "WS2_32.lib")
#pragma comment(lib, "MSWSock.lib")
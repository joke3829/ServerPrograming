#include "stdafx.h"

bool is_pc(long long id)
{
	if (id < MAX_USER)
		return true;
	return false;
}

short getNPCType(long long id)
{
	if (id < MAX_USER + 80000)
		return NPC_PEACE_FIX;
	else if (id < MAX_USER + 120000)
		return NPC_PEACE_ROMING;
	else if (id < MAX_USER + 180000)
		return NPC_AGRO_FIX;
	else
		return NPC_AGRO_ROMING;
}

bool attack_check(short x1, short y1, short x2, short y2)
{
	if (x1 == x2 && y1 == y2)
		return true;
	if (x1 - 1 == x2 && y1 == y2)
		return true;
	if (x1 + 1 == x2 && y1 == y2)
		return true;
	if (x1 == x2 && y1 - 1 == y2)
		return true;
	if (x1 == x2 && y1 + 1 == y2)
		return true;

	return false;
}

bool can_see5(short x1, short y1, short x2, short y2)
{
	if (abs(x1 - x2) > 5) return false;
	return abs(y1 - y2) <= 5;
}

short section_where(short x, short y)
{
	if (x >= 0 && x <= 597) {
		if (y >= 0 && y <= 597)
			return 0;
		else if (y >= 602 && y <= 1395)
			return 6;
		else if (y >= 1402 && y <= 1999)
			return 3;
	}
	else if (x >= 602 && x <= 1395) {
		if (y >= 0 && y <= 597)
			return 4;
		else if (y >= 602 && y <= 1395)
			return 8;
		else if (y >= 1402 && y <= 1999)
			return 5;
	}
	else if (x >= 1402 && x <= 1999) {
		if (y >= 0 && y <= 597)
			return 2;
		else if (y >= 602 && y <= 1395)
			return 7;
		else if (y >= 1402 && y <= 1999)
			return 1;
	}
	return 8;
}
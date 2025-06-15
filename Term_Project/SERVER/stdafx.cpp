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
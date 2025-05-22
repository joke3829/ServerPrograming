#include <stdio.h>
#include "include/lua.hpp"

int main(void)
{
	int dx, dy;
	lua_State* L = luaL_newstate();  //��Ƹ�����.
	luaL_openlibs(L);
	//���ǥ�ض��̺귯��������.
	luaL_loadfile(L, "dragon.lua");
	lua_pcall(L, 0, 0, 0);
	lua_getglobal(L, "pos_x");		// ���ÿ� �ִ´�.
	lua_getglobal(L, "pos_y");
	dx = (int)lua_tonumber(L, -2);
	dy = (int)lua_tonumber(L, -1);
	printf("Rows %d, Cols %d\n", dx, dy);
	lua_pop(L, 2);
	lua_close(L);
	return 0;
}

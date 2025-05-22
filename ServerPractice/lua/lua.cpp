#include <stdio.h>
#include "include/lua.hpp"

int main(void)
{
	int dx, dy;
	lua_State* L = luaL_newstate();  //루아를연다.
	luaL_openlibs(L);
	//루아표준라이브러리를연다.
	luaL_loadfile(L, "dragon.lua");
	lua_pcall(L, 0, 0, 0);
	lua_getglobal(L, "pos_x");		// 스택에 넣는다.
	lua_getglobal(L, "pos_y");
	dx = (int)lua_tonumber(L, -2);
	dy = (int)lua_tonumber(L, -1);
	printf("Rows %d, Cols %d\n", dx, dy);
	lua_pop(L, 2);
	lua_close(L);
	return 0;
}

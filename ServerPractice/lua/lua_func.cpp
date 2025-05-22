#include <stdio.h>
#include "include/lua.hpp"

int main(void)
{
	int result;
	int error;
	lua_State* L = luaL_newstate();
	luaL_openlibs(L);
	error = luaL_loadfile(L, "dragon.lua");
	lua_pcall(L, 0, 0, 0);

	lua_getglobal(L, "plustwo");
	lua_pushnumber(L, 5);
	lua_pcall(L, 1, 1, 0);
	result = (int)lua_tonumber(L, -1);

	printf("result %d\n", result);
	lua_pop(L, 1);
	lua_close(L);
	return 0;
}

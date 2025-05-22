#include <stdio.h>
#include "include/lua.hpp"

int addnum_c(lua_State* L)
{
	// �Ű����� �б�
	int a = (int)lua_tonumber(L, -2);
	int b = (int)lua_tonumber(L, -1);
	lua_pop(L, 3);

	// ���� ����
	int result = a + b;
	// ����� ����
	lua_pushnumber(L, result);
	return 1;
}

int main(void)
{
	int result;
	int error;
	lua_State* L = luaL_newstate();
	luaL_openlibs(L);
	error = luaL_loadfile(L, "dragon.lua");
	lua_pcall(L, 0, 0, 0);
	lua_register(L, "c_addnum", addnum_c);

	lua_getglobal(L, "addnum_lua");
	lua_pushnumber(L, 10);
	lua_pushnumber(L, 20);
	lua_pcall(L, 2, 1, 0);
	result = (int)lua_tonumber(L, -1);

	printf("result %d\n", result);
	lua_pop(L, 1);
	lua_close(L);
	return 0;
}

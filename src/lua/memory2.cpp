#include "lua/internal.hpp"
#include "core/memorymanip.hpp"
#include "core/memorywatch.hpp"
#include "core/moviedata.hpp"
#include "core/moviefile.hpp"
#include "core/rom.hpp"
#include "library/sha256.hpp"
#include "library/string.hpp"
#include "library/minmax.hpp"

namespace
{
	int handle_push_vma(lua_state& L, memory_region& r)
	{
		L.newtable();
		L.pushstring("name");
		L.pushlstring(r.name.c_str(), r.name.size());
		L.settable(-3);
		L.pushstring("address");
		L.pushnumber(r.base);
		L.settable(-3);
		L.pushstring("size");
		L.pushnumber(r.size);
		L.settable(-3);
		L.pushstring("last");
		L.pushnumber(r.last_address());
		L.settable(-3);
		L.pushstring("readonly");
		L.pushboolean(r.readonly);
		L.settable(-3);
		L.pushstring("special");
		L.pushboolean(r.special);
		L.settable(-3);
		L.pushstring("endian");
		L.pushnumber(r.endian);
		L.settable(-3);
		return 1;
	}

	template<typename T> T bswap(T val)
	{
		char buf[sizeof(T)];
		memcpy(buf, &val, sizeof(T));
		for(size_t i = 0; i < sizeof(T) / 2; i++)
			std::swap(buf[i], buf[sizeof(T) - i - 1]);
		memcpy(&val, buf, sizeof(T));
		return val;
	}

	class lua_vma
	{
	public:
		lua_vma(lua_state* L, memory_region* r);
		int info(lua_state& L);
		template<class T, bool _bswap> int rw(lua_state& L);
	private:
		std::string vma;
		uint64_t vmabase;
		uint64_t vmasize;
		bool ro;
	};

	class lua_vma_list
	{
	public:
		lua_vma_list(lua_state* L);
		int index(lua_state& L);
		int newindex(lua_state& L);
		int call(lua_state& L);
	};
}

DECLARE_LUACLASS(lua_vma, "VMA");
DECLARE_LUACLASS(lua_vma_list, "VMALIST");

namespace
{
	lua_vma::lua_vma(lua_state* L, memory_region* r)
	{
		static char doonce_key;
		if(L->do_once(&doonce_key)) {
			objclass<lua_vma>().bind(*L, "info", &lua_vma::info);
			objclass<lua_vma>().bind(*L, "sbyte", &lua_vma::rw<int8_t, false>);
			objclass<lua_vma>().bind(*L, "byte", &lua_vma::rw<uint8_t, false>);
			objclass<lua_vma>().bind(*L, "sword", &lua_vma::rw<int16_t, false>);
			objclass<lua_vma>().bind(*L, "word", &lua_vma::rw<uint16_t, false>);
			objclass<lua_vma>().bind(*L, "sdword", &lua_vma::rw<int32_t, false>);
			objclass<lua_vma>().bind(*L, "dword", &lua_vma::rw<uint32_t, false>);
			objclass<lua_vma>().bind(*L, "sqword", &lua_vma::rw<int64_t, false>);
			objclass<lua_vma>().bind(*L, "qword", &lua_vma::rw<uint64_t, false>);
			objclass<lua_vma>().bind(*L, "isbyte", &lua_vma::rw<int8_t, true>);
			objclass<lua_vma>().bind(*L, "ibyte", &lua_vma::rw<uint8_t, true>);
			objclass<lua_vma>().bind(*L, "isword", &lua_vma::rw<int16_t, true>);
			objclass<lua_vma>().bind(*L, "iword", &lua_vma::rw<uint16_t, true>);
			objclass<lua_vma>().bind(*L, "isdword", &lua_vma::rw<int32_t, true>);
			objclass<lua_vma>().bind(*L, "idword", &lua_vma::rw<uint32_t, true>);
			objclass<lua_vma>().bind(*L, "isqword", &lua_vma::rw<int64_t, true>);
			objclass<lua_vma>().bind(*L, "iqword", &lua_vma::rw<uint64_t, true>);
		}
		vmabase = r->base;
		vmasize = r->size;
		vma = r->name;
		ro = r->readonly;
	}

	int lua_vma::info(lua_state& L)
	{
		for(auto i : lsnes_memory.get_regions())
			if(i->name == vma)
				return handle_push_vma(L, *i);
		throw std::runtime_error("VMA::info: Stale region");
	}

	template<class T, bool _bswap> int lua_vma::rw(lua_state& L)
	{
		uint64_t addr = L.get_numeric_argument<uint64_t>(2, "VMA::rw<T>");
		if(addr > vmasize || addr > vmasize - sizeof(T))
			throw std::runtime_error("VMA::rw<T>: Address outside VMA bounds");
		if(L.type(3) == LUA_TNIL || L.type(3) == LUA_TNONE) {
			//Read.
			T val = lsnes_memory.read<T>(addr + vmabase);
			if(_bswap) val = bswap(val);
			L.pushnumber(val);
			return 1;
		} else if(L.type(3) == LUA_TNUMBER) {
			//Write.
			if(ro)
				throw std::runtime_error("VMA::rw<T>: VMA is read-only");
			T val = L.get_numeric_argument<T>(3, "VMA::rw<T>");
			if(_bswap) val = bswap(val);
			lsnes_memory.write<T>(addr + vmabase, val);
			return 0;
		} else
			throw std::runtime_error("VMA::rw<T>: Parameter #3 must be integer if present");
	}

	lua_vma_list::lua_vma_list(lua_state* L)
	{
		static char doonce_key;
		if(L->do_once(&doonce_key)) {
			objclass<lua_vma_list>().bind(*L, "__index", &lua_vma_list::index, true);
			objclass<lua_vma_list>().bind(*L, "__newindex", &lua_vma_list::newindex, true);
			objclass<lua_vma_list>().bind(*L, "__call", &lua_vma_list::call);
		}
	}

	int lua_vma_list::call(lua_state& L)
	{
		L.newtable();
		size_t key = 1;
		for(auto i : lsnes_memory.get_regions()) {
			L.pushnumber(key++);
			L.pushlstring(i->name);
			L.rawset(-3);
		}
		return 1;
	}

	int lua_vma_list::index(lua_state& L)
	{
		std::string vma = L.get_string(2, "VMALIST::__index");
		auto l = lsnes_memory.get_regions();
		size_t j;
		std::list<memory_region*>::iterator i;
		for(i = l.begin(), j = 0; i != l.end(); i++, j++)
			if((*i)->name == vma) {
				lua_class<lua_vma>::create(L, &L, *i);
				return 1;
			}
		throw std::runtime_error("VMALIST::__index: No such VMA");
	}

	int lua_vma_list::newindex(lua_state& L)
	{
		throw std::runtime_error("Writing is not allowed");
	}

	function_ptr_luafun memory2(lua_func_misc, "memory2", [](lua_state& L, const std::string& fname) ->
		int {
		lua_class<lua_vma_list>::create(L, &L);
		return 1;
	});
}

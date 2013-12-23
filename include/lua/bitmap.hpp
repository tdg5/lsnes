#ifndef _lua__bitmap__hpp__included__
#define _lua__bitmap__hpp__included__

#include <vector>
#include <string>
#include <cstdint>
#include "core/window.hpp"
#include "library/lua-base.hpp"
#include "library/framebuffer.hpp"
#include "library/threadtypes.hpp"
#include "library/string.hpp"

struct lua_palette
{
	std::vector<framebuffer::color> colors;
	lua_palette(lua::state& L);
	~lua_palette();
	mutex_class palette_mutex;
	std::string print();
	int set(lua::state& L, const std::string& fname);
	int hash(lua::state& L, const std::string& fname);
	int debug(lua::state& L, const std::string& fname);
	int adjust_transparency(lua::state& L, const std::string& fname);
};

struct lua_bitmap
{
	lua_bitmap(lua::state& L, uint32_t w, uint32_t h);
	~lua_bitmap();
	size_t width;
	size_t height;
	std::vector<uint16_t> pixels;
	std::vector<char> save_png(const lua_palette& pal) const;
	std::string print();
	int draw(lua::state& L, const std::string& fname);
	int _draw(lua::state& L, const std::string& fname, bool is_method);
	int pset(lua::state& L, const std::string& fname);
	int pget(lua::state& L, const std::string& fname);
	int size(lua::state& L, const std::string& fname);
	int hash(lua::state& L, const std::string& fname);
	int blit(lua::state& L, const std::string& fname);
	int save_png(lua::state& L, const std::string& fname);
	int _save_png(lua::state& L, const std::string& fname, bool is_method);
};

struct lua_dbitmap
{
	lua_dbitmap(lua::state& L, uint32_t w, uint32_t h);
	~lua_dbitmap();
	size_t width;
	size_t height;
	std::vector<framebuffer::color> pixels;
	std::vector<char> save_png() const;
	std::string print();
	int draw(lua::state& L, const std::string& fname);
	int _draw(lua::state& L, const std::string& fname, bool is_method);
	int pset(lua::state& L, const std::string& fname);
	int pget(lua::state& L, const std::string& fname);
	int size(lua::state& L, const std::string& fname);
	int hash(lua::state& L, const std::string& fname);
	int blit(lua::state& L, const std::string& fname);
	int save_png(lua::state& L, const std::string& fname);
	int adjust_transparency(lua::state& L, const std::string& fname);
	int _save_png(lua::state& L, const std::string& fname, bool is_method);
};

struct lua_loaded_bitmap
{
	size_t w;
	size_t h;
	bool d;
	std::vector<int64_t> bitmap;
	std::vector<int64_t> palette;
	static struct lua_loaded_bitmap load(std::istream& stream);
	static struct lua_loaded_bitmap load(const std::string& name);
};


#endif

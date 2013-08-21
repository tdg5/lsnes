#include "lua/internal.hpp"
#include "core/framebuffer.hpp"
#include "library/framebuffer.hpp"
#include "library/png-decoder.hpp"
#include "library/string.hpp"
#include "library/zip.hpp"
#include "lua/bitmap.hpp"
#include "library/threadtypes.hpp"
#include <vector>
#include <sstream>

lua_bitmap::lua_bitmap(uint32_t w, uint32_t h)
{
	width = w;
	height = h;
	pixels.resize(width * height);
	memset(&pixels[0], 0, width * height);
}

lua_bitmap::~lua_bitmap()
{
	render_kill_request(this);
}

lua_dbitmap::lua_dbitmap(uint32_t w, uint32_t h)
{
	width = w;
	height = h;
	pixels.resize(width * height);
}

lua_dbitmap::~lua_dbitmap()
{
	render_kill_request(this);
}

lua_palette::lua_palette()
{
}

lua_palette::~lua_palette()
{
}

namespace
{
	struct render_object_bitmap : public render_object
	{
		render_object_bitmap(int32_t _x, int32_t _y, lua_obj_pin<lua_bitmap> _bitmap,
			lua_obj_pin<lua_palette> _palette) throw()
		{
			x = _x;
			y = _y;
			b = _bitmap;
			p = _palette;
		}

		render_object_bitmap(int32_t _x, int32_t _y, lua_obj_pin<lua_dbitmap> _bitmap) throw()
		{
			x = _x;
			y = _y;
			b2 = _bitmap;
		}

		~render_object_bitmap() throw()
		{
		}

		bool kill_request(void* obj) throw()
		{
				return kill_request_ifeq(p.object(), obj) ||
				kill_request_ifeq(b.object(), obj) ||
				kill_request_ifeq(b2.object(), obj);
		}

		template<bool T> void composite_op(struct framebuffer<T>& scr) throw()
		{
			if(p)
				p->palette_mutex.lock();
			uint32_t originx = scr.get_origin_x();
			uint32_t originy = scr.get_origin_y();
			size_t pallim = 0;
			size_t w, h;
			premultiplied_color* palette;
			if(b) {
				palette = &p->colors[0];
				for(auto& c : p->colors)
					c.set_palette(scr);
				pallim = p->colors.size();
				w = b->width;
				h = b->height;
			} else {
				for(auto& c : b2->pixels)
					c.set_palette(scr);
				w = b2->width;
				h = b2->height;
			}

			int32_t xmin = 0;
			int32_t xmax = w;
			int32_t ymin = 0;
			int32_t ymax = h;
			clip_range(originx, scr.get_width(), x, xmin, xmax);
			clip_range(originy, scr.get_height(), y, ymin, ymax);
			for(int32_t r = ymin; r < ymax; r++) {
				typename framebuffer<T>::element_t* rptr = scr.rowptr(y + r + originy);
				size_t eptr = x + xmin + originx;
				if(b)
					for(int32_t c = xmin; c < xmax; c++, eptr++) {
						uint16_t i = b->pixels[r * b->width + c];
						if(i < pallim)
							palette[i].apply(rptr[eptr]);
					}
				else
					for(int32_t c = xmin; c < xmax; c++, eptr++)
						b2->pixels[r * b2->width + c].apply(rptr[eptr]);
			}
			if(p)
				p->palette_mutex.unlock();
		}
		void operator()(struct framebuffer<false>& x) throw() { composite_op(x); }
		void operator()(struct framebuffer<true>& x) throw() { composite_op(x); }
		void clone(render_queue& q) const throw(std::bad_alloc) { q.clone_helper(this); }
	private:
		int32_t x;
		int32_t y;
		lua_obj_pin<lua_bitmap> b;
		lua_obj_pin<lua_dbitmap> b2;
		lua_obj_pin<lua_palette> p;
	};

	function_ptr_luafun gui_bitmap(lua_func_misc, "gui.bitmap_draw", [](lua_state& L, const std::string& fname)
		-> int {
		if(!lua_render_ctx)
			return 0;
		int32_t x = L.get_numeric_argument<int32_t>(1, fname.c_str());
		int32_t y = L.get_numeric_argument<int32_t>(2, fname.c_str());
		if(lua_class<lua_bitmap>::is(L, 3)) {
			lua_class<lua_bitmap>::get(L, 3, fname.c_str());
			lua_class<lua_palette>::get(L, 4, fname.c_str());
			auto b = lua_class<lua_bitmap>::pin(L, 3, fname.c_str());
			auto p = lua_class<lua_palette>::pin(L, 4, fname.c_str());
			lua_render_ctx->queue->create_add<render_object_bitmap>(x, y, b, p);
		} else if(lua_class<lua_dbitmap>::is(L, 3)) {
			lua_class<lua_dbitmap>::get(L, 3, fname.c_str());
			auto b = lua_class<lua_dbitmap>::pin(L, 3, fname.c_str());
			lua_render_ctx->queue->create_add<render_object_bitmap>(x, y, b);
		} else {
			L.pushstring("Expected BITMAP or DBITMAP as argument 3 for gui.bitmap_draw.");
			L.error();
		}
		return 0;
	});

	function_ptr_luafun gui_cpalette(lua_func_misc, "gui.palette_new", [](lua_state& L, const std::string& fname)
		-> int {
		lua_class<lua_palette>::create(L);
		return 1;
	});

	function_ptr_luafun gui_cbitmap(lua_func_misc, "gui.bitmap_new", [](lua_state& L, const std::string& fname)
		-> int {
		uint32_t w = L.get_numeric_argument<uint32_t>(1, fname.c_str());
		uint32_t h = L.get_numeric_argument<uint32_t>(2, fname.c_str());
		bool d = L.get_bool(3, fname.c_str());
		if(d) {
			int64_t c = -1;
			L.get_numeric_argument<int64_t>(4, c, fname.c_str());
			lua_dbitmap* b = lua_class<lua_dbitmap>::create(L, w, h);
			for(size_t i = 0; i < b->width * b->height; i++)
				b->pixels[i] = premultiplied_color(c);
		} else {
			uint16_t c = 0;
			L.get_numeric_argument<uint16_t>(4, c, fname.c_str());
			lua_bitmap* b = lua_class<lua_bitmap>::create(L, w, h);
			for(size_t i = 0; i < b->width * b->height; i++)
				b->pixels[i] = c;
		}
		return 1;
	});

	function_ptr_luafun gui_epalette(lua_func_misc, "gui.palette_set", [](lua_state& L, const std::string& fname)
		-> int {
		lua_palette* p = lua_class<lua_palette>::get(L, 1, fname.c_str());
		uint16_t c = L.get_numeric_argument<uint16_t>(2, fname.c_str());
		int64_t nval = L.get_numeric_argument<int64_t>(3, fname.c_str());
		premultiplied_color nc(nval);
		//The mutex lock protects only the internals of colors array.
		if(p->colors.size() <= c) {
			p->palette_mutex.lock();
			p->colors.resize(static_cast<uint32_t>(c) + 1);
			p->palette_mutex.unlock();
		}
		p->colors[c] = nc;
		return 0;
	});

	function_ptr_luafun pset_bitmap(lua_func_misc, "gui.bitmap_pset", [](lua_state& L, const std::string& fname)
		-> int {
		uint32_t x = L.get_numeric_argument<uint32_t>(2, fname.c_str());
		uint32_t y = L.get_numeric_argument<uint32_t>(3, fname.c_str());
		if(lua_class<lua_bitmap>::is(L, 1)) {
			lua_bitmap* b = lua_class<lua_bitmap>::get(L, 1, fname.c_str());
			uint16_t c = L.get_numeric_argument<uint16_t>(4, fname.c_str());
			if(x >= b->width || y >= b->height)
				return 0;
			b->pixels[y * b->width + x] = c;
		} else if(lua_class<lua_dbitmap>::is(L, 1)) {
			lua_dbitmap* b = lua_class<lua_dbitmap>::get(L, 1, fname.c_str());
			int64_t c = L.get_numeric_argument<int64_t>(4, fname.c_str());
			if(x >= b->width || y >= b->height)
				return 0;
			b->pixels[y * b->width + x] = premultiplied_color(c);
		} else {
			L.pushstring("Expected BITMAP or DBITMAP as argument 1 for gui.bitmap_pset.");
			L.error();
		}
		return 0;
	});

	inline int64_t demultiply_color(const premultiplied_color& c)
	{
		if(!c.origa)
			return -1;
		else
			return c.orig | ((uint32_t)(256 - c.origa) << 24);
	}

	function_ptr_luafun pget_bitmap(lua_func_misc, "gui.bitmap_pget", [](lua_state& L, const std::string& fname)
		-> int {
		uint32_t x = L.get_numeric_argument<uint32_t>(2, fname.c_str());
		uint32_t y = L.get_numeric_argument<uint32_t>(3, fname.c_str());
		if(lua_class<lua_bitmap>::is(L, 1)) {
			lua_bitmap* b = lua_class<lua_bitmap>::get(L, 1, fname.c_str());
			if(x >= b->width || y >= b->height)
				return 0;
			L.pushnumber(b->pixels[y * b->width + x]);
		} else if(lua_class<lua_dbitmap>::is(L, 1)) {
			lua_dbitmap* b = lua_class<lua_dbitmap>::get(L, 1, fname.c_str());
			if(x >= b->width || y >= b->height)
				return 0;
			L.pushnumber(demultiply_color(b->pixels[y * b->width + x]));
		} else {
			L.pushstring("Expected BITMAP or DBITMAP as argument 1 for gui.bitmap_pget.");
			L.error();
		}
		return 1;
	});

	function_ptr_luafun size_bitmap(lua_func_misc, "gui.bitmap_size", [](lua_state& L, const std::string& fname)
		-> int {
		if(lua_class<lua_bitmap>::is(L, 1)) {
			lua_bitmap* b = lua_class<lua_bitmap>::get(L, 1, fname.c_str());
			L.pushnumber(b->width);
			L.pushnumber(b->height);
		} else if(lua_class<lua_dbitmap>::is(L, 1)) {
			lua_dbitmap* b = lua_class<lua_dbitmap>::get(L, 1, fname.c_str());
			L.pushnumber(b->width);
			L.pushnumber(b->height);
		} else {
			L.pushstring("Expected BITMAP or DBITMAP as argument 1 for gui.bitmap_size.");
			L.error();
		}
		return 2;
	});

	function_ptr_luafun blit_bitmap(lua_func_misc, "gui.bitmap_blit", [](lua_state& L, const std::string& fname)
		-> int {
		uint32_t dx = L.get_numeric_argument<uint32_t>(2, fname.c_str());
		uint32_t dy = L.get_numeric_argument<uint32_t>(3, fname.c_str());
		uint32_t sx = L.get_numeric_argument<uint32_t>(5, fname.c_str());
		uint32_t sy = L.get_numeric_argument<uint32_t>(6, fname.c_str());
		uint32_t w = L.get_numeric_argument<uint32_t>(7, fname.c_str());
		uint32_t h = L.get_numeric_argument<uint32_t>(8, fname.c_str());
		int64_t ck = 0x100000000ULL;
		L.get_numeric_argument<int64_t>(9, ck, fname.c_str());
		bool nck = false;
		premultiplied_color pck(ck);
		uint32_t ckorig = pck.orig;
		uint16_t ckoriga = pck.origa;
		if(ck == 0x100000000ULL)
			nck = true;
		if(lua_class<lua_bitmap>::is(L, 1) && lua_class<lua_bitmap>::is(L, 4)) {
			lua_bitmap* db = lua_class<lua_bitmap>::get(L, 1, fname.c_str());
			lua_bitmap* sb = lua_class<lua_bitmap>::get(L, 4, fname.c_str());
			while((dx + w > db->width || sx + w > sb->width) && w > 0)
				w--;
			while((dy + h > db->height || sy + h > sb->height) && h > 0)
				h--;
			size_t sidx = sy * sb->width + sx;
			size_t didx = dy * db->width + dx;
			size_t srskip = sb->width - w;
			size_t drskip = db->width - w;
			for(uint32_t j = 0; j < h; j++) {
				for(uint32_t i = 0; i < w; i++) {
					uint16_t pix = sb->pixels[sidx];
					if(pix != ck)	//No need to check nck, as that value is out of range.
						db->pixels[didx] = pix;
					sidx++;
					didx++;
				}
				sidx += srskip;
				didx += drskip;
			}
		} else if(lua_class<lua_dbitmap>::is(L, 1) && lua_class<lua_dbitmap>::is(L, 1)) {
			lua_dbitmap* db = lua_class<lua_dbitmap>::get(L, 1, fname.c_str());
			lua_dbitmap* sb = lua_class<lua_dbitmap>::get(L, 4, fname.c_str());
			while((dx + w > db->width || sx + w > sb->width) && w > 0)
				w--;
			while((dy + h > db->height || sy + h > sb->height) && h > 0)
				h--;
			size_t sidx = sy * sb->width + sx;
			size_t didx = dy * db->width + dx;
			size_t srskip = sb->width - w;
			size_t drskip = db->width - w;
			for(uint32_t j = 0; j < h; j++) {
				for(uint32_t i = 0; i < w; i++) {
					premultiplied_color pix = sb->pixels[sidx];
					if(pix.orig != ckorig || pix.origa != ckoriga || nck)
						db->pixels[didx] = pix;
					sidx++;
					didx++;
				}
				sidx += srskip;
				didx += drskip;
			}
		} else {
			L.pushstring("Expected BITMAP or DBITMAP as arguments 1&4 for gui.bitmap_pset.");
			L.error();
		}
		return 0;
	});

	int bitmap_load_fn(lua_state& L, std::function<lua_loaded_bitmap()> src)
	{
		uint32_t w, h;
		auto bitmap = src();
		if(bitmap.d) {
			lua_dbitmap* b = lua_class<lua_dbitmap>::create(L, bitmap.w, bitmap.h);
			for(size_t i = 0; i < bitmap.w * bitmap.h; i++)
				b->pixels[i] = premultiplied_color(bitmap.bitmap[i]);
			return 1;
		} else {
			lua_bitmap* b = lua_class<lua_bitmap>::create(L, bitmap.w, bitmap.h);
			lua_palette* p = lua_class<lua_palette>::create(L);
			for(size_t i = 0; i < bitmap.w * bitmap.h; i++)
				b->pixels[i] = bitmap.bitmap[i];
			p->colors.resize(bitmap.palette.size());
			for(size_t i = 0; i < bitmap.palette.size(); i++)
				p->colors[i] = premultiplied_color(bitmap.palette[i]);
			return 2;
		}
	}

	function_ptr_luafun gui_loadbitmap(lua_func_misc, "gui.bitmap_load", [](lua_state& L,
		const std::string& fname) -> int {
		std::string name2;
		std::string name = L.get_string(1, fname.c_str());
		if(L.type(2) != LUA_TNIL && L.type(2) != LUA_TNONE)
			name2 = L.get_string(2, fname.c_str());
		return bitmap_load_fn(L, [&name, &name2]() -> lua_loaded_bitmap {
			std::string name3 = resolve_file_relative(name, name2);
			return lua_loaded_bitmap::load(name3);
		});
	});

	function_ptr_luafun gui_loadbitmap2(lua_func_misc, "gui.bitmap_load_str", [](lua_state& L,
		const std::string& fname) -> int {
		std::string contents = L.get_string(1, fname.c_str());
		return bitmap_load_fn(L, [&contents]() -> lua_loaded_bitmap {
			std::istringstream strm(contents);
			return lua_loaded_bitmap::load(strm);
		});
	});

	inline int64_t mangle_color(uint32_t c)
	{
		if(c < 0x1000000)
			return -1;
		else
			return ((256 - (c >> 24) - (c >> 31)) << 24) | (c & 0xFFFFFF);
	}

	int base64val(char ch)
	{
		if(ch >= 'A' && ch <= 'Z')
			return ch - 65;
		if(ch >= 'a' && ch <= 'z')
			return ch - 97 + 26;
		if(ch >= '0' && ch <= '9')
			return ch - 48 + 52;
		if(ch == '+')
			return 62;
		if(ch == '/')
			return 63;
		if(ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n')
			return -1;
		if(ch == '=')
			return -2;
		return -3;
	}

	std::string base64_decode(const std::string& str)
	{
		bool end = 0;
		uint32_t memory = 0;
		uint32_t memsize = 1;
		int posmod = 0;
		std::ostringstream x;
		for(auto i : str) {
			int v = base64val(i);
			if(v == -1)
				continue;
			posmod = (posmod + 1) & 3;
			if(v == -2 && (posmod == 1 || posmod == 2))
				throw std::runtime_error("Invalid Base64");
			if(v == -2) {
				end = true;
				continue;
			}
			if(v == -3 || end)
				throw std::runtime_error("Invalid Base64");
			memory = memory * 64 + v;
			memsize = memsize * 64;
			if(memsize >= 256) {
				memsize >>= 8;
				x << static_cast<uint8_t>(memory / memsize);
				memory %= memsize;
			}
		}
		return x.str();
	}

	int bitmap_load_png_fn(lua_state& L, std::function<void(png_decoded_image&)> src)
	{
		png_decoded_image img;
		src(img);
		if(img.has_palette) {
			lua_bitmap* b = lua_class<lua_bitmap>::create(L, img.width, img.height);
			lua_palette* p = lua_class<lua_palette>::create(L);
			for(size_t i = 0; i < img.width * img.height; i++)
				b->pixels[i] = img.data[i];
			p->colors.resize(img.palette.size());
			for(size_t i = 0; i < img.palette.size(); i++)
				p->colors[i] = premultiplied_color(mangle_color(img.palette[i]));
			return 2;
		} else {
			lua_dbitmap* b = lua_class<lua_dbitmap>::create(L, img.width, img.height);
			for(size_t i = 0; i < img.width * img.height; i++)
				b->pixels[i] = premultiplied_color(mangle_color(img.data[i]));
			return 1;
		}
	}

	function_ptr_luafun gui_loadbitmappng(lua_func_misc, "gui.bitmap_load_png", [](lua_state& L,
		const std::string& fname) -> int {
		std::string name2;
		std::string name = L.get_string(1, fname.c_str());
		if(L.type(2) != LUA_TNIL && L.type(2) != LUA_TNONE)
			name2 = L.get_string(2, fname.c_str());
		return bitmap_load_png_fn(L, [&name, &name2](png_decoded_image& img) {
			std::string name3 = resolve_file_relative(name, name2);
			decode_png(name3, img);
		});
	});

	function_ptr_luafun gui_loadbitmappng2(lua_func_misc, "gui.bitmap_load_png_str", [](lua_state& L,
		const std::string& fname) -> int {
		std::string contents = base64_decode(L.get_string(1, fname.c_str()));
		return bitmap_load_png_fn(L, [&contents](png_decoded_image& img) {
			std::istringstream strm(contents);
			decode_png(strm, img);
		});
	});

	int bitmap_palette_fn(lua_state& L, std::istream& s)
	{
		lua_palette* p = lua_class<lua_palette>::create(L);
		while(s) {
			std::string line;
			std::getline(s, line);
			istrip_CR(line);
			regex_results r;
			if(r = regex("[ \t]*([0-9]+)[ \t]+([0-9]+)[ \t]+([0-9]+)[ \t]+([0-9]+)[ \t]*", line)) {
				int64_t cr, cg, cb, ca;
				cr = parse_value<uint8_t>(r[1]);
				cg = parse_value<uint8_t>(r[2]);
				cb = parse_value<uint8_t>(r[3]);
				ca = 256 - parse_value<uint16_t>(r[4]);
				int64_t clr;
				if(ca == 256)
					p->colors.push_back(premultiplied_color(-1));
				else
					p->colors.push_back(premultiplied_color((ca << 24) | (cr << 16)
						| (cg << 8) | cb));
			} else if(r = regex("[ \t]*([0-9]+)[ \t]+([0-9]+)[ \t]+([0-9]+)[ \t]*", line)) {
				int64_t cr, cg, cb;
				cr = parse_value<uint8_t>(r[1]);
				cg = parse_value<uint8_t>(r[2]);
				cb = parse_value<uint8_t>(r[3]);
				p->colors.push_back(premultiplied_color((cr << 16) | (cg << 8) | cb));
			} else if(regex_match("[ \t]*transparent[ \t]*", line)) {
				p->colors.push_back(premultiplied_color(-1));
			} else if(!regex_match("[ \t]*(#.*)?", line))
				throw std::runtime_error("Invalid line format (" + line + ")");
		}
		return 1;
	}

	function_ptr_luafun gui_loadpalette(lua_func_misc, "gui.bitmap_load_pal", [](lua_state& L,
		const std::string& fname) -> int {
		std::string name2;
		std::string name = L.get_string(1, fname.c_str());
		if(L.type(2) != LUA_TNIL && L.type(2) != LUA_TNONE)
			name2 = L.get_string(2, fname.c_str());
		std::istream& s = open_file_relative(name, name2);
		try {
			int r = bitmap_palette_fn(L, s);
			delete &s;
			return r;
		} catch(...) {
			delete &s;
			throw;
		}
	});

	function_ptr_luafun gui_loadpalette2(lua_func_misc, "gui.bitmap_load_pal_str", [](lua_state& L,
		const std::string& fname) -> int {
		std::string content = L.get_string(1, fname.c_str());
		std::istringstream s(content);
		return bitmap_palette_fn(L, s);
	});

	function_ptr_luafun gui_dpalette(lua_func_misc, "gui.palette_debug", [](lua_state& L,
		const std::string& fname) -> int {
		lua_palette* p = lua_class<lua_palette>::get(L, 1, fname.c_str());
		size_t i = 0;
		for(auto c : p->colors)
			messages << "Color #" << (i++) << ": " << c.orig << ":" << c.origa << std::endl;
		return 0;
	});

	inline premultiplied_color tadjust(premultiplied_color c, uint16_t adj)
	{
		uint32_t rgb = c.orig;
		uint32_t a = c.origa;
		a = (a * adj) >> 8;
		if(a > 256)
			a = 256;
		if(a == 0)
			return premultiplied_color(-1);
		else
			return premultiplied_color(rgb | ((uint32_t)(256 - a) << 24));
	}

	function_ptr_luafun adjust_trans(lua_func_misc, "gui.adjust_transparency", [](lua_state& L,
		const std::string& fname) -> int {
		uint16_t tadj = L.get_numeric_argument<uint16_t>(2, fname.c_str());
		if(lua_class<lua_dbitmap>::is(L, 1)) {
			lua_dbitmap* b = lua_class<lua_dbitmap>::get(L, 1, fname.c_str());
			for(auto& c : b->pixels)
				c = tadjust(c, tadj);
		} else if(lua_class<lua_palette>::is(L, 1)) {
			lua_palette* p = lua_class<lua_palette>::get(L, 1, fname.c_str());
			for(auto& c : p->colors)
				c = tadjust(c, tadj);
		} else {
			throw std::runtime_error("Expected BITMAP or PALETTE as argument 1 for "
				"gui.adjust_transparency");
		}
		return 2;
	});
}

DECLARE_LUACLASS(lua_palette, "PALETTE");
DECLARE_LUACLASS(lua_bitmap, "BITMAP");
DECLARE_LUACLASS(lua_dbitmap, "DBITMAP");

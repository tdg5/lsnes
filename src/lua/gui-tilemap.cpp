#include "lua/internal.hpp"
#include "core/framebuffer.hpp"
#include "library/framebuffer.hpp"
#include "library/png.hpp"
#include "library/string.hpp"
#include "library/threads.hpp"
#include "library/lua-framebuffer.hpp"
#include "library/zip.hpp"
#include "lua/bitmap.hpp"
#include <vector>
#include <sstream>

namespace
{
	struct tilemap_entry
	{
		tilemap_entry()
		{
		}
		void erase()
		{
			b.clear();
			d.clear();
			p.clear();
		}
		lua::objpin<lua_bitmap> b;
		lua::objpin<lua_dbitmap> d;
		lua::objpin<lua_palette> p;
	};

	struct tilemap
	{
		tilemap(lua::state& L, size_t _width, size_t _height, size_t _cwidth, size_t _cheight);
		~tilemap()
		{
			threads::alock h(lock);
			render_kill_request(this);
		}
		static int create(lua::state& L, lua::parameters& P);
		int draw(lua::state& L, lua::parameters& P);
		int get(lua::state& L, lua::parameters& P)
		{
			uint32_t x, y;

			P(P.skipped(), x, y);

			threads::alock h(lock);
			if(x >= width || y >= height)
				return 0;
			tilemap_entry& e = map[y * width + x];
			if(e.b) {
				e.b.luapush(L);
				e.p.luapush(L);
				return 2;
			} else if(e.d) {
				e.d.luapush(L);
				return 1;
			} else
				return 0;
		}
		int set(lua::state& L, lua::parameters& P)
		{
			uint32_t x, y;

			P(P.skipped(), x, y);
			int oidx = P.skip();

			threads::alock h(lock);
			if(x >= width || y >= height)
				return 0;
			tilemap_entry& e = map[y * width + x];
			if(P.is<lua_dbitmap>(oidx)) {
				auto d = P.arg<lua::objpin<lua_dbitmap>>(oidx);
				e.erase();
				e.d = d;
			} else if(P.is<lua_bitmap>(oidx)) {
				auto b = P.arg<lua::objpin<lua_bitmap>>(oidx);
				auto p = P.arg<lua::objpin<lua_palette>>(oidx + 1);
				e.erase();
				e.b = b;
				e.p = p;
			} else if(P.is_novalue(oidx)) {
				e.erase();
			} else
				P.expected("BITMAP, DBITMAP or nil", oidx);
			return 0;
		}
		int getsize(lua::state& L, lua::parameters& P)
		{
			L.pushnumber(width);
			L.pushnumber(height);
			return 2;
		}
		int getcsize(lua::state& L, lua::parameters& P)
		{
			L.pushnumber(cwidth);
			L.pushnumber(cheight);
			return 2;
		}
		size_t calcshift(size_t orig, int32_t shift, size_t dimension, size_t offset, bool circular)
		{
			if(circular) {
				orig -= offset;
				//Now the widow is scaled [0,dimension).
				if(shift >= 0)
					orig = (orig + shift) % dimension;
				else {
					orig += shift;
					while(orig > dimension) {
						//It overflowed.
						orig += dimension;
					}
				}
				orig += offset;
				return orig;
			} else
				return orig + shift;
		}
		int scroll(lua::state& L, lua::parameters& P)
		{
			int32_t ox, oy;
			size_t x0, y0, w, h;
			bool circx, circy;

			P(P.skipped(), ox, oy, P.optional(x0, 0), P.optional(y0, 0), P.optional(w, width),
				P.optional(h, height), P.optional(circx, false), P.optional(circy, false));

			threads::alock mh(lock);
			if(x0 > width || x0 + w > width || x0 + w < x0 || y0 > height || y0 + h > height ||
				y0 + h < y0)
				throw std::runtime_error("Scroll window out of range");
			if(!ox && !oy) return 0;
			std::vector<tilemap_entry> tmp;
			tmp.resize(w * h);
			for(size_t _y = 0; _y < h; _y++) {
				size_t y = _y + y0;
				size_t sy = calcshift(y, oy, h, y0, circy);
				if(sy < y0 || sy >= y0 + h)
					continue;
				for(size_t _x = 0; _x < w; _x++) {
					size_t x = _x + x0;
					size_t sx = calcshift(x, ox, w, x0, circx);
					if(sx < x0 || sx >= x0 + w)
						continue;
					else
						tmp[_y * w + _x] = map[sy * width + sx];
				}
			}
			for(size_t _y = 0; _y < h; _y++)
				for(size_t _x = 0; _x < w; _x++)
					map[(_y + y0) * width + (_x + x0)] = tmp[_y * w + _x];
			return 0;
		}
		std::string print()
		{
			return (stringfmt() << width << "*" << height << " (cell " << cwidth << "*" << cheight
				<< ")").str();
		}
		size_t width;
		size_t height;
		size_t cwidth;
		size_t cheight;
		std::vector<tilemap_entry> map;
		threads::lock lock;
	};

	struct render_object_tilemap : public framebuffer::object
	{
		render_object_tilemap(int32_t _x, int32_t _y, int32_t _x0, int32_t _y0, uint32_t _w,
			uint32_t _h, lua::objpin<tilemap> _map)
			: x(_x), y(_y), x0(_x0), y0(_y0), w(_w), h(_h), map(_map) {}
		~render_object_tilemap() throw()
		{
		}
		bool kill_request(void* obj) throw()
		{
			return kill_request_ifeq(map.object(), obj);
		}
		template<bool T> void composite_op(struct framebuffer::fb<T>& scr) throw()
		{
			tilemap& _map = *map;
			threads::alock h(_map.lock);
			for(size_t ty = 0; ty < _map.height; ty++) {
				size_t basey = _map.cheight * ty;
				for(size_t tx = 0; tx < _map.width; tx++) {
					size_t basex = _map.cwidth * tx;
					composite_op(scr, _map.map[ty * _map.width + tx], basex, basey);
				}
			}
		}
		template<bool T> void composite_op(struct framebuffer::fb<T>& scr, int32_t xp,
			int32_t yp, int32_t xmin, int32_t xmax, int32_t ymin, int32_t ymax, lua_dbitmap& d) throw()
		{
			if(xmin >= xmax || ymin >= ymax) return;

			for(int32_t r = ymin; r < ymax; r++) {
				typename framebuffer::fb<T>::element_t* rptr = scr.rowptr(yp + r);
				size_t eptr = xp + xmin;
				for(int32_t c = xmin; c < xmax; c++, eptr++)
					d.pixels[r * d.width + c].apply(rptr[eptr]);
			}
		}
		template<bool T> void composite_op(struct framebuffer::fb<T>& scr, int32_t xp,
			int32_t yp, int32_t xmin, int32_t xmax, int32_t ymin, int32_t ymax, lua_bitmap& b,
			lua_palette& p)
			throw()
		{
			if(xmin >= xmax || ymin >= ymax) return;
			p.palette_mutex.lock();
			framebuffer::color* palette = &p.colors[0];
			size_t pallim = p.colors.size();

			for(int32_t r = ymin; r < ymax; r++) {
				typename framebuffer::fb<T>::element_t* rptr = scr.rowptr(yp + r);
				size_t eptr = xp + xmin;
				for(int32_t c = xmin; c < xmax; c++, eptr++) {
					uint16_t i = b.pixels[r * b.width + c];
					if(i < pallim)
						palette[i].apply(rptr[eptr]);
				}
			}
			p.palette_mutex.unlock();
		}
		template<bool T> void composite_op(struct framebuffer::fb<T>& scr, tilemap_entry& e, int32_t bx,
			int32_t by) throw()
		{
			size_t _w, _h;
			if(e.b) {
				_w = e.b->width;
				_h = e.b->height;
			} else if(e.d) {
				_w = e.d->width;
				_h = e.d->height;
			} else
				return;
			//Calculate notional screen coordinates for the tile.
			int32_t scrx = x + scr.get_origin_x() + bx - x0;
			int32_t scry = y + scr.get_origin_y() + by - y0;
			int32_t scrw = scr.get_width();
			int32_t scrh = scr.get_height();
			int32_t xmin = 0;
			int32_t xmax = _w;
			int32_t ymin = 0;
			int32_t ymax = _h;
			clip(scrx, scrw, x + scr.get_origin_x(), w, xmin, xmax);
			clip(scry, scrh, y + scr.get_origin_y(), h, ymin, ymax);
			if(e.b)
				composite_op(scr, scrx, scry, xmin, xmax, ymin, ymax, *e.b, *e.p);
			else if(e.d)
				composite_op(scr, scrx, scry, xmin, xmax, ymin, ymax, *e.d);
		}
		//scrc + cmin >= 0 and scrc + cmax <= scrd  (Clip on screen).
		//scrc + cmin >= bc and scrc + cmax <= bc + d  (Clip on texture).
		void clip(int32_t scrc, int32_t scrd, int32_t bc, int32_t d, int32_t& cmin, int32_t& cmax)
		{
			if(scrc + cmin < 0)
				cmin = -scrc;
			if(scrc + cmax > scrd)
				cmax = scrd - scrc;
			if(scrc + cmin < bc)
				cmin = bc - scrc;
			if(scrc + cmax > bc + d)
				cmax = bc + d - scrc;
		}
		void operator()(struct framebuffer::fb<false>& x) throw() { composite_op(x); }
		void operator()(struct framebuffer::fb<true>& x) throw() { composite_op(x); }
		void clone(framebuffer::queue& q) const throw(std::bad_alloc) { q.clone_helper(this); }
	private:
		int32_t x;
		int32_t y;
		int32_t x0;
		int32_t y0;
		uint32_t w;
		uint32_t h;
		lua::objpin<tilemap> map;
	};

	int tilemap::draw(lua::state& L, lua::parameters& P)
	{
		uint32_t x, y, w, h;
		int32_t x0, y0;
		lua::objpin<tilemap> t;

		if(!lua_render_ctx) return 0;

		P(t, x, y, P.optional(x0, 0), P.optional(y0, 0), P.optional(w, width * cwidth),
			P.optional(h, height * cheight));

		lua_render_ctx->queue->create_add<render_object_tilemap>(x, y, x0, y0, w, h, t);
		return 0;
	}

	int tilemap::create(lua::state& L, lua::parameters& P)
	{
		uint32_t w, h, px, py;

		P(w, h, px, py);

		lua::_class<tilemap>::create(L, w, h, px, py);
		return 1;
	}

	lua::_class<tilemap> class_tilemap(lua_class_gui, "TILEMAP", {
		{"new", tilemap::create},
	}, {
		{"draw", &tilemap::draw},
		{"set", &tilemap::set},
		{"get", &tilemap::get},
		{"scroll", &tilemap::scroll},
		{"getsize", &tilemap::getsize},
		{"getcsize", &tilemap::getcsize},
	});

	tilemap::tilemap(lua::state& L, size_t _width, size_t _height, size_t _cwidth, size_t _cheight)
		: width(_width), height(_height), cwidth(_cwidth), cheight(_cheight)
	{
		if(width * height / height != width)
			throw std::bad_alloc();
		map.resize(width * height);
	}
}

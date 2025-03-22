/*
 * Portions of this file are copyright Rebirth contributors and licensed as
 * described in COPYING.txt.
 * Portions of this file are copyright Parallax Software and licensed
 * according to the Parallax license below.
 * See COPYING.txt for license details.

THE COMPUTER CODE CONTAINED HEREIN IS THE SOLE PROPERTY OF PARALLAX
SOFTWARE CORPORATION ("PARALLAX").  PARALLAX, IN DISTRIBUTING THE CODE TO
END-USERS, AND SUBJECT TO ALL OF THE TERMS AND CONDITIONS HEREIN, GRANTS A
ROYALTY-FREE, PERPETUAL LICENSE TO SUCH END-USERS FOR USE BY SUCH END-USERS
IN USING, DISPLAYING,  AND CREATING DERIVATIVE WORKS THEREOF, SO LONG AS
SUCH USE, DISPLAY OR CREATION IS FOR NON-COMMERCIAL, ROYALTY OR REVENUE
FREE PURPOSES.  IN NO EVENT SHALL THE END-USER USE THE COMPUTER CODE
CONTAINED HEREIN FOR REVENUE-BEARING PURPOSES.  THE END-USER UNDERSTANDS
AND AGREES TO THE TERMS HEREIN AND ACCEPTS THE SAME BY USE OF THIS FILE.
COPYRIGHT 1993-1999 PARALLAX SOFTWARE CORPORATION.  ALL RIGHTS RESERVED.
*/

/*
 *
 * Routines for bitblt's.
 *
 */

#include <algorithm>
#include <utility>
#include <string.h>
#include "u_mem.h"
#include "gr.h"
#include "rle.h"
#include "dxxerror.h"
#include "byteutil.h"
#include "ogl_init.h"
#include "compiler-range_for.h"
#include "d_range.h"
#include <array>

namespace dcx {
    namespace {
        static void gr_bm_ubitblt00_rle(unsigned w, unsigned h, int dx, int dy, int sx, int sy, const grs_bitmap &src,
                                        grs_bitmap &dest);

#define gr_linear_movsd(S,D,L)	memcpy(D,S,L)

        static void gr_for_each_bitmap_line(grs_canvas &canvas, const unsigned x, const unsigned y,
                                            const grs_bitmap &bm, auto &&f) {
            const size_t src_width = bm.bm_w;
            const uintptr_t src_rowsize = bm.bm_rowsize;
            const uintptr_t dest_rowsize = canvas.cv_bitmap.bm_rowsize;
            auto dest = &(canvas.cv_bitmap.get_bitmap_data()[dest_rowsize * y + x]);
            auto src = bm.get_bitmap_data();
            for (uint_fast32_t y1 = bm.bm_h; y1--;) {
                f(dest, src, src_width);
                src += src_rowsize;
                dest += dest_rowsize;
            }
        }

        static void gr_ubitmap00(grs_canvas &canvas, const unsigned x, const unsigned y, const grs_bitmap &bm) {
            gr_for_each_bitmap_line(canvas, x, y, bm, memcpy);
        }

        static inline void gr_for_each_bitmap_byte(grs_canvas &canvas, const uint_fast32_t bx, const uint_fast32_t by,
                                                   const grs_bitmap &bm, auto &&f) {
            auto src = bm.bm_data;
            const auto ey = by + bm.bm_h;
            const auto ex = bx + bm.bm_w;
            range_for(const auto iy, xrange(by, ey))
                range_for(const auto ix, xrange(bx, ex))
                    f(canvas, src++, ix, iy);
        }

        static void gr_ubitmap012(grs_canvas &canvas, const unsigned x, const unsigned y, const grs_bitmap &bm) {
            const auto a = [](grs_canvas &cv, const color_palette_index *const src, const uint_fast32_t px,
                              const uint_fast32_t py) {
                const auto color = *src;
                gr_upixel(cv.cv_bitmap, px, py, color);
            };
            gr_for_each_bitmap_byte(canvas, x, y, bm, a);
        }

        static void gr_ubitmapGENERIC(grs_canvas &canvas, const unsigned x, const unsigned y, const grs_bitmap &bm) {
            const uint_fast32_t bm_h = bm.bm_h;
            const uint_fast32_t bm_w = bm.bm_w;
            range_for(const uint_fast32_t y1, xrange(bm_h)) {
                range_for(const uint_fast32_t x1, xrange(bm_w)) {
                    const auto color = gr_gpixel(bm, x1, y1);
                    gr_upixel(canvas.cv_bitmap, x + x1, y + y1, color);
                }
            }
        }
    }

    void gr_ubitmap(grs_canvas &canvas, grs_bitmap &bm) {
        const unsigned x = 0;
        const unsigned y = 0;

        const auto source = bm.get_type();
        const auto dest = canvas.cv_bitmap.get_type();

        if (source == bm_mode::linear) {
            switch (dest) {
                case bm_mode::linear:
                    if (bm.get_flag_mask(BM_FLAG_RLE))
                        gr_bm_ubitblt00_rle(bm.bm_w, bm.bm_h, x, y, 0, 0, bm, canvas.cv_bitmap);
                    else
                        gr_ubitmap00(canvas, x, y, bm);
                    return;

		case bm_mode::ogl:
			ogl_ubitmapm_cs(canvas, x, y, opengl_bitmap_use_dst_canvas, opengl_bitmap_use_dst_canvas, bm, ogl_colors::white);
			return;
                default:
                    gr_ubitmap012(canvas, x, y, bm);
                    return;
            }
        } else {
            gr_ubitmapGENERIC(canvas, x, y, bm);
        }
    }

    namespace {
        // From Linear to Linear
        static void gr_bm_ubitblt00(const unsigned w, const unsigned h, const unsigned dx, const unsigned dy,
                                    const unsigned sx, const unsigned sy, const grs_bitmap &src, grs_bitmap &dest) {
            //int	src_bm_rowsize_2, dest_bm_rowsize_2;
            auto sbits = &src.get_bitmap_data()[(src.bm_rowsize * sy) + sx];
            auto dbits = &dest.get_bitmap_data()[(dest.bm_rowsize * dy) + dx];
            const auto dstep = dest.bm_rowsize;
            // No interlacing, copy the whole buffer.
            for (uint_fast32_t i = h; i--;) {
                gr_linear_movsd(sbits, dbits, w);
                //memcpy(dbits, sbits, w);
                sbits += src.bm_rowsize;
                dbits += dstep;
            }
        }

    }

    // Clipped bitmap ...
    void gr_bitmap(grs_canvas &canvas, const unsigned x, const unsigned y, grs_bitmap &bm) {
        int dx1 = x, dx2 = x + bm.bm_w - 1;
        int dy1 = y, dy2 = y + bm.bm_h - 1;

        if (dx1 >= canvas.cv_bitmap.bm_w || dx2 < 0)
            return;
        if (dy1 >= canvas.cv_bitmap.bm_h || dy2 < 0)
            return;
        // Draw bitmap bm[x,y] into (dx1,dy1)-(dx2,dy2)
        ogl_ubitmapm_cs(canvas, x, y, opengl_bitmap_use_src_bitmap, opengl_bitmap_use_src_bitmap, bm,
                        ogl_colors::white);
    }


    namespace {
        class bm_rle_window : bm_rle_src_stride {
        public:
            bm_rle_window(const grs_bitmap &src) : bm_rle_src_stride{src, src.get_flag_mask(BM_FLAG_RLE_BIG)} {
            }

            void skip_upper_rows(uint_fast32_t);

            uint8_t *init(uint_fast32_t dx, uint_fast32_t dy, uint_fast32_t sy, grs_bitmap &dest);

            void apply(uint_fast32_t w, uint_fast32_t h, uint_fast32_t sx, uint8_t *dbits, uint_fast32_t bm_rowsize,
                       auto &&f);
        };

        void bm_rle_window::skip_upper_rows(const uint_fast32_t sy) {
            for (uint_fast32_t i = sy; i; --i)
                advance_src_bits();
        }

        uint8_t *bm_rle_window::init(const uint_fast32_t dx, const uint_fast32_t dy, const uint_fast32_t sy,
                                     grs_bitmap &dest) {
            skip_upper_rows(sy);
            return &dest.get_bitmap_data()[(dest.bm_rowsize * dy) + dx];
        }

        void bm_rle_window::apply(const uint_fast32_t w, const uint_fast32_t h, const uint_fast32_t sx, uint8_t *dbits,
                                  const uint_fast32_t bm_rowsize, auto &&f) {
            // No interlacing, copy the whole buffer.
            for (uint_fast32_t i = h; i; --i) {
                f(std::exchange(dbits, dbits + bm_rowsize), src_bits, sx, w);
                advance_src_bits();
            }
        }

        static void gr_bm_ubitblt00_rle(const unsigned w, const unsigned h, const int dx, const int dy, const int sx,
                                        const int sy, const grs_bitmap &src, grs_bitmap &dest) {
            bm_rle_window bw(src);
            bw.apply(sx + w - 1, h, sx, bw.init(dx, dy, sy, dest), dest.bm_rowsize, gr_rle_expand_scanline);
        }

        // rescalling bitmaps, 10/14/99 Jan Bobrowski jb@wizard.ae.krakow.pl

        static void scale_line(const uint8_t *in, uint8_t *out, const uint_fast32_t ilen, const uint_fast32_t olen) {
            uint_fast32_t a = olen / ilen, b = olen % ilen, c = 0;
            for (uint8_t *const end = out + olen; out != end;) {
                uint_fast32_t i = a;
                c += b;
                if (c >= ilen) {
                    c -= ilen;
                    ++i;
                }
                auto e = out + i;
                std::fill(std::exchange(out, e), e, *in++);
            }
        }

        static void gr_bitmap_scale_to(const grs_bitmap &src, grs_bitmap &dst) {
            auto s = src.get_bitmap_data();
            auto d = dst.get_bitmap_data();
            int h = src.bm_h;
            int a = dst.bm_h / h, b = dst.bm_h % h;
            int c{0}, i;

            for (uint_fast32_t y = src.bm_h; y; --y) {
                i = a;
                c += b;
                if (c >= h) {
                    c -= h;
                    goto inside;
                }
                while (--i >= 0) {
                inside:
                    scale_line(s, d, src.bm_w, dst.bm_w);
                    d += dst.bm_rowsize;
                }
                s += src.bm_rowsize;
            }
        }
    }

    void show_fullscr(grs_canvas &canvas, grs_bitmap &bm) {
        auto &scr = canvas.cv_bitmap;

        if (bm.get_type() == bm_mode::linear && scr.get_type() == bm_mode::ogl &&
            bm.bm_w <= grd_curscreen->get_screen_width() && bm.bm_h <= grd_curscreen->get_screen_height())
        // only scale with OGL if bitmap is not bigger than screen size
        {
            ogl_ubitmapm_cs(canvas, 0, 0, opengl_bitmap_use_dst_canvas, opengl_bitmap_use_dst_canvas, bm,
                            ogl_colors::white); //use opengl to scale, faster and saves ram. -MPM
            return;
        }

        if (scr.get_type() != bm_mode::linear) {
            grs_bitmap_ptr p = gr_create_bitmap(scr.bm_w, scr.bm_h);
            auto &tmp = *p.get();
            gr_bitmap_scale_to(bm, tmp);
            gr_bitmap(canvas, 0, 0, tmp);
            return;
        }
        gr_bitmap_scale_to(bm, scr);
    }

    // Find transparent area in bitmap
    void gr_bitblt_find_transparent_area(const grs_bitmap &bm, unsigned &minx, unsigned &miny, unsigned &maxx,
                                         unsigned &maxy) {
        using std::advance;
        using std::min;
        using std::max;

        if (!bm.get_flag_mask(BM_FLAG_TRANSPARENT))
            return;

        minx = bm.bm_w - 1;
        maxx = 0;
        miny = bm.bm_h - 1;
        maxy = 0;

        unsigned i{0}, count = 0;
        auto check = [&](unsigned x, unsigned y, const color_palette_index c) {
            if (c == TRANSPARENCY_COLOR) {
                // don't look for transparancy color here.
                count++;
                minx = min(x, minx);
                miny = min(y, miny);
                maxx = max(x, maxx);
                maxy = max(y, maxy);
            }
        };
        // decode the bitmap
        const uint_fast32_t bm_h = bm.bm_h;
        const uint_fast32_t bm_w = bm.bm_w;
        if (bm.get_flag_mask(BM_FLAG_RLE)) {
            bm_rle_expand expander(bm);
            for (uint_fast32_t y = 0;; ++y) {
                std::array<uint8_t, 4096> buf;
                if (expander.step(bm_rle_expand_range(buf)) != bm_rle_expand::again)
                    break;
                range_for(const uint_fast32_t x, xrange(bm_w))
                    check(x, y, buf[x]);
            }
        } else {
            range_for(const uint_fast32_t y, xrange(bm_h))
                range_for(const uint_fast32_t x, xrange(bm_w))
                    check(x, y, bm.bm_data[i++]);
        }
        Assert(count);
    }
}

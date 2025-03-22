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
 * Graphical routines for drawing fonts.
 *
 */

#include <algorithm>
#include <bit>
#include <memory>
#include <stdexcept>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef macintosh
#include <fcntl.h>
#endif

#include "ui.h"
#include "editor/editor.h"
#include "u_mem.h"
#include "gr.h"
#include "dxxerror.h"
#include "common/2d/bitmap.h"
#include "physfsx.h"
#include "gamefont.h"
#include "byteutil.h"
#include "console.h"
#include "config.h"
#if DXX_USE_OGL
#include "ogl_init.h"
#endif

#include "compiler-range_for.h"
#include "partial_range.h"
#include "d_range.h"
#include <array>
#include <memory>

namespace dcx {

namespace {

struct grs_disk_font
{
	const uint16_t ft_w;           // Width in pixels
	const uint16_t ft_h;           // Height in pixels
	const int16_t  ft_flags;       // Proportional?
	const int16_t  ft_baseline;    //
	const uint8_t  ft_minchar;     // First char defined by this font
	const uint8_t  ft_maxchar;     // Last char defined by this font
	const uint32_t ft_data;        // Ptr to raw data.
	const uint32_t ft_widths;     // Array of widths (required for prop font)
	const uint32_t ft_kerndata;    // Array of kerning triplet data
};

static font_x_scale_float FONTSCALE_X()
{
	return font_x_scale_float(FNTScaleX.operator float());
}

static auto FONTSCALE_Y(const int &y)
{
	return font_y_scaled_float(FNTScaleY * y);
}

constexpr std::integral_constant<uint8_t, 255> kerndata_terminator{};

#define BITS_TO_BYTES(x)    (((x)+7)>>3)

static int gr_internal_string_clipped(grs_canvas &, const grs_font &cv_font, int x, int y, const char *s);
static int gr_internal_string_clipped_m(grs_canvas &, const grs_font &cv_font, int x, int y, const char *s);

static const uint8_t *find_kern_entry(const grs_font &font, const uint8_t first, const uint8_t second)
{
	auto p{font.ft_kerndata};

	while (*p != kerndata_terminator)
		if (p[0]==first && p[1]==second)
			return p;
		else p+=3;
	return NULL;
}

//takes the character AFTER being offset into font

class font_character_extent
{
	const unsigned r;
public:
	font_character_extent(const grs_font &cv_font) :
		r(cv_font.ft_maxchar - cv_font.ft_minchar)
	{
	}
	bool operator()(const unsigned c) const
	{
		return c <= r;
	}
};

template <typename T>
struct get_char_width_result
{
	T width, spacing;
};

/* Floating form never uses width.  This specialization allows the
 * compiler to recognize width as dead, shortening
 * get_char_width<float>.
 */
template <>
struct get_char_width_result<float>
{
	float spacing;
	get_char_width_result(float, float s) :
		spacing(s)
	{
	}
};

//takes the character BEFORE being offset into current font
template <typename T>
static get_char_width_result<T> get_char_width(const grs_font &cv_font, const uint8_t c, const uint8_t c2)
{
	const unsigned letter = c - cv_font.ft_minchar;
	const auto ft_flags{cv_font.ft_flags};
	const auto proportional{ft_flags & FT_PROPORTIONAL};

	const auto fontscale_x{FONTSCALE_X()};
	const font_character_extent INFONT{cv_font};
	if (!INFONT(letter)) {				//not in font, draw as space
		return {0, static_cast<T>(proportional ? fontscale_x(cv_font.ft_w) / 2 : cv_font.ft_w)};
	}
	const T width = proportional ? fontscale_x(cv_font.ft_widths[letter]).operator float() : cv_font.ft_w;
	if (ft_flags & FT_KERNED) 
	{
		if (!(c2==0 || c2=='\n')) {
			const unsigned letter2 = c2 - cv_font.ft_minchar;

			if (INFONT(letter2)) {
				const auto p{find_kern_entry(cv_font, letter, letter2)};
				if (p)
					return {width, static_cast<T>(fontscale_x(p[2]))};
			}
		}
	}
	return {width, width};
}

static int get_centered_x(const grs_canvas &canvas, const grs_font &cv_font, const char *s)
{
	float w{0.f};
	for (; const char c{*s}; ++s)
	{
		if (c == '\n')
			break;
		if (c <= 0x06)
		{
			if (c <= 0x03)
				s++;
			continue;//skip color codes.
		}
		w += get_char_width<float>(cv_font, c, s[1]).spacing;
	}

	return (canvas.cv_bitmap.bm_w - w) / 2;
}

//hack to allow color codes to be embedded in strings -MPM
//note we subtract one from color, since 255 is "transparent" so it'll never be used, and 0 would otherwise end the string.
//function must already have orig_color var set (or they could be passed as args...)
//perhaps some sort of recursive orig_color type thing would be better, but that would be way too much trouble for little gain
constexpr std::integral_constant<int, 1> gr_message_color_level{};

struct per_character_row_state
{
	uint8_t draw_full_width_as_fg_color{};
};

#define CHECK_EMBEDDED_COLORS() if (const char control_code{*text_ptr}; control_code >= 0x01 && control_code <= 0x02) { \
		text_ptr++; \
		if (*text_ptr){ \
			if (gr_message_color_level >= control_code) \
				canvas.cv_font_fg_color = *text_ptr; \
			text_ptr++; \
		} \
	} \
	else if (control_code == 0x03) \
	{ \
		state.draw_full_width_as_fg_color = 1; \
		text_ptr++; \
	} \
	else if (control_code >= 0x04 && control_code <= 0x06) { \
		if (gr_message_color_level >= control_code - 3) \
			canvas.cv_font_fg_color= orig_color; \
		text_ptr++; \
	}

template <bool masked_draws_background>
static int gr_internal_string0_template(grs_canvas &canvas, const grs_font &cv_font, const int x, int y, const char *const s)
{
	const font_character_extent INFONT{cv_font};
	const auto ft_flags{cv_font.ft_flags};
	const auto proportional{ft_flags & FT_PROPORTIONAL};
	const auto cv_font_bg_color{canvas.cv_font_bg_color};
	int	skip_lines{};

	//to allow easy reseting to default string color with colored strings -MPM
	const auto orig_color{canvas.cv_font_fg_color};
	unsigned VideoOffset1 = y * canvas.cv_bitmap.bm_rowsize + x;
	for (auto next_row{s}; next_row;)
	{
		const auto text_ptr1{std::exchange(next_row, nullptr)};

		if (x==0x8000) {			//centered
			VideoOffset1 = y * canvas.cv_bitmap.bm_rowsize + get_centered_x(canvas, cv_font, text_ptr1);
		}

		for (int r{}; r < cv_font.ft_h; ++r)
		{
			auto text_ptr{text_ptr1};
			unsigned VideoOffset{VideoOffset1};

			for (; const uint8_t c0 = *text_ptr; ++text_ptr)
			{
				if (c0 == '\n')
				{
					next_row = &text_ptr[1];
					break;
				}

				if (c0 == CC_COLOR)
				{
					canvas.cv_font_fg_color = static_cast<uint8_t>(*++text_ptr);
					continue;
				}

				if (c0 == CC_LSPACING)
				{
					skip_lines = *++text_ptr - '0';
					continue;
				}

				per_character_row_state state{
					(unlikely(c0 == CC_UNDERLINE)
						? ++text_ptr, r == cv_font.ft_baseline + 2 || r == cv_font.ft_baseline + 3
						: false),
				};

				const uint8_t c = *text_ptr;
				const auto [width, spacing]{get_char_width<int>(cv_font, c, text_ptr[1])};

				const unsigned letter = c - cv_font.ft_minchar;

				if constexpr (masked_draws_background)
				{
					(void)orig_color;
					if (!INFONT(letter)) {	//not in font, draw as space
						VideoOffset += spacing;
						text_ptr++;
						continue;
					}
				}
				else
				{
					if (!INFONT(letter) || c <= 0x06)	//not in font, draw as space
					{
						CHECK_EMBEDDED_COLORS() else{
							VideoOffset += spacing;
							text_ptr++;
						}
						continue;
					}
				}

				if (width)
				{
					auto data{&canvas.cv_bitmap.get_bitmap_data()[VideoOffset]};
					const auto cv_font_fg_color{canvas.cv_font_fg_color};
					if (state.draw_full_width_as_fg_color)
				{
					std::fill_n(data, width, cv_font_fg_color);
				}
				else
				{
					auto fp{(proportional ? cv_font.ft_chars[letter] : &cv_font.ft_data[letter * BITS_TO_BYTES(width) * cv_font.ft_h]) + (BITS_TO_BYTES(width) * r)};

					/* Setting bits=0 is a dead store, but is necessary to
					 * prevent -Og -Wuninitialized from issuing a bogus
					 * warning.  -Og does not see that bits_remaining=0
					 * guarantees that bits will be initialized from *fp before
					 * it is read.
					 */
					uint8_t bits_remaining{}, bits{};
					for (uint_fast32_t i = width; i--; ++data, --bits_remaining)
					{
						if (!bits_remaining)
						{
							bits = *fp++;
							bits_remaining = 8;
						}

						const auto bit_enabled{(bits & 0x80)};
						bits <<= 1;
						if constexpr (!masked_draws_background)
						{
							if (!bit_enabled)
								continue;
						}
						*data = bit_enabled ? cv_font_fg_color : cv_font_bg_color;
					}
				}
				}
				VideoOffset += spacing;
			}
			VideoOffset1 += canvas.cv_bitmap.bm_rowsize;
			y++;
		}
		y += skip_lines;
		VideoOffset1 += canvas.cv_bitmap.bm_rowsize * skip_lines;
		skip_lines = 0;
	}
	return 0;
}

static int gr_internal_string0(grs_canvas &canvas, const grs_font &cv_font, const int x, const int y, const char *const s)
{
	return gr_internal_string0_template<true>(canvas, cv_font, x, y, s);
}

static int gr_internal_string0m(grs_canvas &canvas, const grs_font &cv_font, const int x, const int y, const char *const s)
{
	return gr_internal_string0_template<false>(canvas, cv_font, x, y, s);
}


static unsigned get_font_total_width(const grs_font &font)
{
	if (font.ft_flags & FT_PROPORTIONAL)
	{
		unsigned w{};
		range_for (const auto v, unchecked_partial_range(font.ft_widths, static_cast<unsigned>(font.ft_maxchar - font.ft_minchar) + 1))
			w += v;
		return w;
	}else{
		return {unsigned{font.ft_w} * (font.ft_maxchar - font.ft_minchar + 1u)};
	}
}

static std::pair<unsigned, unsigned> ogl_font_choose_size(const grs_font *const font, const uint8_t gap)
{
	const auto nchars{font->ft_maxchar - font->ft_minchar + 1u};
	int nc{}, smallest{999999};
	int smallprop{10000};
	std::optional<std::pair<unsigned, unsigned>> rwh;
	for (unsigned h{32}; h <= 256; h *= 2)
	{
		if (font->ft_h>h)continue;
		const auto r{h / (font->ft_h + gap)};
		auto w{std::bit_ceil((get_font_total_width(*font) + (nchars - r) * gap) / r)};
		int tries{};
		do {
			if (tries)
				w = std::bit_ceil(w + 1u);
			if(tries>3){
				break;
			}
			nc=0;
			int y{};
			while(y+font->ft_h<=h){
				int x{};
				while (x<w){
					if (nc==nchars)
						break;
					if (font->ft_flags & FT_PROPORTIONAL){
						if (x+font->ft_widths[nc]+gap>w)break;
						x+=font->ft_widths[nc++]+gap;
					}else{
						if (x+font->ft_w+gap>w)break;
						x+=font->ft_w+gap;
						nc++;
					}
				}
				if (nc==nchars)
					break;
				y+=font->ft_h+gap;
			}
			
			tries++;
		}while(nc!=nchars);
		if (nc!=nchars)
			continue;

		const auto whproduct{w * h};
		if (whproduct == smallest)	//this gives squarer sizes priority (ie, 128x128 would be better than 512*32)
		{
			if (w>=h){
				if (w/h<smallprop){
					smallprop=w/h;
					smallest++;//hack
				}
			}else{
				if (h/w<smallprop){
					smallprop=h/w;
					smallest++;//hack
				}
			}
		}
		if (whproduct < smallest)
		{
			smallest = whproduct;
			rwh = {w, h};
		}
	}
	if (!rwh)
		Error("Could not fit font?\n");
	return *rwh;
}

static void ogl_init_font(grs_font *const font)
{
	int oglflags{OGL_FLAG_ALPHA};
	const unsigned nchars = font->ft_maxchar - font->ft_minchar + 1;
	constexpr uint8_t gap{1};	// x/y offset between the chars so we can filter

	const auto [tw, th]{ogl_font_choose_size(font, gap)};
	{
		RAIIdmem<uint8_t[]> data;
		const unsigned length{tw * th};
		MALLOC(data, uint8_t[], length);
		std::fill_n(data.get(), length, TRANSPARENCY_COLOR); // map the whole data with transparency so we won't have borders if using gap
		gr_init_main_bitmap(font->ft_parent_bitmap, bm_mode::linear, 0, 0, tw, th, tw, std::move(data));
	}
	gr_set_transparent(font->ft_parent_bitmap, 1);

	if (!(font->ft_flags & FT_COLOR))
		oglflags |= OGL_FLAG_NOCOLOR;
	ogl_init_texture(*(font->ft_parent_bitmap.gltexture = ogl_get_free_texture()), tw, th, oglflags); // have to init the gltexture here so the subbitmaps will find it.

	font->ft_bitmaps = std::make_unique<grs_bitmap[]>(nchars);
	const auto h{font->ft_h};

	uint16_t curx{};
	uint16_t cury{};
	for (const auto i : xrange(nchars))
	{
		const auto w{
			(font->ft_flags & FT_PROPORTIONAL)
			? font->ft_widths[i]
			: font->ft_w
		};

		if (std::cmp_less(w, 1u))
			continue;
		if (std::cmp_greater(w, 256u))
			continue;

		if (std::cmp_greater(unsigned{curx} + w + gap, tw))
		{
			const unsigned next_y{unsigned{cury} + h + gap};
			if (!std::in_range<uint16_t>(next_y) || std::cmp_greater(next_y + h, th))
			{
				std::array<char, 124> buf;
				const auto written{std::snprintf(std::data(buf), std::size(buf), "failed to fit font: i=%u, nchars=%u, h=%hu, cury=%hu", i, nchars, h, cury)};
				throw std::runtime_error(std::string{std::data(buf), written > 0 ? std::min<unsigned>(std::size(buf), written) : 0});
			}
			cury = next_y;
			curx=0;
		}

		if (font->ft_flags & FT_COLOR)
		{
			const auto fp{(font->ft_flags & FT_PROPORTIONAL)
				? font->ft_chars[i]
				: font->ft_data + i * w*h};
			for (const auto y : xrange(h))
			{
				for (const auto x : xrange(w))
					font->ft_parent_bitmap.get_bitmap_data()[curx+x+(cury+y)*tw] = fp[x+y*w];
					// Let's call this a HACK:
					// If we filter the fonts, the sliders will be messed up as the border pixels will have an
					// alpha value while filtering. So the slider bitmaps will not look "connected".
					// To prevent this, duplicate the first/last pixel-row with a 1-pixel offset.
				if (gap && i >= 99 && i <= 102)
				{
						// See which bitmaps need left/right shifts:
						// 99  = SLIDER_LEFT - shift RIGHT
						// 100 = SLIDER_RIGHT - shift LEFT
						// 101 = SLIDER_MIDDLE - shift LEFT+RIGHT
						// 102 = SLIDER_MARKER - shift RIGHT

						// shift left border
					std::size_t oi, ii;
					if (i != 99 && i != 102)
					{
						oi = (curx + (cury + y) * tw) - 1;
						ii = y * w;
					}
						// shift right border
					else if (i != 100)
					{
						oi = (curx + (w - 1) + (cury + y) * tw) + 1;
						ii = (w - 1) + y * w;
					}
					else
						continue;
					font->ft_parent_bitmap.get_bitmap_data()[oi] = fp[ii];
				}
			}
		}
		else
		{
			auto white{gr_find_closest_color(63, 63, 63)};
			auto fp{(font->ft_flags & FT_PROPORTIONAL)
				? font->ft_chars[i]
				: font->ft_data + i * BITS_TO_BYTES(w)*h};
			for (const auto y : xrange(h))
			{
				uint8_t BitMask{};
				uint8_t bits{};
				for (const auto x : xrange(w))
				{
					if (BitMask==0) {
						bits = *fp++;
						BitMask = 0x80;
					}

					font->ft_parent_bitmap.get_bitmap_data()[curx+x+(cury+y)*tw] = (bits & BitMask)
						? white
						: 255;
					BitMask >>= 1;
				}
			}
		}
		gr_init_sub_bitmap(font->ft_bitmaps[i], font->ft_parent_bitmap, {curx}, {cury}, {w}, {h});
		curx+=w+gap;
	}
	ogl_loadbmtexture_f(font->ft_parent_bitmap, CGameCfg.TexFilt, 0, 0);
}

static void ogl_internal_string(grs_canvas &canvas, const grs_font &cv_font, const int entry_x, int yy, const char *const s)
{
	auto orig_color{canvas.cv_font_fg_color};	//to allow easy reseting to default string color with colored strings -MPM

	if (grd_curscreen->sc_canvas.cv_bitmap.get_type() != bm_mode::ogl)
		Error("carp.\n");
	const auto &&fspacy1{FSPACY(1)};
	const font_character_extent INFONT{cv_font};
	const auto &&fontscale_x{FONTSCALE_X()};
	const auto &&FONTSCALE_Y_ft_h{FONTSCALE_Y(cv_font.ft_h)};
	ogl_colors colors;
	for (auto next_row{s}; next_row;)
	{
		auto text_ptr{std::exchange(next_row, nullptr)};
		auto line_x{entry_x == 0x8000
			? get_centered_x(canvas, cv_font, text_ptr)
			: entry_x};

		for (; const auto c0{*text_ptr};)
		{
			if (c0 == '\n')
			{
				next_row = &text_ptr[1];
				yy += FONTSCALE_Y_ft_h + fspacy1;
				break;
			}

			const auto letter{c0 - cv_font.ft_minchar};
			const auto spacing{get_char_width<int>(cv_font, c0, text_ptr[1]).spacing};

			per_character_row_state state;
			if (!INFONT(letter) || c0 <= 0x06) //not in font, draw as space
			{
				CHECK_EMBEDDED_COLORS() else{
					line_x += spacing;
					text_ptr++;
				}
				if (state.draw_full_width_as_fg_color)
				{
					const auto color{canvas.cv_font_fg_color};
					gr_rect(canvas, line_x, yy + cv_font.ft_baseline + 2, line_x + cv_font.ft_w, yy + cv_font.ft_baseline + 3, color);
				}

				continue;
			}
			const auto ft_w{(cv_font.ft_flags & FT_PROPORTIONAL)
				? cv_font.ft_widths[letter]
				: cv_font.ft_w};

			ogl_ubitmapm_cs(canvas, line_x, yy, fontscale_x(ft_w), FONTSCALE_Y_ft_h, cv_font.ft_bitmaps[letter], (cv_font.ft_flags & FT_COLOR) ? colors.white : (canvas.cv_bitmap.get_type() == bm_mode::ogl) ? colors.init(canvas.cv_font_fg_color) : throw std::runtime_error("non-color string to non-ogl dest"));

			line_x += spacing;
			text_ptr++;
		}
	}
}

#define gr_internal_color_string ogl_internal_string


static inline int PHYSFSX_readU8_ptr(PHYSFS_File *const file, uint8_t *const b)
{
	return PHYSFSX_readBytes(file, b, sizeof(*b)) == sizeof(*b);
}

static constexpr PHYSFSX_read_helper<uint8_t, PHYSFSX_readU8_ptr> PHYSFSX_readU8{};

static grs_disk_font grs_disk_font_read(NamedPHYSFS_File fp)
{
	return grs_disk_font{
		.ft_w = PHYSFSX_readULE16(fp),
		.ft_h = PHYSFSX_readULE16(fp),
		.ft_flags = PHYSFSX_readSLE16(fp),
		.ft_baseline = PHYSFSX_readSLE16(fp),
		.ft_minchar = PHYSFSX_readU8(fp),
		.ft_maxchar = PHYSFSX_readU8(fp),
		.ft_data = (PHYSFSX_skipBytes<2>(fp), PHYSFSX_readULE32(fp) - GRS_FONT_SIZE),
		.ft_widths = (PHYSFSX_skipBytes<4>(fp), PHYSFSX_readULE32(fp) - GRS_FONT_SIZE),
		.ft_kerndata = PHYSFSX_readULE32(fp) - GRS_FONT_SIZE,
	};
}

}

void gr_string(grs_canvas &canvas, const grs_font &cv_font, const int x, const int y, const char *const s)
{
	const auto &&[w, h]{gr_get_string_size(cv_font, s)};
	gr_string(canvas, cv_font, x, y, s, w, h);
}

namespace {

static void gr_ustring_mono(grs_canvas &canvas, const grs_font &cv_font, const int x, const int y, const char *const s)
{
	switch (canvas.cv_bitmap.get_type())
	{
		case bm_mode::linear:
			if (canvas.cv_font_bg_color == TRANSPARENCY_COLOR)
				gr_internal_string0m(canvas, cv_font, x, y, s);
			else
				gr_internal_string0(canvas, cv_font, x, y, s);
			break;
		case bm_mode::ilbm:
		case bm_mode::rgb15:
#if DXX_USE_OGL
		case bm_mode::ogl:
#endif
			break;
	}
}

}

void gr_string(grs_canvas &canvas, const grs_font &cv_font, const int x, const int y, const char *const s, const int w, const int h)
{
	if (y + h < 0)
		return;
	const auto bm_h{canvas.cv_bitmap.bm_h};
	if (y > bm_h)
		return;
	const auto bm_w{canvas.cv_bitmap.bm_w};
	int xw{w};
	if ( x == 0x8000 )	{
		// for x, since this will be centered, only look at
		// width.
	} else {
		if (x > bm_w)
			return;
		xw += x;
		if (xw < 0)
			return;
	}
	if (

		canvas.cv_bitmap.get_type() == bm_mode::ogl ||

		cv_font.ft_flags & FT_COLOR)
	{
		gr_internal_color_string(canvas, cv_font, x, y, s);
		return;
	}
	// Partially clipped...
	if (!(y < 0 ||
		x < 0 ||
		xw > bm_w ||
		y + h > bm_h))
	{
		gr_ustring_mono(canvas, cv_font, x, y, s);
		return;
	}

	if (canvas.cv_font_bg_color == TRANSPARENCY_COLOR)
	{
		gr_internal_string_clipped_m(canvas, cv_font, x, y, s);
		return;
	}
	gr_internal_string_clipped(canvas, cv_font, x, y, s);
}

void gr_ustring(grs_canvas &canvas, const grs_font &cv_font, const int x, const int y, const char *const s)
{

	if (canvas.cv_bitmap.get_type() == bm_mode::ogl)
	{
		ogl_internal_string(canvas, cv_font, x, y, s);
		return;
	}

	if (cv_font.ft_flags & FT_COLOR)
		gr_internal_color_string(canvas, cv_font, x, y, s);
	else
		gr_ustring_mono(canvas, cv_font, x, y, s);
}

unsigned gr_get_string_height(const grs_font &cv_font, const unsigned lines)
{
	const auto fontscale_y{FONTSCALE_Y(cv_font.ft_h)};
	return static_cast<unsigned>(fontscale_y + (lines * (fontscale_y + FSPACY(1))));
}

gr_string_size gr_get_string_size(const grs_font &cv_font, const char *s)
{
	return gr_get_string_size(cv_font, s, UINT_MAX);
}

gr_string_size gr_get_string_size(const grs_font &cv_font, const char *s, const unsigned max_chars_per_line)
{
	float longest_width{0.f}, string_width_f{0.f};
	unsigned lines{};
	if (s)
	{
		unsigned remaining_chars_this_line{max_chars_per_line};
		while (*s)
		{
			if (*s == '\n')
			{
				if (longest_width < string_width_f)
					longest_width = string_width_f;
				string_width_f = 0;
				const auto os{s};
				while (*++s == '\n')
				{
				}
				lines += s - os;
				if (!*s)
					break;
				remaining_chars_this_line = max_chars_per_line;
			}

			string_width_f += get_char_width<float>(cv_font, s[0], s[1]).spacing;
			s++;
			if (!--remaining_chars_this_line)
				break;
		}
	}
	return {static_cast<unsigned long>(std::max(longest_width, string_width_f)), gr_get_string_height(cv_font, lines)};
}

std::pair<const char *, unsigned> gr_get_string_wrap(const grs_font &cv_font, const char *s, unsigned limit)
{
	assert(s);
	float string_width_f{0.f};
	const float limit_f = limit;
	for (; const uint8_t c{*s}; ++s)
	{
		const float next_string_width{string_width_f + get_char_width<float>(cv_font, c, s[1]).spacing};
		if (next_string_width >= limit_f)
			break;
		string_width_f = next_string_width;
	}
	return {s, static_cast<unsigned>(string_width_f)};
}

template <void (&F)(grs_canvas &, const grs_font &, int, int, const char *)>
void (gr_printt)(grs_canvas &canvas, const grs_font &cv_font, const int x, const int y, const char *const format, ...)
{
	char buffer[1000];
	va_list args;

	va_start(args, format );
	vsnprintf(buffer, sizeof(buffer), format, args);
	va_end(args);
	F(canvas, cv_font, x, y, buffer);
}

template void gr_printt<gr_string>(grs_canvas &, const grs_font &, int, int, const char *, ...);
template void gr_printt<gr_ustring>(grs_canvas &, const grs_font &, int, int, const char *, ...);

//remap a font, re-reading its data & palette
static void gr_remap_font(grs_font *font);

//remap (by re-reading) all the color fonts
void gr_remap_color_fonts()
{
#if DXX_USE_EDITOR
	gr_remap_font(ui_small_font.get());
	gr_remap_font(editor_font.get());
#endif
	for (auto &gf : Gamefonts)
	{
		gr_remap_font(gf.font.get());
	}
}

namespace {

/*
 * reads a grs_font structure from a PHYSFS_File
 */
static void grs_font_read(grs_font *gf, NamedPHYSFS_File fp)
{
	const auto d{grs_disk_font_read(fp)};
	gf->ft_w = {d.ft_w};
	gf->ft_h = {d.ft_h};
	gf->ft_flags = {d.ft_flags};
	gf->ft_baseline = {d.ft_baseline};
	gf->ft_minchar = {d.ft_minchar};
	gf->ft_maxchar = {d.ft_maxchar};
	gf->ft_data = reinterpret_cast<uint8_t *>(d.ft_data);
	gf->ft_widths = reinterpret_cast<uint16_t *>(d.ft_widths);
	gf->ft_kerndata = reinterpret_cast<uint8_t *>(d.ft_kerndata);
}

static std::unique_ptr<grs_font> gr_internal_init_font(const std::span<const char> fontname)
{
	color_palette_index *ptr;
	color_palette_index *ft_data;
	struct {
		std::array<char, 4> magic;
		unsigned datasize;	//size up to (but not including) palette
	} file_header;

	auto &&[fontfile, physfserr]{PHYSFSX_openReadBuffered(fontname.data())};
	if (!fontfile) {
		con_printf(CON_VERBOSE, "Failed to open font file \"%s\": %s", fontname.data(), PHYSFS_getErrorByCode(physfserr));
		return {};
	}

	static_assert(sizeof(file_header) == 8, "file header size error");
	if (PHYSFSX_readBytes(fontfile, &file_header, sizeof(file_header)) != sizeof(file_header) ||
		memcmp(file_header.magic.data(), "PSFN", 4) ||
		(file_header.datasize = INTEL_INT(file_header.datasize)) < GRS_FONT_SIZE)
	{
		con_printf(CON_NORMAL, "Invalid header in font file %s", fontname.data());
		return {};
	}

	file_header.datasize -= GRS_FONT_SIZE; // subtract the size of the header.
	const auto &datasize{file_header.datasize};

	auto font{std::make_unique<grs_font>()};
	grs_font_read(font.get(), fontfile);

	const unsigned nchars = font->ft_maxchar - font->ft_minchar + 1;
	auto ft_chars_storage{(font->ft_flags & FT_PROPORTIONAL)
		? std::size_t{sizeof(uint8_t *) * nchars}
		: std::size_t{}};

	auto ft_allocdata{std::make_unique<color_palette_index[]>(datasize + ft_chars_storage)};
	const auto font_data{&ft_allocdata[ft_chars_storage]};
	if (PHYSFSX_readBytes(fontfile, font_data, datasize) != datasize)
	{
		con_printf(CON_URGENT, "Insufficient data in font file %s", fontname.data());
		return {};
	}

	if (font->ft_flags & FT_PROPORTIONAL) {
		const auto offset_widths{reinterpret_cast<uintptr_t>(font->ft_widths)};
		auto w{reinterpret_cast<uint16_t *>(&font_data[offset_widths])};
		if (offset_widths >= datasize || offset_widths + (nchars * sizeof(*w)) >= datasize)
		{
			con_printf(CON_URGENT, "Missing widths in font file %s", fontname.data());
			return {};
		}
		font->ft_widths = w;
		const auto offset_data{reinterpret_cast<uintptr_t>(font->ft_data)};
		if (offset_data >= datasize)
		{
			con_printf(CON_URGENT, "Missing data in font file %s", fontname.data());
			return {};
		}
		font->ft_data = ptr = ft_data = &font_data[offset_data];
		const auto ft_chars{reinterpret_cast<const uint8_t **>(ft_allocdata.get())};
		font->ft_chars = reinterpret_cast<const uint8_t *const *>(ft_chars);

		const auto is_color{font->ft_flags & FT_COLOR};
		const unsigned ft_h{font->ft_h};
		std::generate_n(ft_chars, nchars, [is_color, ft_h, &w, &ptr]{
			const unsigned s{INTEL_SHORT(*w)};
			if constexpr (words_bigendian)
				*w = static_cast<uint16_t>(s);
			++w;
			const auto r{ptr};
			ptr += ft_h * (is_color ? s : BITS_TO_BYTES(s));
			return r;
		});
	} else  {

		font->ft_data   = ft_data = font_data;
		font->ft_widths = NULL;

		ptr = ft_data + (nchars * font->ft_w * font->ft_h);
	}

	if (font->ft_flags & FT_KERNED)
	{
		const auto offset_kerndata{reinterpret_cast<uintptr_t>(font->ft_kerndata)};
		if (datasize <= offset_kerndata)
		{
			con_printf(CON_URGENT, "Missing kerndata in font file %s", fontname.data());
			return {};
		}
		const auto begin_kerndata{reinterpret_cast<const unsigned char *>(&font_data[offset_kerndata])};
		const auto end_font_data{&font_data[datasize - ((datasize - offset_kerndata + 2) % 3)]};
		for (auto cur_kerndata{begin_kerndata};; cur_kerndata += 3)
		{
			if (cur_kerndata == end_font_data)
			{
				con_printf(CON_URGENT, "Unterminated kerndata in font file %s", fontname.data());
				return {};
			}
			if (*cur_kerndata == kerndata_terminator)
				break;
		}
		font->ft_kerndata = begin_kerndata;
	}
	else
		font->ft_kerndata = nullptr;

	if (font->ft_flags & FT_COLOR) {		//remap palette
		palette_array_t palette;
		std::array<color_palette_index, 256> colormap;
		/* `freq` exists so that decode_data can write to it, but it is
		 * otherwise unused.  `decode_data` is not guaranteed to write
		 * to every element, but the bitset constructor will initialize
		 * the storage, so reading unwritten fields will always return
		 * false.
		 */
		std::bitset<256> freq;

		if (constexpr std::size_t buffer_size{sizeof(palette[0]) * palette.size()}; PHYSFSX_readBytes(fontfile, palette, buffer_size) != buffer_size)		//read the palette
			return nullptr;

		build_colormap_good(palette, colormap);

		colormap[TRANSPARENCY_COLOR] = TRANSPARENCY_COLOR;              // changed from colormap[255] = 255 to this for macintosh

		decode_data(std::span{ft_data, ptr}, colormap, freq);
	}
	fontfile.reset();
	//set curcanv vars

	auto &ft_filename{font->ft_filename};
	font->ft_allocdata = std::move(ft_allocdata);
	std::memcpy(ft_filename.data(), fontname.data(), std::min(fontname.size(), std::size(ft_filename) - 1));
	return font;
}

}

grs_font_ptr gr_init_font(grs_canvas &canvas, const std::span<const char> fontname)
{
	auto font{gr_internal_init_font(fontname)};
	if (!font)
		return {};
	if (!std::in_range<uint8_t>(font->ft_h))
		/* Reject fonts that are very tall.  This is an arbitrary cap that
		 * should never be exceeded, and it allows later logic to assume that
		 * uint16_t can hold all results arising from `ft_h` plus a small
		 * constant.
		 */
		return {};

	canvas.cv_font        = font.get();
	canvas.cv_font_fg_color    = 0;
	canvas.cv_font_bg_color    = 0;
#if DXX_USE_OGL
	ogl_init_font(font.get());
#endif
	return grs_font_ptr(font.release());
}

//remap a font by re-reading its data & palette
void gr_remap_font(grs_font *font)
{
	if (!font)
		/* To simplify callers, allow the caller to pass `nullptr`, and ignore
		 * an attempt to remap it.
		 */
		return;
	if (!(font->ft_flags & FT_COLOR))
		/* If the font does not use color, then remapping it will not change
		 * its content.  Ignore the attempt.
		 */
		return;
	auto n{gr_internal_init_font(font->ft_filename)};
	if (!n)
		/* If the font fails to load, retain the old font, which may have
		 * incorrect colors in the new palette, but that is still better than
		 * having no font.
		 */
		return;
	*font = std::move(*n.get());
#if DXX_USE_OGL
	ogl_init_font(font);
#endif
}

void gr_set_curfont(grs_canvas &canvas, const grs_font &n)
{
	canvas.cv_font = &n;
}

namespace {

template <bool masked_draws_background>
static int gr_internal_string_clipped_template(grs_canvas &canvas, const grs_font &cv_font, int x, int y, const char *const s)
{
	int letter;
	int x1{x};

	const font_character_extent INFONT{cv_font};
	const auto ft_flags{cv_font.ft_flags};
	const auto proportional{ft_flags & FT_PROPORTIONAL};
	const auto cv_font_bg_color{canvas.cv_font_bg_color};

	for (auto next_row{s}; next_row;)
	{
		const auto text_ptr1{next_row};
		next_row = nullptr;

		x = x1;
		if (x==0x8000)			//centered
			x = get_centered_x(canvas, cv_font, text_ptr1);

		int last_x{x};

		for (const auto r : xrange(cv_font.ft_h))
		{
			auto text_ptr{text_ptr1};
			x = last_x;

			for (; const uint8_t c0 = *text_ptr; ++text_ptr)
			{
				if (c0 == '\n')
				{
					next_row = &text_ptr[1];
					break;
				}
				if (c0 == CC_COLOR)
				{
					canvas.cv_font_fg_color = static_cast<uint8_t>(*++text_ptr);
					continue;
				}

				if (c0 == CC_LSPACING)
				{
					Int3();	//	Warning: skip lines not supported for clipped strings.
					text_ptr += 1;
					continue;
				}

				per_character_row_state state{
					(unlikely(c0 == CC_UNDERLINE)
						? ++text_ptr, r == cv_font.ft_baseline + 2 || r == cv_font.ft_baseline + 3
						: false),
				};
				const uint8_t c = *text_ptr;
				const auto [width, spacing]{get_char_width<int>(cv_font, c, text_ptr[1])};

				letter = c - cv_font.ft_minchar;

				if (!INFONT(letter)) {	//not in font, draw as space
					x += spacing;
					continue;
				}
				const auto cv_font_fg_color{canvas.cv_font_fg_color};
				auto color{cv_font_fg_color};
				if (width)
				{
					if (state.draw_full_width_as_fg_color)	{
					for (uint_fast32_t i = width; i--;)
					{
						gr_pixel(canvas.cv_bitmap, x++, y, color);
					}
				} else {
					auto fp{proportional ? cv_font.ft_chars[letter] : cv_font.ft_data + letter * BITS_TO_BYTES(width) * cv_font.ft_h};
					fp += BITS_TO_BYTES(width)*r;

					/* Setting bits=0 is a dead store, but is necessary to
					 * prevent -Og -Wuninitialized from issuing a bogus
					 * warning.  -Og does not see that bits_remaining=0
					 * guarantees that bits will be initialized from *fp before
					 * it is read.
					 */
					uint8_t bits_remaining{}, bits{};

					for (uint_fast32_t i = width; i--; ++x, --bits_remaining)
					{
						if (!bits_remaining)
						{
							bits = *fp++;
							bits_remaining = 8;
						}
						const auto bit_enabled{(bits & 0x80)};
						bits <<= 1;
						if constexpr (masked_draws_background)
							color = bit_enabled ? cv_font_fg_color : cv_font_bg_color;
						else
						{
							if (!bit_enabled)
								continue;
						}
						gr_pixel(canvas.cv_bitmap, x, y, color);
					}
				}
				}
				x += spacing-width;		//for kerning
			}
			y++;
		}
	}
	return 0;
}

static int gr_internal_string_clipped_m(grs_canvas &canvas, const grs_font &cv_font, const int x, const int y, const char *const s)
{
	return gr_internal_string_clipped_template<true>(canvas, cv_font, x, y, s);
}

static int gr_internal_string_clipped(grs_canvas &canvas, const grs_font &cv_font, const int x, const int y, const char *const s)
{
	return gr_internal_string_clipped_template<false>(canvas, cv_font, x, y, s);
}

}

}

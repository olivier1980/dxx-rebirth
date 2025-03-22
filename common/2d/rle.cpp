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
COPYRIGHT 1993-1998 PARALLAX SOFTWARE CORPORATION.  ALL RIGHTS RESERVED.
*/

/*
 *
 * Routines to do run length encoding/decoding
 * on bitmaps.
 *
 */

#include <algorithm>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "pstypes.h"
#include "u_mem.h"
#include "gr.h"
#include "dxxerror.h"
#include "rle.h"
#include "byteutil.h"

#include "compiler-range_for.h"
#include "d_range.h"

namespace dcx {

namespace {

struct rle_cache_element
{
	const grs_bitmap *rle_bitmap;
	grs_bitmap_ptr expanded_bitmap;
	int last_used;
};

constexpr uint8_t RLE_CODE{0xe0};
constexpr uint8_t NOT_RLE_CODE{0x1f};
static_assert((RLE_CODE | NOT_RLE_CODE) == 0xff, "RLE mask error");

static uint8_t rle_cache_initialized;
static int rle_counter;
static int rle_next;

static std::array<rle_cache_element, 32> rle_cache;

static inline int IS_RLE_CODE(const uint8_t &x)
{
	return (x & RLE_CODE) == RLE_CODE;
}

union rle_size_pun {
	uint8_t *rle_little;
	uint16_t *rle_big;
};

}
#define rle_stosb(_dest, _len, _color)	memset(_dest,_color,_len)

uint8_t *gr_rle_decode(const color_palette_index *sb, color_palette_index *db, const rle_position_t e)
{
	using std::advance;
	using std::distance;
	for (; sb != e.src;)
	{
		auto p{sb};
		color_palette_index c;
		for (; c = *p, !IS_RLE_CODE(c);)
			if (++p == e.src)
				return db;
		const size_t count{(size_t{c} & NOT_RLE_CODE)};
		const size_t cn{std::min<size_t>(distance(sb, p), distance(db, e.dst))};
		memcpy(db, sb, cn);
		advance(db, cn);
		if (!count)
			break;
		advance(sb, cn);
		if (sb == e.src || db == e.dst || count > static_cast<size_t>(distance(db, e.dst)))
			break;
		if (++ sb == e.src)
			break;
		std::fill_n(db, count, *sb++);
		advance(db, count);
	}
	return db;
}


void gr_rle_expand_scanline(uint8_t *dest, const uint8_t *src, const uint_fast32_t x1, const uint_fast32_t x2)
{
	int i{0};
	ubyte count;
	uint8_t color{0};

	if ( x2 < x1 ) return;

	count = 0;
	while ( i < x1 )	{
		color = *src++;
		if ( color == RLE_CODE ) return;
		if ( IS_RLE_CODE(color) )	{
			count = color & NOT_RLE_CODE;
			color = *src++;
		} else {
			// unique
			count = 1;
		}
		i += count;
	}
	count = i - x1;
	i = x1;
	// we know have '*count' pixels of 'color'.
	
	if ( x1+count > x2 )	{
		count = x2-x1+1;
		rle_stosb( dest, count, color );
		return;
	}

	rle_stosb( dest, count, color );
	dest += count;
	i += count;

	while( i <= x2 )		{
		auto color{*src++};
		if ( color == RLE_CODE ) return;
		if ( IS_RLE_CODE(color) )	{
			count = color & (~RLE_CODE);
			color = *src++;
		} else {
			// unique
			count = 1;
		}
		// we know have '*count' pixels of 'color'.
		if ( i+count <= x2 )	{
		} else {
			count = x2-i+1;
		}
		rle_stosb(dest, count, color);
		i += count;
		dest += count;
	}
}

namespace {

static std::ptrdiff_t gr_rle_encode(const uint_fast32_t org_size, const uint8_t *src, uint8_t *dest)
{
	if (!org_size)
		return 0;
	const auto dest_start{dest};
	/* The value of count ranges from [1, (NOT_RLE_CODE - 1)], so uint8_t
	 * is sufficient to hold all values.
	 */
	uint8_t count{1};
	const auto flush_pending_rle_record{[&](uint8_t previous_data_byte) {
		if (!count)
			return;
		/* If the count is not 1, then the count must be recorded in the
		 * stream.
		 * If the count is 1, and the data byte will be mistaken for a
		 * count, then the count of 1 must be recorded to escape the
		 * value of the data byte.
		 * Otherwise, write only the data byte.
		 */
		if (count != 1 || IS_RLE_CODE(previous_data_byte))
			*dest++ = count | RLE_CODE;
		*dest++ = previous_data_byte;
	}};

	auto oc{*src++};
	for (const auto i : xrange(org_size - 1, std::integral_constant<uint_fast32_t, 0u>(), xrange_descending()))
	{
		(void)i;
		if (const auto c{*src++}; c != oc)
		{
			flush_pending_rle_record(std::exchange(oc, c));
			count = 0;
		}
		count++;
		if ( count == NOT_RLE_CODE )	{
			/* If count goes any higher, it cannot be recorded properly,
			 * so record it now and reset the count.  This code path
			 * would be hit for a long run of the same data value.
			 */
			count |= RLE_CODE;
			*dest++=count;
			*dest++=oc;
			count = 0;
		}
	}
	flush_pending_rle_record(oc);
	*dest++ = RLE_CODE;

	return std::distance(dest_start, dest);
}

static unsigned gr_rle_getsize(const uint_fast32_t org_size, const uint8_t *src, const unsigned limit)
{
	if (!org_size)
		return 2;
	unsigned dest_size{0};

	uint8_t count{1};
	const auto flush_pending_rle_record{[&](uint8_t previous_data_byte) {
		if (!count)
			return;
		if (count != 1 || IS_RLE_CODE(previous_data_byte))
			dest_size += 2;
		else
			++ dest_size;
	}};

	auto oc{*src++};
	for (const auto i : xrange(org_size - 1, std::integral_constant<uint_fast32_t, 0u>(), xrange_descending()))
	{
		(void)i;
		if (const auto c{*src++}; c != oc)
		{
			flush_pending_rle_record(std::exchange(oc, c));
			if (dest_size > limit)
				return dest_size;
			count = 0;
		}
		count++;
		if ( count == NOT_RLE_CODE )	{
			dest_size++;
			dest_size++;
			if (dest_size > limit)
				return dest_size;
			count = 0;
		}
	}
	flush_pending_rle_record(oc);
	dest_size++;
	return dest_size;
}

}

void gr_bitmap_rle_compress(grs_bitmap &bmp)
{
	if (bmp.get_flag_mask(BM_FLAG_PAGED_OUT))
		return;
	int doffset;

	// first must check to see if this is large bitmap.

	const uint_fast32_t bm_h{bmp.bm_h};
	const uint_fast32_t bm_w{bmp.bm_w};
	const auto large_rle{[&]() {
		/* For each row, scan the contents of the row to determine how many
		 * bytes would be needed to RLE encode that row.
		 */
		for (const uint_fast32_t y : xrange(bm_h))
		{
			constexpr unsigned limit{UINT8_MAX};
			const auto d1{gr_rle_getsize(bm_w, &bmp.get_bitmap_data()[bm_w * y], limit)};
			if (d1 > limit)
				/* This row would need more bytes than can be recorded in a
				 * non-big RLE encoded bitmap, so switch to big mode.
				 * Once one row needs big mode, the entire bitmap will use
				 * big mode, so skip scanning any remaining rows.
				 */
				return BM_FLAG_RLE_BIG;
		}
		/* Every row was checked and none required big mode, so use
		 * non-big mode.
		 */
		return 0;
	}()};

	const auto rle_data{std::make_unique_for_overwrite<uint8_t[]>(MAX_BMP_SIZE(bm_w, bm_h))};
	if (!large_rle)
		doffset = 4 + bm_h;
	else
		doffset = 4 + (2 * bm_h);		// each row of rle'd bitmap has short instead of byte offset now

	const unsigned rle_size_limit{(large_rle ? 32767u : 255u)};
	const rle_size_pun dst_line_size_ptr{&rle_data[4]};
	range_for (const uint_fast32_t y, xrange(bm_h))
	{
		const auto d1{gr_rle_getsize(bm_w, &bmp.get_bitmap_data()[bm_w * y], rle_size_limit)};
		if (d1 > rle_size_limit || doffset + d1 > bm_w * bm_h)
			/* Encoding this row requires more space than was allocated.
			 * This bitmap is smaller when not encoded, so return and
			 * leave it in its original unencoded form.
			 *
			 * This could happen if the bitmap had a long run of
			 * varying colors, with each color encoded as a 1 byte long
			 * run.
			 */
			return;
		const auto d{gr_rle_encode(bmp.bm_w, &bmp.get_bitmap_data()[bmp.bm_w*y], &rle_data[doffset])};
		Assert( d==d1 );
		doffset	+= d;
		if (large_rle)
			PUT_INTEL_SHORT(&dst_line_size_ptr.rle_big[y], static_cast<uint16_t>(d));
		else
			dst_line_size_ptr.rle_little[y] = static_cast<uint8_t>(d);
	}
	memcpy(bmp.get_bitmap_data(), &doffset, 4);
	memcpy(&bmp.get_bitmap_data()[4], &rle_data.get()[4], doffset - 4);
	bmp.add_flags(BM_FLAG_RLE | large_rle);
}

namespace {

static void rle_cache_init()
{
	rle_cache = {};
	rle_cache_initialized = 1;
}

}

void rle_cache_close(void)
{
	if (rle_cache_initialized)	{
		rle_cache_initialized = 0;
		range_for (auto &i, rle_cache)
			i.expanded_bitmap.reset();
	}
}

void rle_cache_flush()
{
	range_for (auto &i, rle_cache)
	{
		i.rle_bitmap = NULL;
		i.last_used = 0;
	}
}

namespace {

static void rle_expand_texture_sub(const grs_bitmap &bmp, grs_bitmap &rle_temp_bitmap_1)
{
	auto sbits{&bmp.get_bitmap_data()[4 + bmp.bm_h]};
	auto dbits{rle_temp_bitmap_1.get_bitmap_data()};

	rle_temp_bitmap_1.set_flags(bmp.get_flags() & (~BM_FLAG_RLE));

	for (int i{0}; i < bmp.bm_h; i++ ) {
		gr_rle_decode(sbits, dbits, rle_end(bmp, rle_temp_bitmap_1));
		sbits += static_cast<int>(bmp.bm_data[4+i]);
		dbits += bmp.bm_w;
	}
}

}

grs_bitmap *_rle_expand_texture(const grs_bitmap &bmp)
{
	int lowest_count, lc;

	if (!rle_cache_initialized) rle_cache_init();

	Assert(!(bmp.get_flag_mask(BM_FLAG_PAGED_OUT)));

	lc = rle_counter;
	rle_counter++;

	if (rle_counter < 0)
		rle_counter = 0;
	
	if ( rle_counter < lc )	{
		rle_cache_flush();
	}

	lowest_count = rle_cache[rle_next].last_used;
	auto least_recently_used{&rle_cache[rle_next]};
	rle_next++;
	if (rle_next >= rle_cache.size())
		rle_next = 0;

	range_for (auto &i, rle_cache)
	{
		if (i.rle_bitmap == &bmp)
		{
			i.last_used = rle_counter;
			return i.expanded_bitmap.get();
		}
		if (i.last_used < lowest_count)
		{
			lowest_count = (least_recently_used = &i)->last_used;
		}
	}

	least_recently_used->expanded_bitmap = gr_create_bitmap(bmp.bm_w, bmp.bm_h);
	rle_expand_texture_sub(bmp, *least_recently_used->expanded_bitmap.get());
	least_recently_used->rle_bitmap = &bmp;
	least_recently_used->last_used = rle_counter;
	return least_recently_used->expanded_bitmap.get();
}


/*
 * swaps entries 0 and 255 in an RLE bitmap without uncompressing it
 */
void rle_swap_0_255(grs_bitmap &bmp)
{
	int len, rle_big;
	unsigned char *start;
	unsigned short line_size;

	rle_big = bmp.get_flag_mask(BM_FLAG_RLE_BIG);

	const auto temp{std::make_unique_for_overwrite<uint8_t[]>(MAX_BMP_SIZE(bmp.bm_w, bmp.bm_h))};

	const std::size_t pointer_offset{rle_big ? 4u + 2u * unsigned{bmp.bm_h} : 4u + unsigned{bmp.bm_h}};
	auto ptr{&bmp.bm_data[pointer_offset]};
	auto ptr2{&temp[pointer_offset]};
	const rle_size_pun src_line_size_ptr{&bmp.get_bitmap_data()[4]},
				dst_line_size_ptr{&temp[4]};
	for (int i{0}; i < bmp.bm_h; i++) {
		start = ptr2;
		if (rle_big)
			line_size = GET_INTEL_SHORT(&src_line_size_ptr.rle_big[i]);
		else
			line_size = src_line_size_ptr.rle_little[i];
		for (int j{0}; j < line_size; j++) {
			if ( ! IS_RLE_CODE(ptr[j]) ) {
				if (ptr[j] == 0) {
					*ptr2++ = RLE_CODE | 1;
					*ptr2++ = 255;
				} else
					*ptr2++ = ptr[j];
			} else {
				*ptr2++ = ptr[j];
				if ((ptr[j] & NOT_RLE_CODE) == 0)
					break;
				j++;
				if (ptr[j] == 0)
					*ptr2++ = 255;
				else if (ptr[j] == 255)
					*ptr2++ = 0;
				else
					*ptr2++ = ptr[j];
			}
		}
		const auto dst_line_size{std::distance(start, ptr2)};
		if (rle_big)                // set line size
			PUT_INTEL_SHORT(&dst_line_size_ptr.rle_big[i], static_cast<uint16_t>(dst_line_size));
		else
			dst_line_size_ptr.rle_little[i] = static_cast<uint8_t>(dst_line_size);
		ptr += line_size;           // go to next line
	}
	len = ptr2 - temp.get();
	memcpy(bmp.get_bitmap_data(), &len, 4);
	memcpy(&bmp.get_bitmap_data()[4], &temp.get()[4], len - 4);
}

/*
 * remaps all entries using colormap in an RLE bitmap without uncompressing it
 */
void rle_remap(grs_bitmap &bmp, std::array<color_palette_index, 256> &colormap)
{
	int len, rle_big;
	unsigned short line_size;

	rle_big = bmp.get_flag_mask(BM_FLAG_RLE_BIG);

	const auto temp{std::make_unique_for_overwrite<color_palette_index[]>(MAX_BMP_SIZE(bmp.bm_w, bmp.bm_h) + 30000)};

	const std::size_t pointer_offset{rle_big ? 4u + 2u * unsigned{bmp.bm_h} : 4u + unsigned{bmp.bm_h}};
	auto ptr{&bmp.get_bitmap_data()[pointer_offset]};
	auto ptr2{&temp[pointer_offset]};
	const rle_size_pun src_line_size_ptr{&bmp.get_bitmap_data()[4]},
				dst_line_size_ptr{&temp[4]};
	for (int i{0}; i < bmp.bm_h; ++i)
	{
		auto start{ptr2};
		if (rle_big)
			line_size = GET_INTEL_SHORT(&src_line_size_ptr.rle_big[i]);
		else
			line_size = src_line_size_ptr.rle_little[i];
		for (int j{0}; j < line_size; j++) {
			const uint8_t v{ptr[j]};
			if (!IS_RLE_CODE(v))
			{
				if (IS_RLE_CODE(colormap[v])) 
					*ptr2++ = color_palette_index{RLE_CODE | 1}; // add "escape sequence"
				*ptr2++ = colormap[v]; // translate
			} else {
				*ptr2++ = ptr[j]; // just copy current rle code
				if ((ptr[j] & NOT_RLE_CODE) == 0)
					break;
				j++;
				*ptr2++ = colormap[ptr[j]]; // translate
			}
		}
		const auto dst_line_size{std::distance(start, ptr2)};
		if (rle_big)                // set line size
			PUT_INTEL_SHORT(&dst_line_size_ptr.rle_big[i], static_cast<uint16_t>(dst_line_size));
		else
			dst_line_size_ptr.rle_little[i] = static_cast<uint8_t>(dst_line_size);
		ptr += line_size;           // go to next line
	}
	len = ptr2 - temp.get();
	memcpy(bmp.get_bitmap_data(), &len, 4);
	memcpy(&bmp.get_bitmap_data()[4], &temp.get()[4], len - 4);
}

void bm_rle_src_stride::advance_src_bits()
{
	/* Both bytes are always legal to read since the bitmap data
	 * is placed after the length table.  Reading both, then
	 * conditionally masking out the high bits (dependent on
	 * BM_FLAG_RLE_BIG) encourages the compiler to implement
	 * this line without using branches.
	 */
	const uintptr_t u{(ptr_src_bit_lengths[0] | (static_cast<uintptr_t>(ptr_src_bit_lengths[1]) << 8)) & src_bit_load_mask};
	ptr_src_bit_lengths += src_bit_stride_size;
	src_bits += u;
}

bm_rle_expand::step_result bm_rle_expand::step_internal(uint8_t *const begin_dbits, uint8_t *const end_dbits)
{
	const auto rd{gr_rle_decode(src_bits, begin_dbits, {end_src_bm, end_dbits})};
	/* If the destination buffer is exhausted, return without
	 * modifying the source state.  This lets the caller retry
	 * with a larger buffer, if desired.
	 */
	if (unlikely(begin_dbits == rd))
		return dst_exhausted;
	advance_src_bits();
	return again;
}

}

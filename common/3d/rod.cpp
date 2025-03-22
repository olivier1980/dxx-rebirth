/*
 * This file is part of the DXX-Rebirth project <https://www.dxx-rebirth.com/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */
/*
 * 
 * Rod routines
 * 
 */


#include "3d.h"
#include "globvars.h"
#include "maths.h"
#include "compiler-range_for.h"

namespace dcx {

namespace {

struct rod_4point
{
	std::array<cg3s_point *, 4> point_list;
	std::array<g3s_point, 4> points;
};

//compute the corners of a rod.  fills in vertbuf.
static clipping_code calc_rod_corners(rod_4point &rod_point_group, const g3s_point &bot_point,fix bot_width,const g3s_point &top_point,fix top_width)
{
	//compute vector from one point to other, do cross product with vector
	//from eye to get perpendiclar

	auto delta_vec{vm_vec_sub(bot_point.p3_vec, top_point.p3_vec)};

	//unscale for aspect

	delta_vec.x = fixdiv(delta_vec.x,Matrix_scale.x);
	delta_vec.y = fixdiv(delta_vec.y,Matrix_scale.y);

	//calc perp vector

	//do lots of normalizing to prevent overflowing.  When this code works,
	//it should be optimized

	vm_vec_normalize(delta_vec);

	const auto top = vm_vec_normalized(top_point.p3_vec);

	auto rod_norm{vm_vec_cross(delta_vec, top)};

	vm_vec_normalize(rod_norm);

	//scale for aspect

	rod_norm.x = fixmul(rod_norm.x,Matrix_scale.x);
	rod_norm.y = fixmul(rod_norm.y,Matrix_scale.y);

	//now we have the usable edge.  generate four points

	//top points

	auto &rod_points = rod_point_group.points;
	{
	auto tempv{vm_vec_copy_scale(rod_norm, top_width)};
	tempv.z = 0;

	rod_point_group.point_list[0] = &rod_point_group.points[0];
	rod_point_group.point_list[1] = &rod_point_group.points[1];
	rod_point_group.point_list[2] = &rod_point_group.points[2];
	rod_point_group.point_list[3] = &rod_point_group.points[3];
	vm_vec_add(rod_points[0].p3_vec,top_point.p3_vec,tempv);
	vm_vec_sub(rod_points[1].p3_vec,top_point.p3_vec,tempv);
	}

	{
	auto tempv{vm_vec_copy_scale(rod_norm, bot_width)};
	tempv.z = 0;

	vm_vec_sub(rod_points[2].p3_vec,bot_point.p3_vec,tempv);
	vm_vec_add(rod_points[3].p3_vec,bot_point.p3_vec,tempv);
	}

	//now code the four points

	clipping_code codes_and{0xff};
	range_for (auto &i, rod_points)
	{
		codes_and &= g3_code_point(i);
	//clear flags for new points (not projected)
		i.p3_flags = {};
	}
	return codes_and;
}

}

//draw a bitmap object that is always facing you
//returns 1 if off screen, 0 if drew
void g3_draw_rod_tmap(grs_canvas &canvas, grs_bitmap &bitmap, const g3s_point &bot_point, fix bot_width, const g3s_point &top_point, fix top_width, g3s_lrgb light, const tmap_drawer_type tmap_drawer_ptr)
{
	rod_4point rod;
	if (calc_rod_corners(rod, bot_point, bot_width, top_point, top_width) != clipping_code::None)
		return;

	const fix average_light = static_cast<unsigned>(light.r+light.g+light.b)/3;
	const std::array<g3s_uvl, 4> uvl_list{{
		{ 0x0200, 0x0200, average_light },
		{ 0xfe00, 0x0200, average_light },
		{ 0xfe00, 0xfe00, average_light },
		{ 0x0200, 0xfe00, average_light }
	}};
	const std::array<g3s_lrgb, 4> lrgb_list{{
		light,
		light,
		light,
		light,
	}};

	g3_draw_tmap(canvas, rod.point_list, uvl_list, lrgb_list, bitmap, tmap_drawer_ptr);
}


}

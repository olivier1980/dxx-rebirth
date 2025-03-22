/*
 * This file is part of the DXX-Rebirth project <https://www.dxx-rebirth.com/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */
/*
 * 
 * Drawing routines
 * 
 */


#include "dxxerror.h"

#include "3d.h"
#include "globvars.h"
#include "d_enumerate.h"
#include "d_zip.h"
#include "partial_range.h"

namespace dcx {

namespace {

const std::array<GLfloat, 8> build_color_array_from_color_palette_index(const color_palette_index color)
{
	auto &&rgb{PAL2T(color)};
	const GLfloat color_r{rgb.r / 63.0f};
	const GLfloat color_g{rgb.g / 63.0f};
	const GLfloat color_b{rgb.b / 63.0f};
	return {{
		color_r, color_g, color_b, 1.0,
		color_r, color_g, color_b, 1.0,
	}};
}

}

g3_draw_line_colors::g3_draw_line_colors(const color_palette_index color) :
	color_array{build_color_array_from_color_palette_index(color)}
{
}


//returns true if a plane is facing the viewer. takes the unrotated surface 
//normal of the plane, and a point on it.  The normal need not be normalized
bool g3_check_normal_facing(const vms_vector &v,const vms_vector &norm)
{
	return (vm_vec_dot(vm_vec_sub(View_position,v),norm) > 0);
}

bool do_facing_check(const std::array<cg3s_point *, 3> &vertlist)
{
	//normal not specified, so must compute
		//get three points (rotated) and compute normal
	const auto tempv{vm_vec_perp(vertlist[0]->p3_vec, vertlist[1]->p3_vec, vertlist[2]->p3_vec)};
		return (vm_vec_dot(tempv,vertlist[1]->p3_vec) < 0);
}

}

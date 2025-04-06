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
 * object rendering
 *
 */

#include <algorithm>
#include <cstdlib>
#include <stdio.h>

#include "inferno.h"
#include "game.h"
#include "gr.h"
#include "bm.h"
#include "3d.h"
#include "segment.h"
#include "texmap.h"
#include "laser.h"
#include "key.h"
#include "gameseg.h"
#include "textures.h"
#include "object.h"
#include "controls.h"
#include "physics.h"
#include "slew.h"
#include "render.h"
#include "wall.h"
#include "vclip.h"
#include "robot.h"
#include "interp.h"
#include "fireball.h"
#include "laser.h"
#include "dxxerror.h"
#include "ai.h"
#include "morph.h"
#include "cntrlcen.h"
#include "powerup.h"
#include "fuelcen.h"
#include "endlevel.h"
#include "hudmsg.h"
#include "sounds.h"
#include "collide.h"
#include "lighting.h"
#include "newdemo.h"
#include "player.h"
#include "weapon.h"
#include "gauges.h"
#include "multi.h"
#include "text.h"
#include "piggy.h"
#include "switch.h"
#include "gameseq.h"
#include "playsave.h"
#include "timer.h"
#if DXX_USE_EDITOR
#include "editor/editor.h"
#endif

#include "compiler-range_for.h"
#include "d_range.h"
#include "d_levelstate.h"
#include "d_underlying_value.h"
#include "partial_range.h"
#include <utility>

using std::min;
using std::max;

namespace dsx {

namespace {

static void obj_detach_all(object_array &Objects, object_base &parent);
static objnum_t obj_detach_one(object_array &Objects, object &sub);

static int is_proximity_bomb_or_any_smart_mine(const weapon_id_type id)
{
	const auto r = is_proximity_bomb_or_player_smart_mine(id);
#if DXX_BUILD_DESCENT == 2
	if (r)
		return r;
	// superprox dropped by robots have their own ID not considered by is_proximity_bomb_or_player_smart_mine() and since that function is used in many other places, I didn't feel safe to add this weapon type in it
	if (id == weapon_id_type::ROBOT_SUPERPROX_ID)
		return 1;
#endif
	return r;
}

}

/*
 *  Global variables
 */

object *ConsoleObject;					//the object that is the player

#ifndef RELEASE
//set viewer object to next object in array
void object_goto_next_viewer(const object_array &Objects, const object *&viewer)
{
	const auto initial_viewer{viewer};
	const auto oe{Objects.vcptr.end()};
	/* Preconditions:
	 * - There exists an integer `i` such that `i < MAX_OBJECTS` and
	 *   `&Objects[i] == initial_viewer`.
	 * - There exists at least one object not of type `OBJ_NONE`.
	 * If the first precondition holds, then:
	 * - The test for `candidate_object != initial_viewer` will always terminate the
	 *   loop, even if there are no other objects of type other than
	 *   `OBJ_NONE`.
	 * If both preconditions hold, then:
	 * - The loop will always find and return a value.
	 */
	unsigned limit{0};
	for (auto candidate_object = initial_viewer; ++candidate_object != initial_viewer && limit < MAX_OBJECTS; ++limit)
	{
		if (unlikely(candidate_object == oe))
			/* Wrap around to the beginning.  This is only computed if
			 * necessary, so if an object can be found without wrapping, then
			 * `begin()` never needs to be called.
			 */
			candidate_object = &*Objects.vcptr.begin();
		auto &objp = *candidate_object;
		if (objp.type != OBJ_NONE)
		{
			viewer = &objp;
			return;
		}
	}
}
#endif

imobjptridx_t obj_find_first_of_type(fvmobjptridx &vmobjptridx, const object_type_t type)
{
	range_for (const auto &&i, vmobjptridx)
	{
		if (i->type==type)
			return i;
	}
	return object_none;
}

}

namespace dcx {

namespace {

static powerup_type_t build_contained_object_powerup_id_from_untrusted(const uint8_t untrusted)
{
	return untrusted < MAX_POWERUP_TYPES ? powerup_type_t{untrusted} : powerup_type_t{};
}

static robot_id build_contained_object_robot_id_from_untrusted(const uint8_t untrusted)
{
	return untrusted < MAX_ROBOT_TYPES ? robot_id{untrusted} : robot_id{};
}

}

icobjidx_t laser_info::get_last_hitobj() const
{
	if (!hitobj_count)
		/* If no elements, return object_none */
		return object_none;
	/* Return the most recently written element.  `hitobj_pos`
	 * indicates the element to write next, so return
	 * hitobj_values[hitobj_pos - 1].  When hitobj_pos == 0, the
	 * most recently written element is at the end of the array, not
	 * before the beginning of the array.
	 */
	if (!hitobj_pos)
		return hitobj_values.back();
	return hitobj_values[hitobj_pos - 1];
}

contained_object_type build_contained_object_type_from_untrusted(const uint8_t untrusted)
{
	switch (untrusted)
	{
		case object_type_t::OBJ_POWERUP:
		case object_type_t::OBJ_ROBOT:
			return contained_object_type{untrusted};
		default:
			return contained_object_type::None;
	}
}

contained_object_parameters build_contained_object_parameters_from_untrusted(const uint8_t untrusted_type, const uint8_t untrusted_id, const uint8_t untrusted_contains_count)
{
	if (untrusted_contains_count > 4)
		/* This is an arbitrary cap, chosen to match GOODY_COUNT_MAX.  Any
		 * value higher than this is likely an error, so force the object to
		 * contain nothing instead.
		 */
		return {};
	switch (const auto type = build_contained_object_type_from_untrusted(untrusted_type))
	{
		case contained_object_type::powerup:
			return {
				.type = type,
				.id = contained_object_id{.powerup = build_contained_object_powerup_id_from_untrusted(untrusted_id)},
				.count = untrusted_contains_count,
			};
		case contained_object_type::robot:
			return {
				.type = type,
				.id = contained_object_id{.robot = build_contained_object_robot_id_from_untrusted(untrusted_id)},
				.count = untrusted_contains_count,
			};
		default:
			return {};
	}
}

}

namespace dsx {

//draw an object that has one bitmap & doesn't rotate
void draw_object_blob(GameBitmaps_array &GameBitmaps, const object_base &Viewer, grs_canvas &canvas, const object_base &obj, const bitmap_index bmi)
{
	auto &bm = GameBitmaps[bmi];
	PIGGY_PAGE_IN( bmi );

	const auto osize{obj.size};
	// draw these with slight offset to viewer preventing too much ugly clipping
	auto pos = obj.pos;
	if (obj.type == OBJ_FIREBALL && get_fireball_id(obj) == vclip_index::volatile_wall_hit)
	{
		vms_vector offs_vec;
		vm_vec_normalized_dir_quick(offs_vec, Viewer.pos, pos);
		vm_vec_scale_add2(pos,offs_vec,F1_0);
	}

	using wh = std::pair<fix, fix>;
	const auto bm_w = bm.bm_w;
	const auto bm_h = bm.bm_h;
	const auto p = (bm_w > bm_h)
		? wh(osize, fixmuldiv(osize, bm_h, bm_w))
		: wh(fixmuldiv(osize, bm_w, bm_h), osize);
	g3_draw_bitmap(canvas, pos, p.first, p.second, bm);
}

//draw an object that is a texture-mapped rod
void draw_object_tmap_rod(grs_canvas &canvas, const d_level_unique_light_state *const LevelUniqueLightState, const vcobjptridx_t obj, const bitmap_index bitmapi)
{
	PIGGY_PAGE_IN(bitmapi);

	auto &bitmap = GameBitmaps[bitmapi];

	const auto delta{vm_vec_copy_scale(obj->orient.uvec, obj->size)};

	const auto top_v = vm_vec_add(obj->pos,delta);

	const auto top_p = g3_rotate_point(top_v);
	const auto bot_p{g3_rotate_point(vm_vec_sub(obj->pos, delta))};

	g3_draw_rod_tmap(canvas, bitmap, bot_p, obj->size, top_p, obj->size, (LevelUniqueLightState ? compute_object_light(*LevelUniqueLightState, obj) : g3s_lrgb{F1_0, F1_0, F1_0}), draw_tmap);
}

//used for robot engine glow
#define MAX_VELOCITY i2f(50)

//what darkening level to use when cloaked
#define CLOAKED_FADE_LEVEL		28

#define	CLOAK_FADEIN_DURATION_PLAYER	F2_0
#define	CLOAK_FADEOUT_DURATION_PLAYER	F2_0

#define	CLOAK_FADEIN_DURATION_ROBOT	F1_0
#define	CLOAK_FADEOUT_DURATION_ROBOT	F1_0

namespace {

//do special cloaked render
static void draw_cloaked_object(grs_canvas &canvas, const object_base &obj, const g3s_lrgb light, glow_values_t glow, const fix64 cloak_start_time, const fix total_cloaked_time, const fix Cloak_fadein_duration, const fix Cloak_fadeout_duration)
{
	fix light_scale=F1_0;
	int cloak_value{0};
	bool fading{};		//if true, fading, else cloaking

	if (const auto cloak_delta_time{GameTime64 - cloak_start_time}; cloak_delta_time < Cloak_fadein_duration/2) {

#if DXX_BUILD_DESCENT == 1
		light_scale = Cloak_fadein_duration/2 - cloak_delta_time;
#elif DXX_BUILD_DESCENT == 2
		light_scale = fixdiv(Cloak_fadein_duration/2 - cloak_delta_time,Cloak_fadein_duration/2);
#endif
		fading = true;
	}
	else if (cloak_delta_time < Cloak_fadein_duration) {

#if DXX_BUILD_DESCENT == 1
		cloak_value = f2i((cloak_delta_time - Cloak_fadein_duration/2) * CLOAKED_FADE_LEVEL);
#elif DXX_BUILD_DESCENT == 2
		cloak_value = f2i(fixdiv(cloak_delta_time - Cloak_fadein_duration/2,Cloak_fadein_duration/2) * CLOAKED_FADE_LEVEL);
#endif

	} else if (GameTime64 < (cloak_start_time + total_cloaked_time) -Cloak_fadeout_duration) {
		static int cloak_delta=0,cloak_dir=1;
		static fix cloak_timer=0;

		//note, if more than one cloaked object is visible at once, the
		//pulse rate will change!

		cloak_timer -= FrameTime;
		while (cloak_timer < 0) {

			cloak_timer += Cloak_fadeout_duration/12;

			cloak_delta += cloak_dir;

			if (cloak_delta==0 || cloak_delta==4)
				cloak_dir = -cloak_dir;
		}

		cloak_value = CLOAKED_FADE_LEVEL - cloak_delta;
	} else if (GameTime64 < (cloak_start_time + total_cloaked_time) -Cloak_fadeout_duration/2) {

#if DXX_BUILD_DESCENT == 1
		cloak_value = f2i((total_cloaked_time - Cloak_fadeout_duration/2 - cloak_delta_time) * CLOAKED_FADE_LEVEL);
#elif DXX_BUILD_DESCENT == 2
		cloak_value = f2i(fixdiv(total_cloaked_time - Cloak_fadeout_duration/2 - cloak_delta_time,Cloak_fadeout_duration/2) * CLOAKED_FADE_LEVEL);
#endif

	} else {

#if DXX_BUILD_DESCENT == 1
		light_scale = Cloak_fadeout_duration/2 - (total_cloaked_time - cloak_delta_time);
#elif DXX_BUILD_DESCENT == 2
		light_scale = fixdiv(Cloak_fadeout_duration/2 - (total_cloaked_time - cloak_delta_time),Cloak_fadeout_duration/2);
#endif
		fading = true;
	}

	const auto alt_textures{
#if DXX_BUILD_DESCENT == 2
		!fading
			? alternate_textures{}
			:
#endif
				({
					const std::size_t ati{static_cast<std::size_t>(obj.rtype.pobj_info.alt_textures) - 1u};
					ati < std::size(multi_player_textures)
					? alternate_textures{multi_player_textures[ati]}
					: alternate_textures{};
				})
	};

	auto &Polygon_models = LevelSharedPolygonModelState.Polygon_models;
	if (fading) {
		glow[0] = fixmul(glow[0],light_scale);
		draw_polygon_model(Polygon_models, canvas, draw_tmap, obj.pos,
				   obj.orient,
				   obj.rtype.pobj_info.anim_angles,
				   obj.rtype.pobj_info.model_num, obj.rtype.pobj_info.subobj_flags,
				   g3s_lrgb{
						.r = fixmul(light.r, light_scale),
						.g = fixmul(light.g, light_scale),
						.b = fixmul(light.b, light_scale)
				   },
				   &glow,
				   alt_textures );
	}
	else {
		gr_settransblend(canvas, static_cast<gr_fade_level>(cloak_value), gr_blend::normal);
		//use special flat drawer
		draw_polygon_model(Polygon_models, canvas, draw_tmap_flat, obj.pos,
				   obj.orient,
				   obj.rtype.pobj_info.anim_angles,
				   obj.rtype.pobj_info.model_num, obj.rtype.pobj_info.subobj_flags,
				   light,
				   &glow,
				   alt_textures );
		gr_settransblend(canvas, GR_FADE_OFF, gr_blend::normal);
	}

}

//draw an object which renders as a polygon model
static void draw_polygon_object(grs_canvas &canvas, const d_level_unique_light_state &LevelUniqueLightState, const vcobjptridx_t obj)
{
	auto &BossUniqueState = LevelUniqueObjectState.BossState;
	auto &Robot_info = LevelSharedRobotInfoState.Robot_info;
	g3s_lrgb light;
	glow_values_t engine_glow_value;
	engine_glow_value[0] = 0;
#if DXX_BUILD_DESCENT == 2
	engine_glow_value[1] = -1;		//element 0 is for engine glow, 1 for headlight
#endif

	//	If option set for bright players in netgame, brighten them!
	light = unlikely(Netgame.BrightPlayers && (Game_mode & GM_MULTI) && obj->type == OBJ_PLAYER)
		? g3s_lrgb{F1_0 * 2, F1_0 * 2, F1_0 * 2}
		: compute_object_light(LevelUniqueLightState, obj);

#if DXX_BUILD_DESCENT == 2
	//make robots brighter according to robot glow field
	if (obj->type == OBJ_ROBOT)
	{
		const auto glow = Robot_info[get_robot_id(obj)].glow<<12;
		light.r += glow; //convert 4:4 to 16:16
		light.g += glow; //convert 4:4 to 16:16
		light.b += glow; //convert 4:4 to 16:16
	}

	if ((obj->type == OBJ_WEAPON &&
			get_weapon_id(obj) == weapon_id_type::FLARE_ID) ||
		obj->type == OBJ_MARKER
		)
		{
			light.r += F1_0*2;
			light.g += F1_0*2;
			light.b += F1_0*2;
		}
#endif

	push_interpolation_method imsave(1, true);

	//set engine glow value
	engine_glow_value[0] = f1_0/5;
	if (obj->movement_source == object::movement_type::physics) {

		if (obj->mtype.phys_info.flags & PF_USES_THRUST && obj->type==OBJ_PLAYER && get_player_id(obj)==Player_num) {
			const auto thrust_mag = vm_vec_mag_quick(obj->mtype.phys_info.thrust);
			engine_glow_value[0] += (fixdiv(thrust_mag,Player_ship->max_thrust)*4)/5;
		}
		else {
			const auto speed = vm_vec_mag_quick(obj->mtype.phys_info.velocity);
#if DXX_BUILD_DESCENT == 1
			engine_glow_value[0] += (fixdiv(speed,MAX_VELOCITY)*4)/5;
#elif DXX_BUILD_DESCENT == 2
			engine_glow_value[0] += (fixdiv(speed,MAX_VELOCITY)*3)/5;
#endif
		}
	}

#if DXX_BUILD_DESCENT == 2
	//set value for player headlight
	if (obj->type == OBJ_PLAYER) {
		auto &player_flags = obj->ctype.player_info.powerup_flags;
		if (player_flags & PLAYER_FLAGS_HEADLIGHT && !Endlevel_sequence)
			if (player_flags & PLAYER_FLAGS_HEADLIGHT_ON)
				engine_glow_value[1] = -2;		//draw white!
			else
				engine_glow_value[1] = -1;		//draw normal color (grey)
		else
			engine_glow_value[1] = -3;			//don't draw
	}
#endif

	auto &Polygon_models = LevelSharedPolygonModelState.Polygon_models;
	if (obj->rtype.pobj_info.tmap_override != -1) {
		std::array<bitmap_index, 12> bm_ptrs;

		//fill whole array, in case simple model needs more
		bm_ptrs.fill(Textures[obj->rtype.pobj_info.tmap_override]);
		draw_polygon_model(Polygon_models, canvas, draw_tmap, obj->pos,
				   obj->orient,
				   obj->rtype.pobj_info.anim_angles,
				   obj->rtype.pobj_info.model_num,
				   obj->rtype.pobj_info.subobj_flags,
				   light,
				   &engine_glow_value,
				   bm_ptrs);
	}
	else {
		std::pair<fix64, fix> cloak_duration;
		std::pair<fix, fix> cloak_fade;
		if (obj->type==OBJ_PLAYER && (obj->ctype.player_info.powerup_flags & PLAYER_FLAGS_CLOAKED))
		{
			auto &cloak_time = obj->ctype.player_info.cloak_time;
			cloak_duration = {cloak_time, CLOAK_TIME_MAX};
			cloak_fade = {CLOAK_FADEIN_DURATION_PLAYER, CLOAK_FADEOUT_DURATION_PLAYER};
		}
		else if ((obj->type == OBJ_ROBOT) && (obj->ctype.ai_info.CLOAKED)) {
			if (Robot_info[get_robot_id(obj)].boss_flag != boss_robot_id::None)
				cloak_duration = {BossUniqueState.Boss_cloak_start_time, Boss_cloak_duration};
			else
				cloak_duration = {GameTime64-F1_0*10, F1_0 * 20};
			cloak_fade = {CLOAK_FADEIN_DURATION_ROBOT, CLOAK_FADEOUT_DURATION_ROBOT};
		} else {
#if DXX_BUILD_DESCENT == 2
			if (obj->type == OBJ_ROBOT)
			{
			//	Snipers get bright when they fire.
				if (obj->ctype.ai_info.ail.next_fire < F1_0/8) {
				if (obj->ctype.ai_info.behavior == ai_behavior::AIB_SNIPE)
				{
					light.r = 2*light.r + F1_0;
					light.g = 2*light.g + F1_0;
					light.b = 2*light.b + F1_0;
				}
			}
			}
#endif

			const auto alt_textures{
				({
					const std::size_t ati{static_cast<std::size_t>(obj->rtype.pobj_info.alt_textures) - 1u};
					ati < std::size(multi_player_textures)
					? alternate_textures{multi_player_textures[ati]}
					: alternate_textures{};
				})
			};
			const auto is_weapon_with_inner_model = (obj->type == OBJ_WEAPON && Weapon_info[get_weapon_id(obj)].model_num_inner != polygon_model_index::None);
			bool draw_simple_model;
			if (is_weapon_with_inner_model)
			{
				gr_settransblend(canvas, GR_FADE_OFF, gr_blend::additive_a);
				draw_simple_model = static_cast<fix>(vm_vec_dist_quick(Viewer->pos, obj->pos)) < Simple_model_threshhold_scale * F1_0*2;
				if (draw_simple_model)
					draw_polygon_model(Polygon_models, canvas, draw_tmap, obj->pos,
							   obj->orient,
							   obj->rtype.pobj_info.anim_angles,
							   Weapon_info[get_weapon_id(obj)].model_num_inner,
							   obj->rtype.pobj_info.subobj_flags,
							   light,
							   &engine_glow_value,
							   alt_textures);
			}
			draw_polygon_model(Polygon_models, canvas, draw_tmap, obj->pos,
					   obj->orient,
					   obj->rtype.pobj_info.anim_angles,obj->rtype.pobj_info.model_num,
					   obj->rtype.pobj_info.subobj_flags,
					   light,
					   &engine_glow_value,
					   alt_textures);

			if (is_weapon_with_inner_model)
			{
				if constexpr (!DXX_USE_OGL) // in software rendering must draw inner model last
				{
				gr_settransblend(canvas, GR_FADE_OFF, gr_blend::additive_a);
				if (draw_simple_model)
					draw_polygon_model(Polygon_models, canvas, draw_tmap, obj->pos,
							   obj->orient,
							   obj->rtype.pobj_info.anim_angles,
							   Weapon_info[obj->id].model_num_inner,
							   obj->rtype.pobj_info.subobj_flags,
							   light,
							   &engine_glow_value,
							   alt_textures);
				}
				gr_settransblend(canvas, GR_FADE_OFF, gr_blend::normal);
			}
			return;
		}
		draw_cloaked_object(canvas, obj, light, engine_glow_value, cloak_duration.first, cloak_duration.second, cloak_fade.first, cloak_fade.second);
	}
}

}

}

//------------------------------------------------------------------------------
// These variables are used to keep a list of the 3 closest robots to the viewer.
// The code works like this: Every time render object is called with a polygon model,
// it finds the distance of that robot to the viewer.  If this distance if within 10
// segments of the viewer, it does the following: If there aren't already 3 robots in
// the closet-robots list, it just sticks that object into the list along with its distance.
// If the list already contains 3 robots, then it finds the robot in that list that is
// farthest from the viewer. If that object is farther than the object currently being
// rendered, then the new object takes over that far object's slot.  *Then* after all
// objects are rendered, object_render_targets is called an it draws a target on top
// of all the objects.

//091494: #define MAX_CLOSE_ROBOTS 3
//--unused-- static int Object_draw_lock_boxes = 0;
//091494: static int Object_num_close = 0;
//091494: static object * Object_close_ones[MAX_CLOSE_ROBOTS];
//091494: static fix Object_close_distance[MAX_CLOSE_ROBOTS];

//091494: set_close_objects(object *obj)
//091494: {
//091494: 	fix dist;
//091494:
//091494: 	if ( (obj->type != OBJ_ROBOT) || (Object_draw_lock_boxes==0) )	
//091494: 		return;
//091494:
//091494: 	// The following code keeps a list of the 10 closest robots to the
//091494: 	// viewer.  See comments in front of this function for how this works.
//091494: 	dist = vm_vec_dist( &obj->pos, &Viewer->pos );
//091494: 	if ( dist < i2f(20*10) )	{				
//091494: 		if ( Object_num_close < MAX_CLOSE_ROBOTS )	{
//091494: 			Object_close_ones[Object_num_close] = obj;
//091494: 			Object_close_distance[Object_num_close] = dist;
//091494: 			Object_num_close++;
//091494: 		} else {
//091494: 			int i, farthest_robot;
//091494: 			fix farthest_distance;
//091494: 			// Find the farthest robot in the list
//091494: 			farthest_robot = 0;
//091494: 			farthest_distance = Object_close_distance[0];
//091494: 			for (i=1; i<Object_num_close; i++ )	{
//091494: 				if ( Object_close_distance[i] > farthest_distance )	{
//091494: 					farthest_distance = Object_close_distance[i];
//091494: 					farthest_robot = i;
//091494: 				}
//091494: 			}
//091494: 			// If this object is closer to the viewer than
//091494: 			// the farthest in the list, replace the farthest with this object.
//091494: 			if ( farthest_distance > dist )	{
//091494: 				Object_close_ones[farthest_robot] = obj;
//091494: 				Object_close_distance[farthest_robot] = dist;
//091494: 			}
//091494: 		}
//091494: 	}
//091494: }

namespace dcx {
objnum_t	Player_fired_laser_this_frame=object_none;

namespace {

static bool predicate_debris(const object_base &o)
{
	return o.type == OBJ_DEBRIS;
}

static bool predicate_flare(const object_base &o)
{
	return (o.type == OBJ_WEAPON) && (get_weapon_id(o) == weapon_id_type::FLARE_ID);
}

static bool predicate_nonflare_weapon(const object_base &o)
{
	return (o.type == OBJ_WEAPON) && (get_weapon_id(o) != weapon_id_type::FLARE_ID);
}

}

}

namespace dsx {

namespace {

static bool predicate_fireball(const object &o)
{
	return o.type == OBJ_FIREBALL && o.ctype.expl_info.delete_objnum == object_none;
}

// -----------------------------------------------------------------------------
//this routine checks to see if an robot rendered near the middle of
//the screen, and if so and the player had fired, "warns" the robot
static void set_robot_location_info(object &objp)
{
	auto &Objects = LevelUniqueObjectState.Objects;
	auto &vcobjptr = Objects.vcptr;
	if (Player_fired_laser_this_frame != object_none) {
		const auto &&temp = g3_rotate_point(objp.pos);
		if ((temp.p3_codes & clipping_code::behind) != clipping_code::None)		//robot behind the screen
			return;

		//the code below to check for object near the center of the screen
		//completely ignores z, which may not be good

		if ((abs(temp.p3_vec.x) < F1_0*4) && (abs(temp.p3_vec.y) < F1_0*4)) {
			objp.ctype.ai_info.danger_laser_num = Player_fired_laser_this_frame;
			objp.ctype.ai_info.danger_laser_signature = vcobjptr(Player_fired_laser_this_frame)->signature;
		}
	}
}

static const object *Viewer_save;
}

//	------------------------------------------------------------------------------------------------------------------
void create_small_fireball_on_object(const vmobjptridx_t objp, fix size_scale, int sound_flag)
{
	auto &Objects = LevelUniqueObjectState.Objects;
	fix			size;
	vms_vector	pos;

	pos = objp->pos;
	auto rand_vec = make_random_vector();

	vm_vec_scale(rand_vec, objp->size/2);

	vm_vec_add2(pos, rand_vec);

#if DXX_BUILD_DESCENT == 1
	size = fixmul(size_scale, F1_0 + d_rand()*4);
#elif DXX_BUILD_DESCENT == 2
	size = fixmul(size_scale, F1_0/2 + d_rand()*4/2);
#endif

	const auto &&segnum = find_point_seg(LevelSharedSegmentState, LevelUniqueSegmentState, pos, Segments.vmptridx(objp->segnum));
	if (segnum != segment_none) {
		const auto &&expl_obj = object_create_explosion_without_damage(Vclip, segnum, pos, size, vclip_index::small_explosion);
		if (!expl_obj)
			return;
		obj_attach(Objects, objp, expl_obj);
		if (d_rand() < 8192) {
			fix	vol = F1_0/2;
			if (objp->type == OBJ_ROBOT)
				vol *= 2;
			if (sound_flag)
				digi_link_sound_to_object(sound_effect::SOUND_EXPLODING_WALL, objp, 0, vol, sound_stack::allow_stacking);
		}
	}
}

// -- mk, 02/05/95 -- #define	VCLIP_INVULNERABILITY_EFFECT	vclip_index::small_explosion
// -- mk, 02/05/95 --
// -- mk, 02/05/95 -- // -----------------------------------------------------------------------------
// -- mk, 02/05/95 -- void do_player_invulnerability_effect(object *objp)
// -- mk, 02/05/95 -- {
// -- mk, 02/05/95 -- 	if (d_rand() < FrameTime*8) {
// -- mk, 02/05/95 -- 		create_vclip_on_object(objp, F1_0, VCLIP_INVULNERABILITY_EFFECT);
// -- mk, 02/05/95 -- 	}
// -- mk, 02/05/95 -- }

// -----------------------------------------------------------------------------
//	Render an object.  Calls one of several routines based on type
void render_object(grs_canvas &canvas, const d_level_unique_light_state &LevelUniqueLightState, const vmobjptridx_t obj)
{
	if (unlikely(obj == Viewer))
		return;
	if (unlikely(obj->type==OBJ_NONE))
	{
		Int3();
		return;
	}

#if !DXX_USE_OGL
	const auto mld_save = std::exchange(Max_linear_depth, Max_linear_depth_objects);
#endif

	bool alpha = false;
	switch (obj->render_type)
	{
		case render_type::RT_NONE:
			break; //doesn't render, like the player

		case render_type::RT_POLYOBJ:
#if DXX_BUILD_DESCENT == 2
			if ( PlayerCfg.AlphaBlendMarkers && obj->type == OBJ_MARKER ) // set nice transparency/blending for certrain objects
			{
				alpha = true;
				gr_settransblend(canvas, gr_fade_level{10}, gr_blend::additive_a);
			}
#endif


			draw_polygon_object(canvas, LevelUniqueLightState, obj);

			if (obj->type == OBJ_ROBOT) //"warn" robot if being shot at
				set_robot_location_info(obj);
			break;

		case render_type::RT_MORPH:
			draw_morph_object(canvas, LevelUniqueLightState, obj);
			break;

		case render_type::RT_FIREBALL:
			if (PlayerCfg.AlphaBlendFireballs) // set nice transparency/blending for certrain objects
			{
				alpha = true;
				gr_settransblend(canvas, GR_FADE_OFF, gr_blend::additive_c);
			}

			draw_fireball(Vclip, canvas, obj);
			break;

		case render_type::RT_WEAPON_VCLIP:
			if (PlayerCfg.AlphaBlendWeapons && (!is_proximity_bomb_or_any_smart_mine(get_weapon_id(obj))
                )) // set nice transparency/blending for certain objects
			{
				alpha = true;
				gr_settransblend(canvas, gr_fade_level{7}, gr_blend::additive_a);
			}

			draw_weapon_vclip(Vclip, Weapon_info, canvas, obj);
			break;

		case render_type::RT_HOSTAGE:
			draw_hostage(Vclip, canvas, LevelUniqueLightState, obj);
			break;

		case render_type::RT_POWERUP:
			if (PlayerCfg.AlphaBlendPowerups) // set nice transparency/blending for certrain objects
				switch ( get_powerup_id(obj) )
				{
					case powerup_type_t::POW_EXTRA_LIFE:
					case powerup_type_t::POW_ENERGY:
					case powerup_type_t::POW_SHIELD_BOOST:
					case powerup_type_t::POW_CLOAK:
					case powerup_type_t::POW_INVULNERABILITY:
#if DXX_BUILD_DESCENT == 2
					case powerup_type_t::POW_HOARD_ORB:
#endif
						alpha = true;
						gr_settransblend(canvas, gr_fade_level{7}, gr_blend::additive_a);
						break;
					case powerup_type_t::POW_LASER:
					case powerup_type_t::POW_KEY_BLUE:
					case powerup_type_t::POW_KEY_RED:
					case powerup_type_t::POW_KEY_GOLD:
					case powerup_type_t::POW_MISSILE_1:
					case powerup_type_t::POW_MISSILE_4:
					case powerup_type_t::POW_QUAD_FIRE:
					case powerup_type_t::POW_VULCAN_WEAPON:
					case powerup_type_t::POW_SPREADFIRE_WEAPON:
					case powerup_type_t::POW_PLASMA_WEAPON:
					case powerup_type_t::POW_FUSION_WEAPON:
					case powerup_type_t::POW_PROXIMITY_WEAPON:
					case powerup_type_t::POW_HOMING_AMMO_1:
					case powerup_type_t::POW_HOMING_AMMO_4:
					case powerup_type_t::POW_SMARTBOMB_WEAPON:
					case powerup_type_t::POW_MEGA_WEAPON:
					case powerup_type_t::POW_VULCAN_AMMO:
					case powerup_type_t::POW_TURBO:
					case powerup_type_t::POW_MEGAWOW:
#if DXX_BUILD_DESCENT == 2
					case powerup_type_t::POW_FULL_MAP:
					case powerup_type_t::POW_HEADLIGHT:
					case powerup_type_t::POW_GAUSS_WEAPON:
					case powerup_type_t::POW_HELIX_WEAPON:
					case powerup_type_t::POW_PHOENIX_WEAPON:
					case powerup_type_t::POW_OMEGA_WEAPON:
					case powerup_type_t::POW_SUPER_LASER:
					case powerup_type_t::POW_CONVERTER:
					case powerup_type_t::POW_AMMO_RACK:
					case powerup_type_t::POW_AFTERBURNER:
					case powerup_type_t::POW_SMISSILE1_1:
					case powerup_type_t::POW_SMISSILE1_4:
					case powerup_type_t::POW_GUIDED_MISSILE_1:
					case powerup_type_t::POW_GUIDED_MISSILE_4:
					case powerup_type_t::POW_SMART_MINE:
					case powerup_type_t::POW_MERCURY_MISSILE_1:
					case powerup_type_t::POW_MERCURY_MISSILE_4:
					case powerup_type_t::POW_EARTHSHAKER_MISSILE:
					case powerup_type_t::POW_FLAG_BLUE:
					case powerup_type_t::POW_FLAG_RED:
#endif
						break;
				}

			draw_powerup(Vclip, canvas, obj);
			break;

		case render_type::RT_LASER:
			if (PlayerCfg.AlphaBlendLasers) // set nice transparency/blending for certrain objects
			{
				alpha = true;
				gr_settransblend(canvas, gr_fade_level{7}, gr_blend::additive_a);
			}

			Laser_render(canvas, obj);
			break;

		default:
			Error("Unknown render_type <%d>", underlying_value(obj->render_type));
	}

	if (alpha)
		gr_settransblend(canvas, GR_FADE_OFF, gr_blend::normal); // revert any transparency/blending setting back to normal

	if (obj->render_type != render_type::RT_NONE && Newdemo_state == ND_STATE_RECORDING)
		newdemo_record_render_object(obj);
#if !DXX_USE_OGL
	Max_linear_depth = mld_save;
#endif
}

void reset_player_object(object_base &ConsoleObject)
{
	//Init physics

	ConsoleObject.mtype.phys_info.velocity = {};
	ConsoleObject.mtype.phys_info.thrust = {};
	ConsoleObject.mtype.phys_info.rotvel = {};
	ConsoleObject.mtype.phys_info.rotthrust = {};
	ConsoleObject.mtype.phys_info.turnroll = 0;
	ConsoleObject.mtype.phys_info.mass = Player_ship->mass;
	ConsoleObject.mtype.phys_info.drag = Player_ship->drag;
	ConsoleObject.mtype.phys_info.flags =
#ifndef DXX_FEATURE_DISABLE_SHIP_WIGGLE
		PF_WIGGLE |
#endif
		PF_TURNROLL | PF_LEVELLING | PF_USES_THRUST;

	//Init render info

	ConsoleObject.render_type = render_type::RT_POLYOBJ;
	ConsoleObject.rtype.pobj_info.model_num = Player_ship->model_num;		//what model is this?
	ConsoleObject.rtype.pobj_info.subobj_flags = 0;		//zero the flags
	ConsoleObject.rtype.pobj_info.tmap_override = -1;		//no tmap override!
	ConsoleObject.rtype.pobj_info.anim_angles = {};

	// Clear misc

	ConsoleObject.flags = 0;
}

//make object0 the player, setting all relevant fields
void init_player_object(const d_level_shared_polygon_model_state &LevelSharedPolygonModelState, object_base &console)
{
	console.type = OBJ_PLAYER;
	set_player_id(console, 0);					//no sub-types for player
	console.signature = object_signature_t{0};
	auto &Polygon_models = LevelSharedPolygonModelState.Polygon_models;
	console.size = Polygon_models[Player_ship->model_num].rad;
	console.control_source = object::control_type::slew;			//default is player slewing
	console.movement_source = object::movement_type::physics;		//change this sometime
	console.lifeleft = IMMORTAL_TIME;
	console.attached_obj = object_none;
	reset_player_object(console);
}

//sets up the free list & init player & whatever else
void init_objects()
{
	auto &Objects = LevelUniqueObjectState.Objects;
	auto &vmobjptr = Objects.vmptr;
	for (objnum_t i = 0; i< MAX_OBJECTS; ++i)
	{
		LevelUniqueObjectState.free_obj_list[i] = i;
		auto &obj = *vmobjptr(i);
		DXX_POISON_VAR(obj, 0xfd);
		obj.type = OBJ_NONE;
	}

	range_for (unique_segment &j, Segments)
		j.objects = object_none;

	Viewer = ConsoleObject = &Objects.front();

	init_player_object(LevelSharedPolygonModelState, *ConsoleObject);
	obj_link_unchecked(Objects.vmptr, Objects.vmptridx(ConsoleObject), Segments.vmptridx(segment_first));	//put in the world in segment 0
	LevelUniqueObjectState.num_objects = 1;						//just the player
	Objects.set_count(1);
}

//after calling init_object(), the network code has grabbed specific
//object slots without allocating them.  Go though the objects & build
//the free list, then set the appropriate globals
void special_reset_objects(d_level_unique_object_state &LevelUniqueObjectState, const d_robot_info_array &Robot_info)
{
	unsigned num_objects = MAX_OBJECTS;

	auto &Objects = LevelUniqueObjectState.get_objects();
	Objects.set_count(1);
	assert(Objects.front().type != OBJ_NONE);		//0 should be used

	DXX_POISON_VAR(LevelUniqueObjectState.free_obj_list, 0xfd);
#if DXX_BUILD_DESCENT == 1
	/* Descent 1 does not have a guidebot, so there is nothing to fix up.  For
	 * simplicity, both games pass the parameter.
	 */
	(void)Robot_info;
#elif DXX_BUILD_DESCENT == 2
	icobjidx_t Buddy_objnum = object_none;
#endif
	for (objnum_t i = MAX_OBJECTS; i--;)
	{
		const auto &obj = *Objects.vcptr(i);
#if DXX_BUILD_DESCENT == 2
		if (obj.type == OBJ_ROBOT)
		{
			auto &robptr = Robot_info[get_robot_id(obj)];
			if (robot_is_companion(robptr))
				Buddy_objnum = i;
		}
#endif
		if (obj.type == OBJ_NONE)
			LevelUniqueObjectState.free_obj_list[--num_objects] = i;
		else
			if (i > Highest_object_index)
				Objects.set_count(i + 1);
	}
#if DXX_BUILD_DESCENT == 2
	LevelUniqueObjectState.BuddyState.Buddy_objnum = Buddy_objnum;
#endif
	LevelUniqueObjectState.num_objects = num_objects;
}

void obj_link_unchecked(fvmobjptr &vmobjptr, const vmobjptridx_t obj, const vmsegptridx_t segnum)
{
	obj->segnum = segnum;
	unique_segment &useg = segnum;
	obj->next = std::exchange(useg.objects, obj);
	obj->prev = object_none;

	if (obj->next != object_none)
		vmobjptr(obj->next)->prev = obj;
}

void obj_unlink(fvmobjptr &vmobjptr, fvmsegptr &vmsegptr, object_base &obj)
{
	const auto next{obj.next};
	/* It is a bug elsewhere if vmsegptr ever fails here.  However, it is
	 * expensive to check, so only force verification in debug builds.
	 *
	 * In debug builds, always compute it, for the side effect of
	 * validating the segment number.
	 *
	 * In release builds, compute it when it is needed.
	 */
#ifndef NDEBUG
	unique_segment &segp = vmsegptr(obj.segnum);
#endif
	((obj.prev == object_none)
		? (
#ifdef NDEBUG
			static_cast<unique_segment &>(vmsegptr(obj.segnum))
#else
			segp
#endif
		).objects
		: vmobjptr(obj.prev)->next) = next;

	obj.segnum = segment_none;

	if (next != object_none)
		vmobjptr(next)->prev = obj.prev;
	DXX_POISON_VAR(obj.next, 0xfa);
	DXX_POISON_VAR(obj.prev, 0xfa);
}

//returns the number of a free object, updating Highest_object_index.
//Generally, obj_create() should be called to get an object, since it
//fills in important fields and does the linking.
//returns -1 if no free objects
imobjptridx_t obj_allocate(d_level_unique_object_state &LevelUniqueObjectState)
{
	auto &Objects = LevelUniqueObjectState.Objects;
	if (LevelUniqueObjectState.num_objects >= Objects.size())
		return object_none;

	const auto objnum = LevelUniqueObjectState.free_obj_list[LevelUniqueObjectState.num_objects++];
	if (objnum >= Objects.get_count())
	{
		Objects.set_count(objnum + 1);
	}
	const auto &&r = Objects.vmptridx(objnum);
	assert(r->type == OBJ_NONE);
	return r;
}

namespace {

//frees up an object.  Generally, obj_delete() should be called to get
//rid of an object.  This function deallocates the object entry after
//the object has been unlinked
static void obj_free(d_level_unique_object_state &LevelUniqueObjectState, const vmobjidx_t objnum)
{
	const auto num_objects = -- LevelUniqueObjectState.num_objects;
	assert(num_objects < LevelUniqueObjectState.free_obj_list.size());
	LevelUniqueObjectState.free_obj_list[num_objects] = objnum;
	auto &Objects = LevelUniqueObjectState.get_objects();

	objnum_t o = objnum;
	if (o == Highest_object_index)
	{
		for (;;)
		{
			--o;
			if (Objects.vcptr(o)->type != OBJ_NONE)
				break;
			if (o == 0)
				break;
		}
		Objects.set_count(o + 1);
	}
}

//-----------------------------------------------------------------------------
//	Scan the object list, freeing down to num_used objects
//	Returns number of slots freed.
static void free_object_slots(uint_fast32_t num_used)
{
	auto &Objects = LevelUniqueObjectState.Objects;
	auto &vmobjptr = Objects.vmptr;
	std::array<object *, MAX_OBJECTS>	obj_list;
	unsigned	num_already_free, num_to_free, olind = 0;

	num_already_free = MAX_OBJECTS - Highest_object_index - 1;

	if (MAX_OBJECTS - num_already_free < num_used)
		return;

	for (auto &obj : vmobjptr)
	{
		if (obj.flags & OF_SHOULD_BE_DEAD)
		{
			num_already_free++;
			if (MAX_OBJECTS - num_already_free < num_used)
				return;
		} else
			switch (obj.type)
			{
				case OBJ_NONE:
					num_already_free++;
					if (MAX_OBJECTS - num_already_free < num_used)
						return;
					break;
				case OBJ_WALL:
					Int3();		//	This is curious.  What is an object that is a wall?
					break;
				case OBJ_FIREBALL:
				case OBJ_WEAPON:
				case OBJ_DEBRIS:
					obj_list[olind++] = &obj;
					break;
				case OBJ_ROBOT:
				case OBJ_HOSTAGE:
				case OBJ_PLAYER:
				case OBJ_CNTRLCEN:
				case OBJ_CLUTTER:
				case OBJ_GHOST:
				case OBJ_LIGHT:
				case OBJ_CAMERA:
				case OBJ_POWERUP:
				case OBJ_COOP:
				case OBJ_MARKER:
					break;
			}

	}

	num_to_free = MAX_OBJECTS - num_used - num_already_free;

	if (num_to_free > olind) {
		num_to_free = olind;
	}

	// Capture before num_to_free modified
	const auto &&r = partial_const_range(obj_list, num_to_free);
	auto l = [&vmobjptr, &r, &num_to_free](const auto predicate) -> bool {
		range_for (const auto i, r)
		{
			auto &o = *vmobjptr(i);
			if (predicate(o))
			{
				o.flags |= OF_SHOULD_BE_DEAD;
				if (!-- num_to_free)
					return true;
			}
		}
		return false;
	};

	if (l(predicate_debris))
		return;

	if (l(predicate_fireball))
		return;

	if (l(predicate_flare))
		return;

	if (l(predicate_nonflare_weapon))
		return;
}

}

//-----------------------------------------------------------------------------
//initialize a new object.  adds to the list for the given segment
//note that segnum is really just a suggestion, since this routine actually
//searches for the correct segment
//returns the object number
imobjptridx_t obj_create(d_level_unique_object_state &LevelUniqueObjectState, const d_level_shared_segment_state &LevelSharedSegmentState, d_level_unique_segment_state &LevelUniqueSegmentState, const object_type_t type, const unsigned id, vmsegptridx_t segnum, const vms_vector &pos, const vms_matrix *const orient, const fix size, const typename object::control_type ctype, const typename object::movement_type mtype, const render_type rtype)
{
	auto &LevelSharedVertexState = LevelSharedSegmentState.get_vertex_state();
	auto &Objects = LevelUniqueObjectState.Objects;
	auto &Vertices = LevelSharedVertexState.get_vertices();
	// Some consistency checking. FIXME: Add more debug output here to probably trace all possible occurances back.
	assert(ctype <= object::control_type::cntrlcen);

	if (type == OBJ_DEBRIS && LevelUniqueObjectState.Debris_object_count >= Max_debris_objects && !PERSISTENT_DEBRIS)
		return object_none;

	auto &vcvertptr = Vertices.vcptr;
	if (get_seg_masks(vcvertptr, pos, segnum, 0).centermask != sidemask_t{})
	{
		const auto &&p = find_point_seg(LevelSharedSegmentState, LevelUniqueSegmentState, pos, segnum);
		if (p == segment_none) {
			return object_none;		//don't create this object
		}
		segnum = p;
	}

	// Find next free object
	const auto &&obj = obj_allocate(LevelUniqueObjectState);

	if (obj == object_none)		//no free objects
		return object_none;

	// Zero out object structure to keep weird bugs from happening
	// in uninitialized fields.
	const auto signature = obj->signature;
	/* Test the version in the object structure, not the local copy.
	 * This produces a more useful diagnostic from Valgrind if the
	 * test reports a problem.
	 */
	DXX_CHECK_VAR_IS_DEFINED(obj->signature);
	*obj = {};
	// Tell Valgrind to warn on any uninitialized fields.
	DXX_POISON_VAR(*obj, 0xfd);

	obj->signature = next(signature);
	obj->type 				= type;
	obj->id 				= id;
	obj->pos 				= pos;
	obj->size 				= size;
	obj->flags 				= 0;
	//@@if (orient != NULL)
	//@@	obj->orient 			= *orient;

	obj->orient 				= orient?*orient:vmd_identity_matrix;

	obj->control_source 		        = ctype;
	obj->movement_source = mtype;
	obj->render_type 			= rtype;
	obj->contains.count                     = 0;
        obj->matcen_creator                     = 0;
	obj->lifeleft 				= IMMORTAL_TIME;		//assume immortal
	obj->attached_obj			= object_none;

	if (obj->control_source == object::control_type::powerup)
        {
		obj->ctype.powerup_info.count = 1;
                obj->ctype.powerup_info.flags = 0;
                obj->ctype.powerup_info.creation_time = {GameTime64};
        }

	// Init physics info for this object
	if (obj->movement_source == object::movement_type::physics) {
		obj->mtype.phys_info = {};
	}

	if (obj->render_type == render_type::RT_POLYOBJ)
        {
                obj->rtype.pobj_info.subobj_flags = 0;
		obj->rtype.pobj_info.tmap_override = -1;
                obj->rtype.pobj_info.alt_textures = 0;
        }

	obj->shields 				= 20*F1_0;

	{
		const auto &&p = find_point_seg(LevelSharedSegmentState, LevelUniqueSegmentState, pos, segnum);		//find correct segment
		// Previously this was only an assert check.  Now it is also
		// checked at runtime.
		segnum = p;
	}

	obj_link_unchecked(Objects.vmptr, obj, segnum);

	if (obj->control_source == object::control_type::explosion)
		obj->ctype.expl_info.next_attach = obj->ctype.expl_info.prev_attach = obj->ctype.expl_info.attach_parent = object_none;

	if (obj->type == OBJ_DEBRIS)
		++ LevelUniqueObjectState.Debris_object_count;
	return obj;
}

imobjptridx_t obj_weapon_create(d_level_unique_object_state &LevelUniqueObjectState, const d_level_shared_segment_state &LevelSharedSegmentState, d_level_unique_segment_state &LevelUniqueSegmentState, const weapon_info_array &Weapon_info, const unsigned id, const vmsegptridx_t segnum, const vms_vector &pos, const fix size, const render_type rtype)
{
	constexpr auto ctype = object::control_type::weapon;
	constexpr auto mtype = object::movement_type::physics;
	constexpr const vms_matrix *orient = nullptr;
	const auto &&objp = obj_create(LevelUniqueObjectState, LevelSharedSegmentState, LevelUniqueSegmentState, OBJ_WEAPON, id, segnum, pos, orient, size, ctype, mtype, rtype);
	if (objp == object_none)
		return objp;
	auto &obj = *objp;
	//	Set (or not) persistent bit in phys_info.
	assert(obj.control_source == object::control_type::weapon);
	if (Weapon_info[id].persistent != weapon_info::persistence_flag::terminate_on_impact)
		obj.mtype.phys_info.flags |= PF_PERSISTENT;
	obj.ctype.laser_info.creation_time = {GameTime64};
	obj.ctype.laser_info.clear_hitobj();
	obj.ctype.laser_info.multiplier = F1_0;
#if DXX_BUILD_DESCENT == 2
	obj.ctype.laser_info.last_afterburner_time = 0;
#endif
	return objp;
}

//create a copy of an object. returns new object number
imobjptridx_t obj_create_copy(const object &srcobj, const vmsegptridx_t newsegnum)
{
	auto &Objects = LevelUniqueObjectState.Objects;
	// Find next free object
	const auto &&obj = obj_allocate(LevelUniqueObjectState);

	if (obj == object_none)
		return object_none;

	const auto signature = obj->signature;
	*obj = srcobj;

	obj_link_unchecked(Objects.vmptr, obj, newsegnum);
	obj->signature = next(signature);

	//we probably should initialize sub-structures here

	return obj;
}

//remove object from the world
void obj_delete(d_level_unique_object_state &LevelUniqueObjectState, segment_array &Segments, const vmobjptridx_t obj)
{
	auto &Objects = LevelUniqueObjectState.get_objects();
	Assert(obj->type != OBJ_NONE);
	Assert(obj != ConsoleObject);

#if DXX_BUILD_DESCENT == 2
	if (obj->type==OBJ_WEAPON && get_weapon_id(obj)==weapon_id_type::GUIDEDMISS_ID && obj->ctype.laser_info.parent_type==OBJ_PLAYER)
	{
		const auto pnum = get_player_id(Objects.vcptr(obj->ctype.laser_info.parent_num));
		const auto &&gimobj = LevelUniqueObjectState.Guided_missile.get_player_active_guided_missile(Objects.vmptridx, pnum);
		if (gimobj == obj)
		{
			LevelUniqueObjectState.Guided_missile.clear_player_active_guided_missile(pnum);
			if (pnum == Player_num)
			{
				if (!PlayerCfg.GuidedInBigWindow)
					do_cockpit_window_view(gauge_inset_window_view::secondary, weapon_box_user::post_missile_static);
				if (Newdemo_state == ND_STATE_RECORDING)
					newdemo_record_guided_end();
			}
		}
	}
	else if (LevelUniqueObjectState.BuddyState.Buddy_objnum == obj)
		LevelUniqueObjectState.BuddyState.Buddy_objnum = object_none;
#endif

	if (obj == Viewer)		//deleting the viewer?
		Viewer = ConsoleObject;						//..make the player the viewer

	if (obj->flags & OF_ATTACHED)		//detach this from object
		obj_detach_one(Objects, obj);

	if (obj->attached_obj != object_none)		//detach all objects from this
		obj_detach_all(Objects, obj);

	if (obj->type == OBJ_DEBRIS)
		-- LevelUniqueObjectState.Debris_object_count;

	if (obj->movement_source == object::movement_type::physics && (obj->mtype.phys_info.flags & PF_STICK))
		LevelUniqueStuckObjectState.remove_stuck_object(obj);
	obj_unlink(Objects.vmptr, Segments.vmptr, obj);
	const auto signature = obj->signature;
	DXX_POISON_VAR(*obj, 0xfa);
	obj->type = OBJ_NONE;		//unused!
	/* Preserve signature across the poison value.  When the object slot
	 * is reused, the allocator will need the old signature so that the
	 * new one can be derived from it.  No other sites should read it
	 * until that happens.
	 */
	obj->signature = signature;
	obj_free(LevelUniqueObjectState, obj);
}

#define	DEATH_SEQUENCE_LENGTH			(F1_0*5)
#define	DEATH_SEQUENCE_EXPLODE_TIME	(F1_0*2)

object *Dead_player_camera;	//	Object index of object watching deader.
}
namespace dcx {
player_dead_state Player_dead_state = player_dead_state::no;			//	If !0, then player is dead, but game continues so he can watch.
namespace {
static int Player_flags_save;
static fix Camera_to_player_dist_goal = F1_0*4;
static typename object::control_type Control_type_save;
static render_type Render_type_save;
}

unsigned laser_parent_is_matching_signature(const laser_parent &l, const object_base &o)
{
	if (l.parent_type != o.type)
		return 0;
	return l.parent_signature == o.signature;
}

}

namespace dsx {

//	------------------------------------------------------------------------------------------------------------------
void dead_player_end(void)
{
	auto &Objects = LevelUniqueObjectState.Objects;
	auto &vmobjptridx = Objects.vmptridx;
	if (Player_dead_state == player_dead_state::no)
		return;

	if (Newdemo_state == ND_STATE_RECORDING)
		newdemo_record_restore_cockpit();

	Player_dead_state = player_dead_state::no;
	obj_delete(LevelUniqueObjectState, Segments, vmobjptridx(Dead_player_camera));
	Dead_player_camera = NULL;
	select_cockpit(PlayerCfg.CockpitMode[0]);
	Viewer = Viewer_save;
	ConsoleObject->type = OBJ_PLAYER;
	ConsoleObject->flags = Player_flags_save;

	assert(Control_type_save == object::control_type::flying || Control_type_save == object::control_type::slew);

	ConsoleObject->control_source = Control_type_save;
	ConsoleObject->render_type = Render_type_save;
	auto &player_info = ConsoleObject->ctype.player_info;
	player_info.powerup_flags &= ~PLAYER_FLAGS_INVULNERABLE;
	player_info.Player_eggs_dropped = false;
}

namespace {

//	------------------------------------------------------------------------------------------------------------------
//	Camera is less than size of player away from
static void set_camera_pos(vms_vector &camera_pos, const vcobjptridx_t objp)
{
	int count{0};
	fix	camera_player_dist;
	fix	far_scale;

	camera_player_dist = vm_vec_dist_quick(camera_pos, objp->pos);

	if (camera_player_dist < Camera_to_player_dist_goal) {
		//	Camera is too close to player object, so move it away.
		fvi_info		hit_data;

		auto player_camera_vec{vm_vec_sub(camera_pos, objp->pos)};
		if (player_camera_vec == vms_vector{})
			player_camera_vec.x += F1_0/16;

		auto hit_type = fvi_hit_type::Wall;
		far_scale = F1_0;

		while (hit_type != fvi_hit_type::None && count++ < 6)
		{
			vm_vec_normalize_quick(player_camera_vec);
			vm_vec_scale(player_camera_vec, Camera_to_player_dist_goal);

			const auto closer_p1 = vm_vec_add(objp->pos, player_camera_vec);		//	This is the actual point we want to put the camera at.
			vm_vec_scale(player_camera_vec, far_scale);						//	...but find a point 50% further away...
			const auto local_p1 = vm_vec_add(objp->pos, player_camera_vec);		//	...so we won't have to do as many cuts.

			hit_type = find_vector_intersection(fvi_query{
				objp->pos,
				local_p1,
				fvi_query::unused_ignore_obj_list,
				fvi_query::unused_LevelUniqueObjectState,
				fvi_query::unused_Robot_info,
				0,
				objp,
			}, objp->segnum, 0, hit_data);
			if (hit_type == fvi_hit_type::None)
			{
				camera_pos = closer_p1;
			} else {
				make_random_vector(player_camera_vec);
				far_scale = 3*F1_0/2;
			}
		}
	}
}

}

//	------------------------------------------------------------------------------------------------------------------
window_event_result dead_player_frame(const d_robot_info_array &Robot_info)
{
	auto &Objects = LevelUniqueObjectState.Objects;
	auto &vcobjptridx = Objects.vcptridx;
	auto &vmobjptr = Objects.vmptr;
	auto &vmobjptridx = Objects.vmptridx;
	static fix	time_dead = 0;

	if (Player_dead_state != player_dead_state::no)
	{
		time_dead += FrameTime;

		//	If unable to create camera at time of death, create now.
		if (Dead_player_camera == Viewer_save) {
			const auto &player = get_local_plrobj();
			const auto &&objnum = obj_create(LevelUniqueObjectState, LevelSharedSegmentState, LevelUniqueSegmentState, OBJ_CAMERA, 0, vmsegptridx(player.segnum), player.pos, &player.orient, 0, object::control_type::None, object::movement_type::None, render_type::RT_NONE);

			if (objnum != object_none)
				Viewer = Dead_player_camera = objnum;
			else {
				Int3();
			}
		}		

		ConsoleObject->mtype.phys_info.rotvel.x = max(0, DEATH_SEQUENCE_EXPLODE_TIME - time_dead)/4;
		ConsoleObject->mtype.phys_info.rotvel.y = max(0, DEATH_SEQUENCE_EXPLODE_TIME - time_dead)/2;
		ConsoleObject->mtype.phys_info.rotvel.z = max(0, DEATH_SEQUENCE_EXPLODE_TIME - time_dead)/3;

		Camera_to_player_dist_goal = min(time_dead*8, F1_0*20) + ConsoleObject->size;

		set_camera_pos(Dead_player_camera->pos, vcobjptridx(ConsoleObject));

		// the following line uncommented by WraithX, 4-12-00
		if (time_dead < DEATH_SEQUENCE_EXPLODE_TIME + F1_0 * 2)
		{
			const auto fvec{vm_vec_sub(ConsoleObject->pos, Dead_player_camera->pos)};
			vm_vector_to_matrix(Dead_player_camera->orient, fvec);
			Dead_player_camera->mtype.phys_info = ConsoleObject->mtype.phys_info;

			// the following "if" added by WraithX to get rid of camera "wiggle"
			Dead_player_camera->mtype.phys_info.flags &= ~PF_WIGGLE;
			// end "if" added by WraithX, 4/13/00

		// the following line uncommented by WraithX, 4-12-00
		}
		else
		{
			// the following line uncommented by WraithX, 4-11-00
			Dead_player_camera->movement_source = object::movement_type::physics;
			//Dead_player_camera->mtype.phys_info.rotvel.y = F1_0/8;
		// the following line uncommented by WraithX, 4-12-00
		}
		// end addition by WX

		if (time_dead > DEATH_SEQUENCE_EXPLODE_TIME) {
			if (Player_dead_state != player_dead_state::exploded)
			{
				auto &player_info = get_local_plrobj().ctype.player_info;
				const auto hostages_lost = std::exchange(player_info.mission.hostages_on_board, 0);

				if (hostages_lost > 1)
					HUD_init_message(HM_DEFAULT, TXT_SHIP_DESTROYED_2, hostages_lost);
				else
					HUD_init_message_literal(HM_DEFAULT, hostages_lost == 1 ? ( { const auto &&m = TXT_SHIP_DESTROYED_1; std::span<const char>(m, strlen(m)); } ) : ( { const auto &&m = TXT_SHIP_DESTROYED_0; std::span<const char>(m, strlen(m)); } ));

				Player_dead_state = player_dead_state::exploded;
				
				const auto cobjp = vmobjptridx(ConsoleObject);
				drop_player_eggs(cobjp);
				player_info.Player_eggs_dropped = true;
				if (Game_mode & GM_MULTI)
				{
					multi_send_player_deres(deres_explode);
				}

				explode_badass_player(Robot_info, cobjp);

				//is this next line needed, given the badass call above?
				explode_object(LevelUniqueObjectState, Robot_info, LevelSharedSegmentState, LevelUniqueSegmentState, cobjp, 0);
				ConsoleObject->flags &= ~OF_SHOULD_BE_DEAD;		//don't really kill player
				ConsoleObject->render_type = render_type::RT_NONE;				//..just make him disappear
				ConsoleObject->type = OBJ_GHOST;						//..and kill intersections
#if DXX_BUILD_DESCENT == 2
				player_info.powerup_flags &= ~PLAYER_FLAGS_HEADLIGHT_ON;
#endif
			}
		} else {
			if (d_rand() < FrameTime*4) {
				if (Game_mode & GM_MULTI)
					multi_send_create_explosion(Player_num);
				create_small_fireball_on_object(vmobjptridx(ConsoleObject), F1_0, 1);
			}
		}


		if (GameViewUniqueState.Death_sequence_aborted)
		{
			auto &player_info = get_local_plrobj().ctype.player_info;
			if (!player_info.Player_eggs_dropped) {
				player_info.Player_eggs_dropped = true;
				drop_player_eggs(vmobjptridx(ConsoleObject));
				if (Game_mode & GM_MULTI)
				{
					multi_send_player_deres(deres_explode);
				}
			}

			return DoPlayerDead();		//kill_player();
		}
	}
	else
		time_dead = 0;

	return window_event_result::handled;
}

namespace {

//	------------------------------------------------------------------------------------------------------------------
static void start_player_death_sequence(object &player)
{
	auto &Objects = LevelUniqueObjectState.Objects;
#if DXX_BUILD_DESCENT == 2
	auto &vmobjptr = Objects.vmptr;
#endif
	auto &vmobjptridx = Objects.vmptridx;
	assert(&player == ConsoleObject);
	if (Player_dead_state != player_dead_state::no ||
		Dead_player_camera != NULL ||
		((Game_mode & GM_MULTI) && get_local_player().connected != player_connection_status::playing))
		return;

	//Assert(Dead_player_camera == NULL);

	reset_rear_view();

	if (!(Game_mode & GM_MULTI))
		HUD_clear_messages();

	GameViewUniqueState.Death_sequence_aborted = 0;

	if (Game_mode & GM_MULTI)
	{
#if DXX_BUILD_DESCENT == 2
		// If Hoard, increase number of orbs by 1. Only if you haven't killed yourself. This prevents cheating
		if (game_mode_hoard(Game_mode))
		{
			auto &player_info = player.ctype.player_info;
			auto &proximity = player_info.hoard.orbs;
			if (proximity < player_info.max_hoard_orbs)
			{
				const auto is_bad_kill = [&vmobjptr]{
					auto &lplr = get_local_player();
					auto &lplrobj = get_local_plrobj();
					const auto killer_objnum = lplrobj.ctype.player_info.killer_objnum;
					if (killer_objnum == lplr.objnum)
						/* Self kill */
						return true;
					if (killer_objnum == object_none)
						/* Non-player kill */
						return true;
					const auto &&killer_objp = vmobjptr(killer_objnum);
					if (killer_objp->type != OBJ_PLAYER)
						return true;
					if (!(Game_mode & GM_TEAM))
						return false;
					return multi_get_team_from_player(Netgame, Player_num) == multi_get_team_from_player(Netgame, get_player_id(killer_objp));
				};
				if (!is_bad_kill())
					++ proximity;
			}
		}
#endif
		multi_send_kill(vmobjptridx(get_local_player().objnum));
	}
	
	PaletteRedAdd = 40;
	Player_dead_state = player_dead_state::yes;

	player.mtype.phys_info.rotthrust = {};
	player.mtype.phys_info.thrust = {};

	const auto &&objnum = obj_create(LevelUniqueObjectState, LevelSharedSegmentState, LevelUniqueSegmentState, OBJ_CAMERA, 0, vmsegptridx(player.segnum), player.pos, &player.orient, 0, object::control_type::None, object::movement_type::None, render_type::RT_NONE);
	Viewer_save = Viewer;
	if (objnum != object_none)
		Viewer = Dead_player_camera = objnum;
	else {
		Int3();
		Dead_player_camera = ConsoleObject;
	}

	select_cockpit(cockpit_mode_t::letterbox);
	if (Newdemo_state == ND_STATE_RECORDING)
		newdemo_record_letterbox();

	Player_flags_save = player.flags;
	Control_type_save = player.control_source;
	Render_type_save = player.render_type;

	player.flags &= ~OF_SHOULD_BE_DEAD;
//	Players[Player_num].flags |= PLAYER_FLAGS_INVULNERABLE;
	player.control_source = object::control_type::None;

	PALETTE_FLASH_SET(0,0,0);
}

//	------------------------------------------------------------------------------------------------------------------
static void obj_delete_all_that_should_be_dead()
{
	auto &Objects = LevelUniqueObjectState.Objects;
	auto &vmobjptridx = Objects.vmptridx;
	objnum_t		local_dead_player_object=object_none;

	// Move all objects
	range_for (const auto &&objp, vmobjptridx)
	{
		if ((objp->type!=OBJ_NONE) && (objp->flags&OF_SHOULD_BE_DEAD) )	{
			Assert(!(objp->type==OBJ_FIREBALL && objp->ctype.expl_info.delete_time!=-1));
			if (objp->type==OBJ_PLAYER) {
				if ( get_player_id(objp) == Player_num ) {
					if (local_dead_player_object == object_none) {
						start_player_death_sequence(objp);
						local_dead_player_object = objp;
					} else
						Int3();	//	Contact Mike: Illegal, killed player twice in this frame!
									// Ok to continue, won't start death sequence again!
					// kill_player();
				}
			} else {					
				obj_delete(LevelUniqueObjectState, Segments, objp);
			}
		}
	}
}

}

//when an object has moved into a new segment, this function unlinks it
//from its old segment, and links it into the new segment
void obj_relink(fvmobjptr &vmobjptr, fvmsegptr &vmsegptr, const vmobjptridx_t objnum, const vmsegptridx_t newsegnum)
{
	obj_unlink(vmobjptr, vmsegptr, objnum);
	obj_link_unchecked(vmobjptr, objnum, newsegnum);
}

// for getting out of messed up linking situations (i.e. caused by demo playback)
void obj_relink_all(void)
{
	auto &Objects = LevelUniqueObjectState.Objects;
	for (unique_segment &useg : vmsegptr)
	{
		useg.objects = object_none;
	}

	for (const auto &&obj : Objects.vmptridx)
	{
		if (obj->type != OBJ_NONE)
		{
			auto segnum = obj->segnum;
			if (segnum > Highest_segment_index)
				segnum = segment_first;
			obj_link_unchecked(Objects.vmptr, obj, Segments.vmptridx(segnum));
		}
	}
}

namespace {

//process a continuously-spinning object
static void spin_object(object_base &obj)
{
	vms_angvec rotangs;
	assert(obj.movement_source == object::movement_type::spinning);

	const fix frametime = FrameTime;
	rotangs.p = fixmul(obj.mtype.spin_rate.x, frametime);
	rotangs.h = fixmul(obj.mtype.spin_rate.y, frametime);
	rotangs.b = fixmul(obj.mtype.spin_rate.z, frametime);

	const auto &&rotmat = vm_angles_2_matrix(rotangs);
	obj.orient = vm_matrix_x_matrix(obj.orient, rotmat);
	check_and_fix_matrix(obj.orient);
}

}

#if DXX_BUILD_DESCENT == 2
imobjidx_t d_guided_missile_indices::get_player_active_guided_missile(const playernum_t pnum) const
{
	return operator[](pnum);
}

/* Place debug checks out of line so that they are shared among the
 * template instantiations.
 */
bool d_guided_missile_indices::debug_check_current_object(const object_base &obj)
{
	assert(obj.type == OBJ_WEAPON);
	const auto gmid = get_weapon_id(obj);
	if (obj.type != OBJ_WEAPON)
		return false;
	assert(gmid == weapon_id_type::GUIDEDMISS_ID);
	if (gmid != weapon_id_type::GUIDEDMISS_ID)
		return false;
	return true;
}

template <typename result_type, typename object_factory>
result_type d_guided_missile_indices::get_player_active_guided_missile_tmpl(object_factory &fobjptr, const playernum_t pnum) const
{
	const auto gmidx = get_player_active_guided_missile(pnum);
	if (gmidx == object_none)
		return object_none;
	auto &&gmobj = fobjptr(gmidx);
	if (!debug_check_current_object(gmobj))
		return object_none;
	return gmobj;
}

imobjptr_t d_guided_missile_indices::get_player_active_guided_missile(fvmobjptr &vmobjptr, const playernum_t pnum) const
{
	return this->template get_player_active_guided_missile_tmpl<imobjptr_t>(vmobjptr, pnum);
}

imobjptridx_t d_guided_missile_indices::get_player_active_guided_missile(fvmobjptridx &vmobjptridx, const playernum_t pnum) const
{
	return this->template get_player_active_guided_missile_tmpl<imobjptridx_t>(vmobjptridx, pnum);
}

void d_guided_missile_indices::set_player_active_guided_missile(const vmobjidx_t obji, const playernum_t pnum)
{
	auto &i = operator[](pnum);
	i = obji;
}

void d_guided_missile_indices::clear_player_active_guided_missile(const playernum_t pnum)
{
	auto &i = operator[](pnum);
	i = object_none;
}

int Drop_afterburner_blob_flag;		//ugly hack
//see if wall is volatile, and if so, cause damage to player
//returns true if player is in lava
#endif

namespace {

//--------------------------------------------------------------------
//move an object for the current frame
static window_event_result object_move_one(const d_level_shared_robot_info_state &LevelSharedRobotInfoState, const vmobjptridx_t obj, control_info &Controls)
{
#if DXX_BUILD_DESCENT == 2
	auto &LevelSharedVertexState = LevelSharedSegmentState.get_vertex_state();
	auto &Vertices = LevelSharedVertexState.get_vertices();
#endif
	auto &Objects = LevelUniqueObjectState.Objects;
	auto &vmobjptr = Objects.vmptr;
	const auto previous_segment = obj->segnum;
	auto result = window_event_result::handled;

	const auto obj_previous_position = obj->pos;			// Save the current position

	if ((obj->type==OBJ_PLAYER) && (Player_num==get_player_id(obj)))	{
		const auto &&segp = vmsegptr(obj->segnum);
#if DXX_BUILD_DESCENT == 2
		if (game_mode_capture_flag(Game_mode))
			fuelcen_check_for_goal(obj, segp);
		else if (game_mode_hoard(Game_mode))
			fuelcen_check_for_hoard_goal(obj, segp);
#endif

		auto &player_info = obj->ctype.player_info;
		auto &energy = player_info.energy;
		const fix fuel = fuelcen_give_fuel(segp, INITIAL_ENERGY - energy);
		if (fuel > 0 )	{
			energy += fuel;
		}
		auto &pl_shields = obj->shields;
		const fix shields = repaircen_give_shields(segp, INITIAL_SHIELDS - pl_shields);
		if (shields > 0) {
			pl_shields += shields;
		}
	}

	{
		auto lifeleft = obj->lifeleft;
		if (lifeleft != IMMORTAL_TIME) //if not immortal...
		{
			lifeleft -= FrameTime; //...inevitable countdown towards death
#if DXX_BUILD_DESCENT == 2
			if (obj->type == OBJ_MARKER)
			{
				if (lifeleft < F1_0*1000)
					lifeleft += F1_0; // Make sure this object doesn't go out.
			}
#endif
			obj->lifeleft = lifeleft;
		}
	}
#if DXX_BUILD_DESCENT == 2
	Drop_afterburner_blob_flag = 0;
#endif

	switch (obj->control_source) {

		case object::control_type::None:
			break;

		case object::control_type::flying:
			read_flying_controls(obj, Controls);
			break;

		case object::control_type::repaircen:
			Int3();
			// -- hey! these are no longer supported!! -- do_repair_sequence(obj);
			break;

		case object::control_type::powerup:
			do_powerup_frame(Vclip, obj);
			break;
	
		case object::control_type::morph:			//morph implies AI
			do_morph_frame(obj);
			//NOTE: FALLS INTO AI HERE!!!!
			[[fallthrough]];

		case object::control_type::ai:
			//NOTE LINK TO object::control_type::morph ABOVE!!!
			if (Game_suspended & SUSP_ROBOTS) return window_event_result::ignored;
			do_ai_frame(LevelSharedRobotInfoState, obj);
			break;

		case object::control_type::weapon:
			Laser_do_weapon_sequence(LevelSharedRobotInfoState.Robot_info, obj);
			break;
		case object::control_type::explosion:
			do_explosion_sequence(LevelSharedRobotInfoState.Robot_info, obj);
			break;

		case object::control_type::slew:
#ifdef RELEASE
			obj->control_source = object::control_type::None;
			con_printf(CON_URGENT, DXX_STRINGIZE_FL(__FILE__, __LINE__, "BUG: object %hu has control type object::control_type::slew, sig/type/id = %hu/%i/%i"), static_cast<objnum_t>(obj), static_cast<uint16_t>(obj->signature), obj->type, obj->id);
#else
			if ( keyd_pressed[KEY_PAD5] ) slew_stop();
			if ( keyd_pressed[KEY_NUMLOCK] ) 		{
				slew_reset_orient();
			}
			slew_frame(0 );		// Does velocity addition for us.
#endif
			break;

//		case object::control_type::flythrough:
//			do_flythrough(obj,0);			// HACK:do_flythrough should operate on an object!!!!
//			//check_object_seg(obj);
//			return;	// DON'T DO THE REST OF OBJECT STUFF SINCE THIS IS A SPECIAL CASE!!!
//			break;

		case object::control_type::debris:
			do_debris_frame(LevelSharedRobotInfoState.Robot_info, obj);
			break;

		case object::control_type::light:
			break;		//doesn't do anything

		case object::control_type::remote:
			break;		//doesn't do anything

		case object::control_type::cntrlcen:
			do_controlcen_frame(LevelSharedRobotInfoState.Robot_info, obj);
			break;

		default:
			Error("Unknown control type %u in object %hu, sig/type/id = %i/%i/%i", static_cast<unsigned>(obj->control_source), static_cast<objnum_t>(obj), static_cast<uint16_t>(obj->signature), obj->type, obj->id);
			break;

	}

	if (obj->lifeleft < 0 ) {		// We died of old age
		obj->flags |= OF_SHOULD_BE_DEAD;
		if ( obj->type==OBJ_WEAPON && Weapon_info[get_weapon_id(obj)].damage_radius )
			explode_badass_weapon(LevelSharedRobotInfoState.Robot_info, obj, obj->pos);
#if DXX_BUILD_DESCENT == 2
		else if ( obj->type==OBJ_ROBOT)	//make robots explode
			explode_object(LevelUniqueObjectState, LevelSharedRobotInfoState.Robot_info, LevelSharedSegmentState, LevelUniqueSegmentState, obj, 0);
#endif
	}

	if (obj->type == OBJ_NONE || obj->flags&OF_SHOULD_BE_DEAD)
		return window_event_result::ignored;         // object has been deleted

	bool prepare_seglist = false;
	phys_visited_seglist phys_visited_segs;
	switch (obj->movement_source) {

		case object::movement_type::None:
			break;				//this doesn't move

		case object::movement_type::physics:	//move by physics
			result = do_physics_sim(LevelSharedRobotInfoState.Robot_info, obj, obj_previous_position, obj->type == OBJ_PLAYER ? (prepare_seglist = true, phys_visited_segs.nsegs = 0, &phys_visited_segs) : nullptr);
			break;

		case object::movement_type::spinning:
			spin_object(obj);
			break;
	}

#if DXX_BUILD_DESCENT == 2
	auto &Walls = LevelUniqueWallSubsystemState.Walls;
	auto &vcwallptr = Walls.vcptr;
#endif
	//	If player and moved to another segment, see if hit any triggers.
	// also check in player under a lavafall
	if (prepare_seglist)
	{
		if (previous_segment != obj->segnum && phys_visited_segs.nsegs > 1)
		{
			auto seg0 = vmsegptridx(phys_visited_segs.seglist[0]);
#if DXX_BUILD_DESCENT == 2
			int	old_level = Current_level_num;
#endif
			range_for (const auto i, partial_const_range(phys_visited_segs.seglist, 1u, phys_visited_segs.nsegs))
			{
				const auto &&seg1 = seg0.absolute_sibling(i);
				const auto connect_side = find_connect_side(seg1, seg0);
				if (connect_side != side_none)
				{
					result = check_trigger(seg0, connect_side, get_local_plrobj(), obj, 0);
#if DXX_BUILD_DESCENT == 2
				//maybe we've gone on to the next level.  if so, bail!
				if (Current_level_num != old_level)
					return window_event_result::ignored;
#endif
				}
				seg0 = seg1;
			}
		}
#if DXX_BUILD_DESCENT == 2
		{
			bool under_lavafall = false;

			auto &playing = obj->ctype.player_info.lavafall_hiss_playing;
			const auto &&segp = vcsegptr(obj->segnum);
			auto &vcvertptr = Vertices.vcptr;
			if (const auto sidemask = get_seg_masks(vcvertptr, obj->pos, segp, obj->size).sidemask; sidemask != sidemask_t{})
			{
				for (const auto sidenum : MAX_SIDES_PER_SEGMENT)
				{
					if (!(sidemask & build_sidemask(sidenum)))
						continue;
					const auto wall_num = segp->shared_segment::sides[sidenum].wall_num;
					if (wall_num != wall_none && vcwallptr(wall_num)->type == WALL_ILLUSION)
					{
						const auto type = check_volatile_wall(obj, segp->unique_segment::sides[sidenum]);
						if (type != volatile_wall_result::none)
						{
							under_lavafall = 1;
							if (!playing)
							{
								playing = 1;
								const auto sound = (type == volatile_wall_result::lava) ? sound_effect::SOUND_LAVAFALL_HISS : sound_effect::SOUND_SHIP_IN_WATERFALL;
								digi_link_sound_to_object3(sound, obj, 1, F1_0, sound_stack::allow_stacking, vm_distance{i2f(256)}, -1, -1);
								break;
							}
						}
					}
				}
			}
	
			if (!under_lavafall && playing)
			{
				playing = 0;
				digi_kill_sound_linked_to_object( obj);
			}
		}
#endif
	}

#if DXX_BUILD_DESCENT == 2
	//see if guided missile has flown through exit trigger
	if (obj == LevelUniqueObjectState.Guided_missile.get_player_active_guided_missile(Player_num))
	{
		if (previous_segment != obj->segnum) {
			const auto &&psegp = vcsegptr(previous_segment);
			const auto &&connect_side = find_connect_side(vcsegptridx(obj->segnum), psegp);
			if (connect_side != side_none)
			{
				const auto wall_num = psegp->shared_segment::sides[connect_side].wall_num;
				if ( wall_num != wall_none ) {
					auto trigger_num = vcwallptr(wall_num)->trigger;
					if (trigger_num != trigger_none)
					{
						auto &Triggers = LevelUniqueWallSubsystemState.Triggers;
						auto &vctrgptr = Triggers.vcptr;
						const auto &&t = vctrgptr(trigger_num);
						if (t->type == trigger_action::normal_exit)
							obj->lifeleft = 0;
					}
				}
			}
		}
	}

	if (Drop_afterburner_blob_flag) {
		Assert(obj==ConsoleObject);
		drop_afterburner_blobs(obj, 2, i2f(5)/2, -1);	//	-1 means use default lifetime
		if (Game_mode & GM_MULTI)
			multi_send_drop_blobs(Player_num);
		Drop_afterburner_blob_flag = 0;
	}

	if ((obj->type == OBJ_WEAPON) && (Weapon_info[get_weapon_id(obj)].afterburner_size)) {
		fix	vel = vm_vec_mag_quick(obj->mtype.phys_info.velocity);
		fix	delay, lifetime;

		if (vel > F1_0*200)
			delay = F1_0/16;
		else if (vel > F1_0*40)
			delay = fixdiv(F1_0*13,vel);
		else
			delay = F1_0/4;

		lifetime = (delay * 3)/2;
		if (!(Game_mode & GM_MULTI)) {
			delay /= 2;
			lifetime *= 2;
		}

		assert(obj->control_source == object::control_type::weapon);
		if ((obj->ctype.laser_info.last_afterburner_time + delay < GameTime64) || (obj->ctype.laser_info.last_afterburner_time > GameTime64)) {
			obj->ctype.laser_info.last_afterburner_time = {GameTime64};
			drop_afterburner_blobs(obj, 1, i2f(Weapon_info[get_weapon_id(obj)].afterburner_size)/16, lifetime);
		}
	}

#endif
	return result;
}

//--------------------------------------------------------------------
//move all objects for the current frame
static window_event_result object_move_all(const d_level_shared_robot_info_state &LevelSharedRobotInfoState)
{
	auto &Objects = LevelUniqueObjectState.Objects;
	auto &vmobjptridx = Objects.vmptridx;
	auto result = window_event_result::ignored;

	if (Highest_object_index > MAX_USED_OBJECTS)
		free_object_slots(MAX_USED_OBJECTS);		//	Free all possible object slots.

	obj_delete_all_that_should_be_dead();

	if (PlayerCfg.AutoLeveling)
		ConsoleObject->mtype.phys_info.flags |= PF_LEVELLING;
	else
		ConsoleObject->mtype.phys_info.flags &= ~PF_LEVELLING;

	// Move all objects
	range_for (const auto &&objp, vmobjptridx)
	{
		if ( (objp->type != OBJ_NONE) && (!(objp->flags&OF_SHOULD_BE_DEAD)) )	{
			result = std::max(object_move_one(LevelSharedRobotInfoState, objp, Controls), result);
		}
	}

//	check_duplicate_objects();
//	remove_incorrect_objects();

	return result;
}

}

window_event_result game_move_all_objects(const d_level_shared_robot_info_state &LevelSharedRobotInfoState)
{
	LevelUniqueObjectState.last_console_player_position = ConsoleObject->pos;
	return object_move_all(LevelSharedRobotInfoState);
}

window_event_result endlevel_move_all_objects(const d_level_shared_robot_info_state &LevelSharedRobotInfoState)
{
	return object_move_all(LevelSharedRobotInfoState);
}

//--unused-- // -----------------------------------------------------------
//--unused-- //	Moved here from eobject.c on 02/09/94 by MK.
//--unused-- int find_last_obj(int i)
//--unused-- {
//--unused-- 	for (i=MAX_OBJECTS;--i>=0;)
//--unused-- 		if (Objects[i].type != OBJ_NONE) break;
//--unused--
//--unused-- 	return i;
//--unused--
//--unused-- }

#if DXX_USE_EDITOR
//make object array non-sparse
void compress_objects(void)
{
	auto &Objects = LevelUniqueObjectState.Objects;
	auto &vmobjptr = Objects.vmptr;
	auto &vmobjptridx = Objects.vmptridx;
	//last_i = find_last_obj(MAX_OBJECTS);

	//	Note: It's proper to do < (rather than <=) Highest_object_index here because we
	//	are just removing gaps, and the last object can't be a gap.
	for (objnum_t start_i=0;start_i<Highest_object_index;start_i++)
	{
		const auto &&start_objp = vmobjptridx(start_i);
		if (start_objp->type == OBJ_NONE) {
			auto highest = Highest_object_index;
			const auto &&h = vmobjptr(static_cast<objnum_t>(highest));
			auto segnum_copy = h->segnum;

			obj_unlink(Objects.vmptr, Segments.vmptr, h);

			*start_objp = *h;

			if (Cur_object_index == Highest_object_index)
				Cur_object_index = start_i;

			h->type = OBJ_NONE;

			{
				//link the object into the list for its segment
				const auto &&segnum = vmsegptridx(segnum_copy);
#ifndef NDEBUG
				const auto &obj = *start_objp;
				assert(obj.segnum == segment_none);
				assert(obj.next == object_none);
				assert(obj.prev == object_none);
#endif
				obj_link_unchecked(Objects.vmptr, start_objp, segnum);
			}

			while (vmobjptr(static_cast<objnum_t>(--highest))->type == OBJ_NONE)
			{
			}
			Objects.set_count(highest + 1);

			//last_i = find_last_obj(last_i);
			
		}
	}
	reset_objects(LevelUniqueObjectState, LevelUniqueObjectState.num_objects);
}
#endif

//called after load.  Takes number of objects,  and objects should be
//compressed.  resets free list, marks unused objects as unused
void reset_objects(d_level_unique_object_state &LevelUniqueObjectState, const unsigned n_objs)
{
	LevelUniqueObjectState.Debris_object_count = 0;
	LevelUniqueObjectState.num_objects = n_objs;
	assert(LevelUniqueObjectState.num_objects > 0);
	auto &Objects = LevelUniqueObjectState.get_objects();
	assert(LevelUniqueObjectState.num_objects < Objects.size());
	Objects.set_count(n_objs);
#if DXX_BUILD_DESCENT == 2
	if (LevelUniqueObjectState.BuddyState.Buddy_objnum.get_unchecked_index() >= n_objs)
		LevelUniqueObjectState.BuddyState.Buddy_objnum = object_none;
#endif

	for (objnum_t i = n_objs; i < MAX_OBJECTS; ++i)
	{
		LevelUniqueObjectState.free_obj_list[i] = i;
		auto &obj = *Objects.vmptr(i);
		DXX_POISON_VAR(obj, 0xfd);
		obj.type = OBJ_NONE;
		obj.signature = object_signature_t{0};
	}
}

//Tries to find a segment for an object, using find_point_seg()
imsegptridx_t find_object_seg(const d_level_shared_segment_state &LevelSharedSegmentState, d_level_unique_segment_state &LevelUniqueSegmentState, const object_base &obj)
{
	auto &Segments = LevelUniqueSegmentState.get_segments();
	return find_point_seg(LevelSharedSegmentState, LevelUniqueSegmentState, obj.pos, Segments.vmptridx(obj.segnum));
}


//If an object is in a segment, set its segnum field and make sure it's
//properly linked.  If not in any segment, returns 0, else 1.
//callers should generally use find_vector_intersection()
int update_object_seg(fvmobjptr &vmobjptr, const d_level_shared_segment_state &LevelSharedSegmentState, d_level_unique_segment_state &LevelUniqueSegmentState, const vmobjptridx_t obj)
{
	const auto &&newseg = find_object_seg(LevelSharedSegmentState, LevelUniqueSegmentState, obj);
	if (newseg == segment_none)
		return 0;

	if ( newseg != obj->segnum )
		obj_relink(vmobjptr, LevelUniqueSegmentState.get_segments().vmptr, obj, newseg);

	return 1;
}

unsigned laser_parent_is_player(fvcobjptr &vcobjptr, const laser_parent &l, const object_base &o)
{
	/* Player objects are never recycled, so skip the signature check.
	 */
	if (l.parent_type != OBJ_PLAYER)
		return 0;
	/* As a special case, let the player be recognized even if he died
	 * before the weapon hit the target.
	 */
	if (o.type != OBJ_PLAYER && o.type != OBJ_GHOST)
		return 0;
	auto &parent_object = *vcobjptr(l.parent_num);
	return (&parent_object == &o);
}

unsigned laser_parent_is_object(fvcobjptr &vcobjptr, const laser_parent &l, const object_base &o)
{
	auto &parent_object = *vcobjptr(l.parent_num);
	if (&parent_object != &o)
		return 0;
	return laser_parent_is_matching_signature(l, o);
}

unsigned laser_parent_is_object(const laser_parent &l, const vcobjptridx_t o)
{
	if (l.parent_num != o.get_unchecked_index())
		return 0;
	return laser_parent_is_matching_signature(l, *o);
}

unsigned laser_parent_object_exists(fvcobjptr &vcobjptr, const laser_parent &l)
{
	return laser_parent_is_matching_signature(l, *vcobjptr(l.parent_num));
}

void set_powerup_id(const d_powerup_info_array &Powerup_info, const d_vclip_array &Vclip, object_base &o, powerup_type_t id)
{
	o.id = underlying_value(id);
	o.size = Powerup_info[id].size;
	const auto vclip_num = Powerup_info[id].vclip_num;
	o.rtype.vclip_info.vclip_num = vclip_num;
	o.rtype.vclip_info.frametime = Vclip[vclip_num].frame_time;
}

//go through all objects and make sure they have the correct segment numbers
void fix_object_segs()
{
	auto &LevelSharedVertexState = LevelSharedSegmentState.get_vertex_state();
	auto &Objects = LevelUniqueObjectState.Objects;
	auto &Vertices = LevelSharedVertexState.get_vertices();
	auto &vmobjptr = Objects.vmptr;
	auto &vmobjptridx = Objects.vmptridx;
	auto &vcvertptr = Vertices.vcptr;
	range_for (const auto &&o, vmobjptridx)
	{
		if (o->type != OBJ_NONE)
		{
			const auto oldsegnum = o->segnum;
			if (update_object_seg(vmobjptr, LevelSharedSegmentState, LevelUniqueSegmentState, o) == 0)
			{
				const auto pos = o->pos;
				const auto segnum = o->segnum;
				o->pos = compute_segment_center(vcvertptr, vcsegptr(segnum));
				con_printf(CON_URGENT, "Object %hu claims segment %hu, but has position {%i,%i,%i}; moving to %hu:{%i,%i,%i}", o.get_unchecked_index(), oldsegnum, pos.x, pos.y, pos.z, segnum, o->pos.x, o->pos.y, o->pos.z);
			}
		}
	}
}


//--unused-- void object_use_new_object_list( object * new_list )
//--unused-- {
//--unused-- 	int i, segnum;
//--unused-- 	object *obj;
//--unused--
//--unused-- 	// First, unlink all the old objects for the segments array
//--unused-- 	for (segnum=0; segnum <= Highest_segment_index; segnum++) {
//--unused-- 		Segments[segnum].objects = -1;
//--unused-- 	}
//--unused-- 	// Then, erase all the objects
//--unused-- 	reset_objects(1);
//--unused--
//--unused-- 	// Fill in the object array
//--unused-- 	memcpy( Objects, new_list, sizeof(object)*MAX_OBJECTS );
//--unused--
//--unused-- 	Highest_object_index=-1;
//--unused--
//--unused-- 	// Relink 'em
//--unused-- 	for (i=0; i<MAX_OBJECTS; i++ )	{
//--unused-- 		obj = &Objects[i];
//--unused-- 		if ( obj->type != OBJ_NONE )	{
//--unused-- 			num_objects++;
//--unused-- 			Highest_object_index = i;
//--unused-- 			segnum = obj->segnum;
//--unused-- 			obj->next = obj->prev = obj->segnum = -1;
//--unused-- 			obj_link(i,segnum);
//--unused-- 		} else {
//--unused-- 			obj->next = obj->prev = obj->segnum = -1;
//--unused-- 		}
//--unused-- 	}
//--unused-- 	
//--unused-- }

namespace {

#if DXX_BUILD_DESCENT == 1
#define object_is_clearable_weapon(W,a,b)	object_is_clearable_weapon(a,b)
#endif
static unsigned object_is_clearable_weapon(const weapon_info_array &Weapon_info, const object_base obj, const unsigned clear_all)
{
	if (!(obj.type == OBJ_WEAPON))
		return 0;
	const auto weapon_id = get_weapon_id(obj);
#if DXX_BUILD_DESCENT == 2
	if (Weapon_info[weapon_id].flags & WIF_PLACABLE)
		return 0;
#endif
	if (clear_all)
		return clear_all;
	return !is_proximity_bomb_or_player_smart_mine(weapon_id);
}

}

//delete objects, such as weapons & explosions, that shouldn't stay between levels
//	Changed by MK on 10/15/94, don't remove proximity bombs.
//if clear_all is set, clear even proximity bombs
void clear_transient_objects(int clear_all)
{
	auto &Objects = LevelUniqueObjectState.Objects;
	auto &vmobjptridx = Objects.vmptridx;
	range_for (const auto &&obj, vmobjptridx)
	{
		if (object_is_clearable_weapon(Weapon_info, obj, clear_all) ||
			 obj->type == OBJ_FIREBALL ||
			 obj->type == OBJ_DEBRIS ||
			 (obj->type!=OBJ_NONE && obj->flags & OF_EXPLODING)) {
			obj_delete(LevelUniqueObjectState, Segments, obj);
		}
	}
}

//attaches an object, such as a fireball, to another object, such as a robot
void obj_attach(object_array &Objects, const vmobjptridx_t parent, const vmobjptridx_t sub)
{
	Assert(sub->type == OBJ_FIREBALL);
	assert(sub->control_source == object::control_type::explosion);

	Assert(sub->ctype.expl_info.next_attach==object_none);
	Assert(sub->ctype.expl_info.prev_attach==object_none);

	assert(parent->attached_obj == object_none || Objects.vcptr(parent->attached_obj)->ctype.expl_info.prev_attach == object_none);

	sub->ctype.expl_info.next_attach = parent->attached_obj;

	if (sub->ctype.expl_info.next_attach != object_none)
		Objects.vmptr(sub->ctype.expl_info.next_attach)->ctype.expl_info.prev_attach = sub;

	parent->attached_obj = sub;

	sub->ctype.expl_info.attach_parent = parent;
	sub->flags |= OF_ATTACHED;

	Assert(sub->ctype.expl_info.next_attach != sub);
	Assert(sub->ctype.expl_info.prev_attach != sub);
}

namespace {

//detaches one object
static objnum_t obj_detach_one(object_array &Objects, object &sub)
{
	Assert(sub.flags & OF_ATTACHED);
	Assert(sub.ctype.expl_info.attach_parent != object_none);

	/* Check for the impossible case that the parent does not exist, or does
	 * not have any attached children.
	 */
	if (const auto &parent_objp = *Objects.vcptr(sub.ctype.expl_info.attach_parent); parent_objp.type == OBJ_NONE || parent_objp.attached_obj == object_none)
	{
		sub.flags &= ~OF_ATTACHED;
		return object_none;
	}

	/* If this is not the last sub-object in the list */
	if (sub.ctype.expl_info.next_attach != object_none)
	{
		/* Find the prev pointer of the next object */
		auto &a = Objects.vmptr(sub.ctype.expl_info.next_attach)->ctype.expl_info.prev_attach;
		/* Check that sub->next->prev is `sub` */
		assert(Objects.vcptr(a) == &sub);
		/* Change the next object's prev pointer to point to `sub`'s
		 * predecessor.  This is one half of the work required to remove `sub`
		 * from the chain of sub-objects.  The other half is handled below.
		 */
		a = sub.ctype.expl_info.prev_attach;
	}

	/* If `sub` is the first sub-object in the chain, then `use_prev_attach` is
	 * false and the update should target the parent object.
	 *
	 * If `sub` is not the first sub-object in the chain, then
	 * `use_prev_attach` is true and the update should target the previous
	 * object in the chain.
	 */
	const auto use_prev_attach = (sub.ctype.expl_info.prev_attach != object_none);
	/* If `use_prev_attach` is true, set `o` to the previous object and change
	 * `sub->prev` to object_none so that a future call will set
	 * `use_prev_attach` to false.
	 *
	 * If `use_prev_attach` is false, set `o` to the parent object.
	 */
	auto &o = *Objects.vmptr(use_prev_attach ? std::exchange(sub.ctype.expl_info.prev_attach, object_none) : sub.ctype.expl_info.attach_parent);
	/* If `use_prev_attach` is true, then update the `next` reference in the
	 * previous object.  This is the second half of removing `sub` from a chain
	 * of objects.
	 *
	 * If `use_prev_attach` is false, update `attached_obj` on the parent
	 * object, so that the parent has no attached objects.  This assumes that
	 * `use_prev_attach` is false if and only if `sub` is the first child of
	 * the parent object.  If `sub` is not the first child, then `sub` should
	 * have a back pointer (and thus `use_prev_attach` would be true) referring
	 * to a previous child, which may or may not be the first child.
	 */
	auto &update_attach = use_prev_attach ? o.ctype.expl_info.next_attach : o.attached_obj;
	/* Assert that the link that will be overwritten points to `sub`.  If the
	 * link is from parent to child, then failure means that sub was not the
	 * first child of the parent.  If the link is from previous to next, then
	 * failure means that sub pointed back to an element that did not point
	 * forward to sub.
	 */
	assert(Objects.vcptr(update_attach) == &sub);
	sub.flags &= ~OF_ATTACHED;
	const auto next_attach = std::exchange(sub.ctype.expl_info.next_attach, object_none);
	update_attach = next_attach;
	return next_attach;
}

//detaches all objects from this object
static void obj_detach_all(object_array &Objects, object_base &parent)
{
	for (auto attached_obj = parent.attached_obj; attached_obj != object_none;)
		attached_obj = obj_detach_one(Objects, Objects.vmptr(attached_obj));
}

}

#if DXX_BUILD_DESCENT == 2
//creates a marker object in the world.  returns the object number
imobjptridx_t drop_marker_object(const vms_vector &pos, const vmsegptridx_t segnum, const vms_matrix &orient, const game_marker_index marker_num)
{
	auto &Polygon_models = LevelSharedPolygonModelState.Polygon_models;
	const auto Marker_model_num = LevelSharedPolygonModelState.Marker_model_num;
	if (Marker_model_num == polygon_model_index::None)
	{
		con_printf(CON_URGENT, "%s:%u: failed to drop marker object: invalid model number %u", __FILE__, __LINE__, underlying_value(Marker_model_num));
		return object_none;
	}
	const auto movement_type =
		((Game_mode & GM_MULTI) && !(Game_mode & GM_MULTI_COOP) && Netgame.Allow_marker_view)
		? object::movement_type::None
		: object::movement_type::spinning;
	const auto &&obj = obj_create(LevelUniqueObjectState, LevelSharedSegmentState, LevelUniqueSegmentState, OBJ_MARKER, underlying_value(marker_num), segnum, pos, &orient, Polygon_models[Marker_model_num].rad, object::control_type::None, movement_type, render_type::RT_POLYOBJ);
	if (obj != object_none) {
		auto &o = *obj;
		o.rtype.pobj_info.model_num = Marker_model_num;

		if (movement_type == object::movement_type::spinning)
		{
		constexpr fix scale = F1_0 / 2;
		const auto oi = obj.get_unchecked_index();
		auto &spin_vec = o.mtype.spin_rate;
		spin_vec = {};
		if (oi & 1)
			vm_vec_scale_add2(spin_vec, o.orient.fvec, (oi & 8) ? scale : -scale);
		if (oi & 2)
			vm_vec_scale_add2(spin_vec, o.orient.uvec, (oi & 16) ? scale : -scale);
		if (oi & 4)
			vm_vec_scale_add2(spin_vec, o.orient.rvec, (oi & 32) ? scale : -scale);
		}

		//	MK, 10/16/95: Using lifeleft to make it flash, thus able to trim lightlevel from all objects.
		o.lifeleft = IMMORTAL_TIME - 1;
	}
	return obj;	
}

//	*viewer is a viewer, probably a missile.
//	wake up all robots that were rendered last frame subject to some constraints.
void wake_up_rendered_objects(const object &viewer, window_rendered_data &window)
{
	auto &Objects = LevelUniqueObjectState.Objects;
	auto &vmobjptr = Objects.vmptr;
	//	Make sure that we are processing current data.
	if (timer_query() != window.time) {
		return;
	}

	Ai_last_missile_camera = &viewer;

	range_for (const auto objnum, window.rendered_robots)
	{
			const auto &&objp = vmobjptr(objnum);
			if (objp->type == OBJ_ROBOT) {
				if (vm_vec_dist_quick(viewer.pos, objp->pos) < F1_0*100)
				{
					ai_local		*ailp = &objp->ctype.ai_info.ail;
					{
						objp->ctype.ai_info.SUB_FLAGS |= SUB_FLAGS_CAMERA_AWAKE;
						ailp->player_awareness_type = player_awareness_type_t::PA_WEAPON_ROBOT_COLLISION;
						ailp->player_awareness_time = F1_0*3;
						ailp->previous_visibility = player_visibility_state::visible_and_in_field_of_view;
					}
				}
			}
	}
}
#endif

// Swap endianess of given object_rw if swap == 1
void object_rw_swap(object_rw *obj, const physfsx_endian swap)
{
	if (swap == physfsx_endian::native)
		return;

	obj->signature     = SWAPINT(obj->signature);
	obj->next          = SWAPSHORT(obj->next);
	obj->prev          = SWAPSHORT(obj->prev);
	obj->segnum        = SWAPSHORT(obj->segnum);
	obj->attached_obj  = SWAPSHORT(obj->attached_obj);
	obj->pos.x         = SWAPINT(obj->pos.x);
	obj->pos.y         = SWAPINT(obj->pos.y);
	obj->pos.z         = SWAPINT(obj->pos.z);
	obj->orient.rvec.x = SWAPINT(obj->orient.rvec.x);
	obj->orient.rvec.y = SWAPINT(obj->orient.rvec.y);
	obj->orient.rvec.z = SWAPINT(obj->orient.rvec.z);
	obj->orient.fvec.x = SWAPINT(obj->orient.fvec.x);
	obj->orient.fvec.y = SWAPINT(obj->orient.fvec.y);
	obj->orient.fvec.z = SWAPINT(obj->orient.fvec.z);
	obj->orient.uvec.x = SWAPINT(obj->orient.uvec.x);
	obj->orient.uvec.y = SWAPINT(obj->orient.uvec.y);
	obj->orient.uvec.z = SWAPINT(obj->orient.uvec.z);
	obj->size          = SWAPINT(obj->size);
	obj->shields       = SWAPINT(obj->shields);
	obj->last_pos.x    = SWAPINT(obj->last_pos.x);
	obj->last_pos.y    = SWAPINT(obj->last_pos.y);
	obj->last_pos.z    = SWAPINT(obj->last_pos.z);
	obj->lifeleft      = SWAPINT(obj->lifeleft);
	
	switch (typename object::movement_type{obj->movement_source})
	{
		case object::movement_type::None:
			obj->mtype = {};
			break;
		case object::movement_type::physics:
			obj->mtype.phys_info.velocity.x  = SWAPINT(obj->mtype.phys_info.velocity.x);
			obj->mtype.phys_info.velocity.y  = SWAPINT(obj->mtype.phys_info.velocity.y);
			obj->mtype.phys_info.velocity.z  = SWAPINT(obj->mtype.phys_info.velocity.z);
			obj->mtype.phys_info.thrust.x    = SWAPINT(obj->mtype.phys_info.thrust.x);
			obj->mtype.phys_info.thrust.y    = SWAPINT(obj->mtype.phys_info.thrust.y);
			obj->mtype.phys_info.thrust.z    = SWAPINT(obj->mtype.phys_info.thrust.z);
			obj->mtype.phys_info.mass        = SWAPINT(obj->mtype.phys_info.mass);
			obj->mtype.phys_info.drag        = SWAPINT(obj->mtype.phys_info.drag);
			obj->mtype.phys_info.rotvel.x    = SWAPINT(obj->mtype.phys_info.rotvel.x);
			obj->mtype.phys_info.rotvel.y    = SWAPINT(obj->mtype.phys_info.rotvel.y);
			obj->mtype.phys_info.rotvel.z    = SWAPINT(obj->mtype.phys_info.rotvel.z);
			obj->mtype.phys_info.rotthrust.x = SWAPINT(obj->mtype.phys_info.rotthrust.x);
			obj->mtype.phys_info.rotthrust.y = SWAPINT(obj->mtype.phys_info.rotthrust.y);
			obj->mtype.phys_info.rotthrust.z = SWAPINT(obj->mtype.phys_info.rotthrust.z);
			obj->mtype.phys_info.turnroll    = SWAPINT(obj->mtype.phys_info.turnroll);
			obj->mtype.phys_info.flags       = SWAPSHORT(obj->mtype.phys_info.flags);
			break;
			
		case object::movement_type::spinning:
			obj->mtype.spin_rate.x = SWAPINT(obj->mtype.spin_rate.x);
			obj->mtype.spin_rate.y = SWAPINT(obj->mtype.spin_rate.y);
			obj->mtype.spin_rate.z = SWAPINT(obj->mtype.spin_rate.z);
			break;
	}
	
	switch (typename object::control_type{obj->control_source})
	{
		case object::control_type::weapon:
			obj->ctype.laser_info.parent_type      = SWAPSHORT(obj->ctype.laser_info.parent_type);
			obj->ctype.laser_info.parent_num       = SWAPSHORT(obj->ctype.laser_info.parent_num);
			obj->ctype.laser_info.parent_signature = SWAPINT(obj->ctype.laser_info.parent_signature);
			obj->ctype.laser_info.creation_time    = SWAPINT(obj->ctype.laser_info.creation_time);
			obj->ctype.laser_info.last_hitobj      = SWAPSHORT(obj->ctype.laser_info.last_hitobj);
			obj->ctype.laser_info.track_goal       = SWAPSHORT(obj->ctype.laser_info.track_goal);
			obj->ctype.laser_info.multiplier       = SWAPINT(obj->ctype.laser_info.multiplier);
			break;
			
		case object::control_type::explosion:
			obj->ctype.expl_info.spawn_time    = SWAPINT(obj->ctype.expl_info.spawn_time);
			obj->ctype.expl_info.delete_time   = SWAPINT(obj->ctype.expl_info.delete_time);
			obj->ctype.expl_info.delete_objnum = SWAPSHORT(obj->ctype.expl_info.delete_objnum);
			obj->ctype.expl_info.attach_parent = SWAPSHORT(obj->ctype.expl_info.attach_parent);
			obj->ctype.expl_info.prev_attach   = SWAPSHORT(obj->ctype.expl_info.prev_attach);
			obj->ctype.expl_info.next_attach   = SWAPSHORT(obj->ctype.expl_info.next_attach);
			break;
			
		case object::control_type::ai:
			obj->ctype.ai_info.hide_segment           = SWAPSHORT(obj->ctype.ai_info.hide_segment);
			obj->ctype.ai_info.hide_index             = SWAPSHORT(obj->ctype.ai_info.hide_index);
			obj->ctype.ai_info.path_length            = SWAPSHORT(obj->ctype.ai_info.path_length);
#if DXX_BUILD_DESCENT == 1
			obj->ctype.ai_info.cur_path_index         = SWAPSHORT(obj->ctype.ai_info.cur_path_index);
#elif DXX_BUILD_DESCENT == 2
			obj->ctype.ai_info.dying_start_time       = SWAPINT(obj->ctype.ai_info.dying_start_time);
#endif
			obj->ctype.ai_info.danger_laser_num       = SWAPSHORT(obj->ctype.ai_info.danger_laser_num);
			obj->ctype.ai_info.danger_laser_signature = SWAPINT(obj->ctype.ai_info.danger_laser_signature);
			break;
			
		case object::control_type::light:
			obj->ctype.light_info.intensity = SWAPINT(obj->ctype.light_info.intensity);
			break;
			
		case object::control_type::powerup:
			obj->ctype.powerup_info.count         = SWAPINT(obj->ctype.powerup_info.count);
#if DXX_BUILD_DESCENT == 2
			obj->ctype.powerup_info.creation_time = SWAPINT(obj->ctype.powerup_info.creation_time);
			obj->ctype.powerup_info.flags         = SWAPINT(obj->ctype.powerup_info.flags);
#endif
			break;
		case object::control_type::None:
		case object::control_type::flying:
		case object::control_type::slew:
		case object::control_type::flythrough:
		case object::control_type::repaircen:
		case object::control_type::morph:
		case object::control_type::debris:
		case object::control_type::remote:
		default:
			break;
	}
	
	switch (render_type{obj->render_type})
	{
		case render_type::RT_NONE:
			if (obj->type != OBJ_GHOST) // HACK: when a player is dead or not connected yet, clients still expect to get polyobj data - even if render_type == RT_NONE at this time.
				break;
			[[fallthrough]];
		case render_type::RT_MORPH:
		case render_type::RT_POLYOBJ:
		{
			obj->rtype.pobj_info.model_num                = SWAPINT(obj->rtype.pobj_info.model_num);
			for (uint_fast32_t i=0;i<MAX_SUBMODELS;i++)
			{
				obj->rtype.pobj_info.anim_angles[i].p = SWAPINT(obj->rtype.pobj_info.anim_angles[i].p);
				obj->rtype.pobj_info.anim_angles[i].b = SWAPINT(obj->rtype.pobj_info.anim_angles[i].b);
				obj->rtype.pobj_info.anim_angles[i].h = SWAPINT(obj->rtype.pobj_info.anim_angles[i].h);
			}
			obj->rtype.pobj_info.subobj_flags             = SWAPINT(obj->rtype.pobj_info.subobj_flags);
			obj->rtype.pobj_info.tmap_override            = SWAPINT(obj->rtype.pobj_info.tmap_override);
			obj->rtype.pobj_info.alt_textures             = SWAPINT(obj->rtype.pobj_info.alt_textures);
			break;
		}
			
		case render_type::RT_WEAPON_VCLIP:
		case render_type::RT_HOSTAGE:
		case render_type::RT_POWERUP:
		case render_type::RT_FIREBALL:
			obj->rtype.vclip_info.vclip_num = SWAPINT(obj->rtype.vclip_info.vclip_num);
			obj->rtype.vclip_info.frametime = SWAPINT(obj->rtype.vclip_info.frametime);
			break;
			
		case render_type::RT_LASER:
			break;
			
	}
}

}

namespace dcx {

void (check_warn_object_type)(const object_base &o, object_type_t t, const char *file, unsigned line)
{
	if (o.type != t)
		con_printf(CON_URGENT, "%s:%u: BUG: object %p has type %u, expected %u", file, line, &o, o.type, t);
}

}

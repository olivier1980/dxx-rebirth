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
 * This will contain the laser code
 *
 */

#include <stdlib.h>
#include <stdio.h>
#include <time.h>

#include "inferno.h"
#include "game.h"
#include "bm.h"
#include "object.h"
#include "laser.h"
#include "segment.h"
#include "fvi.h"
#include "dxxerror.h"
#include "gameseg.h"
#include "textures.h"
#include "fireball.h"
#include "polyobj.h"
#include "robot.h"
#include "weapon.h"
#include "newdemo.h"
#include "timer.h"
#include "player.h"
#include "sounds.h"
#include "ai.h"
#include "powerup.h"
#include "multi.h"
#include "physics.h"
#include "multi.h"
#include "fwd-wall.h"
#include "playsave.h"
#include "vclip.h"

#include "compiler-range_for.h"
#include "d_levelstate.h"
#include "d_underlying_value.h"
#include "partial_range.h"
#include "letsplay.h"
namespace {
#ifdef NEWHOMER
#define HOMING_TRACKABLE_DOT_FRAME_TIME	HOMING_TURN_TIME
static ubyte d_homer_tick_step = 0;
static fix d_homer_tick_count = 0;
#else
#define HOMING_TRACKABLE_DOT_FRAME_TIME	FrameTime
#endif

static int Muzzle_queue_index;
}

namespace dsx {
namespace {
static imobjptridx_t find_homing_object_complete(const vms_vector &curpos, const vmobjptridx_t tracker, int track_obj_type1, int track_obj_type2);
static imobjptridx_t find_homing_object(const vms_vector &curpos, vmobjptridx_t tracker);
}

//---------------------------------------------------------------------------------
// Called by render code.... determines if the laser is from a robot or the
// player and calls the appropriate routine.

void Laser_render(grs_canvas &canvas, const object_base &obj)
{
	auto &wi = Weapon_info[get_weapon_id(obj)];
	switch(wi.render)
	{
	case WEAPON_RENDER_LASER:
		Int3();	// Not supported anymore!
					//Laser_draw_one(obj-Objects, Weapon_info[obj->id].bitmap );
		break;
	case WEAPON_RENDER_BLOB:
		draw_object_blob(GameBitmaps, *Viewer, canvas, obj, wi.bitmap);
		break;
	case WEAPON_RENDER_POLYMODEL:
		break;
	case WEAPON_RENDER_VCLIP:
		Int3();	//	Oops, not supported, type added by mk on 09/09/94, but not for lasers...
		[[fallthrough]];
	default:
		Error( "Invalid weapon render type in Laser_render\n" );
	}
}

//---------------------------------------------------------------------------------
// Draws a texture-mapped laser bolt

//void Laser_draw_one( int objnum, grs_bitmap * bmp )
//{
//	int t1, t2, t3;
//	g3s_point p1, p2;
//	object *obj;
//	vms_vector start_pos,end_pos;
//
//	obj = &Objects[objnum];
//
//	start_pos = obj->pos;
//	vm_vec_scale_add(&end_pos,&start_pos,&obj->orient.fvec,-Laser_length);
//
//	g3_rotate_point(&p1,&start_pos);
//	g3_rotate_point(&p2,&end_pos);
//
//	t1 = Lighting_on;
//	t2 = Interpolation_method;
//	t3 = Transparency_on;
//
//	Lighting_on  = 0;
//	//Interpolation_method = 3;	// Full perspective
//	Interpolation_method = 1;	// Linear
//	Transparency_on = 1;
//
//	//gr_setcolor( gr_getcolor(31,15,0));
//	//g3_draw_line_ptrs(p1,p2);
//	//g3_draw_rod(p1,0x2000,p2,0x2000);
//	//g3_draw_rod(p1,Laser_width,p2,Laser_width);
//	g3_draw_rod_tmap(bmp,&p2,Laser_width,&p1,Laser_width,0);
//	Lighting_on = t1;
//	Interpolation_method = t2;
//	Transparency_on = t3;
//
//}

namespace {

#if DXX_BUILD_DESCENT == 2
static constexpr unsigned LASER_HELIX_MASK{7};   // must match number of bits in flags
#endif

static bool ignore_proximity_weapon(const object &o)
{
	if (!is_proximity_bomb_or_player_smart_mine(get_weapon_id(o)))
		return false;
#if DXX_BUILD_DESCENT == 1
	return GameTime64 > o.ctype.laser_info.creation_time + F1_0*2;
#elif DXX_BUILD_DESCENT == 2
	return GameTime64 > o.ctype.laser_info.creation_time + F1_0*4;
#endif
}

#if DXX_BUILD_DESCENT == 1
static bool ignore_phoenix_weapon(const object &)
{
	return false;
}

static bool ignore_guided_missile_weapon(const object &)
{
	return false;
}
#elif DXX_BUILD_DESCENT == 2
static bool ignore_phoenix_weapon(const object &o)
{
	return get_weapon_id(o) == weapon_id_type::PHOENIX_ID && GameTime64 > o.ctype.laser_info.creation_time + F1_0/4;
}

static bool ignore_guided_missile_weapon(const object &o)
{
	return get_weapon_id(o) == weapon_id_type::GUIDEDMISS_ID && GameTime64 > o.ctype.laser_info.creation_time + F1_0*2;
}
#endif
}

//	Changed by MK on 09/07/94
//	I want you to be able to blow up your own bombs.
//	AND...Your proximity bombs can blow you up if they're 2.0 seconds or more old.
//	Changed by MK on 06/06/95: Now must be 4.0 seconds old.  Much valid Net-complaining.
bool laser_are_related(const vcobjptridx_t o1, const vcobjptridx_t o2)
{
	// See if o2 is the parent of o1
	if (o1->type == OBJ_WEAPON)
		if (laser_parent_is_object(o1->ctype.laser_info, o2))
		{
			//	o1 is a weapon, o2 is the parent of 1, so if o1 is PROXIMITY_BOMB and o2 is player, they are related only if o1 < 2.0 seconds old
			if (ignore_proximity_weapon(o1) || ignore_guided_missile_weapon(o1) || ignore_phoenix_weapon(o1))
			{
				return 0;
			} else
				return 1;
		}

	// See if o1 is the parent of o2
	if (o2->type == OBJ_WEAPON)
	{
		if (laser_parent_is_object(o2->ctype.laser_info, o1))
		{
#if DXX_BUILD_DESCENT == 2
			//	o2 is a weapon, o1 is the parent of 2, so if o2 is PROXIMITY_BOMB and o1 is player, they are related only if o1 < 2.0 seconds old
			if (ignore_proximity_weapon(o2) || ignore_guided_missile_weapon(o2) || ignore_phoenix_weapon(o2))
			{
				return 0;
			} else
#endif
				return 1;
		}
	}

	// They must both be weapons
	if (o1->type != OBJ_WEAPON || o2->type != OBJ_WEAPON)
		return 0;

	//	Here is the 09/07/94 change -- Siblings must be identical, others can hurt each other
	// See if they're siblings...
	//	MK: 06/08/95, Don't allow prox bombs to detonate for 3/4 second.  Else too likely to get toasted by your own bomb if hit by opponent.
	const auto o1id{get_weapon_id(o1)};
	const auto o2id{get_weapon_id(o2)};
	auto &o1li = o1->ctype.laser_info;
	auto &o2li = o2->ctype.laser_info;
	if (o1li.parent_num == o2li.parent_num && o1li.parent_signature == o2li.parent_signature)
	{
		if (is_proximity_bomb_or_player_smart_mine(o1id) || is_proximity_bomb_or_player_smart_mine(o2id))
		{
			//	If neither is older than 1/2 second, then can't blow up!
#if DXX_BUILD_DESCENT == 2
			if (!(GameTime64 > o1li.creation_time + F1_0/2 || GameTime64 > o2li.creation_time + F1_0/2))
				return 1;
			else
#endif
				return 0;
		} else
			return 1;
	}

#if DXX_BUILD_DESCENT == 2
	//	Anything can cause a collision with a robot super prox mine.
	if (!(
		o1id == weapon_id_type::ROBOT_SUPERPROX_ID || o2id == weapon_id_type::ROBOT_SUPERPROX_ID ||
		o1id == weapon_id_type::PROXIMITY_ID || o2id == weapon_id_type::PROXIMITY_ID ||
		o1id == weapon_id_type::SUPERPROX_ID || o2id == weapon_id_type::SUPERPROX_ID ||
		o1id == weapon_id_type::PMINE_ID || o2id == weapon_id_type::PMINE_ID
	))
		return 1;
#endif
	return 0;
}

}

namespace dcx {
namespace {

constexpr vm_distance MAX_SMART_DISTANCE(F1_0*150);
constexpr vm_distance_squared MAX_SMART_DISTANCE_SQUARED{MAX_SMART_DISTANCE * MAX_SMART_DISTANCE};
static void do_muzzle_stuff(segnum_t segnum, const vms_vector &pos)
{
	auto &m = Muzzle_data[Muzzle_queue_index];
	Muzzle_queue_index++;
	if (Muzzle_queue_index >= MUZZLE_QUEUE_MAX)
		Muzzle_queue_index = 0;
	m.segnum = segnum;
	m.pos = pos;
	m.create_time = timer_query();
}

[[noreturn]]
dxx_compiler_attribute_cold
static void report_invalid_weapon_render_type(const int weapon_type, const weapon_info::render_type render)
{
	char buf[96];
	snprintf(buf, sizeof(buf), "invalid weapon render type %u on weapon %i", static_cast<unsigned>(render), weapon_type);
	throw std::runtime_error(buf);
}

[[nodiscard]]
static vms_vector build_homing_weapon_initial_vector(const object_base &parent, const object_base *const goal_obj)
{
	if (goal_obj == nullptr)
		return make_random_vector();
	vms_vector vector_to_goal;
	//	Create a vector towards the goal, then add some noise to it.
	vm_vec_normalized_dir_quick(vector_to_goal, goal_obj->pos, parent.pos);
	vm_vec_scale_add2(vector_to_goal, make_random_vector(), F1_0 / 4);
	vm_vec_normalize_quick(vector_to_goal);
	return vector_to_goal;
}

}
}

namespace dsx {
namespace {

//creates a weapon object
static imobjptridx_t create_weapon_object(int weapon_type,const vmsegptridx_t segnum, const vms_vector &position)
{
	render_type rtype;
	fix laser_radius;

	switch(Weapon_info[weapon_type].render)
	{
		case WEAPON_RENDER_BLOB:
			rtype = render_type::RT_LASER;			// Render as a laser even if blob (see render code above for explanation)
			laser_radius = Weapon_info[weapon_type].blob_size;
			break;
		case WEAPON_RENDER_POLYMODEL:
			laser_radius = 0;	//	Filled in below.
			rtype = render_type::RT_POLYOBJ;
			break;
		case WEAPON_RENDER_LASER:
			Int3(); 	// Not supported anymore
			return object_none;
		case WEAPON_RENDER_NONE:
			rtype = render_type::RT_NONE;
			laser_radius = F1_0;
			break;
		case WEAPON_RENDER_VCLIP:
			rtype = render_type::RT_WEAPON_VCLIP;
			laser_radius = Weapon_info[weapon_type].blob_size;
			break;
		default:
			report_invalid_weapon_render_type(weapon_type, Weapon_info[weapon_type].render);
	}

	const auto &&obj = obj_weapon_create(LevelUniqueObjectState, LevelSharedSegmentState, LevelUniqueSegmentState, Weapon_info, weapon_type, segnum, position, laser_radius, rtype);
	if (obj == object_none)
		return object_none;

	if (Weapon_info[weapon_type].render == WEAPON_RENDER_POLYMODEL) {
		auto &Polygon_models = LevelSharedPolygonModelState.Polygon_models;
		obj->rtype.pobj_info.model_num = Weapon_info[get_weapon_id(obj)].model_num;
		obj->size = fixdiv(Polygon_models[obj->rtype.pobj_info.model_num].rad,Weapon_info[get_weapon_id(obj)].po_len_to_width_ratio);
	}

	obj->mtype.phys_info.mass = Weapon_info[weapon_type].mass;
	obj->mtype.phys_info.drag = Weapon_info[weapon_type].drag;
	obj->mtype.phys_info.thrust = {};

	bool alwaysBounce = false;
#if LP_ALWAYS_BOUNCE == 1
	alwaysBounce = true;
#endif

	const auto bounce{Weapon_info[weapon_type].bounce};
	if (alwaysBounce || (bounce == weapon_info::bounce_type::always))
		obj->mtype.phys_info.flags |= PF_BOUNCE;

#if DXX_BUILD_DESCENT == 2
	if (bounce == weapon_info::bounce_type::twice || cheats.bouncyfire)
		obj->mtype.phys_info.flags |= PF_BOUNCE+PF_BOUNCES_TWICE;
#endif


	return obj;
}
}

#if DXX_BUILD_DESCENT == 2
namespace {
//	-------------------------------------------------------------------------------------------------------------------------------
//	***** HEY ARTISTS!! *****
//	Here are the constants you're looking for! --MK

//	Change the following constants to affect the look of the omega cannon.
//	Changing these constants will not affect the damage done.
//	WARNING: If you change DESIRED_OMEGA_DIST and MAX_OMEGA_BLOBS, you don't merely change the look of the cannon,
//	you change its range.  If you decrease DESIRED_OMEGA_DIST, you decrease how far the gun can fire.
constexpr std::integral_constant<fix, F1_0/20> OMEGA_BASE_TIME{}; // How many blobs per second!! No FPS-based blob creation anymore, no FPS-based damage anymore!
constexpr std::integral_constant<unsigned, 3> MIN_OMEGA_BLOBS{};				//	No matter how close the obstruction, at this many blobs created.
constexpr std::integral_constant<fix, F1_0*3> MIN_OMEGA_DIST{};		//	At least this distance between blobs, unless doing so would violate MIN_OMEGA_BLOBS
constexpr std::integral_constant<fix, F1_0*5> DESIRED_OMEGA_DIST{};		//	This is the desired distance between blobs.  For distances > MIN_OMEGA_BLOBS*DESIRED_OMEGA_DIST, but not very large, this will apply.
constexpr std::integral_constant<unsigned, 16> MAX_OMEGA_BLOBS{};				//	No matter how far away the obstruction, this is the maximum number of blobs.
constexpr vm_distance MAX_OMEGA_DIST{MAX_OMEGA_BLOBS * DESIRED_OMEGA_DIST};		//	Maximum extent of lightning blobs.
constexpr vm_distance_squared MAX_OMEGA_DIST_SQUARED{MAX_OMEGA_DIST * MAX_OMEGA_DIST};

//	Additionally, several constants which apply to homing objects in general control the behavior of the Omega Cannon.
//	They are defined in laser.h.  They are copied here for reference.  These values are valid on 1/10/96:
//	If you want the Omega Cannon view cone to be different than the Homing Missile viewcone, contact MK to make the change.
//	(Unless you are a programmer, in which case, do it yourself!)
#define	OMEGA_MIN_TRACKABLE_DOT			(15*F1_0/16)		//	Larger values mean narrower cone.  F1_0 means damn near impossible.  0 means 180 degree field of view.
constexpr vm_distance OMEGA_MAX_TRACKABLE_DIST{MAX_OMEGA_DIST}; //	An object must be at least this close to be tracked.

//	Note, you don't need to change these constants.  You can control damage and energy consumption by changing the
//	usual bitmaps.tbl parameters.
#define	OMEGA_DAMAGE_SCALE			32				//	Controls how much damage is done.  This gets multiplied by the damage specified in bitmaps.tbl in the $WEAPON line.
#define	OMEGA_ENERGY_CONSUMPTION	16				//	Controls how much energy is consumed.  This gets multiplied by the energy parameter from bitmaps.tbl.
//	-------------------------------------------------------------------------------------------------------------------------------

// Delete omega blobs further away than MAX_OMEGA_DIST
// Since last omega blob has VERY high velocity it's impossible to ensure a constant travel distance on varying FPS. So delete if they exceed their maximum distance.
static int omega_cleanup(fvcobjptr &vcobjptr, const vmobjptridx_t weapon)
{
	if (weapon->type != OBJ_WEAPON || get_weapon_id(weapon) != weapon_id_type::OMEGA_ID)
		return 0;
	auto &weapon_laser_info = weapon->ctype.laser_info;
	auto &obj = *vcobjptr(weapon_laser_info.parent_num);
	if (laser_parent_is_matching_signature(weapon_laser_info, obj))
		if (vm_vec_dist2(weapon->pos, obj.pos) > MAX_OMEGA_DIST_SQUARED)
		{
			obj_delete(LevelUniqueObjectState, Segments, weapon);
			return 1;
		}

	return 0;
}

}

// Return true if ok to do Omega damage. For Multiplayer games. See comment for omega_cleanup()
int ok_to_do_omega_damage(const object &weapon)
{
	auto &Objects = LevelUniqueObjectState.Objects;
	auto &vcobjptr = Objects.vcptr;
	if (weapon.type != OBJ_WEAPON || get_weapon_id(weapon) != weapon_id_type::OMEGA_ID)
		return 1;
	if (!(Game_mode & GM_MULTI))
		return 1;
	auto &weapon_laser_info = weapon.ctype.laser_info;
	auto &obj = *vcobjptr(weapon_laser_info.parent_num);
	if (laser_parent_is_matching_signature(weapon_laser_info, obj))
		if (vm_vec_dist2(obj.pos, weapon.pos) > MAX_OMEGA_DIST_SQUARED)
			return 0;

	return 1;
}

namespace {

// ---------------------------------------------------------------------------------
static bool create_omega_blobs(d_level_unique_object_state &LevelUniqueObjectState, const d_level_shared_segment_state &LevelSharedSegmentState, d_level_unique_segment_state &LevelUniqueSegmentState, const weapon_info_array &Weapon_info, const Difficulty_level_type Difficulty_level, const imsegptridx_t firing_segnum, const vms_vector &firing_pos, const vms_vector &goal_pos, const vmobjptridx_t parent_objp)
{
	const auto &&[magnitude_to_goal, vec_to_goal] = vm_vec_normalize_quick_with_magnitude(vm_vec_sub(goal_pos, firing_pos));
	const fix dist_to_goal{magnitude_to_goal};
	const auto &&[omega_blob_dist, num_omega_blobs] = [](const unsigned dist_to_goal) -> std::pair<fix, unsigned> {
		if (dist_to_goal < MIN_OMEGA_BLOBS * MIN_OMEGA_DIST)
		{
			constexpr unsigned omega_blob_dist{MIN_OMEGA_DIST};
			const unsigned num_omega_blobs{dist_to_goal / omega_blob_dist};
			return std::pair(omega_blob_dist, num_omega_blobs ? num_omega_blobs : 1u);
		}
		else
		{
			constexpr unsigned omega_blob_dist{DESIRED_OMEGA_DIST};
			const unsigned num_omega_blobs{dist_to_goal / omega_blob_dist};
			if (num_omega_blobs > MAX_OMEGA_BLOBS)
				return std::pair(dist_to_goal / MAX_OMEGA_BLOBS, MAX_OMEGA_BLOBS);
			else if (num_omega_blobs < MIN_OMEGA_BLOBS)
				return std::pair(dist_to_goal / MIN_OMEGA_BLOBS, MIN_OMEGA_BLOBS);
			else
				return std::pair(dist_to_goal / omega_blob_dist, num_omega_blobs);
		}
	}(dist_to_goal);

	const auto omega_delta_vector{vm_vec_copy_scale(vec_to_goal, omega_blob_dist)};

	//	Now, create all the blobs
	auto blob_pos{firing_pos};
	auto last_segnum{firing_segnum};

	//	If nearby, don't perturb vector.  If not nearby, start halfway out.
	std::array<fix, MAX_OMEGA_BLOBS> perturb_array;
	if (dist_to_goal < MIN_OMEGA_DIST*4) {
		perturb_array = {};
	} else {
		vm_vec_scale_add2(blob_pos, omega_delta_vector, F1_0/2);	//	Put first blob half way out.
		for (int i=0; i<num_omega_blobs/2; i++) {
			perturb_array[i] = F1_0*i + F1_0/4;
			perturb_array[num_omega_blobs-1-i] = F1_0*i;
		}
	}

	//	Create random perturbation vector, but favor _not_ going up in player's reference.
	auto perturb_vec{make_random_vector()};
	vm_vec_scale_add2(perturb_vec, parent_objp->orient.uvec, -F1_0/2);

	Doing_lighting_hack_flag = 1;	//	Ugly, but prevents blobs which are probably outside the mine from killing framerate.

	imobjptridx_t last_created_objnum{object_none};
	for (const auto i : xrange(num_omega_blobs))
	{
		//	This will put the last blob right at the destination object, causing damage.
		if (i == num_omega_blobs-1)
			vm_vec_scale_add2(blob_pos, omega_delta_vector, 15*F1_0/32);	//	Move last blob another (almost) half section

		//	Every so often, re-perturb blobs
		if ((i % 4) == 3) {
			vm_vec_scale_add2(perturb_vec, make_random_vector(), F1_0/4);
		}

		const auto temp_pos{vm_vec_scale_add(blob_pos, perturb_vec, perturb_array[i])};

		const auto &&segnum = find_point_seg(LevelSharedSegmentState, LevelUniqueSegmentState, temp_pos, last_segnum);
		if (segnum != segment_none) {
			last_segnum = segnum;
			const auto &&objp = obj_weapon_create(LevelUniqueObjectState, LevelSharedSegmentState, LevelUniqueSegmentState, Weapon_info, weapon_id_type::OMEGA_ID, segnum, temp_pos, 0, render_type::RT_WEAPON_VCLIP);
			if (objp == object_none)
				break;

			last_created_objnum = objp;

			objp->lifeleft = OMEGA_BASE_TIME+(d_rand()/8); // add little randomness so the lighting effect becomes a little more interesting
			objp->mtype.phys_info.velocity = vec_to_goal;

			//	Only make the last one move fast, else multiple blobs might collide with target.
			vm_vec_scale(objp->mtype.phys_info.velocity, F1_0*4);

			const auto &weapon_info = Weapon_info[get_weapon_id(objp)];
			objp->size = weapon_info.blob_size;

			objp->shields = fixmul(OMEGA_DAMAGE_SCALE*OMEGA_BASE_TIME, weapon_info.strength[Difficulty_level]);

			objp->ctype.laser_info.parent_type			= parent_objp->type;
			objp->ctype.laser_info.parent_signature	= parent_objp->signature;
			objp->ctype.laser_info.parent_num			= parent_objp;
			objp->movement_source = object::movement_type::None;	//	Only last one moves, that will get bashed below.

		}
		vm_vec_add2(blob_pos, omega_delta_vector);
	}

	Doing_lighting_hack_flag = 0;

	//	Make last one move faster, but it's already moving at speed = F1_0*4.
	if (last_created_objnum != object_none) {
		vm_vec_scale(last_created_objnum->mtype.phys_info.velocity, Weapon_info[weapon_id_type::OMEGA_ID].speed[Difficulty_level]/4);
		last_created_objnum->movement_source = object::movement_type::physics;
		return true;
	}
	return false;
}

}

#define	MIN_OMEGA_CHARGE	(MAX_OMEGA_CHARGE/8)
#define	OMEGA_CHARGE_SCALE	4			//	FrameTime / OMEGA_CHARGE_SCALE added to Omega_charge every frame.

fix get_omega_energy_consumption(const fix delta_charge)
{
	const fix energy_used{fixmul(F1_0 * 190 / 17, delta_charge)};
	const auto Difficulty_level = GameUniqueState.Difficulty_level;
	return (Difficulty_level == Difficulty_level_type::_0 || Difficulty_level == Difficulty_level_type::_1)
		? fixmul(energy_used, i2f(underlying_value(Difficulty_level) + 2) / 4)
		: energy_used;
}

// ---------------------------------------------------------------------------------
//	Call this every frame to recharge the Omega Cannon.
void omega_charge_frame(player_info &player_info)
{
	if (!(player_info.primary_weapon_flags & HAS_PRIMARY_FLAG(primary_weapon_index_t::OMEGA_INDEX)))
		return;
	auto &Omega_charge = player_info.Omega_charge;
	if (Omega_charge >= MAX_OMEGA_CHARGE)
		return;

	if (Player_dead_state != player_dead_state::no)
		return;

	//	Don't charge while firing. Wait 1/3 second after firing before recharging
	auto &Omega_recharge_delay = player_info.Omega_recharge_delay;
	if (Omega_recharge_delay)
	{
		if (Omega_recharge_delay > FrameTime)
		{
			Omega_recharge_delay -= FrameTime;
			return;
		}
		Omega_recharge_delay = 0;
	}

	if (auto &energy = player_info.energy)
	{
		const auto old_omega_charge{Omega_charge};
		Omega_charge += FrameTime/OMEGA_CHARGE_SCALE;
		if (Omega_charge > MAX_OMEGA_CHARGE)
			Omega_charge = MAX_OMEGA_CHARGE;

		const auto energy_used{get_omega_energy_consumption(Omega_charge - old_omega_charge)};
		energy = (energy > energy_used) ? energy - energy_used : 0;
	}
}

namespace {

// ---------------------------------------------------------------------------------
//	*objp is the object firing the omega cannon
//	*pos is the location from which the omega bolt starts
static void do_omega_stuff(fvmsegptridx &vmsegptridx, const vmobjptridx_t parent_objp, const vms_vector &firing_pos, const vmobjptridx_t weapon_objp)
{
	vms_vector	goal_pos;
	player_info *pl_info{};
	if (parent_objp->type == OBJ_PLAYER && get_player_id(parent_objp) == Player_num)
	{
		//	If charge >= min, or (some charge and zero energy), allow to fire.
		auto &player_info = parent_objp->ctype.player_info;
		if (!(player_info.Omega_charge >= MIN_OMEGA_CHARGE || (player_info.Omega_charge && !player_info.energy)))
		{
			obj_delete(LevelUniqueObjectState, Segments, weapon_objp);
			return;
		}
		pl_info = &player_info;
	}

	weapon_objp->ctype.laser_info.parent_type = parent_objp->type;
	weapon_objp->ctype.laser_info.parent_num = parent_objp.get_unchecked_index();
	weapon_objp->ctype.laser_info.parent_signature = parent_objp->signature;

	const auto &&lock_objnum = find_homing_object(firing_pos, weapon_objp);

	const auto &&firing_segnum = find_point_seg(LevelSharedSegmentState, LevelUniqueSegmentState, firing_pos, Segments.vmptridx(parent_objp->segnum));

	// -- if ((Last_omega_muzzle_flash_time + F1_0/4 < GameTime) || (Last_omega_muzzle_flash_time > GameTime)) {
	// -- 	do_muzzle_stuff(firing_segnum, firing_pos);
	// -- 	Last_omega_muzzle_flash_time = GameTime;
	// -- }

	//	Delete the original object.  Its only purpose in life was to determine which object to home in on.
	obj_delete(LevelUniqueObjectState, Segments, weapon_objp);

	//	If couldn't lock on anything, fire straight ahead.
	if (lock_objnum == object_none) {
		fvi_info		hit_data;
		const auto perturbed_fvec{vm_vec_scale_add(parent_objp->orient.fvec, make_random_vector(), F1_0 / 16)};
		goal_pos = vm_vec_scale_add(firing_pos, perturbed_fvec, MAX_OMEGA_DIST);
		if (firing_segnum == segment_none)
			return;
		const auto fate = find_vector_intersection(fvi_query{
			firing_pos,
			goal_pos,
			fvi_query::unused_ignore_obj_list,
			&LevelUniqueObjectState,
			&LevelSharedRobotInfoState.Robot_info,
			FQ_IGNORE_POWERUPS | FQ_TRANSPOINT,		//what about trans walls???
			parent_objp,
		}, firing_segnum, 0, hit_data);
		if (fate != fvi_hit_type::None)
		{
			Assert(hit_data.hit_seg != segment_none);		//	How can this be?  We went from inside the mine to outside without hitting anything?
			goal_pos = hit_data.hit_pnt;
		}
	} else
		goal_pos = lock_objnum->pos;

	//	This is where we create a pile of omega blobs!
	if (!create_omega_blobs(LevelUniqueObjectState, LevelSharedSegmentState, LevelUniqueSegmentState, Weapon_info, GameUniqueState.Difficulty_level, firing_segnum, firing_pos, goal_pos, parent_objp))
		return;

	//	Play sound.
	{
		const auto flash_sound{Weapon_info[get_weapon_id(weapon_objp)].flash_sound};
		if (parent_objp == Viewer)
			digi_play_sample(flash_sound, F1_0);
		else
			digi_link_sound_to_pos(flash_sound, vmsegptridx(weapon_objp->segnum), sidenum_t::WLEFT, weapon_objp->pos, 0, F1_0);
	}
	if (pl_info)
	{
		if (auto &Omega_charge = pl_info->Omega_charge; Omega_charge > OMEGA_BASE_TIME)
			Omega_charge -= OMEGA_BASE_TIME;
		else
			Omega_charge = 0;
		pl_info->Omega_recharge_delay = F1_0 / 3;
	}
}

/* Descent 2 attempted to penalize super lasers, but did so incorrectly, so the
 * penalty only applied successfully on non-super lasers.  Therefore, this test
 * excludes super lasers from the penalty, in order to behave more like the
 * original game.
 */
// Note that this is only used for determining if the quad laser per-bolt damage penalty should be applied, and consequently excludes super lasers
static int is_laser_weapon_type(const weapon_id_type weapon_type)
{
	return weapon_type == weapon_id_type::LASER_ID_L1 ||
		weapon_type == weapon_id_type::LASER_ID_L2 ||
		weapon_type == weapon_id_type::LASER_ID_L3 ||
		weapon_type == weapon_id_type::LASER_ID_L4;
}

}
#endif

// ---------------------------------------------------------------------------------
// Initializes a laser after Fire is pressed
//	Returns object number.
imobjptridx_t Laser_create_new(const vms_vector &direction, const vms_vector &position, const vmsegptridx_t segnum, const vmobjptridx_t parent, weapon_id_type weapon_type, const weapon_sound_flag make_sound)
{
	auto &Objects = LevelUniqueObjectState.Objects;
	auto &vmobjptr = Objects.vmptr;
	fix parent_speed;

	if (weapon_type >= N_weapon_types)
	{
		con_printf(CON_URGENT, DXX_STRINGIZE_FL(__FILE__, __LINE__, "invalid weapon id %u fired by parent %hu (type %u) in segment %hu"), weapon_type, parent.get_unchecked_index(), parent->type, segnum.get_unchecked_index());
		weapon_type = weapon_id_type::LASER_ID_L1;
	}

	//	Don't let homing blobs make muzzle flash.
	if (parent->type == OBJ_ROBOT)
		do_muzzle_stuff(segnum, position);

	const imobjptridx_t obj = create_weapon_object(weapon_type,segnum,position);

	if (obj == object_none)
	{
		return object_none;
	}
	const auto &weapon_info = Weapon_info[weapon_type];

#if DXX_BUILD_DESCENT == 2
	//	Do the special Omega Cannon stuff.  Then return on account of everything that follows does
	//	not apply to the Omega Cannon.
	if (weapon_type == weapon_id_type::OMEGA_ID) {
		// Create orientation matrix for tracking purposes.
		vm_vector_to_matrix_u(obj->orient, direction, parent->orient.uvec);

		if (parent != Viewer && parent->type != OBJ_WEAPON) {
			// Muzzle flash
			if (const auto flash_vclip = weapon_info.flash_vclip; Vclip.valid_index(flash_vclip))
				object_create_explosion_without_damage(Vclip, vmsegptridx(obj->segnum), obj->pos, weapon_info.flash_size, flash_vclip);
		}

		do_omega_stuff(vmsegptridx, parent, position, obj);

		return obj;
	}
#endif

	if (parent->type == OBJ_PLAYER) {
		if (weapon_type == weapon_id_type::FUSION_ID) {
			int	fusion_scale;
#if DXX_BUILD_DESCENT == 1
			if ((Game_mode & GM_MULTI) && !(Game_mode & GM_MULTI_COOP))
				fusion_scale = 2;
			else
#endif
				fusion_scale = 4;

			auto &player_info = parent->ctype.player_info;
			const auto Fusion_charge = player_info.Fusion_charge;
			if (Fusion_charge <= 0)
				obj->ctype.laser_info.multiplier = F1_0;
			else if (Fusion_charge <= F1_0*fusion_scale)
				obj->ctype.laser_info.multiplier = F1_0 + Fusion_charge/2;
			else
				obj->ctype.laser_info.multiplier = F1_0*fusion_scale;

#if DXX_BUILD_DESCENT == 1
			//	Fusion damage was boosted by mk on 3/27 (for reg 1.1 release), but we only want it to apply to single player games.
			if ((Game_mode & GM_MULTI) && !(Game_mode & GM_MULTI_COOP))
				obj->ctype.laser_info.multiplier /= 2;
#endif
		}
#if DXX_BUILD_DESCENT == 2
		/* Descent 1 attempted to penalize laser damage, but did so
		 * incorrectly, so it never applied to the player's weapons.
		 * Therefore, this penalty check is applied only in the Descent 2
		 * build, and then only if not playing a Descent 1 mission, so that
		 * when the Descent 2 build plays a Descent 1 mission, the weapons work
		 * more like they would in a Descent 1 build playing that mission.
		 */
		else if (!EMULATING_D1 && is_laser_weapon_type(weapon_type) && (parent->ctype.player_info.powerup_flags & PLAYER_FLAGS_QUAD_LASERS))
			obj->ctype.laser_info.multiplier = F1_0*3/4;
		else if (weapon_type == weapon_id_type::GUIDEDMISS_ID) {
			LevelUniqueObjectState.Guided_missile.set_player_active_guided_missile(obj, get_player_id(parent));
			if (parent==get_local_player().objnum) {
				if (Newdemo_state==ND_STATE_RECORDING)
					newdemo_record_guided_start();
			}
		}
#endif
	}

	//	Make children of smart bomb bounce so if they hit a wall right away, they
	//	won't detonate.  The frame interval code will clear this bit after 1/2 second.
#if DXX_BUILD_DESCENT == 1
	if ((weapon_type == weapon_id_type::PLAYER_SMART_HOMING_ID) || (weapon_type == weapon_id_type::ROBOT_SMART_HOMING_ID))
#elif DXX_BUILD_DESCENT == 2
	if ((weapon_type == weapon_id_type::PLAYER_SMART_HOMING_ID) || (weapon_type == weapon_id_type::SMART_MINE_HOMING_ID) || (weapon_type == weapon_id_type::ROBOT_SMART_HOMING_ID) || (weapon_type == weapon_id_type::ROBOT_SMART_MINE_HOMING_ID) || (weapon_type == weapon_id_type::EARTHSHAKER_MEGA_ID))
#endif
		obj->mtype.phys_info.flags |= PF_BOUNCE;

	const fix laser_length{
		(weapon_info.render == WEAPON_RENDER_POLYMODEL)
		? (LevelSharedPolygonModelState.Polygon_models[obj->rtype.pobj_info.model_num].rad * 2)
		: 0
	};

	if (weapon_type == weapon_id_type::FLARE_ID)
		obj->mtype.phys_info.flags |= PF_STICK;		//this obj sticks to walls

	const auto Difficulty_level = GameUniqueState.Difficulty_level;
	obj->shields = weapon_info.strength[Difficulty_level];

	// Fill in laser-specific data

	obj->lifeleft							= weapon_info.lifetime;
	obj->ctype.laser_info.parent_type		= parent->type;
	obj->ctype.laser_info.parent_signature = parent->signature;
	obj->ctype.laser_info.parent_num			= parent;

	//	Assign parent type to highest level creator.  This propagates parent type down from
	//	the original creator through weapons which create children of their own (ie, smart missile)
	if (parent->type == OBJ_WEAPON) {
		auto highest_parent = parent;
		int	count;

		count = 0;
		while ((count++ < 10) && (highest_parent->type == OBJ_WEAPON)) {
			const auto next_parent = highest_parent->ctype.laser_info.parent_num;
			const auto &&parent_objp = parent.absolute_sibling(next_parent);
			if (!laser_parent_is_object(highest_parent->ctype.laser_info, parent_objp))
				break;	//	Probably means parent was killed.  Just continue.

			if (next_parent == highest_parent) {
				Int3();	//	Hmm, object is parent of itself.  This would seem to be bad, no?
				break;
			}

			highest_parent = parent_objp;

			obj->ctype.laser_info.parent_num			= highest_parent;
			obj->ctype.laser_info.parent_type = highest_parent->type;
			obj->ctype.laser_info.parent_signature = highest_parent->signature;
		}
	}

	// Create orientation matrix so we can look from this pov
	//	Homing missiles also need an orientation matrix so they know if they can make a turn.
	if ((weapon_info.homing_flag && (obj->ctype.laser_info.track_goal = object_none, true)) || obj->render_type == render_type::RT_POLYOBJ)
		vm_vector_to_matrix_u(obj->orient, direction, parent->orient.uvec);

	if (( parent != Viewer ) && (parent->type != OBJ_WEAPON))	{
		// Muzzle flash
		if (const auto flash_vclip = weapon_info.flash_vclip; Vclip.valid_index(flash_vclip))
			object_create_explosion_without_damage(Vclip, segnum.absolute_sibling(obj->segnum), obj->pos, weapon_info.flash_size, flash_vclip);
	}

	if (weapon_info.flash_sound != sound_effect::None)
	{
		if (make_sound != weapon_sound_flag::silent)
		{
			if (parent == Viewer)
			{
				// Make your own vulcan gun  1/2 as loud.
				digi_play_sample(weapon_info.flash_sound, weapon_type == weapon_id_type::VULCAN_ID ? F0_5 : F1_0);
			} else {
				digi_link_sound_to_pos(weapon_info.flash_sound, segnum.absolute_sibling(obj->segnum), sidenum_t::WLEFT, obj->pos, 0, F1_0);
			}
		}
	}

	//	Fire the laser from the gun tip so that the back end of the laser bolt is at the gun tip.
	// Move 1 frame, so that the end-tip of the laser is touching the gun barrel.
	// This also jitters the laser a bit so that it doesn't alias.
	//	Don't do for weapons created by weapons.
#if DXX_BUILD_DESCENT == 1
	if (parent->type != OBJ_WEAPON && weapon_info.render != WEAPON_RENDER_NONE && weapon_type != weapon_id_type::FLARE_ID)
#elif DXX_BUILD_DESCENT == 2
	if (parent->type == OBJ_PLAYER && weapon_info.render != WEAPON_RENDER_NONE && weapon_type != weapon_id_type::FLARE_ID)
#endif
	{
	 	const auto end_pos{vm_vec_scale_add(obj->pos, direction, (laser_length / 2))};
		const auto &&end_segnum = find_point_seg(LevelSharedSegmentState, LevelUniqueSegmentState, end_pos, Segments.vmptridx(obj->segnum));
		if (end_segnum != obj->segnum) {
			if (end_segnum != segment_none) {
				obj->pos = end_pos;
				obj_relink(vmobjptr, vmsegptr, obj, end_segnum);
			}
		} else
			obj->pos = end_pos;
	}

	//	Here's where to fix the problem with objects which are moving backwards imparting higher velocity to their weaponfire.
	//	Find out if moving backwards.
	if (is_proximity_bomb_or_player_smart_mine(weapon_type)) {
		parent_speed = vm_vec_mag_quick(parent->mtype.phys_info.velocity);
		if (vm_vec_dot(parent->mtype.phys_info.velocity, parent->orient.fvec) < 0)
			parent_speed = -parent_speed;
	} else
		parent_speed = 0;

	auto weapon_speed{weapon_info.speed[Difficulty_level]};
#if DXX_BUILD_DESCENT == 2
	if (weapon_info.speedvar != 128)
	{
		fix	randval;

		//	Get a scale factor between speedvar% and 1.0.
		randval = F1_0 - ((d_rand() * weapon_info.speedvar) >> 6);
		weapon_speed = fixmul(weapon_speed, randval);
	}
#endif

	//	Ugly hack (too bad we're on a deadline), for homing missiles dropped by smart bomb, start them out slower.
#if DXX_BUILD_DESCENT == 1
	if (weapon_type == weapon_id_type::PLAYER_SMART_HOMING_ID || weapon_type == weapon_id_type::ROBOT_SMART_HOMING_ID)
#elif DXX_BUILD_DESCENT == 2
	if (weapon_type == weapon_id_type::PLAYER_SMART_HOMING_ID || weapon_type == weapon_id_type::SMART_MINE_HOMING_ID || weapon_type == weapon_id_type::ROBOT_SMART_HOMING_ID || weapon_type == weapon_id_type::ROBOT_SMART_MINE_HOMING_ID || weapon_type == weapon_id_type::EARTHSHAKER_MEGA_ID)
#endif
		weapon_speed /= 4;

	if (weapon_info.thrust)
		weapon_speed /= 2;

	obj->mtype.phys_info.velocity = vm_vec_copy_scale(direction, weapon_speed + parent_speed);

	//	Set thrust
	if (weapon_info.thrust)
	{
		obj->mtype.phys_info.thrust = obj->mtype.phys_info.velocity;
		vm_vec_scale(obj->mtype.phys_info.thrust, fixdiv(weapon_info.thrust, weapon_speed+parent_speed));
	}

	if (obj->type == OBJ_WEAPON && weapon_type == weapon_id_type::FLARE_ID)
		obj->lifeleft += (d_rand()-16384) << 2;		//	add in -2..2 seconds

	return obj;
}

//	-----------------------------------------------------------------------------------------------------------
//	Calls Laser_create_new, but takes care of the segment and point computation for you.
imobjptridx_t Laser_create_new_easy(const d_robot_info_array &Robot_info, const vms_vector &direction, const vms_vector &position, const vmobjptridx_t parent, weapon_id_type weapon_type, const weapon_sound_flag make_sound)
{
	fvi_info		hit_data;

	//	Find segment containing laser fire position.  If the robot is straddling a segment, the position from
	//	which it fires may be in a different segment, which is bad news for find_vector_intersection.  So, cast
	//	a ray from the object center (whose segment we know) to the laser position.  Then, in the call to Laser_create_new
	//	use the data returned from this call to find_vector_intersection.
	//	Note that while find_vector_intersection is pretty slow, it is not terribly slow if the destination point is
	//	in the same segment as the source point.

	const auto fate = find_vector_intersection(fvi_query{
		parent->pos,
		position,
		fvi_query::unused_ignore_obj_list,
		&LevelUniqueObjectState,
		&Robot_info,
		FQ_TRANSWALL,		//what about trans walls???
		parent,
	}, parent->segnum, 0, hit_data);
	if (fate != fvi_hit_type::None || hit_data.hit_seg == segment_none)
	{
		return object_none;
	}

	return Laser_create_new(direction, hit_data.hit_pnt, vmsegptridx(hit_data.hit_seg), parent, weapon_type, make_sound);
}

}

namespace dcx {

std::array<muzzle_info, MUZZLE_QUEUE_MAX> Muzzle_data;

namespace {

static fix get_weapon_energy_usage_with_difficulty(const weapon_info &wi, const Difficulty_level_type Difficulty_level)
{
	const auto energy_usage{wi.energy_usage};
	if (Difficulty_level == Difficulty_level_type::_0 || Difficulty_level == Difficulty_level_type::_1)
		return fixmul(energy_usage, i2f(underlying_value(Difficulty_level) + 2) / 4);
	return energy_usage;
}

}

}

namespace d1x {
namespace {

static fix get_scaled_min_trackable_dot()
{
	const fix curFT{HOMING_TRACKABLE_DOT_FRAME_TIME};
	if (curFT <= F1_0 / 16)
		return (3 * (F1_0 - HOMING_MIN_TRACKABLE_DOT) / 4 + HOMING_MIN_TRACKABLE_DOT);
	else if (curFT < F1_0 / 4)
		return (fixmul(F1_0 - HOMING_MIN_TRACKABLE_DOT, F1_0 - 4 * curFT) + HOMING_MIN_TRACKABLE_DOT);
	else
		return (HOMING_MIN_TRACKABLE_DOT);
}

}
}

namespace dsx {

//	-----------------------------------------------------------------------------------------------------------
//	Determine if two objects are on a line of sight.  If so, return true, else return false.
//	Calls fvi.
int object_to_object_visibility(const vcobjptridx_t obj1, const object_base &obj2, int trans_type)
{
	fvi_info		hit_data;
	switch(const auto fate = find_vector_intersection(fvi_query{
		obj1->pos,
		obj2.pos,
		fvi_query::unused_ignore_obj_list,
		fvi_query::unused_LevelUniqueObjectState,
		fvi_query::unused_Robot_info,
		trans_type,
		obj1,
	}, obj1->segnum, 0x10, hit_data))
	{
		case fvi_hit_type::None:
			return 1;
		case fvi_hit_type::Wall:
			return 0;
		default:
			con_printf(CON_VERBOSE, "object_to_object_visibility: fate=%u for object %hu{%hu/%i,%i,%i} to {%i,%i,%i}", underlying_value(fate), static_cast<vcobjptridx_t::integral_type>(obj1), obj1->segnum, obj1->pos.x, obj1->pos.y, obj1->pos.z, obj2.pos.x, obj2.pos.y, obj2.pos.z);
		// Int3();		//	Contact Mike: Oops, what happened?  What is fate?
						// 2 = hit object (impossible), 3 = bad starting point (bad)
			break;
	}
	return 0;
}

namespace {

#if DXX_BUILD_DESCENT == 2
static fix get_scaled_min_trackable_dot()
{
	if (EMULATING_D1)
		return ::d1x::get_scaled_min_trackable_dot();
	const fix curFT{HOMING_TRACKABLE_DOT_FRAME_TIME};
	if (curFT <= F1_0/64)
		return (HOMING_MIN_TRACKABLE_DOT);
	else if (curFT < F1_0/32)
		return (HOMING_MIN_TRACKABLE_DOT + F1_0/64 - 2*curFT);
	else if (curFT < F1_0/4)
		return (HOMING_MIN_TRACKABLE_DOT + F1_0/64 - F1_0/16 - curFT);
	else
		return (HOMING_MIN_TRACKABLE_DOT + F1_0/64 - F1_0/8);
}
#endif

//	-----------------------------------------------------------------------------------------------------------
//	Return true if weapon *tracker is able to track object Objects[track_goal], else return false.
//	In order for the object to be trackable, it must be within a reasonable turning radius for the missile
//	and it must not be obstructed by a wall.
static int object_is_trackable(const imobjptridx_t objp, const vmobjptridx_t tracker, fix *dot)
{
	if (objp == object_none)
		return 0;
	if (Game_mode & GM_MULTI_COOP)
		return 0;
	//	Don't track player if he's cloaked.
	if ((objp == get_local_player().objnum) && (objp->ctype.player_info.powerup_flags & PLAYER_FLAGS_CLOAKED))
		return 0;
#if DXX_BUILD_DESCENT == 2
	auto &Robot_info = LevelSharedRobotInfoState.Robot_info;
#endif

	//	Can't track AI object if he's cloaked.
	if (objp->type == OBJ_ROBOT) {
		if (objp->ctype.ai_info.CLOAKED)
			return 0;
#if DXX_BUILD_DESCENT == 2
		//	Your missiles don't track your escort.
		if (Robot_info[get_robot_id(objp)].companion)
			if (tracker->ctype.laser_info.parent_type == OBJ_PLAYER)
				return 0;
#endif
	}
	auto vector_to_goal{vm_vec_normalized_quick(vm_vec_sub(objp->pos, tracker->pos))};
	*dot = vm_vec_dot(vector_to_goal, tracker->orient.fvec);

#if DXX_BUILD_DESCENT == 2
	if ((*dot < get_scaled_min_trackable_dot()) && (*dot > F1_0*9/10)) {
		vm_vec_normalize(vector_to_goal);
		*dot = vm_vec_dot(vector_to_goal, tracker->orient.fvec);
	}
#endif

	if (*dot >= get_scaled_min_trackable_dot()) {
		//	dot is in legal range, now see if object is visible
		return object_to_object_visibility(tracker, objp, FQ_TRANSWALL);
	} else {
		return 0;
	}
}

//	--------------------------------------------------------------------------------------------
static imobjptridx_t call_find_homing_object_complete(const vms_vector &curpos, const vmobjptridx_t tracker)
{
	if (Game_mode & GM_MULTI) {
		if (tracker->ctype.laser_info.parent_type == OBJ_PLAYER) {
			//	It's fired by a player, so if robots present, track robot, else track player.
			if (Game_mode & GM_MULTI_COOP)
				return find_homing_object_complete( curpos, tracker, OBJ_ROBOT, -1);
			else
				return find_homing_object_complete( curpos, tracker, OBJ_PLAYER, OBJ_ROBOT);
		} else {
			int	goal2_type{
#if DXX_BUILD_DESCENT == 2
				(cheats.robotskillrobots)
				? OBJ_ROBOT
				:
#endif
				-1
			};
			assert(tracker->ctype.laser_info.parent_type == OBJ_ROBOT);
			return find_homing_object_complete(curpos, tracker, OBJ_PLAYER, goal2_type);
		}
	} else
		return find_homing_object_complete( curpos, tracker, OBJ_ROBOT, -1);
}

//	--------------------------------------------------------------------------------------------
//	Find object to home in on.
//	Scan list of objects rendered last frame, find one that satisfies function of nearness to center and distance.
static imobjptridx_t find_homing_object(const vms_vector &curpos, const vmobjptridx_t tracker)
{
	//	Contact Mike: This is a bad and stupid thing.  Who called this routine with an illegal laser type??
#ifndef NDEBUG
	const auto tracker_id{get_weapon_id(tracker)};
#if DXX_BUILD_DESCENT == 2
	if (tracker_id != weapon_id_type::OMEGA_ID)
#endif
		assert(Weapon_info[tracker_id].homing_flag);
#endif

	//	Find an object to track based on game mode (eg, whether in network play) and who fired it.

		return call_find_homing_object_complete(curpos, tracker);
}

//	--------------------------------------------------------------------------------------------
//	Find object to home in on.
//	Scan list of objects rendered last frame, find one that satisfies function of nearness to center and distance.
//	Can track two kinds of objects.  If you are only interested in one type, set track_obj_type2 to NULL
//	Always track proximity bombs.  --MK, 06/14/95
//	Make homing objects not track parent's prox bombs.
imobjptridx_t find_homing_object_complete(const vms_vector &curpos, const vmobjptridx_t tracker, int track_obj_type1, int track_obj_type2)
{
	auto &Objects = LevelUniqueObjectState.Objects;
	auto &vcobjptr = Objects.vcptr;
	auto &vmobjptridx = Objects.vmptridx;

	const auto tracker_id{get_weapon_id(tracker)};
#if DXX_BUILD_DESCENT == 2
	auto &Robot_info = LevelSharedRobotInfoState.Robot_info;
	if (tracker_id != weapon_id_type::OMEGA_ID)
	//	Contact Mike: This is a bad and stupid thing.  Who called this routine with an illegal laser type??
#endif
	{
		if (!Weapon_info[tracker_id].homing_flag)
			throw std::logic_error("tracking without homing_flag");
	}

	const fix64 HOMING_MAX_TRACKABLE_DIST{F1_0 * 250};
	const auto max_trackable_dist{
#if DXX_BUILD_DESCENT == 2
		(tracker_id == weapon_id_type::OMEGA_ID)
		? vm_distance_squared{(OMEGA_MAX_TRACKABLE_DIST * OMEGA_MAX_TRACKABLE_DIST)}
		:
#endif
		vm_distance_squared{HOMING_MAX_TRACKABLE_DIST * HOMING_MAX_TRACKABLE_DIST}
	};
	const auto min_trackable_dot{
#if DXX_BUILD_DESCENT == 2
		(tracker_id == weapon_id_type::OMEGA_ID)
		? OMEGA_MIN_TRACKABLE_DOT
		:
#endif
		HOMING_MIN_TRACKABLE_DOT
	};

	imobjptridx_t best_objnum{object_none};
	fix	max_dot{-F1_0 * 2};
	range_for (const auto &&curobjp, vmobjptridx)
	{
		int is_proximity{0};

		if ((curobjp->type != track_obj_type1) && (curobjp->type != track_obj_type2))
		{
#if DXX_BUILD_DESCENT == 2
			if ((curobjp->type == OBJ_WEAPON) && (is_proximity_bomb_or_player_smart_mine(get_weapon_id(curobjp)))) {
				auto &cur_laser_info = curobjp->ctype.laser_info;
				auto &tracker_laser_info = tracker->ctype.laser_info;
				if (cur_laser_info.parent_num != tracker_laser_info.parent_num || cur_laser_info.parent_signature != tracker_laser_info.parent_signature)
					is_proximity = 1;
				else
					continue;
			} else
#endif
				continue;
		}

		if (curobjp == tracker->ctype.laser_info.parent_num) // Don't track shooter
			continue;

		//	Don't track cloaked players.
		if (curobjp->type == OBJ_PLAYER)
		{
			if (curobjp->ctype.player_info.powerup_flags & PLAYER_FLAGS_CLOAKED)
				continue;
			// Don't track teammates in team games
			if (Game_mode & GM_TEAM)
			{
				const auto &&objparent = vcobjptr(tracker->ctype.laser_info.parent_num);
				if (objparent->type == OBJ_PLAYER && multi_get_team_from_player(Netgame, get_player_id(curobjp)) == multi_get_team_from_player(Netgame, get_player_id(objparent)))
					continue;
			}
		}

		//	Can't track AI object if he's cloaked.
		if (curobjp->type == OBJ_ROBOT) {
			if (curobjp->ctype.ai_info.CLOAKED)
				continue;

#if DXX_BUILD_DESCENT == 2
			//	Your missiles don't track your escort.
			if (Robot_info[get_robot_id(curobjp)].companion)
				if (tracker->ctype.laser_info.parent_type == OBJ_PLAYER)
					continue;
#endif
		}

		auto vec_to_curobj{vm_vec_sub(curobjp->pos, curpos)};
		auto dist{vm_vec_mag2(vec_to_curobj)};

		if (build_vm_distance_squared(dist) < max_trackable_dist) {
			vm_vec_normalize(vec_to_curobj);
			fix dot{vm_vec_dot(vec_to_curobj, tracker->orient.fvec)};
			if (is_proximity)
				dot = ((dot << 3) + dot) >> 3;		//	I suspect Watcom would be too stupid to figure out the obvious...

			if (dot > min_trackable_dot) {
				if (dot > max_dot) {
					if (object_to_object_visibility(tracker, curobjp, FQ_TRANSWALL)) {
						max_dot = dot;
						best_objnum = curobjp;
					}
				}
			}
		}
	}
	return best_objnum;
}

}

#ifdef NEWHOMER
// Similar to calc_d_tick but made just for the homers.
// Causes d_homer_tick_step to be true in intervals dictated by HOMING_TURN_TIME
// and increments d_homer_tick_count accordingly
void calc_d_homer_tick()
{
	auto &Objects = LevelUniqueObjectState.Objects;
	auto &vmobjptr = Objects.vmptr;
        static fix timer = 0;
	auto t = timer + FrameTime;
	d_homer_tick_step = t >= HOMING_TURN_TIME;
	if (d_homer_tick_step)
        {
                d_homer_tick_count++;
                if (d_homer_tick_count > F1_0)
                        d_homer_tick_count = 0;
		t -= HOMING_TURN_TIME;
        // Don't let slowdowns have a lasting impact; allow you to build up at most 3 frames worth
		if (t > HOMING_TURN_TIME*3)
			t = HOMING_TURN_TIME*3;

		get_local_plrobj().ctype.player_info.homing_object_dist = -1; // Assume not being tracked.  Laser_do_weapon_sequence modifies this. Let's do this here since the homers do not track every frame, we may not want to reset this ever frame.
	}
	timer = t;
}
#endif

namespace {

//	------------------------------------------------------------------------------------------------------------
//	See if legal to keep tracking currently tracked object.  If not, see if another object is trackable.  If not, return -1,
//	else return object number of tracking object.
//	Computes and returns a fairly precise dot product.
static imobjptridx_t track_track_goal(fvcobjptr &vcobjptr, const imobjptridx_t track_goal, const vmobjptridx_t tracker, fix *dot, fix tick_count)
{
#if DXX_BUILD_DESCENT == 1
	if (object_is_trackable(track_goal, tracker, dot))
#elif DXX_BUILD_DESCENT == 2
	//	Every 8 frames for each object, scan all objects.
	if (object_is_trackable(track_goal, tracker, dot) && (((tracker ^ tick_count) % 8) != 0))
#endif
	{
		return track_goal;
	} else if (((tracker ^ tick_count) % 4) == 0)
	{
		int	goal_type, goal2_type;
		//	If player fired missile, then search for an object, if not, then give up.
		if (vcobjptr(tracker->ctype.laser_info.parent_num)->type == OBJ_PLAYER)
		{
			if (track_goal == object_none)
			{
				if (Game_mode & GM_MULTI)
				{
					if (Game_mode & GM_MULTI_COOP)
						goal_type = OBJ_ROBOT, goal2_type = -1;
					else
					{
						goal_type = OBJ_PLAYER;
						goal2_type = (Game_mode & GM_MULTI_ROBOTS)
							? OBJ_ROBOT	//	Not cooperative, if robots, track either robot or player
							: -1;		//	Not cooperative and no robots, track only a player
					}
				}
				else
					goal_type = OBJ_PLAYER, goal2_type = OBJ_ROBOT;
			}
			else
			{
				goal_type = vcobjptr(tracker->ctype.laser_info.track_goal)->type;
				if ((goal_type == OBJ_PLAYER) || (goal_type == OBJ_ROBOT))
					goal2_type = -1;
				else
					return object_none;
			}
		}
		else {
			goal2_type = -1;

#if DXX_BUILD_DESCENT == 2
			if (cheats.robotskillrobots)
				goal2_type = OBJ_ROBOT;
#endif

			if (track_goal == object_none)
				goal_type = OBJ_PLAYER;
			else {
				goal_type = vcobjptr(tracker->ctype.laser_info.track_goal)->type;
				assert(goal_type != OBJ_GHOST);
			}
		}
		return find_homing_object_complete(tracker->pos, tracker, goal_type, goal2_type);
	}

	return object_none;
}

//-------------- Initializes a laser after Fire is pressed -----------------

static imobjptridx_t Laser_player_fire_spread_delay(const d_robot_info_array &Robot_info, fvmsegptridx &vmsegptridx, const vmobjptridx_t obj, const weapon_id_type laser_type, const player_gun_number gun_num, const fix spreadr, const fix spreadu, const fix delay_time, const weapon_sound_flag make_sound, const vms_vector &shot_orientation, const icobjidx_t Network_laser_track)
{
	fvi_info		hit_data;
	// Find the initial position of the laser
	auto pnt{&Player_ship->gun_points[gun_num]};
	const auto gun_point{vm_vec_rotate(*pnt, vm_transposed_matrix(obj->orient))};
	auto LaserPos{vm_vec_add(obj->pos, gun_point)};

	//	If supposed to fire at a delayed time (delay_time), then move this point backwards.
	if (delay_time)
		vm_vec_scale_add2(LaserPos, shot_orientation, -fixmul(delay_time, Weapon_info[laser_type].speed[GameUniqueState.Difficulty_level]));

//	do_muzzle_stuff(obj, &Pos);

	//--------------- Find LaserPos and LaserSeg ------------------
	const auto Fate{find_vector_intersection(fvi_query{
		obj->pos,
		LaserPos,
		fvi_query::unused_ignore_obj_list,
		&LevelUniqueObjectState,
		&Robot_info,
#if DXX_BUILD_DESCENT == 1
		0,
#elif DXX_BUILD_DESCENT == 2
		FQ_IGNORE_POWERUPS,
#endif
		obj,
	}, obj->segnum, 0x10, hit_data)};

	auto LaserSeg{hit_data.hit_seg};
	if (LaserSeg == segment_none)		//some sort of annoying error
		return object_none;

	//SORT OF HACK... IF ABOVE WAS CORRECT THIS WOULDNT BE NECESSARY.
	if ( vm_vec_dist_quick(LaserPos, obj->pos) > 0x50000 )
		return object_none;

	if (Fate == fvi_hit_type::Wall)
		return object_none;

	if (Fate == fvi_hit_type::Object)
	{
//		if ( Objects[hit_data.hit_object].type == OBJ_ROBOT )
//			Objects[hit_data.hit_object].flags |= OF_SHOULD_BE_DEAD;
//		if ( Objects[hit_data.hit_object].type != OBJ_POWERUP )
//			return;
	//as of 12/6/94, we don't care if the laser is stuck in an object. We
	//just fire away normally
	}

	//	Now, make laser spread out.
	auto LaserDir{shot_orientation};
	if ((spreadr != 0) || (spreadu != 0)) {
		vm_vec_scale_add2(LaserDir, obj->orient.rvec, spreadr);
		vm_vec_scale_add2(LaserDir, obj->orient.uvec, spreadu);
	}

	const auto &&objnum = Laser_create_new(LaserDir, LaserPos, vmsegptridx(LaserSeg), obj, laser_type, make_sound);

	if (objnum == object_none)
		return object_none;

#if DXX_BUILD_DESCENT == 2
	create_awareness_event(obj, player_awareness_type_t::PA_WEAPON_WALL_COLLISION, LevelUniqueRobotAwarenessState);

	//	Omega cannon is a hack, not surprisingly.  Don't want to do the rest of this stuff.
	if (laser_type == weapon_id_type::OMEGA_ID)
		return objnum;

	if (laser_type == weapon_id_type::CONCUSSION_ID ||
			 laser_type == weapon_id_type::HOMING_ID ||
			 laser_type == weapon_id_type::SMART_ID ||
			 laser_type == weapon_id_type::MEGA_ID ||
			 laser_type == weapon_id_type::FLASH_ID ||
			 //laser_type == GUIDEDMISS_ID ||
			 //laser_type == SUPERPROX_ID ||
			 laser_type == weapon_id_type::MERCURY_ID ||
			 laser_type == weapon_id_type::EARTHSHAKER_ID)
	{
		const auto need_new_missile_viewer = [obj]{
			if (!Missile_viewer)
				return true;
			if (Missile_viewer->type != OBJ_WEAPON)
				return true;
			if (Missile_viewer->signature != Missile_viewer_sig)
				return true;
			if (get_player_id(obj) == Player_num && Missile_viewer->ctype.laser_info.parent_num != get_local_player().objnum)
				/* New missile fired-by local player &&
				 * currently viewing missile not-fired-by local player
				 */
				return true;
			return false;
		};
		const auto can_view_missile = [obj]{
			const auto obj_id{get_player_id(obj)};
			if (obj_id == Player_num)
				return true;
			if (PlayerCfg.MissileViewEnabled != MissileViewMode::EnabledSelfAndAllies)
				return false;
                        {
                                if (Game_mode & GM_MULTI_COOP)
                                        return true;
                                if (Game_mode & GM_TEAM)
					return multi_get_team_from_player(Netgame, Player_num) == multi_get_team_from_player(Netgame, obj_id);
                        }
			return false;
		};
		if (need_new_missile_viewer() && can_view_missile())
		{
			Missile_viewer = objnum;
			Missile_viewer_sig = objnum->signature;
		}
	}
#endif

	//	If this weapon is supposed to be silent, set that bit!
	if (make_sound == weapon_sound_flag::silent)
		objnum->flags |= OF_SILENT;

	//	If the object firing the laser is the player, then indicate the laser object so robots can dodge.
	//	New by MK on 6/8/95, don't let robots evade proximity bombs, thereby decreasing uselessness of bombs.
	if (obj == ConsoleObject)
#if DXX_BUILD_DESCENT == 2
		if (!is_proximity_bomb_or_player_smart_mine(get_weapon_id(objnum)))
#endif
		Player_fired_laser_this_frame = objnum;

	if (Weapon_info[laser_type].homing_flag) {
		if (obj == ConsoleObject)
		{
			objnum->ctype.laser_info.track_goal = find_homing_object(LaserPos, objnum);
		}
		else // Some other player shot the homing thing
		{
			Assert(Game_mode & GM_MULTI);
			objnum->ctype.laser_info.track_goal = Network_laser_track;
		}
	}

	return objnum;
}

//	-----------------------------------------------------------------------------------------------------------
static imobjptridx_t Laser_player_fire_spread(const d_robot_info_array &Robot_info, const vmobjptridx_t obj, const weapon_id_type laser_type, const player_gun_number gun_num, const fix spreadr, const fix spreadu, const weapon_sound_flag make_sound, const vms_vector &shot_orientation, const icobjidx_t Network_laser_track)
{
	return Laser_player_fire_spread_delay(Robot_info, vmsegptridx, obj, laser_type, gun_num, spreadr, spreadu, 0, make_sound, shot_orientation, Network_laser_track);
}

}

//	-----------------------------------------------------------------------------------------------------------
imobjptridx_t Laser_player_fire(const d_robot_info_array &Robot_info, const vmobjptridx_t obj, const weapon_id_type laser_type, const player_gun_number gun_num, const weapon_sound_flag make_sound, const vms_vector &shot_orientation, const icobjidx_t Network_laser_track)
{
	return Laser_player_fire_spread(Robot_info, obj, laser_type, gun_num, 0, 0, make_sound, shot_orientation, Network_laser_track);
}

//	-----------------------------------------------------------------------------------------------------------
void Flare_create(const vmobjptridx_t obj)
{
	const auto energy_usage{get_weapon_energy_usage_with_difficulty(Weapon_info[weapon_id_type::FLARE_ID], GameUniqueState.Difficulty_level)};

//	MK, 11/04/95: Allowed to fire flare even if no energy.
// -- 	if (Players[Player_num].energy >= energy_usage)
	auto &&plrobj = *obj;
	auto &player_info = plrobj.ctype.player_info;
	auto &energy = player_info.energy;
#if DXX_BUILD_DESCENT == 1
	if (energy > 0)
#endif
	{
		const auto &&flare = Laser_player_fire(LevelSharedRobotInfoState.Robot_info, obj, weapon_id_type::FLARE_ID, player_gun_number::center, weapon_sound_flag::audible, plrobj.orient.fvec, object_none);
		if (flare == object_none)
			return;
		const fix next_energy{
			energy > energy_usage
			? energy - energy_usage
			: 0
		};
		energy = next_energy;
#if DXX_BUILD_DESCENT == 1
		if (next_energy == 0)
			auto_select_primary_weapon(player_info);
#endif

		if (Game_mode & GM_MULTI)
			multi_send_fire(plrobj.orient, FLARE_ADJUST, laser_level::_1	/* unused */, 0, object_none, object_none);
	}

}

namespace {

#if DXX_BUILD_DESCENT == 1
static constexpr int HOMING_MISSILE_SCALE{8};
#elif DXX_BUILD_DESCENT == 2
static constexpr int HOMING_MISSILE_SCALE{16};

static bool is_active_guided_missile(d_level_unique_object_state &LevelUniqueObjectState, const vcobjptridx_t obj)
{
	if (obj->ctype.laser_info.parent_type != OBJ_PLAYER)
		return false;
	auto &vcobjptr = LevelUniqueObjectState.get_objects().vcptr;
	auto &parent_obj = *vcobjptr(obj->ctype.laser_info.parent_num);
	if (parent_obj.type != OBJ_PLAYER)
		return false;
	return LevelUniqueObjectState.Guided_missile.get_player_active_guided_missile(get_player_id(parent_obj)) == obj;
}
#endif

//--------------------------------------------------------------------
//	Set object *objp's orientation to (or towards if I'm ambitious) its velocity.
static void homing_missile_turn_towards_velocity(object_base &obj, vms_vector new_fvec, fix ft)
{
	vm_vec_scale(new_fvec, ft * HOMING_MISSILE_SCALE);
	vm_vec_add2(new_fvec, obj.orient.fvec);
	vm_vector_to_matrix(obj.orient, vm_vec_normalized_quick(new_fvec));
}

}

//-------------------------------------------------------------------------------------------
//sequence this laser object for this _frame_ (underscores added here to aid MK in his searching!)
void Laser_do_weapon_sequence(const d_robot_info_array &Robot_info, const vmobjptridx_t obj)
{
	auto &Objects = LevelUniqueObjectState.Objects;
	auto &imobjptridx = Objects.imptridx;
	auto &vcobjptr = Objects.vcptr;
	auto &vmobjptr = Objects.vmptr;
	assert(obj->control_source == object::control_type::weapon);

	if (obj->lifeleft < 0 ) {		// We died of old age
		obj->flags |= OF_SHOULD_BE_DEAD;
		if ( Weapon_info[get_weapon_id(obj)].damage_radius )
			explode_badass_weapon(Robot_info, obj, obj->pos);
		return;
	}

#if DXX_BUILD_DESCENT == 2
	if (omega_cleanup(vcobjptr, obj))
		return;
#endif

	//delete weapons that are not moving
	const auto Difficulty_level{GameUniqueState.Difficulty_level};
	if (	!((d_tick_count ^ static_cast<uint16_t>(obj->signature)) & 3) &&
			(get_weapon_id(obj) != weapon_id_type::FLARE_ID) &&
			(Weapon_info[get_weapon_id(obj)].speed[Difficulty_level] > 0) &&
			(vm_vec_mag_quick(obj->mtype.phys_info.velocity) < F2_0)) {
		obj_delete(LevelUniqueObjectState, Segments, obj);
		return;
	}

	if (get_weapon_id(obj) == weapon_id_type::FUSION_ID)
	{		//always set fusion weapon to max vel
		vm_vec_normalize_quick(obj->mtype.phys_info.velocity);
		vm_vec_scale(obj->mtype.phys_info.velocity, Weapon_info[weapon_id_type::FUSION_ID].speed[Difficulty_level]);
	}

	//	For homing missiles, turn towards target. (unless it's the guided missile)
#if DXX_BUILD_DESCENT == 1
	if (Weapon_info[get_weapon_id(obj)].homing_flag)
#elif DXX_BUILD_DESCENT == 2
	if (Weapon_info[get_weapon_id(obj)].homing_flag && !is_active_guided_missile(LevelUniqueObjectState, obj))
#endif
	{
		fix dot{F1_0};

		//	For first 125ms of life, missile flies straight.
		if (obj->ctype.laser_info.creation_time + HOMING_FLY_STRAIGHT_TIME < GameTime64)
		{
			//	If it's time to do tracking, then it's time to grow up, stop bouncing and start exploding!.
#if DXX_BUILD_DESCENT == 1
			if ((get_weapon_id(obj) == weapon_id_type::ROBOT_SMART_HOMING_ID) || (get_weapon_id(obj) == weapon_id_type::PLAYER_SMART_HOMING_ID))
#elif DXX_BUILD_DESCENT == 2
			if ((get_weapon_id(obj) == weapon_id_type::ROBOT_SMART_MINE_HOMING_ID) || (get_weapon_id(obj) == weapon_id_type::ROBOT_SMART_HOMING_ID) || (get_weapon_id(obj) == weapon_id_type::SMART_MINE_HOMING_ID) || (get_weapon_id(obj) == weapon_id_type::PLAYER_SMART_HOMING_ID) || (get_weapon_id(obj) == weapon_id_type::EARTHSHAKER_MEGA_ID))
#endif
			{
				obj->mtype.phys_info.flags &= ~PF_BOUNCE;
			}

#ifdef NEWHOMER
			if (d_homer_tick_step)
			{
				const auto &&track_goal = track_track_goal(vcobjptr, imobjptridx(obj->ctype.laser_info.track_goal), obj, &dot, d_homer_tick_count);

				if (track_goal == get_local_player().objnum) {
					const fix dist_to_player{vm_vec_dist_quick(obj->pos, track_goal->pos)};
					auto &homing_object_dist = get_local_plrobj().ctype.player_info.homing_object_dist;
					if (dist_to_player < homing_object_dist || homing_object_dist < 0)
						homing_object_dist = dist_to_player;
				}

				if (track_goal != object_none)
				{
					auto vector_to_object{vm_vec_sub(track_goal->pos, obj->pos)};
					vm_vec_normalize_quick(vector_to_object);
					auto &&[speed_magnitude, temp_vec] = vm_vec_normalize_quick_with_magnitude(obj->mtype.phys_info.velocity);
					fix speed{speed_magnitude};
					const auto max_speed{Weapon_info[get_weapon_id(obj)].speed[Difficulty_level]};
					if (speed+F1_0 < max_speed) {
						speed += fixmul(max_speed, HOMING_TURN_TIME/2);
						if (speed > max_speed)
							speed = max_speed;
					}
#if DXX_BUILD_DESCENT == 1
					dot = vm_vec_dot(temp_vec, vector_to_object);
#endif
					vm_vec_add2(temp_vec, vector_to_object);
					//	The boss' smart children track better...
					if (Weapon_info[get_weapon_id(obj)].render != WEAPON_RENDER_POLYMODEL)
						vm_vec_add2(temp_vec, vector_to_object);
					vm_vec_normalize_quick(temp_vec);
#if DXX_BUILD_DESCENT == 1
					vm_vec_scale(temp_vec, speed);
					obj->mtype.phys_info.velocity = temp_vec;
#elif DXX_BUILD_DESCENT == 2
					obj->mtype.phys_info.velocity = temp_vec;
					vm_vec_scale(obj->mtype.phys_info.velocity, speed);
#endif

					//	Subtract off life proportional to amount turned.
					//	For hardest turn, it will lose 2 seconds per second.
					{
						fix absdot{abs(F1_0 - dot)};
#if DXX_BUILD_DESCENT == 1
						if (absdot > F1_0/8) {
							if (absdot > F1_0/4)
								absdot = F1_0/4;
							obj->lifeleft -= fixmul(absdot*16, HOMING_TURN_TIME);
						}
#elif DXX_BUILD_DESCENT == 2
						obj->lifeleft -= fixmul(absdot*32, HOMING_TURN_TIME);
#endif
					}

					//	Only polygon objects have visible orientation, so only they should turn.
					if (Weapon_info[get_weapon_id(obj)].render == WEAPON_RENDER_POLYMODEL)
						homing_missile_turn_towards_velocity(obj, temp_vec, HOMING_TURN_TIME);		//	temp_vec is normalized velocity.
                                }
                        }
#else // old FPS-dependent homers - NOTE: I know this is very redundant but I want to keep the historical code 100% preserved to compare against potential changes in the above.
			//	Make sure the object we are tracking is still trackable.
			const auto &&track_goal = track_track_goalvcobjptr, (imobjptridx(obj->ctype.laser_info.track_goal), obj, &dot, d_tick_count);

			if (track_goal == get_local_player().objnum) {
				fix	dist_to_player;

				dist_to_player = vm_vec_dist_quick(obj->pos, track_goal->pos);
				auto &homing_object_dist = get_local_plrobj().ctype.player_info.homing_object_dist;
				if (dist_to_player < homing_object_dist || homing_object_dist < 0)
					homing_object_dist = dist_to_player;
			}

			if (track_goal != object_none)
			{
				auto vector_to_object{vm_vec_sub(track_goal->pos, obj->pos)};
				vm_vec_normalize_quick(vector_to_object);
				temp_vec = obj->mtype.phys_info.velocity;
				speed = vm_vec_normalize_quick(temp_vec);
				max_speed = Weapon_info[get_weapon_id(obj)].speed[Difficulty_level];
				if (speed+F1_0 < max_speed) {
					speed += fixmul(max_speed, FrameTime/2);
					if (speed > max_speed)
						speed = max_speed;
				}
#if DXX_BUILD_DESCENT == 1
				dot = vm_vec_dot(temp_vec, vector_to_object);
#endif
				vm_vec_add2(temp_vec, vector_to_object);
				//	The boss' smart children track better...
				if (Weapon_info[get_weapon_id(obj)].render != WEAPON_RENDER_POLYMODEL)
					vm_vec_add2(temp_vec, vector_to_object);
				vm_vec_normalize_quick(temp_vec);
#if DXX_BUILD_DESCENT == 1
				vm_vec_scale(temp_vec, speed);
				obj->mtype.phys_info.velocity = temp_vec;
#elif DXX_BUILD_DESCENT == 2
				obj->mtype.phys_info.velocity = temp_vec;
				vm_vec_scale(obj->mtype.phys_info.velocity, speed);
#endif

				//	Subtract off life proportional to amount turned.
				//	For hardest turn, it will lose 2 seconds per second.
				{
					fix	lifelost, absdot;

					absdot = abs(F1_0 - dot);
#if DXX_BUILD_DESCENT == 1
					if (absdot > F1_0/8) {
						if (absdot > F1_0/4)
							absdot = F1_0/4;
						lifelost = fixmul(absdot*16, FrameTime);
						obj->lifeleft -= lifelost;
					}
#elif DXX_BUILD_DESCENT == 2
					lifelost = fixmul(absdot*32, FrameTime);
					obj->lifeleft -= lifelost;
#endif
				}

				//	Only polygon objects have visible orientation, so only they should turn.
				if (Weapon_info[get_weapon_id(obj)].render == WEAPON_RENDER_POLYMODEL)
					homing_missile_turn_towards_velocity(obj, temp_vec, FrameTime);		//	temp_vec is normalized velocity.
			}
#endif
		}
	}

	//	Make sure weapon is not moving faster than allowed speed.
	const auto &weapon_info = Weapon_info[get_weapon_id(obj)];
#if DXX_BUILD_DESCENT == 1
	if (weapon_info.thrust != 0)
#endif
	{
		fix	weapon_speed{vm_vec_mag_quick(obj->mtype.phys_info.velocity)};
		if (weapon_speed > weapon_info.speed[Difficulty_level])
		{
			//	Only slow down if not allowed to move.  Makes sense, huh?  Allows proxbombs to get moved by physics force. --MK, 2/13/96
#if DXX_BUILD_DESCENT == 2
			if (weapon_info.speed[Difficulty_level])
#endif
			{
				const fix scale_factor{fixdiv(weapon_info.speed[Difficulty_level], weapon_speed)};
				vm_vec_scale(obj->mtype.phys_info.velocity, scale_factor);
			}
		}
	}
}

}

namespace dcx {
namespace {

constexpr std::integral_constant<unsigned, 30> MAX_OBJDISTS{};

static inline int sufficient_energy(int energy_used, fix energy)
{
	return !energy_used || (energy >= energy_used);
}

static inline int sufficient_ammo(int ammo_used, int uses_vulcan_ammo, ushort vulcan_ammo)
{
	return !ammo_used || (!uses_vulcan_ammo || vulcan_ammo >= ammo_used);
}

}
}

namespace dsx {

//	--------------------------------------------------------------------------------------------------
// Assumption: This is only called by the actual console player, not for network players

void do_laser_firing_player(object &plrobj)
{
	auto &Objects = LevelUniqueObjectState.Objects;
	auto &vmobjptridx = Objects.vmptridx;
	int rval{0};

	if (Player_dead_state != player_dead_state::no)
		return;

	auto &player_info = plrobj.ctype.player_info;
	const auto Primary_weapon{player_info.Primary_weapon};
	const auto weapon_index{Primary_weapon_to_weapon_info[Primary_weapon]};
	int ammo_used{Weapon_info[weapon_index].ammo_usage};
	const auto uses_vulcan_ammo{weapon_index_uses_vulcan_ammo(Primary_weapon)};

	auto &pl_energy = player_info.energy;
	const auto base_energy_used{
#if DXX_BUILD_DESCENT == 2
		(Primary_weapon == primary_weapon_index_t::OMEGA_INDEX)
		? 0	//	Omega consumes energy when recharging, not when firing.
		:
#endif
		get_weapon_energy_usage_with_difficulty(Weapon_info[weapon_index], GameUniqueState.Difficulty_level)
	};

	const auto energy_used{
#if DXX_BUILD_DESCENT == 2
	//	MK, 01/26/96, Helix use 2x energy in multiplayer.  bitmaps.tbl parm should have been reduced for single player.
		(weapon_index == weapon_id_type::HELIX_ID && (Game_mode & GM_MULTI))
		? base_energy_used * 2
		:
#endif
		base_energy_used
	};

	if	(!(sufficient_energy(energy_used, pl_energy) && sufficient_ammo(ammo_used, uses_vulcan_ammo, player_info.vulcan_ammo)))
		auto_select_primary_weapon(player_info);		//	Make sure the player can fire from this weapon.

	auto &Next_laser_fire_time = player_info.Next_laser_fire_time;
	while (Next_laser_fire_time <= GameTime64) {
		if	(sufficient_energy(energy_used, pl_energy) && sufficient_ammo(ammo_used, uses_vulcan_ammo, player_info.vulcan_ammo)) {
			int fire_frame_overhead{0};

			if (GameTime64 - Next_laser_fire_time <= FrameTime) // if firing is prolonged by FrameTime overhead, let's try to fix that.
				fire_frame_overhead = GameTime64 - Next_laser_fire_time;

			auto laser_level = player_info.laser_level;
			unsigned flags{};

			switch (Primary_weapon)
			{
				case primary_weapon_index_t::LASER_INDEX:
					if (player_info.powerup_flags & PLAYER_FLAGS_QUAD_LASERS)
						flags |= LASER_QUAD;
					break;
				case primary_weapon_index_t::SPREADFIRE_INDEX:
					flags |= (player_info.Spreadfire_toggle ^= LASER_SPREADFIRE_TOGGLED) & LASER_SPREADFIRE_TOGGLED;
					break;
				case primary_weapon_index_t::VULCAN_INDEX:
				case primary_weapon_index_t::PLASMA_INDEX:
				case primary_weapon_index_t::FUSION_INDEX:
				default:
					break;
#if DXX_BUILD_DESCENT == 2
				case primary_weapon_index_t::HELIX_INDEX:
					flags |= (player_info.Helix_orientation++ & LASER_HELIX_MASK);
					break;
				case primary_weapon_index_t::SUPER_LASER_INDEX:
				case primary_weapon_index_t::GAUSS_INDEX:
				case primary_weapon_index_t::PHOENIX_INDEX:
				case primary_weapon_index_t::OMEGA_INDEX:
					break;
#endif
			}

			const auto shot_fired{do_laser_firing(vmobjptridx(get_local_player().objnum), Primary_weapon, laser_level, flags, plrobj.orient.fvec, object_none)};
			if (!shot_fired)
				break;
			rval += shot_fired;
			Next_laser_fire_time = GameTime64 - fire_frame_overhead + (unlikely(cheats.rapidfire)
				? (F1_0 / 25)
				: (
#if DXX_BUILD_DESCENT == 2
					weapon_index == weapon_id_type::OMEGA_ID
					? OMEGA_BASE_TIME
					:
#endif
					Weapon_info[weapon_index].fire_wait
				)
			);

			if (const auto all_shots_energy_used{(energy_used * rval) / Weapon_info[weapon_index].fire_count}; pl_energy > all_shots_energy_used)
				pl_energy -= all_shots_energy_used;
			else
				pl_energy = 0;

			if (uses_vulcan_ammo) {
				auto &v = player_info.vulcan_ammo;
				if (v < ammo_used)
					v = 0;
				else
					v -= ammo_used;
				maybe_drop_net_powerup(powerup_type_t::POW_VULCAN_AMMO, 1, 0);
			}
		} else {
#if DXX_BUILD_DESCENT == 2
			Next_laser_fire_time = {GameTime64};	//	Prevents shots-to-fire from building up.
#endif
			break;	//	Couldn't fire weapon, so abort.
		}
	}
	auto_select_primary_weapon(player_info);		//	Make sure the player can fire from this weapon.
}

//	--------------------------------------------------------------------------------------------------
//	Object "objnum" fires weapon "weapon_num" of level "level".  (Right now (9/24/94) level is used only for type 0 laser.
//	Flags are the player flags.  For network mode, set to 0.
//	It is assumed that this is a player object (as in multiplayer), and therefore the gun positions are known.
//	Returns whether the weapon was fired.  This is 1 on success, 0 on failure.
//	More than one shot is fired with a pseudo-delay so that players on slow machines can fire (for themselves
//	or other players) often enough for things like the vulcan cannon.
int do_laser_firing(vmobjptridx_t objp, const primary_weapon_index_t weapon_num, const laser_level level, unsigned flags, vms_vector shot_orientation, const icobjidx_t Network_laser_track)
{
	switch (weapon_num) {
		case primary_weapon_index_t::LASER_INDEX: {
			weapon_id_type weapon_type;

			switch(level)
			{
				case laser_level::_1:
					weapon_type = weapon_id_type::LASER_ID_L1;
					break;
				case laser_level::_2:
					weapon_type = weapon_id_type::LASER_ID_L2;
					break;
				case laser_level::_3:
					weapon_type = weapon_id_type::LASER_ID_L3;
					break;
				case laser_level::_4:
					weapon_type = weapon_id_type::LASER_ID_L4;
					break;
#if DXX_BUILD_DESCENT == 2
				case laser_level::_5:
					weapon_type = weapon_id_type::LASER_ID_L5;
					break;
				case laser_level::_6:
					weapon_type = weapon_id_type::LASER_ID_L6;
					break;
#endif
				default:
					/* Invalid laser level.  Cancel the shot. */
					return 0;
			}
			if (Laser_player_fire(LevelSharedRobotInfoState.Robot_info, objp, weapon_type, player_gun_number::_0, weapon_sound_flag::audible, shot_orientation, object_none) == object_none)
				/* If the first one fails, assume all will fail.  Tell the
				 * caller that no shots were fired, so that the player is not
				 * charged for the shot.
				 */
				return 0;
			Laser_player_fire(LevelSharedRobotInfoState.Robot_info, objp, weapon_type, player_gun_number::_1, weapon_sound_flag::silent, shot_orientation, object_none);

			if (flags & LASER_QUAD) {
				//	hideous system to make quad laser 1.5x powerful as normal laser, make every other quad laser bolt harmless
				Laser_player_fire(LevelSharedRobotInfoState.Robot_info, objp, weapon_type, player_gun_number::_2, weapon_sound_flag::silent, shot_orientation, object_none);
				Laser_player_fire(LevelSharedRobotInfoState.Robot_info, objp, weapon_type, player_gun_number::_3, weapon_sound_flag::silent, shot_orientation, object_none);
			}
			break;
		}
		case primary_weapon_index_t::VULCAN_INDEX: {
			if (Laser_player_fire_spread(LevelSharedRobotInfoState.Robot_info, objp, weapon_id_type::VULCAN_ID, player_gun_number::center, d_rand()/8 - 32767/16, d_rand()/8 - 32767/16, weapon_sound_flag::audible, shot_orientation, object_none) == object_none)
				return 0;
			break;
		}
		case primary_weapon_index_t::SPREADFIRE_INDEX:
			{
				fix spreadr0, spreadu0, spreadr1, spreadu1;
				if (flags & LASER_SPREADFIRE_TOGGLED)
					spreadr0 = F1_0 / 16, spreadr1 = -F1_0 / 16, spreadu0 = spreadu1 = 0;
				else
					spreadu0 = F1_0 / 16, spreadu1 = -F1_0 / 16, spreadr0 = spreadr1 = 0;
				if (Laser_player_fire_spread(LevelSharedRobotInfoState.Robot_info, objp, weapon_id_type::SPREADFIRE_ID, player_gun_number::center, spreadr0, spreadu0, weapon_sound_flag::silent, shot_orientation, object_none) == object_none)
					return 0;
				Laser_player_fire_spread(LevelSharedRobotInfoState.Robot_info, objp, weapon_id_type::SPREADFIRE_ID, player_gun_number::center, spreadr1, spreadu1, weapon_sound_flag::silent, shot_orientation, object_none);
				Laser_player_fire_spread(LevelSharedRobotInfoState.Robot_info, objp, weapon_id_type::SPREADFIRE_ID, player_gun_number::center, 0, 0, weapon_sound_flag::audible, shot_orientation, object_none);
			}
			break;

		case primary_weapon_index_t::PLASMA_INDEX:
			if (Laser_player_fire(LevelSharedRobotInfoState.Robot_info, objp, weapon_id_type::PLASMA_ID, player_gun_number::_0, weapon_sound_flag::audible, shot_orientation, object_none) == object_none)
				return 0;
			Laser_player_fire(LevelSharedRobotInfoState.Robot_info, objp, weapon_id_type::PLASMA_ID, player_gun_number::_1, weapon_sound_flag::silent, shot_orientation, object_none);
			break;

		case primary_weapon_index_t::FUSION_INDEX: {
			auto &&weapon_obj = Laser_player_fire(LevelSharedRobotInfoState.Robot_info, objp, weapon_id_type::FUSION_ID, player_gun_number::_0, weapon_sound_flag::audible, shot_orientation, object_none);
			Laser_player_fire(LevelSharedRobotInfoState.Robot_info, objp, weapon_id_type::FUSION_ID, player_gun_number::_1, weapon_sound_flag::audible, shot_orientation, object_none);

			assert(objp->type == OBJ_PLAYER);
			auto &Fusion_charge = objp->ctype.player_info.Fusion_charge;
			flags = static_cast<int8_t>(Fusion_charge >> 12);

			Fusion_charge = 0;
			if (weapon_obj == object_none)
				/* Clear the fusion charge even if the shot fails.  This is bad
				 * for the player, but it is safer than leaving Fusion_charge
				 * set while the cannon is not charging.
				 */
				return 0;

			const auto &obj_fvec = objp->orient.fvec;
			const vms_vector backward_vec{
				.x = -(obj_fvec.x << 7),
				.y = -(obj_fvec.y << 7),
				.z = -(obj_fvec.z << 7)
			};
			phys_apply_force(objp, backward_vec);

			const vms_vector rotation_vec{
				.x = (backward_vec.x >> 4) + d_rand() - 16384,
				.y = (backward_vec.y >> 4) + d_rand() - 16384,
				.z = (backward_vec.z >> 4) + d_rand() - 16384
			};
			phys_apply_rot(objp, rotation_vec);

		}
			break;
#if DXX_BUILD_DESCENT == 2
		case primary_weapon_index_t::GAUSS_INDEX: {
			if (Laser_player_fire_spread(LevelSharedRobotInfoState.Robot_info, objp, weapon_id_type::GAUSS_ID, player_gun_number::center, (d_rand()/8 - 32767/16)/5, (d_rand()/8 - 32767/16)/5, weapon_sound_flag::audible, shot_orientation, object_none) == object_none)
				return 0;
			break;
		}
		case primary_weapon_index_t::HELIX_INDEX: {
			static constexpr std::array<std::pair<fix, fix>, 8> spread{{
				{ F1_0/16, 0},			// Vertical
				{ F1_0/17, F1_0/42},	//  22.5 degrees
				{ F1_0/22, F1_0/22},	//  45   degrees
				{ F1_0/42, F1_0/17},	//  67.5 degrees
				{		0, F1_0/16},	//  90   degrees
				{-F1_0/42, F1_0/17},	// 112.5 degrees
				{-F1_0/22, F1_0/22},	// 135   degrees
				{-F1_0/17, F1_0/42},	// 157.5 degrees
			}};
			const unsigned helix_orient{flags & LASER_HELIX_MASK};
			if (helix_orient >= spread.size())
				break;
			auto &&[spreadr, spreadu] = spread[helix_orient];
			if (Laser_player_fire_spread(LevelSharedRobotInfoState.Robot_info, objp, weapon_id_type::HELIX_ID, player_gun_number::center,  0,  0, weapon_sound_flag::audible, shot_orientation, object_none) == object_none)
				return 0;
			Laser_player_fire_spread(LevelSharedRobotInfoState.Robot_info, objp, weapon_id_type::HELIX_ID, player_gun_number::center,  spreadr,  spreadu, weapon_sound_flag::silent, shot_orientation, object_none);
			Laser_player_fire_spread(LevelSharedRobotInfoState.Robot_info, objp, weapon_id_type::HELIX_ID, player_gun_number::center, -spreadr, -spreadu, weapon_sound_flag::silent, shot_orientation, object_none);
			Laser_player_fire_spread(LevelSharedRobotInfoState.Robot_info, objp, weapon_id_type::HELIX_ID, player_gun_number::center,  spreadr*2,  spreadu*2, weapon_sound_flag::silent, shot_orientation, object_none);
			Laser_player_fire_spread(LevelSharedRobotInfoState.Robot_info, objp, weapon_id_type::HELIX_ID, player_gun_number::center, -spreadr*2, -spreadu*2, weapon_sound_flag::silent, shot_orientation, object_none);
			break;
		}

		case primary_weapon_index_t::PHOENIX_INDEX:
			if (Laser_player_fire(LevelSharedRobotInfoState.Robot_info, objp, weapon_id_type::PHOENIX_ID, player_gun_number::_0, weapon_sound_flag::audible, shot_orientation, object_none) == object_none)
				return 0;
			Laser_player_fire(LevelSharedRobotInfoState.Robot_info, objp, weapon_id_type::PHOENIX_ID, player_gun_number::_1, weapon_sound_flag::silent, shot_orientation, object_none);
			break;

		case primary_weapon_index_t::OMEGA_INDEX:
			if (Laser_player_fire(LevelSharedRobotInfoState.Robot_info, objp, weapon_id_type::OMEGA_ID, player_gun_number::_1, weapon_sound_flag::audible, shot_orientation, Network_laser_track) == object_none)
				return 0;
			break;

		case primary_weapon_index_t::SUPER_LASER_INDEX:
#endif
		default:
			Int3();	//	Contact Yuan: Unknown Primary weapon type, setting to 0.
			return 0;
	}

	// Set values to be recognized during comunication phase, if we are the
	//  one shooting
	if ((Game_mode & GM_MULTI) && objp == get_local_player().objnum)
		multi_send_fire(objp->orient, underlying_value(weapon_num), level, flags, Network_laser_track, object_none);
	return 1;
}

}

namespace dcx {

uint_fast8_t laser_info::test_set_hitobj(const vcobjidx_t o)
{
	if (const auto r{test_hitobj(o)})
		return r;
	const std::size_t values_size{hitobj_values.size()};
	assert(hitobj_count <= values_size);
	assert(hitobj_pos < values_size);
	hitobj_values[hitobj_pos++] = o;
	if (unlikely(hitobj_pos == values_size))
		hitobj_pos = 0;
	if (likely(hitobj_count != values_size))
		++ hitobj_count;
	return false;
}

uint_fast8_t laser_info::test_hitobj(const vcobjidx_t o) const
{
	/* Search backward so that the highest added element is the first
	 * one considered.  This preserves most of the benefit of tracking
	 * the most recent hit separately, without storing it separately or
	 * requiring expensive shift operations as new elements are added.
	 *
	 * This efficiency hack becomes ineffective (but not incorrect) if
	 * the list wraps.  Wrapping should be very rare, and even a full
	 * search is relatively cheap, so it is not worth complicating the
	 * code to ensure that elements are always searched in
	 * most-recently-added order.
	 */
	const auto &&r = partial_range(hitobj_values, hitobj_count).reversed();
	const auto &&e = r.end();
	return std::find(r.begin(), e, o) != e;
}

}

namespace dsx {
namespace {

//	-------------------------------------------------------------------------------------------
//	if goal_obj == -1, then create random vector
static imobjptridx_t create_homing_missile(fvmsegptridx &vmsegptridx, const vmobjptridx_t objp, const imobjptridx_t goal_obj, weapon_id_type objtype, const weapon_sound_flag make_sound)
{
	const auto &&objnum = Laser_create_new(build_homing_weapon_initial_vector(objp, static_cast<const object *>(goal_obj)), objp->pos, vmsegptridx(objp->segnum), objp, objtype, make_sound);
	if (objnum != object_none)
	{
	// Fixed to make sure the right person gets credit for the kill

//	Objects[objnum].ctype.laser_info.parent_num = objp->ctype.laser_info.parent_num;
//	Objects[objnum].ctype.laser_info.parent_type = objp->ctype.laser_info.parent_type;
//	Objects[objnum].ctype.laser_info.parent_signature = objp->ctype.laser_info.parent_signature;
	objnum->ctype.laser_info.track_goal = goal_obj;
	}
	return objnum;
}

struct miniparent
{
	short type;
	objnum_t num;
};

//-----------------------------------------------------------------------------
// Create the children of a smart bomb, which is a bunch of homing missiles.
static void create_smart_children(object_array &Objects, const vmobjptridx_t objp, const uint_fast32_t num_smart_children, const miniparent parent)
{
	auto &vcobjptridx = Objects.vcptridx;
	auto &vcobjptr = Objects.vcptr;
	unsigned numobjs{0};
	weapon_id_type blob_id;

	std::array<objnum_t, MAX_OBJDISTS> objlist;
#if DXX_BUILD_DESCENT == 2
	auto &Robot_info = LevelSharedRobotInfoState.Robot_info;
#endif
	{
		if (Game_mode & GM_MULTI)
			d_srand(8321L);

		range_for (const auto &&curobjp, vcobjptridx)
		{
			if (((curobjp->type == OBJ_ROBOT && !curobjp->ctype.ai_info.CLOAKED) || curobjp->type == OBJ_PLAYER) && curobjp != parent.num)
			{
				if (curobjp->type == OBJ_PLAYER)
				{
					if (parent.type == OBJ_PLAYER && (Game_mode & GM_MULTI_COOP))
						continue;
					if ((Game_mode & GM_TEAM) && multi_get_team_from_player(Netgame, get_player_id(curobjp)) == multi_get_team_from_player(Netgame, get_player_id(vcobjptr(parent.num))))
						continue;
					if (curobjp->ctype.player_info.powerup_flags & PLAYER_FLAGS_CLOAKED)
						continue;
				}

				//	Robot blobs can't track robots.
				if (curobjp->type == OBJ_ROBOT) {
					if (parent.type == OBJ_ROBOT)
						continue;

#if DXX_BUILD_DESCENT == 2
					//	Your shots won't track the buddy.
					if (parent.type == OBJ_PLAYER)
						if (Robot_info[get_robot_id(curobjp)].companion)
							continue;
#endif
				}

				const auto &&dist = vm_vec_dist2(objp->pos, curobjp->pos);
				if (dist < MAX_SMART_DISTANCE_SQUARED)
				{
					if (object_to_object_visibility(objp, curobjp, FQ_TRANSWALL))
					{
						objlist[numobjs] = curobjp;
						numobjs++;
						if (numobjs >= MAX_OBJDISTS) {
							numobjs = MAX_OBJDISTS;
							break;
						}
					}
				}
			}
		}

		//	Get type of weapon for child from parent.
#if DXX_BUILD_DESCENT == 1
		if (parent.type == OBJ_PLAYER)
		{
			blob_id = weapon_id_type::PLAYER_SMART_HOMING_ID;
		} else {
			blob_id = ((N_weapon_types<weapon_id_type::ROBOT_SMART_HOMING_ID)?(weapon_id_type::PLAYER_SMART_HOMING_ID):(weapon_id_type::ROBOT_SMART_HOMING_ID)); // NOTE: Shareware & reg 1.0 do not have their own Smart structure for bots. It was introduced in 1.4 to make Smart blobs from lvl 7 boss easier to dodge. So if we do not have this type, revert to player's Smart behaviour..,
		}
#elif DXX_BUILD_DESCENT == 2
		if (objp->type == OBJ_WEAPON) {
			blob_id = Weapon_info[get_weapon_id(objp)].children;
			Assert(blob_id != weapon_none);		//	Hmm, missing data in bitmaps.tbl.  Need "children=NN" parameter.
		} else {
			Assert(objp->type == OBJ_ROBOT);
			blob_id = weapon_id_type::ROBOT_SMART_HOMING_ID;
		}
#endif

		objnum_t last_sel_objnum{object_none};
		const auto get_random_different_object = [&]{
			for (;;)
			{
				const auto r{objlist[(d_rand() * numobjs) >> 15]};
				if (last_sel_objnum != r)
					return last_sel_objnum = r;
			}
		};
		for (auto i = num_smart_children; i--;)
		{
			const auto &&sel_objnum = numobjs
				? Objects.imptridx((numobjs == 1)
					? objlist[0]
					: get_random_different_object())
				: object_none;
			create_homing_missile(vmsegptridx, objp, sel_objnum, blob_id, (i==0) ? weapon_sound_flag::audible : weapon_sound_flag::silent);
		}
	}
}

}

void create_weapon_smart_children(const vmobjptridx_t objp)
{
	auto &Objects = LevelUniqueObjectState.Objects;
	const auto wid{get_weapon_id(objp)};
#if DXX_BUILD_DESCENT == 1
	if (wid != weapon_id_type::SMART_ID)
#elif DXX_BUILD_DESCENT == 2
	if (Weapon_info[wid].children == weapon_id_type::unspecified)
#endif
		return;
#if DXX_BUILD_DESCENT == 2
	if (wid == weapon_id_type::EARTHSHAKER_ID)
		blast_nearby_glass(objp, Weapon_info[weapon_id_type::EARTHSHAKER_ID].strength[GameUniqueState.Difficulty_level]);
#endif
	create_smart_children(Objects, objp, NUM_SMART_CHILDREN, {objp->ctype.laser_info.parent_type, objp->ctype.laser_info.parent_num});
}

#if DXX_BUILD_DESCENT == 2

void create_robot_smart_children(const vmobjptridx_t objp, const uint_fast32_t num_smart_children)
{
	auto &Objects = LevelUniqueObjectState.Objects;
	create_smart_children(Objects, objp, num_smart_children, {OBJ_ROBOT, objp});
}

//give up control of the guided missile
void release_local_guided_missile(d_level_unique_object_state &LevelUniqueObjectState, const playernum_t player_num, object &missile)
{
	Missile_viewer = &missile;
	if (Game_mode & GM_MULTI)
		multi_send_guided_info(missile, 1);
	if (Newdemo_state == ND_STATE_RECORDING)
		newdemo_record_guided_end();
	LevelUniqueObjectState.Guided_missile.clear_player_active_guided_missile(player_num);
}

void release_remote_guided_missile(d_level_unique_object_state &LevelUniqueObjectState, const playernum_t player_num)
{
	LevelUniqueObjectState.Guided_missile.clear_player_active_guided_missile(player_num);
}
#endif

//	-------------------------------------------------------------------------------------------
//changed on 31/3/10 by kreatordxx to distinguish between drop bomb and secondary fire
void do_missile_firing(const secondary_weapon_index_t weapon, const vmobjptridx_t plrobjidx)
{
	int gun_flag{0};
	fix fire_frame_overhead{0};

	auto &plrobj = *plrobjidx;
	auto &player_info = plrobj.ctype.player_info;
	auto &Next_missile_fire_time = player_info.Next_missile_fire_time;
	if (GameTime64 - Next_missile_fire_time <= FrameTime) // if firing is prolonged by FrameTime overhead, let's try to fix that.
		fire_frame_overhead = GameTime64 - Next_missile_fire_time;

#if DXX_BUILD_DESCENT == 2
	const auto &&gimobj = LevelUniqueObjectState.Guided_missile.get_player_active_guided_missile(LevelUniqueObjectState.get_objects().vmptr, Player_num);
	if (gimobj != nullptr)
	{
		release_local_guided_missile(LevelUniqueObjectState, Player_num, *gimobj);
		Next_missile_fire_time = GameTime64 + Weapon_info[Secondary_weapon_to_weapon_info[weapon]].fire_wait - fire_frame_overhead;
		return;
	}
#endif

	if (Player_dead_state != player_dead_state::no)
		return;
	if (auto &secondary_weapon_ammo = player_info.secondary_ammo[weapon])
	{
		const auto weapon_index{Secondary_weapon_to_weapon_info[weapon]};
		const auto base_weapon_gun{Secondary_weapon_to_gun_num[weapon]};
		auto &Missile_gun = plrobj.ctype.player_info.missile_gun;
		const auto weapon_gun{(base_weapon_gun == player_gun_number::_4)
			? static_cast<player_gun_number>(static_cast<uint8_t>(base_weapon_gun) + (gun_flag = (Missile_gun & 1)))
			: base_weapon_gun
		};
		const auto &&objnum = Laser_player_fire(LevelSharedRobotInfoState.Robot_info, plrobjidx, weapon_index, weapon_gun, weapon_sound_flag::audible, plrobj.orient.fvec, object_none);
		if (objnum == object_none)
			/* If the missile was not created, return early.  Do not charge for
			 * it, and do not report it to other players.
			 */
			return;
		/* Toggle between the left and right missile guns.
		 */
		if (base_weapon_gun == player_gun_number::_4)
			Missile_gun++;
		if (!cheats.rapidfire)
			Next_missile_fire_time = GameTime64 + Weapon_info[weapon_index].fire_wait - fire_frame_overhead;
		else
			Next_missile_fire_time = GameTime64 + (F1_0/25) - fire_frame_overhead;

		-- secondary_weapon_ammo;
		if (weapon != secondary_weapon_index_t::CONCUSSION_INDEX)
			maybe_drop_net_powerup(Secondary_weapon_to_powerup[weapon], 1, 0);

#if DXX_BUILD_DESCENT == 1
		if (weapon == secondary_weapon_index_t::MEGA_INDEX)
#elif DXX_BUILD_DESCENT == 2
		if (weapon == secondary_weapon_index_t::MEGA_INDEX || weapon == secondary_weapon_index_t::SMISSILE5_INDEX)
#endif
		{
			const vms_vector backward_vec{
				-(plrobj.orient.fvec.x << 7),
				-(plrobj.orient.fvec.y << 7),
				-(plrobj.orient.fvec.z << 7)
			};
			phys_apply_force(plrobj, backward_vec);

			const vms_vector rotation_vec{
				(backward_vec.x >> 4) + d_rand() - 16384,
				(backward_vec.y >> 4) + d_rand() - 16384,
				(backward_vec.z >> 4) + d_rand() - 16384
			};
			phys_apply_rot(plrobj, rotation_vec);
		}

		if (Game_mode & GM_MULTI)
		{
			const object &obj = *objnum;
			multi_send_fire(plrobj.orient, underlying_value(weapon) + MISSILE_ADJUST, laser_level::_1	/* unused */, gun_flag, obj.ctype.laser_info.track_goal, weapon_index_is_player_bomb(weapon) ? objnum : object_none);
		}

		// don't autoselect if dropping prox and prox not current weapon
		if (player_info.Secondary_weapon == weapon)
			auto_select_secondary_weapon(player_info);		//select next missile, if this one out of ammo
	}
}
}

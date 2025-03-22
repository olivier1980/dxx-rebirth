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
 * Code for handling robots
 *
 */


#include "dxxerror.h"
#include "inferno.h"
#include "robot.h"
#include "ai.h"
#include "object.h"
#include "polyobj.h"
#include "physfsx.h"
#include "d_levelstate.h"
#include "segment.h"

#include "d_enumerate.h"
#include "compiler-range_for.h"
#include "partial_range.h"

#include <iostream>

#if 0
static inline void PHYSFSX_writeAngleVec(PHYSFS_File *fp, const vms_angvec &v)
{
	PHYSFS_writeSLE16(fp, v.p);
	PHYSFS_writeSLE16(fp, v.b);
	PHYSFS_writeSLE16(fp, v.h);
}
#endif

namespace dsx {

//given an object and a gun number, return position in 3-space of gun
//fills in gun_point
void calc_gun_point(const robot_info &r, vms_vector &gun_point, const object_base &obj, const robot_gun_number entry_gun_num)
{
	assert(obj.render_type == render_type::RT_POLYOBJ || obj.render_type == render_type::RT_MORPH);
	assert(underlying_value(get_robot_id(obj)) < LevelSharedRobotInfoState.N_robot_types);

	auto &Polygon_models = LevelSharedPolygonModelState.Polygon_models;
	const auto &pm = Polygon_models[r.model_num];

	const auto gun_num = (underlying_value(entry_gun_num) >= r.n_guns)
		? robot_gun_number::_0
		: entry_gun_num;

	auto pnt = r.gun_points[gun_num];

	//instance up the tree for this gun
	auto &anim_angles = obj.rtype.pobj_info.anim_angles;
	for (unsigned mn = r.gun_submodels[gun_num]; mn != 0; mn = pm.submodel_parents[mn])
	{
		const auto &&m = vm_transposed_matrix(vm_angles_2_matrix(anim_angles[mn]));
		const auto tpnt = vm_vec_rotate(pnt,m);

		vm_vec_add(pnt, tpnt, pm.submodel_offsets[mn]);
	}

	//now instance for the entire object

	const auto &&m = vm_transposed_matrix(obj.orient);
	vm_vec_rotate(gun_point,pnt,m);
	vm_vec_add2(gun_point, obj.pos);
}

//fills in ptr to list of joints, and returns the number of joints in list
//takes the robot type (object id), gun number, and desired state
std::ranges::subrange<const jointpos *> robot_get_anim_state(const d_robot_info_array &robot_info, const std::array<jointpos, MAX_ROBOT_JOINTS> &robot_joints, const robot_id robot_type, const robot_gun_animation_index gun_num, const robot_animation_state state)
{
	auto &rirt = robot_info[robot_type];
	auto &as = rirt.anim_states[gun_num][state];
	const unsigned o = as.offset;
	return partial_range(robot_joints, o, o + as.n_joints);
}

#ifndef NDEBUG
//for test, set a robot to a specific state
dxx_compiler_attribute_used
static void set_robot_state(object_base &obj, const robot_animation_state state)
{
	auto &Robot_joints = LevelSharedRobotJointState.Robot_joints;
	int j,jo;

	assert(obj.type == OBJ_ROBOT);

	auto &Robot_info = LevelSharedRobotInfoState.Robot_info;
	auto &ri = Robot_info[get_robot_id(obj)];

	for (auto &as : partial_range(ri.anim_states, std::size_t{ri.n_guns} + 1u))
	{
		const auto jl = &as[state];
		jo = jl->offset;

		for (j=0;j<jl->n_joints;j++,jo++) {
			int jn;

			jn = Robot_joints[jo].jointnum;

			obj.rtype.pobj_info.anim_angles[jn] = Robot_joints[jo].angles;
		}
	}
}
#endif

//set the animation angles for this robot.  Gun fields of robot info must
//be filled in.
void robot_set_angles(robot_info &r, polymodel &pm, enumerated_array<std::array<vms_angvec, MAX_SUBMODELS>, N_ANIM_STATES, robot_animation_state> &angs)
{
	auto &Robot_joints = LevelSharedRobotJointState.Robot_joints;
	std::array<robot_gun_animation_index, MAX_SUBMODELS> gun_nums;			//which gun each submodel is part of
	gun_nums[0] = robot_gun_animation_index::None;		//body never animates, at least for now
	{
		auto &&gr = partial_range(gun_nums, 1u, pm.n_models);
		//assume part of body...
		std::fill(gr.begin(), gr.end(), robot_gun_animation_index{r.n_guns});
	}

	for (const std::size_t bound{std::min(gun_nums.size(), pm.submodel_parents.size())}; auto [g, entry_m] : enumerate(partial_const_range(r.gun_submodels, r.n_guns)))
	{
		auto m = entry_m;
		while (m != 0 && m < bound)
		{
			/* This cast is safe: `g` is `robot_gun_number` with a valid range of [0, MAX_GUNS - 1].
			 * `robot_gun_animation_index` has a valid range of [0, MAX_GUNS].
			 * Therefore, any valid `robot_gun_number` is a valid
			 * `robot_gun_animation_index`.
			 */
			gun_nums[m] = static_cast<robot_gun_animation_index>(g);				//...unless we find it in a gun
			m = pm.submodel_parents[m];
		}
	}

	const auto n_models = pm.n_models;
	const auto &&gun_num_model_range = enumerate(partial_range(gun_nums, n_models));
	for (auto &&[g, ras] : enumerate(partial_range(r.anim_states, std::size_t{r.n_guns} + 1u)))
	{
		for (auto &&[state, as] : enumerate(ras))
		{
			as.n_joints = 0;
			as.offset = LevelSharedRobotJointState.N_robot_joints;

			for (auto &&[m, gn] : gun_num_model_range)
			{
				if (gn == g)
				{
					const auto N_robot_joints = LevelSharedRobotJointState.N_robot_joints ++;
					Robot_joints[N_robot_joints].jointnum = m;
					Robot_joints[N_robot_joints].angles = angs[state][m];
					ras[state].n_joints++;
					Assert(N_robot_joints < MAX_ROBOT_JOINTS);
				}
			}
		}
	}

}

}

namespace {

/*
 * reads n jointlist structs from a PHYSFS_File
 */
static void jointlist_read(const NamedPHYSFS_File fp, std::array<jointlist, N_ANIM_STATES> &jl)
{
	range_for (auto &i, jl)
	{
		i.n_joints = PHYSFSX_readSLE16(fp);
		i.offset = PHYSFSX_readSLE16(fp);
		if (!i.n_joints)
			/* The custom campaign `Descent 2: Enemy Vignettes` has
			 * custom robots with invalid joints.  These joints have
			 * invalid offsets, but `n_joints` of 0.  This makes the
			 * invalid data easy to detect and clean.
			 *
			 * When the number of joints is zero, discard the loaded
			 * offset and set it to 0, which will always be in range.
			 */
			i.offset = 0;
	}
}

}

namespace dsx {

imobjptridx_t robot_create(const d_robot_info_array &Robot_info, const robot_id id, const vmsegptridx_t segnum, const vms_vector &pos, const vms_matrix *const orient, const fix size, const ai_behavior behavior, const imsegidx_t hide_segment)
{
	const auto &&objp = obj_create(LevelUniqueObjectState, LevelSharedSegmentState, LevelUniqueSegmentState, OBJ_ROBOT, underlying_value(id), segnum, pos, orient, size, object::control_type::ai, object::movement_type::physics, render_type::RT_POLYOBJ);
	if (objp)
		init_ai_object(Robot_info, objp, behavior, hide_segment);
	return objp;
}

/*
 * reads n robot_info structs from a PHYSFS_File
 */
void robot_info_read(const NamedPHYSFS_File fp, robot_info &ri)
{
	ri.model_num = build_polygon_model_index_from_untrusted(PHYSFSX_readInt(fp));
#if DXX_BUILD_DESCENT == 1
	ri.n_guns = PHYSFSX_readInt(fp);
#endif
	range_for (auto &j, ri.gun_points)
		PHYSFSX_readVector(fp, j);
	range_for (auto &j, ri.gun_submodels)
		j = PHYSFSX_readByte(fp);

	ri.exp1_vclip_num = build_vclip_index_from_untrusted(PHYSFSX_readSLE16(fp));
	ri.exp1_sound_num = build_sound_effect_from_untrusted(PHYSFSX_readSLE16(fp));

	ri.exp2_vclip_num = build_vclip_index_from_untrusted(PHYSFSX_readSLE16(fp));
	ri.exp2_sound_num = build_sound_effect_from_untrusted(PHYSFSX_readSLE16(fp));

#if DXX_BUILD_DESCENT == 1
	ri.weapon_type = static_cast<weapon_id_type>(PHYSFSX_readSLE16(fp));
#elif DXX_BUILD_DESCENT == 2
	ri.weapon_type = static_cast<weapon_id_type>(PHYSFSX_readByte(fp));
	ri.weapon_type2 = static_cast<weapon_id_type>(PHYSFSX_readByte(fp));
	ri.n_guns = PHYSFSX_readByte(fp);
#endif
	const uint8_t contains_id = PHYSFSX_readByte(fp);
	const uint8_t contains_count = PHYSFSX_readByte(fp);
	ri.contains_prob = PHYSFSX_readByte(fp);
	const auto contains_type = PHYSFSX_readByte(fp);
	ri.contains = build_contained_object_parameters_from_untrusted(contains_type, contains_id, contains_count);
#if DXX_BUILD_DESCENT == 1
	ri.score_value = PHYSFSX_readInt(fp);
#elif DXX_BUILD_DESCENT == 2
	ri.kamikaze = PHYSFSX_readByte(fp);

	ri.score_value = PHYSFSX_readSLE16(fp);
	ri.badass = PHYSFSX_readByte(fp);
	ri.energy_drain = PHYSFSX_readByte(fp);
#endif

	ri.lighting = PHYSFSX_readFix(fp);
	ri.strength = PHYSFSX_readFix(fp);

	ri.mass = PHYSFSX_readFix(fp);
	ri.drag = PHYSFSX_readFix(fp);

	range_for (auto &j, ri.field_of_view)
		j = PHYSFSX_readFix(fp);
	range_for (auto &j, ri.firing_wait)
		j = PHYSFSX_readFix(fp);
#if DXX_BUILD_DESCENT == 2
	range_for (auto &j, ri.firing_wait2)
		j = PHYSFSX_readFix(fp);
#endif
	range_for (auto &j, ri.turn_time)
		j = PHYSFSX_readFix(fp);
#if DXX_BUILD_DESCENT == 1
	PHYSFSX_skipBytes<4 * (NDL * 2)>(fp);
#endif
	range_for (auto &j, ri.max_speed)
		j = PHYSFSX_readFix(fp);
	range_for (auto &j, ri.circle_distance)
		j = PHYSFSX_readFix(fp);
	range_for (auto &j, ri.rapidfire_count)
		j = PHYSFSX_readByte(fp);
	range_for (auto &j, ri.evade_speed)
		j = PHYSFSX_readByte(fp);

	sbyte cloakType = PHYSFSX_readByte(fp);

	std::cout << "Cloak type: "+std::to_string(cloakType) << std::endl;

    // OLI: set all enemies cloaked
	//ri.cloak_type = RI_CLOAKED_ALWAYS;
	ri.cloak_type = cloakType;
	ri.attack_type = PHYSFSX_readByte(fp);
#if DXX_BUILD_DESCENT == 1
	const uint8_t boss_flag = PHYSFSX_readByte(fp);
	ri.boss_flag = (boss_flag == static_cast<uint8_t>(boss_robot_id::d1_1) || boss_flag == static_cast<uint8_t>(boss_robot_id::d1_superboss)) ? boss_robot_id{boss_flag} : boss_robot_id::None;
#endif

	ri.see_sound = build_sound_effect_from_untrusted(PHYSFSX_readByte(fp));
	ri.attack_sound = build_sound_effect_from_untrusted(PHYSFSX_readByte(fp));
	ri.claw_sound = build_sound_effect_from_untrusted(PHYSFSX_readByte(fp));
#if DXX_BUILD_DESCENT == 2
	ri.taunt_sound = build_sound_effect_from_untrusted(PHYSFSX_readByte(fp));

	const uint8_t boss_flag = PHYSFSX_readByte(fp);
	ri.boss_flag = (boss_flag == static_cast<uint8_t>(boss_robot_id::d1_1) || boss_flag == static_cast<uint8_t>(boss_robot_id::d1_superboss) || Boss_spew_more.valid_index(build_boss_robot_index_from_boss_robot_id(boss_robot_id{boss_flag}))) ? boss_robot_id{boss_flag} : boss_robot_id::None;
	ri.companion = PHYSFSX_readByte(fp);
	ri.smart_blobs = PHYSFSX_readByte(fp);
	ri.energy_blobs = PHYSFSX_readByte(fp);

	ri.thief = PHYSFSX_readByte(fp);
	ri.pursuit = PHYSFSX_readByte(fp);
	ri.lightcast = PHYSFSX_readByte(fp);
	ri.death_roll = PHYSFSX_readByte(fp);

	ri.flags = PHYSFSX_readByte(fp);
	PHYSFSX_skipBytes<3>(fp);

	ri.deathroll_sound = build_sound_effect_from_untrusted(PHYSFSX_readByte(fp));
	ri.glow = PHYSFSX_readByte(fp);
	ri.behavior = static_cast<ai_behavior>(PHYSFSX_readByte(fp));
	ri.aim = PHYSFSX_readByte(fp);
#endif

	range_for (auto &j, ri.anim_states)
		jointlist_read(fp, j);

	ri.always_0xabcd = PHYSFSX_readInt(fp);
}
}

/*
 * reads n jointpos structs from a PHYSFS_File
 */
void jointpos_read(const NamedPHYSFS_File fp, jointpos &jp)
{
	jp.jointnum = PHYSFSX_readSLE16(fp);
	PHYSFSX_readAngleVec(fp, jp.angles);
}

#if 0
void jointpos_write(PHYSFS_File *fp, const jointpos &jp)
{
	PHYSFS_writeSLE16(fp, jp.jointnum);
	PHYSFSX_writeAngleVec(fp, jp.angles);
}
#endif

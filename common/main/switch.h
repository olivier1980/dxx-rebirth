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
 * Triggers and Switches.
 *
 */

#pragma once

#include <type_traits>
#include <physfs.h>
#include "maths.h"

#include "pack.h"
#include "fwd-object.h"
#include "fwd-segment.h"
#include "fwd-valptridx.h"
#include "valptridx.h"
#include "dsx-ns.h"
#include "fwd-player.h"
#include "fwd-window.h"
#include <array>

namespace dcx {
constexpr std::integral_constant<std::size_t, 100> MAX_TRIGGERS{};
constexpr std::integral_constant<unsigned, 10> MAX_WALLS_PER_LINK{};
}

#ifdef DXX_BUILD_DESCENT
// Trigger types
enum class trigger_action : uint8_t
{
	open_door = 0,   // Open a door
	matcen = 2,   // Activate a matcen
	normal_exit = 3,   // End the level
	secret_exit = 4,   // Go to secret level
	illusion_off = 5,   // Turn an illusion off
	illusion_on = 6,   // Turn an illusion on
#if DXX_BUILD_DESCENT == 2
	close_door = 1,	// Close a door
	unlock_door = 7,	// Unlock a door
	lock_door = 8,	// Lock a door
	open_wall = 9,	// Makes a wall open
	close_wall = 10,	// Makes a wall closed
	illusory_wall = 11,	// Makes a wall illusory
	light_off = 12,	// Turn a light off
	light_on = 13,	// Turn a light on
#endif
};

#if DXX_BUILD_DESCENT == 2
#define NUM_TRIGGER_TYPES   14

// Trigger flags

//could also use flags for one-shots

enum class trigger_behavior_flags : uint8_t
{
	no_message = 1,	// Don't show a message when triggered
	one_shot = 2,	// Only trigger once
	disabled = 4,	// Set after one-shot fires
};

enum class trigger_behavior_flag_mask : uint8_t;

static constexpr trigger_behavior_flag_mask operator~(const trigger_behavior_flags value)
{
	return static_cast<trigger_behavior_flag_mask>(~static_cast<uint8_t>(value));
}

static constexpr uint8_t operator&(const trigger_behavior_flags value, const trigger_behavior_flags mask)
{
	return static_cast<uint8_t>(value) & static_cast<uint8_t>(mask);
}

static constexpr trigger_behavior_flags &operator|=(trigger_behavior_flags &value, const trigger_behavior_flags mask)
{
	value = static_cast<trigger_behavior_flags>(static_cast<uint8_t>(value) | static_cast<uint8_t>(mask));
	return value;
}

static constexpr trigger_behavior_flags &operator&=(trigger_behavior_flags &value, const trigger_behavior_flag_mask mask)
{
	value = static_cast<trigger_behavior_flags>(static_cast<uint8_t>(value) & static_cast<uint8_t>(mask));
	return value;
}

//old trigger structs

struct v29_trigger : prohibit_void_ptr<v29_trigger>
{
	sbyte   type;
	short   flags;
	fix     value;
	sbyte   link_num;
	short   num_links;
	std::array<segnum_t, MAX_WALLS_PER_LINK>   seg;
	std::array<sidenum_t, MAX_WALLS_PER_LINK>  side;
};

struct v30_trigger : prohibit_void_ptr<v30_trigger>
{
	short   flags;
	sbyte   num_links;
	sbyte   pad;                        //keep alignment
	fix     value;
	std::array<segnum_t, MAX_WALLS_PER_LINK>   seg;
	std::array<sidenum_t, MAX_WALLS_PER_LINK>   side;
};
#endif

enum TRIGGER_FLAG : uint16_t
{
//flags for V30 & below triggers
	CONTROL_DOORS =      1,    // Control Trigger
	SHIELD_DAMAGE =      2,    // Shield Damage Trigger
	ENERGY_DRAIN =       4,    // Energy Drain Trigger
	EXIT =               8,    // End of level Trigger
	ONE_SHOT =          32,    // If Trigger can only be triggered once
	MATCEN =            64,    // Trigger for materialization centers
	ILLUSION_OFF =     128,    // Switch Illusion OFF trigger
	SECRET_EXIT =      256,    // Exit to secret level
	ILLUSION_ON =      512,    // Switch Illusion ON trigger
#if DXX_BUILD_DESCENT == 2
	UNLOCK_DOORS =    1024,    // Unlocks a door
	OPEN_WALL =       2048,    // Makes a wall open
	CLOSE_WALL =      4096,    // Makes a wall closed
	ILLUSORY_WALL =   8192,    // Makes a wall illusory
#endif
};

#define TRIGGER_CONTROL_DOORS TRIGGER_FLAG::CONTROL_DOORS
#define TRIGGER_SHIELD_DAMAGE TRIGGER_FLAG::SHIELD_DAMAGE
#define TRIGGER_ENERGY_DRAIN TRIGGER_FLAG::ENERGY_DRAIN
#define TRIGGER_EXIT TRIGGER_FLAG::EXIT
#define TRIGGER_ONE_SHOT TRIGGER_FLAG::ONE_SHOT
#define TRIGGER_MATCEN TRIGGER_FLAG::MATCEN
#define TRIGGER_ILLUSION_OFF TRIGGER_FLAG::ILLUSION_OFF
#define TRIGGER_SECRET_EXIT TRIGGER_FLAG::SECRET_EXIT
#define TRIGGER_ILLUSION_ON TRIGGER_FLAG::ILLUSION_ON
#if DXX_BUILD_DESCENT == 2
#define TRIGGER_UNLOCK_DOORS TRIGGER_FLAG::UNLOCK_DOORS
#define TRIGGER_OPEN_WALL TRIGGER_FLAG::OPEN_WALL
#define TRIGGER_CLOSE_WALL TRIGGER_FLAG::CLOSE_WALL
#define TRIGGER_ILLUSORY_WALL TRIGGER_FLAG::ILLUSORY_WALL
#endif

//the trigger really should have both a type & a flags, since most of the
//flags bits are exclusive of the others.
enum class trgnum_t : uint8_t
{
	None = UINT8_MAX
};

namespace dsx {

struct trigger : public prohibit_void_ptr<trigger>
{
#if DXX_BUILD_DESCENT == 1
	uint16_t flags;
#elif DXX_BUILD_DESCENT == 2
	trigger_action type;       //what this trigger does
	trigger_behavior_flags flags;
#endif
	uint8_t   num_links;  //how many doors, etc. linked to this
	fix     value;
	std::array<segnum_t, MAX_WALLS_PER_LINK>   seg;
	std::array<sidenum_t, MAX_WALLS_PER_LINK>  side;
};

}
DXX_VALPTRIDX_DECLARE_SUBTYPE(dsx::, trigger, trgnum_t, MAX_TRIGGERS);
namespace dsx {
DXX_VALPTRIDX_DEFINE_SUBTYPE_TYPEDEFS(trigger, trg);

struct d_level_unique_trigger_state
{
	valptridx<trigger>::array_managed_type Triggers;
};
}

constexpr std::integral_constant<trgnum_t, trgnum_t::None> trigger_none{};

namespace dsx {
window_event_result check_trigger(vcsegptridx_t seg, sidenum_t side, object &plrobj, vcobjptridx_t objnum, int shot);
window_event_result check_trigger_sub(object &, trgnum_t trigger_num, playernum_t player_num, unsigned shot);

static inline int trigger_is_exit(const trigger *t)
{
#if DXX_BUILD_DESCENT == 1
	return t->flags & TRIGGER_EXIT;
#elif DXX_BUILD_DESCENT == 2
	return t->type == trigger_action::normal_exit;
#endif
}

static inline int trigger_is_matcen(const trigger &t)
{
#if DXX_BUILD_DESCENT == 1
	return t.flags & TRIGGER_MATCEN;
#elif DXX_BUILD_DESCENT == 2
	return t.type == trigger_action::matcen;
#endif
}

#if DXX_BUILD_DESCENT == 1
void v25_trigger_read(NamedPHYSFS_File fp, trigger *);
void v26_trigger_read(NamedPHYSFS_File fp, trigger &);
#endif

#if DXX_BUILD_DESCENT == 2
/*
 * reads a v29_trigger structure from a PHYSFS_File
 */
void v29_trigger_read(v29_trigger *t, NamedPHYSFS_File fp);

/*
 * reads a v30_trigger structure from a PHYSFS_File
 */
void v30_trigger_read(v30_trigger *t, NamedPHYSFS_File fp);

/*
 * reads a trigger structure from a PHYSFS_File
 */
void v29_trigger_read_as_v31(NamedPHYSFS_File fp, trigger &t);
void v30_trigger_read_as_v31(NamedPHYSFS_File fp, trigger &t);
#endif

void trigger_read(NamedPHYSFS_File fp, trigger &t);
void trigger_write(PHYSFS_File *fp, const trigger &t);

void v29_trigger_write(PHYSFS_File *fp, const trigger &t);
void v30_trigger_write(PHYSFS_File *fp, const trigger &t);
void v31_trigger_write(PHYSFS_File *fp, const trigger &t);
}
#endif

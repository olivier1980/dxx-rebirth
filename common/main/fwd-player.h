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

#pragma once

#include <physfs.h>
#include "fwd-object.h"

#define MAX_MULTI_PLAYERS MAX_PLAYERS+3
#define MULTI_PNUM_UNDEF 0xcc

// Initial player stat values
#define INITIAL_ENERGY  i2f(100)    // 100% energy to start
#define INITIAL_SHIELDS i2f(100)    // 100% shields to start

#define MAX_ENERGY      i2f(200)    // go up to 200
#define MAX_SHIELDS     i2f(200)

#define INITIAL_LIVES               3   // start off with 3 lives

// Values for special flags
#ifdef DXX_BUILD_DESCENT
#if DXX_BUILD_DESCENT == 2
#define PLAYER_MAX_AMMO(powerup_flags,BASE)	((powerup_flags & PLAYER_FLAGS_AMMO_RACK) ? BASE * 2 : BASE)

#define AFTERBURNER_MAX_TIME    (F1_0*5)    // Max time afterburner can be on.
#endif
#endif
#define CALLSIGN_LEN                8       // so can use as filename (was: 12)

// Amount of time player is cloaked.
#define CLOAK_TIME_MAX          (F1_0*30)
#define INVULNERABLE_TIME_MAX   (F1_0*30)

#ifdef DXX_BUILD_DESCENT
#if DXX_BUILD_DESCENT == 1
#define PLAYER_STRUCT_VERSION 	16		//increment this every time player struct changes
#define PLAYER_MAX_AMMO(powerup_flags,BASE)	(static_cast<void>(powerup_flags), BASE)
#elif DXX_BUILD_DESCENT == 2
#define PLAYER_STRUCT_VERSION   17  // increment this every time player struct changes
#endif
#endif
#include "dxxsconf.h"
#include "dsx-ns.h"
#include <array>
struct callsign_t;

#define N_PLAYER_SHIP_TEXTURES 32

namespace dcx {

enum class player_connection_status : uint8_t;
enum class player_gun_number : uint8_t;

struct player_ship;

struct player;
using playernum_t = uint32_t;
constexpr unsigned MAX_PLAYERS = 8;
template <typename T>
	using per_player_array = std::array<T, MAX_PLAYERS>;
using playernum_array_t = per_player_array<playernum_t>;
#if DXX_USE_MULTIPLAYER
enum class team_number : uint8_t;
template <typename T>
	using per_team_array = enumerated_array<T, 2, team_number>;
#endif

extern unsigned N_players;   // Number of players ( >1 means a net game, eh?)
extern playernum_t Player_num;  // The player number who is on the console.
}

#ifdef DXX_BUILD_DESCENT
DXX_VALPTRIDX_DECLARE_SUBTYPE(dcx::, player, playernum_t, MAX_PLAYERS);
namespace dsx {
struct player_rw;
struct player_info;
void player_rw_swap(player_rw *p, physfsx_endian swap);
int allowed_to_fire_missile(const player_info &);
#if DXX_BUILD_DESCENT == 2
fix get_omega_energy_consumption(fix delta_charge);
void omega_charge_frame(player_info &);
#endif
}
#endif

cmake_minimum_required(VERSION 3.30)
project(dxx_rebirth)

set(CMAKE_CXX_STANDARD 20)

#find_package(PHYSFS REQUIRED)
find_package(SDL2 REQUIRED)

include_directories(libs)
include_directories(common/2d)
include_directories(common/3d)
include_directories(common/arch/cocoa)
include_directories(common/include)
include_directories(common/include/imgui)
include_directories(common/include/editor)
include_directories(common/main)
include_directories(common/misc)
include_directories(common/texmap)
include_directories(d1x-rebirth/main)
include_directories(.)
#include_directories(similar/arch/win32)
#include_directories(similar/arch/win32/include)

add_executable(dxx_rebirth
        common/2d/2dsline.cpp
    common/2d/bitblt.cpp
    common/2d/bitmap.cpp
    common/2d/bitmap.h
    common/2d/box.cpp
    common/2d/canvas.cpp
    common/2d/circle.cpp
    common/2d/clip.h
    common/2d/disc.cpp
    common/2d/gpixel.cpp
    common/2d/line.cpp
    common/2d/palette.cpp
    common/2d/pixel.cpp
    common/2d/rect.cpp
    common/2d/rle.cpp
    common/2d/scalec.cpp
#    common/3d/clipper.cpp
#    common/3d/clipper.h
    common/3d/draw.cpp
    common/3d/globvars.cpp
    common/3d/globvars.h
    common/3d/instance.cpp
    common/3d/matrix.cpp
    common/3d/points.cpp
    common/3d/rod.cpp
    common/3d/setup.cpp
#    common/arch/cocoa/messagebox.mm
#    common/arch/cocoa/SDLMain.h
    common/arch/ogl/ogl_extensions.cpp
    common/arch/ogl/ogl_sync.cpp
#    common/arch/sdl/digi_mixer_music.cpp
#    common/arch/sdl/event.cpp
#    common/arch/sdl/joy.cpp
#    common/arch/sdl/jukebox.cpp
#    common/arch/sdl/key.cpp
#    common/arch/sdl/messagebox.cpp
#    common/arch/sdl/mouse.cpp
#    common/arch/sdl/rbaudio.cpp
#    common/arch/sdl/timer.cpp
#    common/arch/sdl/window.cpp
#    common/arch/win32/except.cpp
#    common/arch/win32/messagebox.cpp
#    common/arch/win32/rbaudio.cpp
    common/editor/autosave.cpp
    common/editor/func.cpp
#    common/include/editor/centers.h
    common/include/editor/editor.h
#    common/include/editor/ehostage.h
#    common/include/editor/eobject.h
#    common/include/editor/esegment.h
#    common/include/editor/eswitch.h
#    common/include/editor/info.h
#    common/include/editor/kdefs.h
#    common/include/editor/macro.h
#    common/include/editor/meddraw.h
#    common/include/editor/medlisp.h
#    common/include/editor/medmisc.h
#    common/include/editor/medrobot.h
#    common/include/editor/medsel.h
#    common/include/editor/medwall.h
#    common/include/editor/objpage.h
#    common/include/editor/seguvs.h
#    common/include/editor/texpage.h
    common/include/3d.h
    common/include/adlmidi_dynamic.h
    common/include/args.h
    common/include/byteutil.h
    common/include/compiler-cf_assert.h
    common/include/compiler-poison.h
    common/include/compiler-range_for.h
    common/include/console.h
    common/include/countarray.h
    common/include/cpp-valptridx.h
    common/include/d_gl.h
    common/include/d_underlying_value.h
    common/include/digi_audio.h
    common/include/digi_mixer.h
    common/include/digi_mixer_music.h
    common/include/dsx-ns.h
    common/include/dxxerror.h
    common/include/event.h
    common/include/fmtcheck.h
    common/include/func.h
    common/include/fwd-event.h
    common/include/fwd-gr.h
    common/include/fwd-partial_range.h
    common/include/fwd-valptridx.h
    common/include/fwd-vecmat.h
    common/include/fwd-window.h
    common/include/gr.h
    common/include/grdef.h
    common/include/hash.h
    common/include/hmp.h
    common/include/iff.h
    common/include/ignorecase.h
    common/include/internal.h
    common/include/interp.h
    common/include/joy.h
    common/include/jukebox.h
    common/include/key.h
    common/include/loadgl.h
    common/include/makesig.h
    common/include/maths.h
    common/include/messagebox.h
    common/include/mouse.h
    common/include/ntstring.h
    common/include/null_sentinel_iterator.h
    common/include/ogl_extensions.h
    common/include/ogl_init.h
    common/include/ogl_sync.h
    common/include/pack.h
    common/include/palette.h
    common/include/partial_range.h
    common/include/pcx.h
    common/include/physfs-serial.h
    common/include/physfs_list.h
    common/include/physfsx.h
    common/include/pstypes.h
    common/include/rbaudio.h
    common/include/rle.h
    common/include/serial.h
    common/include/strutil.h
    common/include/texmap.h
    common/include/timer.h
    common/include/u_mem.h
    common/include/ui.h
    common/include/valptridx.h
    common/include/varutil.h
    common/include/vecmat.h
    common/include/window.h
    common/main/ai.h
    common/main/aistruct.h
    common/main/automap.h
    common/main/backports-ranges.h
    common/main/bm.h
    common/main/cglobal.cpp
    common/main/cli.cpp
    common/main/cli.h
    common/main/cmd.cpp
    common/main/cmd.h
    common/main/cntrlcen.h
    common/main/collide.h
    common/main/config.h
    common/main/controls.h
    common/main/credits.h
    common/main/cvar.cpp
    common/main/cvar.h
    common/main/d_array.h
    common/main/d_bitset.h
    common/main/d_crange.h
    common/main/d_enumerate.h
    common/main/d_levelstate.h
    common/main/d_range.h
    common/main/d_sdl_audio.h
    common/main/d_srcloc.h
    common/main/d_uspan.h
    common/main/d_zip.h
    common/main/digi.h
    common/main/effects.h
    common/main/endlevel.h
    common/main/escort.h
    common/main/fireball.h
    common/main/fuelcen.h
    common/main/fvi.h
    common/main/fwd-d_array.h
    common/main/fwd-game.h
    common/main/fwd-inferno.h
    common/main/fwd-object.h
    common/main/fwd-piggy.h
    common/main/fwd-player.h
    common/main/fwd-powerup.h
    common/main/fwd-robot.h
    common/main/fwd-segment.h
    common/main/fwd-vclip.h
    common/main/fwd-wall.h
    common/main/fwd-weapon.h
    common/main/game.h
    common/main/gamefont.h
    common/main/gamemine.h
    common/main/gamepal.h
    common/main/gameplayopt.h
    common/main/gamesave.h
    common/main/gameseg.h
    common/main/gameseq.h
    common/main/gauges.h
    common/main/hostage.h
    common/main/hudmsg.h
    common/main/inferno.h
    common/main/kconfig.h
    common/main/kmatrix.h
    common/main/laser.h
    common/main/lighting.h
    common/main/menu.h
    common/main/mission.h
    common/main/morph.h
    common/main/multi.h
    common/main/multibot.h
    common/main/multiinternal.h
    common/main/net_udp.h
    common/main/newdemo.h
    common/main/newmenu.h
    common/main/nvparse.h
    common/main/object.h
    common/main/objnum.h
    common/main/paging.h
    common/main/physfsrwops.h
    common/main/physics.h
    common/main/physics_info.h
    common/main/piggy.cpp
    common/main/piggy.h
    common/main/player-callsign.h
    common/main/player-flags.h
    common/main/player.h
    common/main/playsave.h
    common/main/polyobj.h
    common/main/powerup.h
    common/main/render.h
    common/main/render_state.h
    common/main/robot.h
    common/main/scores.h
    common/main/screens.h
    common/main/segiter.h
    common/main/segment.h
    common/main/segpoint.h
    common/main/slew.h
    common/main/songs.h
    common/main/sounds.h
    common/main/state.h
    common/main/switch.h
    common/main/terrain.h
    common/main/texmerge.h
    common/main/text.h
    common/main/textures.h
    common/main/titles.h
    common/main/valptridx.tcc
    common/main/vclip.h
    common/main/vers_id.h
    common/main/wall.h
    common/main/weapon.h
    common/main/weapon_id.h
    common/maths/fixc.cpp
    common/maths/rand.cpp
    common/maths/tables.cpp
    common/maths/vecmat.cpp
    common/mem/mem.cpp
    common/misc/error.cpp
    common/misc/hash.cpp
    common/misc/hmp.cpp
    common/misc/ignorecase.cpp
    common/misc/physfsrwops.cpp
    common/misc/physfsx.cpp
    common/misc/strutil.cpp
    common/misc/vg-wrap-physfs.h
    common/misc/vgrphys.cpp
    common/misc/vgwphys.cpp
#    common/music/adlmidi_dynamic.cpp
#    common/texmap/ntmap.cpp
#    common/texmap/scanline.cpp
#    common/texmap/scanline.h
#    common/texmap/texmapl.h
    common/ui/button.cpp
    common/ui/checkbox.cpp
    common/ui/dialog.cpp
    common/ui/file.cpp
    common/ui/gadget.cpp
    common/ui/icon.cpp
    common/ui/inputbox.cpp
    common/ui/keypad.cpp
    common/ui/keypress.cpp
    common/ui/listbox.cpp
    common/ui/menu.cpp
    common/ui/menubar.cpp
    common/ui/message.cpp
    common/ui/radio.cpp
    common/ui/scroll.cpp
    common/ui/ui.cpp
    common/ui/uidraw.cpp
    common/ui/userbox.cpp
#    common/unittest/enumerate.cpp
#    common/unittest/partial_range.cpp
#    common/unittest/serial.cpp
#    common/unittest/valptridx-range.cpp
#    common/unittest/vecmat.cpp
#    common/unittest/xrange.cpp
#    common/unittest/zip.cpp
    d1x-rebirth/editor/ehostage.cpp
    d1x-rebirth/main/custom.cpp
    d1x-rebirth/main/custom.h
    d1x-rebirth/main/snddecom.cpp
    d1x-rebirth/main/snddecom.h
    d1x-rebirth/utilities/extractD1Data.cpp
    similar/2d/font.cpp
    similar/2d/palette.cpp
    similar/2d/pcx.cpp
    similar/3d/interp.cpp
    similar/arch/ogl/gr.cpp
    similar/arch/ogl/ogl.cpp
    similar/arch/sdl/digi.cpp
    similar/arch/sdl/digi_audio.cpp
    similar/arch/sdl/digi_mixer.cpp
    similar/arch/sdl/gr.cpp
    similar/arch/sdl/init.cpp
    similar/arch/win32/include/resource.h
#    similar/editor/centers.cpp
#    similar/editor/curves.cpp
#    similar/editor/eglobal.cpp
#    similar/editor/elight.cpp
#    similar/editor/eobject.cpp
#    similar/editor/eswitch.cpp
#    similar/editor/group.cpp
#    similar/editor/info.cpp
#    similar/editor/kbuild.cpp
#    similar/editor/kcurve.cpp
#    similar/editor/kfuncs.cpp
#    similar/editor/kgame.cpp
#    similar/editor/khelp.cpp
#    similar/editor/kmine.cpp
#    similar/editor/ksegmove.cpp
#    similar/editor/ksegsel.cpp
#    similar/editor/ksegsize.cpp
#    similar/editor/ktmap.cpp
#    similar/editor/kview.cpp
#    similar/editor/med.cpp
#    similar/editor/meddraw.cpp
#    similar/editor/medmisc.cpp
#    similar/editor/medrobot.cpp
#    similar/editor/medsel.cpp
#    similar/editor/medwall.cpp
#    similar/editor/mine.cpp
#    similar/editor/objpage.cpp
#    similar/editor/segment.cpp
#    similar/editor/seguvs.cpp
#    similar/editor/texpage.cpp
#    similar/editor/texture.cpp
    similar/main/ai.cpp
    similar/main/aipath.cpp
    similar/main/automap.cpp
    similar/main/bm.cpp
    similar/main/bmread.cpp
    similar/main/cntrlcen.cpp
    similar/main/collide.cpp
    similar/main/config.cpp
    similar/main/console.cpp
    similar/main/controls.cpp
    similar/main/credits.cpp
    similar/main/digiobj.cpp
    similar/main/dumpmine.cpp
    similar/main/effects.cpp
    similar/main/endlevel.cpp
    similar/main/fireball.cpp
    similar/main/fuelcen.cpp
    similar/main/fvi.cpp
    similar/main/game.cpp
    similar/main/gamecntl.cpp
    similar/main/gamefont.cpp
    similar/main/gamemine.cpp
    similar/main/gamerend.cpp
    similar/main/gamesave.cpp
    similar/main/gameseg.cpp
    similar/main/gameseq.cpp
    similar/main/gauges.cpp
    similar/main/hostage.cpp
    similar/main/hud.cpp
    similar/main/iff.cpp
    similar/main/inferno.cpp
    similar/main/kconfig.cpp
    similar/main/kconfig.ui-table.cpp
    similar/main/kmatrix.cpp
    similar/main/laser.cpp
    similar/main/lighting.cpp
    similar/main/menu.cpp
    similar/main/mglobal.cpp
    similar/main/mission.cpp
    similar/main/morph.cpp
    similar/main/multi.cpp
    similar/main/multibot.cpp
    similar/main/net_udp.cpp
    similar/main/newdemo.cpp
    similar/main/newmenu.cpp
    similar/main/object.cpp
    similar/main/paging.cpp
    similar/main/physics.cpp
    similar/main/piggy.cpp
    similar/main/player.cpp
    similar/main/playsave.cpp
    similar/main/polyobj.cpp
    similar/main/powerup.cpp
    similar/main/render.cpp
    similar/main/robot.cpp
    similar/main/scores.cpp
    similar/main/segment.cpp
    similar/main/slew.cpp
    similar/main/songs.cpp
    similar/main/state.cpp
    similar/main/switch.cpp
    similar/main/terrain.cpp
    similar/main/texmerge.cpp
    similar/main/text.cpp
    similar/main/titles.cpp
    similar/main/vclip.cpp
    similar/main/vers_id.cpp
    similar/main/wall.cpp
    similar/main/weapon.cpp
    similar/misc/args.cpp
    similar/misc/physfsx.cpp)

link_directories(libs)
target_link_libraries(${PROJECT_NAME} SDL2 SDL2_mixer)
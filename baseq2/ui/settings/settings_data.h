// Generated audit source for the RmlUi settings menu.
// Sources read:
//   src/client/ui/menu.c
//   src/client/ui/playerconfig.c
//   src/client/ui/ui.c
//   src/client/ui/script.c
//   baseq2/q2rtx.menu
//   src/refresh/vkpt/path_tracer.c
//   src/refresh/vkpt/bloom.c
//   src/refresh/vkpt/asvgf.c
//   src/refresh/vkpt/tone_mapping.c
//
// src/client/ui/settings.c does not exist in this checkout. The concrete legacy
// settings screens are script-defined in baseq2/q2rtx.menu and parsed by
// src/client/ui/script.c.
//
// AUDIT: Environment
// cvar                         | type     | range / values                                               | default | label shown
// physical_sky                 | dropdown | 0 original env.map, 1 earth, 2 stroggos                       | 2       | sky type
// physical_sky_brightness      | slider   | -10 to 2 step .1                                              | 0       | sun and sky brightness
// sun_preset                   | dropdown | 0 custom, 1 current, 2 12x current, 3 night, 4 dawn, ... 8 dusk | n/a   | time of day
// next_sun                     | keybind  | key binding                                                   | n/a     | next time of day key
// sun_elevation                | slider   | -20 to 90 step 1                                              | 45      | sun elevation
// sun_azimuth                  | slider   | -20 to 380 step 5                                             | 345     | sun azimuth
// sun_gamepad                  | dropdown | 0 off, 1 left stick, 2 right stick                            | 0       | control sun with gamepad
// physical_sky_draw_clouds     | toggle   | 0/1                                                          | 1       | clouds
// sun_latitude                 | slider   | -90 to 90 step 2                                              | 32.9    | latitude
// pt_waterwarp                 | toggle   | 0/1                                                          | 0       | screen warping
// pt_water_fbm                 | dropdown | 0 fallback, 1 analytic, 2 sine, 3 exp sine, 4 Gerstner, 5 FBM | n/a     | Water mode
// pt_water_wave_scale          | slider   | .1 to 4                                                       | n/a     | Water wave scale
// pt_water_wave_speed          | slider   | 0 to 4                                                        | n/a     | Water wave speed
// pt_water_wave_steepness      | slider   | 1 to 4                                                        | n/a     | Water wave steepness
//
// AUDIT: Resolution
// drs_enable                   | toggle   | 0/1                                                          | 0       | dynamic resolution scaling
// drs_target                   | slider   | 30 to 250 step 1                                             | 60      | target frames per second
// drs_minscale                 | slider   | 25 to 100 step 5                                             | 50      | minimum scale
// drs_maxscale                 | slider   | 50 to 150 step 5                                             | 100     | maximum scale
// viewsize                     | slider   | 25 to 200 step 5                                             | 100     | fixed resolution scale / viewsize
//
// AUDIT: HDR
// vid_hdr                      | toggle   | 0/1                                                          | 0       | Enable HDR
// tm_hdr_peak_nits             | slider   | 100 to 2000 step 10                                          | n/a     | peak brightness
// ui_hdr_nits                  | slider   | 100 to 2000 step 10                                          | 300     | UI brightness
// tm_hdr_saturation_scale      | slider   | 0 to 200 step 5                                              | n/a     | Saturation adjustment
//
// AUDIT: Photo Mode
// pt_accumulation_rendering    | dropdown | 0 no, 1 yes, 2 yes hide GUI                                  | 1       | enable photo mode
// pause                        | keybind  | key binding                                                   | n/a     | activation key
// scr_showpause                | toggle   | 0/1                                                          | 1       | show pause plaque
// pt_accumulation_rendering_framenum | slider | 100 to 8000 step 100                                  | 500     | number of frames to accumulate
// pt_freecam                   | toggle   | 0/1                                                          | 1       | free camera
// pt_dof                       | toggle   | 0/1                                                          | 1       | depth of field
// pt_aperture_type             | dropdown | circle 0, triangle 3, square 4, pentagon 5, ... octagon 8    | n/a     | camera aperture shape
// pt_aperture_angle            | slider   | 0 to 1 step .1                                               | n/a     | aperture rotation
//
// AUDIT: Video / Graphics
// vid_rtx                      | dropdown | 0 opengl, 1 rtx                                              | n/a     | renderer
// vid_fullscreen               | dropdown | windowed plus vid_modelist values                            | 0       | video mode
// cl_adjustfov                 | dropdown | vert-, hor+                                                  | 1       | fov scaling
// vid_display                  | dropdown | vid_displaylist pairs                                        | 0       | full screen display
// vid_vsync                    | toggle   | 0/1                                                          | 0       | vertical sync
// cl_reflex                    | dropdown | 0 off, 1 on, 2 on + boost                                    | 2       | NVIDIA Reflex
// scr_fps                      | dropdown | 0 off, 1 FPS, 2 FPS + resolution scale                       | 0       | FPS counter
// fov                          | slider   | 60 to 160 step 5                                             | 90      | field of view
// pt_dlss                      | toggle   | 0/1                                                          | 1       | NVIDIA DLSS Super Resolution
// pt_dlss_quality              | dropdown | 0 quality, 1 balanced, 2 performance, 3 ultra performance, 4 dlaa | 0  | DLSS quality mode
// tm_exposure_bias             | slider   | -5 to 0 step .1                                              | n/a     | exposure bias (brightness)
// tm_reinhard                  | slider   | 0 to 1 step .1                                               | n/a     | contrast
// pt_texture_lod_bias          | slider   | -2 to 2 step .5                                              | n/a     | texture LOD bias
// flt_taa                      | dropdown | 0 none, 1 temporal AA, 2 temporal upscaling                  | n/a     | anti-aliasing
// pt_num_bounce_rays           | dropdown | .5 low, 1 medium, 2 high                                     | n/a     | global illumination
// pt_restir_di                 | dropdown | 0 legacy, 1 ReSTIR                                           | n/a     | ReSTIR direct lighting (DI)
// pt_reflect_refract           | dropdown | 0 off, 1, 2, 4, 8                                           | n/a     | reflection/refraction depth
// pt_cameras                   | toggle   | 0/1                                                          | n/a     | security cameras
// pt_caustics                  | toggle   | 0/1                                                          | 1       | caustics
// gr_enable                    | toggle   | 0/1                                                          | 1       | god rays
// bloom_enable                 | toggle   | 0/1                                                          | 1       | bloom
// flt_enable                   | toggle   | 0/1                                                          | n/a     | denoiser
// pt_nearest                   | dropdown | 0 anisotropic, 1 mixed, 2 nearest                            | 0       | texture filtering
// pt_thick_glass               | dropdown | 0 disabled, 1 photo mode only, 2 enabled                     | n/a     | thick glass refraction
// pt_projection                | dropdown | 0 perspective, 1 panini, 2 stereographic, 3 cylindrical, 4 equirectangular, 5 mercator | 0 | projection
//
// AUDIT: Developer
// pt_restir_di_spatial         | toggle   | 0/1                                                          | n/a     | ReSTIR DI spatial reuse
// pt_restir_di_debug           | dropdown | 0 off, 1 W, 2 M, 3 light type, 4 target PDF, 5 spatial, 6 temporal lifecycle | n/a | ReSTIR DI debug view
// pt_nrd                       | dropdown | 0 ASVGF, 1 NRD ReLAX                                        | 1       | denoiser (ReSTIR)
// pt_light_stats               | toggle   | 0/1                                                          | n/a     | ReSTIR light PDF correction
// flt_fixed_albedo             | bitmask  | bit 1 inverted                                               | n/a     | textures (fixed albedo debug)
// sli                          | dropdown | 0 disabled, 1 when available                                 | 1       | multi-gpu support
// ray_tracing_api              | dropdown | auto, query, pipeline                                        | auto    | ray tracing API
// profiler                     | toggle   | 0/1                                                          | 0       | GPU profiler
// flt_show_gradients           | toggle   | 0/1                                                          | n/a     | SVGF gradient overlay
// tm_debug                     | dropdown | 0 off, 1 histogram, 2 curve                                  | n/a     | tone mapping debug
// pt_show_sky                  | toggle   | 0/1                                                          | n/a     | show sky geometry
//
// AUDIT: Sound
// s_volume                     | slider   | 0 to 1 step .05                                              | n/a     | master volume
// ogg_volume                   | slider   | 0 to 1 step .05                                              | n/a     | music volume
// ogg_enable                   | toggle   | 0/1                                                          | n/a     | enable music
// ogg_shuffle                  | toggle   | 0/1                                                          | n/a     | shuffle tracks
// s_underwater                 | toggle   | 0/1                                                          | n/a     | underwater effect
// s_ambient                    | dropdown | no, yes, only player's own                                   | n/a     | ambient sounds
// cl_chat_sound                | dropdown | disabled, default, alternative                               | 1       | chat beep
// s_enable                     | dropdown | no sound, software, OpenAL                                   | n/a     | sound engine
//
// AUDIT: Effects / Rail Trail / Screen / Crosshair / Input / Network
// tm_blend_enable              | toggle   | 0/1                                                          | 1       | screen blending
// tm_blend_scale_center        | slider   | 0 to 1                                                       | n/a     | screen blending center strength
// tm_blend_scale_border        | slider   | 0 to 1                                                       | n/a     | screen blending border strength
// cl_disable_explosions        | bitmask  | bit 0 inverted grenade, bit 1 inverted rocket                | 0       | grenade/rocket explosions
// cl_explosion_sprites         | toggle   | 0/1                                                          | 1       | explosion sprites
// cl_railtrail_type            | dropdown | 0 default, 1 core only, 2 core and spiral                    | 0       | rail trail type
// cl_railtrail_time            | slider   | .1 to 3 step .1                                              | 1.0     | rail trail duration
// cl_railcore_width            | slider   | 1 to 6 step 1                                                | 2       | core width
// cl_railspiral_radius         | slider   | 1 to 6 step 1                                                | 3       | spiral radius
// cl_railcore_color            | dropdown | black/red/green/yellow/blue/cyan/magenta/white               | red     | core color
// cl_railspiral_color          | dropdown | black/red/green/yellow/blue/cyan/magenta/white               | blue    | spiral color
// crosshair                    | dropdown | none/cross/dot/angle                                         | 0       | crosshair type
// ch_scale                     | dropdown | .5, 1, 2                                                     | 1       | crosshair scale
// ch_health                    | toggle   | 0/1                                                          | 0       | color by health
// ch_red/ch_green/ch_blue/ch_alpha | slider | 0 to 1                                                   | 1       | crosshair RGBA channels
// cl_cinematics                | toggle   | 0/1                                                          | 1       | cutscenes
// scr_showitemname             | dropdown | 0 do not show, 1 show on change, 2 always show               | 1       | selected inventory item
// scr_lag_draw                 | toggle   | 0/1                                                          | 0       | ping graph
// scr_demobar                  | dropdown | no/yes/verbose                                               | 1       | demo bar
// scr_alpha/con_alpha          | slider   | 0 to 1                                                       | 1       | HUD/console opacity
// scr_scale/con_scale/ui_scale | dropdown | auto, 1x, 2x, 4x                                            | 0       | HUD/console/menu scale
// sensitivity                  | input    | numeric                                                      | 3       | mouse sensitivity
// m_invert/m_autosens/m_filter/freelook/cl_run | toggle | 0/1                                           | varies   | input toggles
// cl_maxpackets/cl_packetdup/cl_instantpacket/cl_fuzzhack/cl_updaterate | network | see Cvar_Get defaults | varies | network performance
//
// AUDIT: Player Setup
// name                         | input    | string                                                       | Player  | name
// skin                         | dropdown | model/skin from filesystem                                   | male/grunt | model, skin
// hand                         | dropdown | 0 right, 1 left, 2 center                                    | 0       | handedness
// aimfix                       | dropdown | 0 default, 1 at crosshair                                    | n/a     | aiming point
// cl_player_model              | dropdown | 0 nothing, 1 just gun, 2 first person model, 3 third person | n/a     | view

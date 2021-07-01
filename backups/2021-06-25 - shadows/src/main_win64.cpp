/** OpenGL 3D Renderer

TODO:
    - Shadow mapping
        - directional shadows DONE
        - omnidirectional shadows for point lights and spot lights DONE
        - multiple onnidirectional shadow maps DONE
        - CASCADED SHADOW MAPS https://developer.download.nvidia.com/SDK/10.5/opengl/src/cascaded_shadow_maps/doc/cascaded_shadow_maps.pdf
            - make a test map for CSM. (e.g. field of trees or cubes all with shadows)
        - option to completely disable shadows (test if we return to pre-shadows performance)
    - BUG console command bug - commands get cut off when entered - could be a memory bug?
    - Skyboxes
    - Map Editor:
        - console command 'editor' to enter
        - quits the game inside the gamemode, and simply loads the map into the editor (keep camera in same transformation)
        - use console to load and save
            - 'save folder/path/name.map'
            - 'load folder/path/name.map' load automatically saves current map
            - maybe keep loaded maps in memory? until we explicitly call unload? or there are too many maps loaded?
                - so that we can switch between maps without losing the map data in memory
                    - then we can have a "palette" map with a collection of loaded model objs that we can copy instead of finding on disk
            - use console commands to create geometry? or is that retarded?
        - texture blending - store the bitmap in memory, and we can write it to map data or an actual bitmap whatever
            - brush system like in unity/unreal where you paint textures, and behind the scenes we can edit the blend map in memory
    - Modify kc_truetypeassembler.h documentation to say that one can use translation and scaling matrices with the resulting
      vertices in order to transform them on the screen (e.g. animate the text).

Backlog:
    - Implement GJK EPA collisions in a clone of this repo
    - option for some console messages to be displayed to game screen.
    - Memory management / custom memory allocator / replace all mallocs and callocs
    - Arrow rendering for debugging
        - in the future arrow can also be used for translation gizmo
    - add SIMD for kc_math library
    - Entity - pos, orientation, scale, mesh, few boolean flags, collider, tags
    - Fixed timestep? for physics only?
    - Face culling
    - texture_t GL_NEAREST option
    - texture_t do something like source engine
        - Build simple polygons and shapes, and the textures get wrapped
          automatically(1 unit in vertices is 1 unit in texture uv)
    - Console:
        - remember previously entered commands
        - shader hotloading/compiling during runtime - pause all update / render while shaders are being recompiled
        - mouse picking entities

THIS PROJECT IS A SINGLE TRANSLATION UNIT BUILD / UNITY BUILD

BUILD MODES

    INTERNAL_BUILD:
        0 - Build for public release
        1 - Build for developer only

    SLOW_BUILD:
        0 - No slow code allowed
        1 - Slow code fine

*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>
#include <map>
#include <iostream>
#include <fstream>

#include <gl/glew.h>
#include <SDL.h>
#include <SDL_syswm.h>
#include <assimp\Importer.hpp>
#include <assimp\scene.h>
#include <assimp\postprocess.h>
#define STB_SPRINTF_IMPLEMENTATION
#include "stb_sprintf.h"
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"
#define KC_TRUETYPEASSEMBLER_IMPLEMENTATION
#include "kc_truetypeassembler.h"
#define KC_MATH_IMPLEMENTATION
#include "kc_math.h"

#include "gamedefine.h" // defines and typedefs
#include "console.h"
#include "core.h"

// --- global variables  --- note: static variables are initialized to their default values
// Width and Height of writable buffer
global_var uint32 g_buffer_width;
global_var uint32 g_buffer_height;

// Global Input Data
global_var const uint8* g_keystate = nullptr;       // Stores keyboard state this frame. Access via g_keystate[SDL_Scancode].
global_var int32 g_last_mouse_pos_x = INDEX_NONE;   // Stores mouse state this frame. mouse_pos is not updated when using SDL RelativeMouseMode.
global_var int32 g_last_mouse_pos_y = INDEX_NONE;
global_var int32 g_curr_mouse_pos_x = INDEX_NONE;
global_var int32 g_curr_mouse_pos_y = INDEX_NONE;
global_var int32 g_mouse_delta_x = INDEX_NONE;
global_var int32 g_mouse_delta_y = INDEX_NONE;

// Game Window and States
global_var bool b_is_game_running = true;
global_var bool b_is_update_running = true;
global_var SDL_Window* window = nullptr;
global_var SDL_GLContext opengl_context = nullptr;

// -------------------------
// Temporary
global_var camera_t g_camera;
global_var mat4 g_matrix_projection_ortho;
global_var bool g_b_wireframe = false;

/* NOTE we're not going to have multiple maps loaded at once later on
   eventually we want to load maps from disk when we switch maps
   The only reason we are keeping an array right now is so we can preset
   the model transforms since we can't read that from disk yet. */
global_var uint8 active_map_index = 0;
global_var temp_map_t loaded_maps[4];

// Fonts
tta_font_t g_font_handle_c64;
texture_t g_font_atlas_c64;
// -------------------------

#include "timer.cpp"
#include "diskapi.cpp"
#include "opengl.cpp"
#include "camera.cpp"
#include "profiler.cpp"
#include "debugger.cpp"
#include "core.cpp"
#include "commands.cpp"
#include "console.cpp"

shader_lighting_t shader_common;

shader_directional_shadow_map_t shader_directional_shadow_map;
shader_omni_shadow_map_t        shader_omni_shadow_map;
shader_base_t                   shader_debug_dir_shadow_map;

shader_orthographic_t shader_text;
shader_orthographic_t shader_ui;
shader_perspective_t shader_simple;
material_t material_shiny = {4.f, 128.f };
material_t material_dull = {0.5f, 1.f };

unsigned int directionalShadowMapTexture;
unsigned int directionalShadowMapFBO;
mat4 directionalLightSpaceMatrix;

struct {
    unsigned int depthCubeMapTexture = 0;
    unsigned int depthCubeMapFBO = 0;
    float depthCubeMapFarPlane = 25.f;
    std::vector<mat4> shadowTransforms;
} omni_shadow_maps[10];


static const char* vertex_shader_path = "shaders/default_phong.vert";
static const char* frag_shader_path = "shaders/default_phong.frag";
static const char* ui_vs_path = "shaders/ui.vert";
static const char* ui_fs_path = "shaders/ui.frag";
static const char* text_vs_path = "shaders/text_ui.vert";
static const char* text_fs_path = "shaders/text_ui.frag";
static const char* simple_vs_path = "shaders/simple.vert";
static const char* simple_fs_path = "shaders/simple.frag";

internal inline void win64_load_font(tta_font_t* font_handle,
                                     texture_t& font_atlas,
                                     const char* font_path,
                                     uint8 font_size)
{
    binary_file_handle_t fontfile;
    read_file_binary(fontfile, font_path);
        if(fontfile.memory)
        {
            kctta_init_font(font_handle, (uint8*) fontfile.memory, font_size);
        }
    free_file_binary(fontfile);
    gl_load_texture_from_bitmap(font_atlas,
                                font_handle->font_atlas.pixels,
                                font_handle->font_atlas.width,
                                font_handle->font_atlas.height,
                                GL_RED, GL_RED);
    free(font_handle->font_atlas.pixels);
}

internal inline void sdl_vsync(int vsync)
{
    /** This makes our Buffer Swap (SDL_GL_SwapWindow) synchronized with the monitor's 
    vertical refresh - basically vsync; 0 = immediate, 1 = vsync, -1 = adaptive vsync. 
    Remark: If application requests adaptive vsync and the system does not support it, 
    this function will fail and return -1. In such a case, you should probably retry 
    the call with 1 for the interval. */
    switch(vsync){ // 0 = immediate (no vsync), 1 = vsync, 2 = adaptive vsync
        case 0:{SDL_GL_SetSwapInterval(0);}break;
        case 1:{SDL_GL_SetSwapInterval(1);}break;
        case 2:{if(SDL_GL_SetSwapInterval(-1)==-1) SDL_GL_SetSwapInterval(1);}break;
        default:{
            console_print("Invalid vsync option; 0 = immediate, 1 = vsync, 2 = adaptive vsync");}break;
    }
}

internal inline void gl_update_viewport_size()
{
    /** Get the size of window's underlying drawable in pixels (for use with glViewport).
    Remark: This may differ from SDL_GetWindowSize() if we're rendering to a high-DPI drawable, i.e. the window was created 
    with SDL_WINDOW_ALLOW_HIGHDPI on a platform with high-DPI support (Apple calls this "Retina"), and not disabled by the 
    SDL_HINT_VIDEO_HIGHDPI_DISABLED hint. */
    SDL_GL_GetDrawableSize(window, (int*)&g_buffer_width, (int*)&g_buffer_height);
    glViewport(0, 0, g_buffer_width, g_buffer_height);
    console_printf("Viewport updated - x: %d y: %d\n", g_buffer_width, g_buffer_height);
}

/** Create window, set up OpenGL context, initialize SDL and GLEW */
internal bool game_init()
{
    // Initialize SDL
    if (SDL_Init(SDL_INIT_EVERYTHING) < 0)
    {
        printf("SDL failed to initialize.\n");
        return false;
    }

    // OpenGL Context Attributes
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3); // version major
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3); // version minor
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE); // core means not backward compatible. not using deprecated code.
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG); // allow forward compatibility
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1); // double buffering is on by default, but let's just call this anyway
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24); // depth buffer precision of 24 bits 
    // Setup SDL Window
    if ((window = SDL_CreateWindow(
        "test win",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        WIDTH,
        HEIGHT,
        SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN
    )) == nullptr)
    {
        printf("SDL window failed to create.\n");
        return false;
    }

    /** GRABBING WINDOW INFORMATION - https://wiki.libsdl.org/SDL_GetWindowWMInfo
    *   Remarks: You must include SDL_syswm.h for the declaration of SDL_SysWMinfo. The info structure must 
        be initialized with the SDL version, and is then filled in with information about the given window, 
        as shown in the Code Example. */
    SDL_SysWMinfo sys_windows_info;
    SDL_VERSION(&sys_windows_info.version);
    if (SDL_GetWindowWMInfo(window, &sys_windows_info)) 
    {
        const char* subsystem = "an unknown system!";
        switch (sys_windows_info.subsystem) {
            case SDL_SYSWM_UNKNOWN:   break;
            case SDL_SYSWM_WINDOWS:   subsystem = "Microsoft Windows(TM)";  break;
            case SDL_SYSWM_X11:       subsystem = "X Window System";        break;
#if SDL_VERSION_ATLEAST(2, 0, 3)
            case SDL_SYSWM_WINRT:     subsystem = "WinRT";                  break;
#endif
            case SDL_SYSWM_DIRECTFB:  subsystem = "DirectFB";               break;
            case SDL_SYSWM_COCOA:     subsystem = "Apple OS X";             break;
            case SDL_SYSWM_UIKIT:     subsystem = "UIKit";                  break;
#if SDL_VERSION_ATLEAST(2, 0, 2)
            case SDL_SYSWM_WAYLAND:   subsystem = "Wayland";                break;
            case SDL_SYSWM_MIR:       subsystem = "Mir";                    break;
#endif
#if SDL_VERSION_ATLEAST(2, 0, 4)
            case SDL_SYSWM_ANDROID:   subsystem = "Android";                break;
#endif
#if SDL_VERSION_ATLEAST(2, 0, 5)
            case SDL_SYSWM_VIVANTE:   subsystem = "Vivante";                break;
#endif
        }

        console_printf("This program is running SDL version %u.%u.%u on %s\n", sys_windows_info.version.major,
                       sys_windows_info.version.minor, sys_windows_info.version.patch, subsystem);
    }
    else 
    {
        console_printf("Couldn't get window information: %s\n", SDL_GetError());
    }

    // Set context for SDL to use. Let SDL know that this window is the window that the OpenGL context should be tied to; everything that is drawn should be drawn to this window.
    if ((opengl_context = SDL_GL_CreateContext(window)) == nullptr)
    {
        printf("Failed to create OpenGL Context with SDL.\n");
        return false;
    }
    console_printf("OpenGL context created.\n");

    // Initialize GLEW
    glewExperimental = GL_TRUE; // Enable us to access modern opengl extension features
    if (glewInit() != GLEW_OK)
    {
        printf("GLEW failed to initialize.\n");
        return false;
    }
    console_printf("GLEW initialized.\n");

    gl_update_viewport_size();
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); // alpha blending func: a * (rgb) + (1 - a) * (rgb) = final color output
    glBlendEquation(GL_FUNC_ADD);
    sdl_vsync(0);                               // vsync
    SDL_SetRelativeMouseMode(SDL_TRUE);         // Lock mouse to window
    g_keystate = SDL_GetKeyboardState(nullptr); // Grab keystate array
    stbi_set_flip_vertically_on_load(true);     // stb_image setting
    kctta_setflags(KCTTA_CREATE_INDEX_BUFFER);  // kc_truetypeassembler setting

    // LOAD FONTS
    win64_load_font(&g_font_handle_c64, g_font_atlas_c64, "data/fonts/SourceCodePro.ttf", CONSOLE_TEXT_SIZE);

    console_initialize(&g_font_handle_c64, g_font_atlas_c64);
    profiler_initialize(&g_font_handle_c64, g_font_atlas_c64);
    debug_initialize();

    return true;
}

/** Process input and SDL events */
internal void game_process_events()
{
    // Store Mouse state
    SDL_bool b_relative_mouse = SDL_GetRelativeMouseMode();
    if(b_relative_mouse)
    {
        SDL_GetRelativeMouseState(&g_mouse_delta_x, &g_mouse_delta_y);
    }
    else
    {
        g_last_mouse_pos_x = g_curr_mouse_pos_x;
        g_last_mouse_pos_y = g_curr_mouse_pos_y;
        SDL_GetMouseState(&g_curr_mouse_pos_x, &g_curr_mouse_pos_y);
        if (g_last_mouse_pos_x >= 0) { g_mouse_delta_x = g_curr_mouse_pos_x - g_last_mouse_pos_x; } else { g_mouse_delta_x = 0; }
        if (g_last_mouse_pos_y >= 0) { g_mouse_delta_y = g_curr_mouse_pos_y - g_last_mouse_pos_y; } else { g_mouse_delta_y = 0; }
    }

    // SDL Events
    SDL_Event event;
    while (SDL_PollEvent(&event))
    {
        switch (event.type)
        {
            case SDL_QUIT:
            {
                b_is_game_running = false;
            } break;

            case SDL_WINDOWEVENT:
            {
                switch(event.window.event)
                {
                    case SDL_WINDOWEVENT_RESIZED:
                    {
                        gl_update_viewport_size();
                        calculate_perspectivematrix(g_camera, 90.f);
                        g_matrix_projection_ortho = projection_matrix_orthographic_2d(0.0f, (real32)g_buffer_width, (real32)g_buffer_height, 0.0f);
                    } break;
                    case SDL_WINDOWEVENT_SIZE_CHANGED:
                    {
                        gl_update_viewport_size();
                        calculate_perspectivematrix(g_camera, 90.f);
                        g_matrix_projection_ortho = projection_matrix_orthographic_2d(0.0f, (real32)g_buffer_width, (real32)g_buffer_height, 0.0f);
                    } break;
                }
            } break;

            case SDL_MOUSEWHEEL:
            {
                if(console_is_shown() && g_curr_mouse_pos_y < CONSOLE_HEIGHT)
                {
                    if(event.wheel.y > 0)
                    {
                        console_scroll_up();
                    }
                    else if(event.wheel.y < 0)
                    {
                        console_scroll_down();
                    }
                }
            } break;

            case SDL_KEYDOWN:
            {
                if (event.key.keysym.sym == SDLK_BACKQUOTE)
                {
                    console_toggle();
                    break;
                }

                if (console_is_shown())
                {
                    console_keydown(event.key);
                    break;
                }

                if (event.key.keysym.sym == SDLK_ESCAPE && console_is_hidden())
                {
                    b_is_game_running = false;
                    break;
                }

                if (event.key.keysym.sym == SDLK_F1)
                {
                    perf_profiler_level ? console_command("profiler 0") : console_command("profiler 1");
                    break;
                }

                if (event.key.keysym.sym == SDLK_F2)
                {
                    debugger_level ? console_command("debug 0") : console_command("debug 1");
                    break;
                }

                if (event.key.keysym.sym == SDLK_z)
                {
                    SDL_SetRelativeMouseMode((SDL_bool) !b_relative_mouse);
                    console_printf("mouse grab = %s\n", !b_relative_mouse ? "true" : "false");
                    break;
                }
            } break;
        }
    }
}

/** Tick game logic. Delta time is in seconds. */
internal void game_update(real32 dt)
{
    console_update(dt);

    if(b_is_update_running)
    {
        update_camera(g_camera, dt);
    }
}

/** Process graphics and render them to the screen. */
internal void game_render()
{
    glViewport(0, 0, g_buffer_width, g_buffer_height);
    //glClearColor(0.39f, 0.582f, 0.926f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT); // Clear opengl context's buffer

// NOT ALPHA BLENDED
    glDisable(GL_BLEND);
// DEPTH TESTED
    glEnable(GL_DEPTH_TEST);
    // TODO Probably should make own shader for wireframe draws so that wireframe fragments aren't affected by lighting or textures
    if(g_b_wireframe)
    {
        glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    }
    else
    {
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    }
    
    calculate_viewmatrix(g_camera);
    
    gl_use_shader(shader_common);

        gl_bind_view_matrix(shader_common, g_camera.matrix_view.ptr());
        gl_bind_projection_matrix(shader_common, g_camera.matrix_perspective.ptr());
        gl_bind_camera_position(shader_common, g_camera);

        temp_map_t* loaded_map = &loaded_maps[active_map_index];

        gl_bind_directional_light(shader_common, loaded_map->directionallight);
        // set texture unit 1 to directionalShadowMapTexture
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, directionalShadowMapTexture);

        glUniformMatrix4fv(shader_common.uid_directional_light_transform, 1, GL_FALSE, directionalLightSpaceMatrix.ptr());
        glUniform1i(shader_common.uid_texture, 1);
        glUniform1i(shader_common.uid_directional_shadow_map, 2); // set directional shadow map to reference texture unit 1

        for(int omniLightCount = 0; omniLightCount < loaded_map->pointlights.size() + loaded_map->spotlights.size(); ++omniLightCount)
        {
            glActiveTexture(GL_TEXTURE3 + omniLightCount);
            glBindTexture(GL_TEXTURE_CUBE_MAP, omni_shadow_maps[omniLightCount].depthCubeMapTexture);

            glUniform1i(shader_common.uid_omni_shadow_maps[omniLightCount].shadowMap, 3 + omniLightCount);
            glUniform1f(shader_common.uid_omni_shadow_maps[omniLightCount].farPlane, omni_shadow_maps[omniLightCount].depthCubeMapFarPlane);
        }

        gl_bind_point_lights(shader_common, loaded_map->pointlights.data(), (uint8)loaded_map->pointlights.size());
        gl_bind_spot_lights(shader_common, loaded_map->spotlights.data(), (uint8)loaded_map->spotlights.size());

        /** We could simply update the game object's position, rotation, scale fields,
            then construct the model matrix in game_render based on those fields.
        */

        mat4 matrix_model = identity_mat4();
        matrix_model = identity_mat4();
        matrix_model *= translation_matrix(loaded_map->mainobject.pos);
        matrix_model *= rotation_matrix(loaded_map->mainobject.orient);
        matrix_model *= scale_matrix(loaded_map->mainobject.scale);
        gl_bind_model_matrix(shader_common, matrix_model.ptr());
        gl_bind_material(shader_common, material_dull);
        render_meshgroup(loaded_map->mainobject.model);

    glUseProgram(0);

// ALPHA BLENDED
    glEnable(GL_BLEND);
    debug_render(shader_simple);

// NOT DEPTH TESTED
    glDisable(GL_DEPTH_TEST); 
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

/////////////////////
    if(g_keystate[SDL_SCANCODE_F3])
    {
        local_persist mesh_t quad;
        local_persist bool meshmade = false;
        if(!meshmade)
        {
            meshmade = true;

            uint32 quadindices[6] = { 
                0, 1, 3,
                0, 3, 2
            };
            GLfloat quadvertices[16] = {
            //  x     y        u    v   
                -1.f, -1.f,   -0.1f, -0.1f,
                1.f, -1.f,    1.1f, -0.1f,
                -1.f, 1.f,    -0.1f, 1.1f,
                1.f, 1.f,     1.1f, 1.1f
            };
            quad = gl_create_mesh_array(quadvertices, quadindices, 16, 6, 2, 2, 0);
        }
        gl_use_shader(shader_debug_dir_shadow_map);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, directionalShadowMapTexture);

            gl_render_mesh(quad);
        glUseProgram(0);
    }
/////////////////////

    profiler_render(shader_ui, shader_text);
    console_render(shader_ui, shader_text);


    // Enable depth test before swapping buffers
    // (NOTE: if we don't enable depth test before swap, the shadow map shows up as blank white texture on the quad.)
    glEnable(GL_DEPTH_TEST);
    /* Swap our buffer to display the current contents of buffer on screen. 
    This is used with double-buffered OpenGL contexts, which are the default. */
    SDL_GL_SwapWindow(window);
}

void calc_average_normals(uint32* indices,
                          uint32 indices_count,
                          real32* vertices,
                          uint32 vertices_count,
                          uint32 vertex_size,
                          uint32 normal_offset)
{
    for(size_t i = 0; i < indices_count; i += 3)
    {
        uint32 in0 = indices[i] * vertex_size;
        uint32 in1 = indices[i+1] * vertex_size;
        uint32 in2 = indices[i+2] * vertex_size;
        vec3 v0 = make_vec3(vertices[in1] - vertices[in0],
                                 vertices[in1+1] - vertices[in0+1],
                                 vertices[in1+2] - vertices[in0+2]);
        vec3 v1 = make_vec3(vertices[in2] - vertices[in0],
                                 vertices[in2+1] - vertices[in0+1],
                                 vertices[in2+2] - vertices[in0+2]);
        vec3 normal = normalize(cross(v0, v1));

        in0 += normal_offset;
        in1 += normal_offset;
        in2 += normal_offset;
        vertices[in0] += normal.x;
        vertices[in0+1] += normal.y;
        vertices[in0+2] += normal.z;
        vertices[in1] += normal.x;
        vertices[in1+1] += normal.y;
        vertices[in1+2] += normal.z;
        vertices[in2] += normal.x;
        vertices[in2+1] += normal.y;
        vertices[in2+2] += normal.z;
    }

    for(size_t i = 0; i < vertices_count/vertex_size; ++i)
    {
        uint32 v_normal_offset = (int32)i * vertex_size + normal_offset;
        vec3 norm_vec = normalize(make_vec3(vertices[v_normal_offset],
                                            vertices[v_normal_offset+1],
                                            vertices[v_normal_offset+2]));
        vertices[v_normal_offset] = -norm_vec.x;
        vertices[v_normal_offset+1] = -norm_vec.y;
        vertices[v_normal_offset+2] = -norm_vec.z;
    }
}

int main(int argc, char* argv[]) // Our main entry point MUST be in this form when using SDL
{
    if (game_init() == false) return 1;

    uint32 indices[12] = { 
        0, 3, 1,
        1, 3, 2,
        2, 3, 0,
        0, 1, 2
    };

    GLfloat vertices[32] = {
    //  x     y     z    u    v    normals
        -1.f, -1.f, -0.6f, 0.f, 0.f, 0.f, 0.f, 0.f,
        0.f, -1.f, 1.f, 0.5f, 0.f, 0.f, 0.f, 0.f,
        1.f, -1.f, -0.6f, 1.f, 0.f, 0.f, 0.f, 0.f,
        0.f, 1.f, 0.f, 0.5f, 1.f, 0.f, 0.f, 0.f
    };

    uint32 floor_indices[6] = { 
        0, 2, 1,
        1, 2, 3
    };

    GLfloat floor_vertices[32] = {
        -10.f, 0.f, -10.f, 0.f, 0.f, 0.f, 1.f, 0.f,
        10.f, 0.f, -10.f, 10.f, 0.f, 0.f, 1.f, 0.f,
        -10.f, 0.f, 10.f, 0.f, 10.f, 0.f, 1.f, 0.f,
        10.f, 0.f, 10.f, 10.f, 10.f, 0.f, 1.f, 0.f
    };

    calc_average_normals(indices, 12, vertices, 32, 8, 5);

    gl_load_shader_program_from_file(shader_common, vertex_shader_path, frag_shader_path);
    gl_load_shader_program_from_file(shader_directional_shadow_map, "shaders/directional_shadow_map.vert", "shaders/directional_shadow_map.frag");
    gl_load_shader_program_from_file(shader_omni_shadow_map, "shaders/omni_shadow_map.vert", "shaders/omni_shadow_map.geom", "shaders/omni_shadow_map.frag");
    gl_load_shader_program_from_file(shader_debug_dir_shadow_map, "shaders/debug_directional_shadow_map.vert", "shaders/debug_directional_shadow_map.frag");
    gl_load_shader_program_from_file(shader_text, text_vs_path, text_fs_path);
    gl_load_shader_program_from_file(shader_ui, ui_vs_path, ui_fs_path);
    gl_load_shader_program_from_file(shader_simple, simple_vs_path, simple_fs_path);

    g_camera.position = { 0.f, 0.f, 0.f };
    g_camera.rotation = { 0.f, 270.f, 0.f };
    calculate_perspectivematrix(g_camera, 90.f);
    g_matrix_projection_ortho = projection_matrix_orthographic_2d(0.0f, (real32)g_buffer_width, (real32)g_buffer_height, 0.0f);

    // TEMPORARY setting up maps/scenes (TODO replace once we are loading maps from disk)
    loaded_maps[0].directionallight.orientation = euler_to_quat(make_vec3(0.f, 30.f, -47.f) * KC_DEG2RAD);
    loaded_maps[0].directionallight.ambient_intensity = 0.3f;
    loaded_maps[0].directionallight.diffuse_intensity = 0.8f;
    loaded_maps[0].directionallight.colour = { 1.f, 1.f, 1.f };
    loaded_maps[0].temp_obj_path = "data/models/sponza/sponza.obj";
    loaded_maps[0].mainobject.pos = make_vec3(0.f, -6.f, 0.f);
    //loaded_maps[0].mainobject.orient = euler_to_quat(make_vec3(0.f, 30.f, -47.f)*KC_DEG2RAD);
    loaded_maps[0].mainobject.scale = make_vec3(0.04f, 0.04f, 0.04f);
    loaded_maps[0].cam_start_pos = make_vec3(0.f, 0.f, 0.f);
    loaded_maps[0].cam_start_rot = make_vec3(0.f, 211.f, -28.f);
    light_point_t lm0pl;
    lm0pl.colour = { 0.0f, 1.0f, 0.0f };
    lm0pl.position = { -6.f, -2.0f, 4.0f };
    lm0pl.ambient_intensity = 0.f;
    lm0pl.diffuse_intensity = 1.f;
    loaded_maps[0].pointlights.push_back(lm0pl);
    light_point_t lm1pl;
    lm1pl.colour = { 0.0f, 0.0f, 1.0f };
    lm1pl.position = { 10.f, -2.0f, 4.0f };
    lm1pl.ambient_intensity = 0.f;
    lm1pl.diffuse_intensity = 1.f;
    loaded_maps[0].pointlights.push_back(lm1pl);

    loaded_maps[1].directionallight.orientation = direction_to_orientation(make_vec3(2.f, -1.f, -2.f));
    loaded_maps[1].directionallight.ambient_intensity = 0.3f;
    loaded_maps[1].directionallight.diffuse_intensity = 1.0f;
    loaded_maps[1].directionallight.colour = { 255.f/255.f, 231.f/255.f, 155.f/255.f };
    loaded_maps[1].temp_obj_path = "data/models/doomguy/doommarine.obj";
    loaded_maps[1].mainobject.scale = make_vec3(0.04f, 0.04f, 0.04f);
    loaded_maps[1].cam_start_pos = make_vec3(0.f, 7.6f, 6.3f);
    loaded_maps[1].cam_start_rot = make_vec3(-21.f, -90.f, 0.f);

    loaded_maps[2].directionallight.orientation = direction_to_orientation(make_vec3(2.f, -1.f, -2.f));
    loaded_maps[2].directionallight.ambient_intensity = 0.3f;
    loaded_maps[2].directionallight.diffuse_intensity = 1.0f;
    loaded_maps[2].directionallight.colour = { 255.f/255.f, 231.f/255.f, 155.f/255.f };
    loaded_maps[2].temp_obj_path = "data/models/Alduin/Alduin.obj";
    loaded_maps[2].mainobject.scale = make_vec3(0.1f, 0.1f, 0.1f);
    loaded_maps[2].cam_start_pos = make_vec3(-63.f, 15.f, -14.5f);
    loaded_maps[2].cam_start_rot = make_vec3(9.69f, 13.6f, 0.f);

    loaded_maps[3].directionallight.orientation = direction_to_orientation(make_vec3(2.f, -1.f, -2.f));
    loaded_maps[3].directionallight.ambient_intensity = 0.3f;
    loaded_maps[3].directionallight.diffuse_intensity = 1.0f;
    loaded_maps[3].directionallight.colour = { 255.f/255.f, 231.f/255.f, 155.f/255.f };
    loaded_maps[3].temp_obj_path = "data/models/gallery/gallery.obj";
    loaded_maps[3].mainobject.orient = make_quaternion_deg(-89.f, make_vec3(1.f, 0.f, 0.f));
    loaded_maps[3].mainobject.scale = make_vec3(10.f, 10.f, 10.f);
    loaded_maps[3].cam_start_pos = make_vec3(-30.f, 31.f, -70.f);
    loaded_maps[3].cam_start_rot = make_vec3(-6.8f, -306.f, 0.f);

    game_switch_map(0);

//////////// shadow implementation ////////////
    glGenFramebuffers(1, &directionalShadowMapFBO);

    const unsigned int SHADOW_WIDTH = 2048, SHADOW_HEIGHT = 2048;

    glGenTextures(1, &directionalShadowMapTexture);
    glBindTexture(GL_TEXTURE_2D, directionalShadowMapTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, 
                 SHADOW_WIDTH, SHADOW_HEIGHT, 0, GL_DEPTH_COMPONENT, GL_FLOAT, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    float smap_bordercolor[] = { 1.0f, 1.0f, 1.0f, 1.0f };
    glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, smap_bordercolor);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);

    glBindFramebuffer(GL_FRAMEBUFFER, directionalShadowMapFBO);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, directionalShadowMapTexture, 0);
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    mat4 lightProjection = projection_matrix_orthographic(-50.0f, 50.0f, -50.0f, 50.0f, 0.1f, 150.f);
    directionalLightSpaceMatrix = lightProjection
    //* view_matrix_look_at(-orientation_to_direction(loaded_maps[0].directionallight.orientation) + make_vec3(-47.f, 66.f, 0.f), make_vec3(-47.f, 66.f, 0.f), make_vec3(0.f,1.f,0.f)); // TODO make up 0,0,1 if light is straight up or down
    //* view_matrix_look_at(make_vec3(-2.0f, 4.0f, -1.0f), make_vec3(0.f, 0.f, 0.f), make_vec3(0.f,1.f,0.f)); // TODO make up 0,0,1 if light is straight up or down
    * view_matrix_look_at(make_vec3(-47.44f, 66.29f, 9.65f), make_vec3(-47.44f, 66.29f, 9.65f) + orientation_to_direction(loaded_maps[0].directionallight.orientation), make_vec3(0.f,1.f,0.f)); // TODO make up 0,0,1 if light is straight up or down

    temp_map_t* loaded_map = &loaded_maps[active_map_index];


    const unsigned int CUBE_SHADOW_WIDTH = 1024, CUBE_SHADOW_HEIGHT = 1024;
    for(int omniLightCount = 0; omniLightCount < loaded_map->pointlights.size() + loaded_map->spotlights.size(); ++omniLightCount)
    {
        glGenFramebuffers(1, &omni_shadow_maps[omniLightCount].depthCubeMapFBO);

        glGenTextures(1, &omni_shadow_maps[omniLightCount].depthCubeMapTexture);
        glBindTexture(GL_TEXTURE_CUBE_MAP, omni_shadow_maps[omniLightCount].depthCubeMapTexture);
        for (unsigned int i = 0; i < 6; ++i)
        {
            glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_DEPTH_COMPONENT, 
                         CUBE_SHADOW_WIDTH, CUBE_SHADOW_HEIGHT, 0, GL_DEPTH_COMPONENT, GL_FLOAT, NULL);
        }
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

        glBindFramebuffer(GL_FRAMEBUFFER, omni_shadow_maps[omniLightCount].depthCubeMapFBO);
        glFramebufferTexture(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, omni_shadow_maps[omniLightCount].depthCubeMapTexture, 0);
        glDrawBuffer(GL_NONE);
        glReadBuffer(GL_NONE);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);

        float aspect = (float)CUBE_SHADOW_WIDTH/(float)CUBE_SHADOW_HEIGHT;
        float nearPlane = 1.0f;
        mat4 shadowProj = projection_matrix_perspective(90.f * KC_DEG2RAD, aspect, nearPlane, omni_shadow_maps[omniLightCount].depthCubeMapFarPlane);

        vec3 lightPos = loaded_map->pointlights[omniLightCount].position;
        omni_shadow_maps[omniLightCount].shadowTransforms.push_back(shadowProj * 
                         view_matrix_look_at(lightPos, lightPos + WORLD_FORWARD_VECTOR, WORLD_DOWN_VECTOR));
        omni_shadow_maps[omniLightCount].shadowTransforms.push_back(shadowProj * 
                         view_matrix_look_at(lightPos, lightPos + WORLD_BACKWARD_VECTOR, WORLD_DOWN_VECTOR));
        omni_shadow_maps[omniLightCount].shadowTransforms.push_back(shadowProj * 
                         view_matrix_look_at(lightPos, lightPos + WORLD_UP_VECTOR, WORLD_RIGHT_VECTOR));
        omni_shadow_maps[omniLightCount].shadowTransforms.push_back(shadowProj * 
                         view_matrix_look_at(lightPos, lightPos + WORLD_DOWN_VECTOR, WORLD_LEFT_VECTOR));
        omni_shadow_maps[omniLightCount].shadowTransforms.push_back(shadowProj * 
                         view_matrix_look_at(lightPos, lightPos + WORLD_RIGHT_VECTOR, WORLD_DOWN_VECTOR));
        omni_shadow_maps[omniLightCount].shadowTransforms.push_back(shadowProj * 
                         view_matrix_look_at(lightPos, lightPos + WORLD_LEFT_VECTOR, WORLD_DOWN_VECTOR));
    }



////////////////////////////////////////////////////

    glEnable(GL_CULL_FACE);

    // Game Loop
    int64 perf_counter_frequency = win64_counter_frequency();
    int64 last_tick = win64_get_ticks(); // cpu cycles count of last tick
    while (b_is_game_running)
    {
        game_process_events();
        if (b_is_game_running == false) { break; }
        int64 this_tick = win64_get_ticks();
        int64 delta_tick = this_tick - last_tick;
        real32 deltatime_secs = (real32) delta_tick / (real32) perf_counter_frequency;
        last_tick = this_tick;
        perf_frametime_secs = deltatime_secs;
        game_update(deltatime_secs);


        gl_use_shader(shader_directional_shadow_map);
        glUniformMatrix4fv(shader_directional_shadow_map.uniformDirectionalLightTransform, 1, GL_FALSE, directionalLightSpaceMatrix.ptr());
        glViewport(0, 0, SHADOW_WIDTH, SHADOW_HEIGHT);
        glBindFramebuffer(GL_FRAMEBUFFER, directionalShadowMapFBO);
            glClear(GL_DEPTH_BUFFER_BIT);
            //glCullFace(GL_FRONT);

            mat4 matrix_model = identity_mat4();
            matrix_model = identity_mat4();
            matrix_model *= translation_matrix(loaded_map->mainobject.pos);
            matrix_model *= rotation_matrix(loaded_map->mainobject.orient);
            matrix_model *= scale_matrix(loaded_map->mainobject.scale);
            gl_bind_model_matrix(shader_directional_shadow_map, matrix_model.ptr());
            render_meshgroup(loaded_map->mainobject.model);

            //glCullFace(GL_BACK);

        glBindFramebuffer(GL_FRAMEBUFFER, 0);


        gl_use_shader(shader_omni_shadow_map);
        for(int omniLightCount = 0; omniLightCount < loaded_map->pointlights.size() + loaded_map->spotlights.size(); ++omniLightCount)
        {
            glViewport(0, 0, CUBE_SHADOW_WIDTH, CUBE_SHADOW_HEIGHT);
            glBindFramebuffer(GL_FRAMEBUFFER, omni_shadow_maps[omniLightCount].depthCubeMapFBO);
                glClear(GL_DEPTH_BUFFER_BIT);
                shader_omni_shadow_map.SetLightMatrices(omni_shadow_maps[omniLightCount].shadowTransforms.data());
                vec3 lightPos = loaded_map->pointlights[omniLightCount].position;
                shader_omni_shadow_map.SetLightPos(lightPos);
                shader_omni_shadow_map.SetFarPlane(omni_shadow_maps[omniLightCount].depthCubeMapFarPlane);

                matrix_model = identity_mat4();
                matrix_model = identity_mat4();
                matrix_model *= translation_matrix(loaded_map->mainobject.pos);
                matrix_model *= rotation_matrix(loaded_map->mainobject.orient);
                matrix_model *= scale_matrix(loaded_map->mainobject.scale);
                gl_bind_model_matrix(shader_omni_shadow_map, matrix_model.ptr());
                render_meshgroup(loaded_map->mainobject.model);

            glBindFramebuffer(GL_FRAMEBUFFER, 0);
        }


        game_render();
    }

    console_printf("Game shutting down...\n");

    // Cleanup
    gl_delete_shader(shader_common);
    gl_delete_shader(shader_text);
    gl_delete_shader(shader_ui);

    SDL_DestroyWindow(window);
    SDL_GL_DeleteContext(opengl_context);
    SDL_Quit();

    return 0;
}
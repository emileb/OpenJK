// OpenJK mobile entry point.
//
// Bridges the OpenTouch Android touch layer (Clibs_OpenTouch/game_interface.h)
// to the OpenJK single-player engine. The touch UI / JNI bridge calls these
// Portable* functions to drive input, query screen state and pump the engine.
//
// Modelled on TheForceEngine's TheForceEngine/mobile/game_interface.cpp.
// Keyboard and mouse-look are wired through SDL injection (which OpenJK's normal
// sdl_input.cpp consumes). Gameplay movement / discrete actions need engine-side
// hooks OpenJK does not expose yet, so PortableMove / PortableAction are stubs.

#include "SDL.h"
#include "SDL_scancode.h"

#include "game_interface.h"

// OpenJK client state, used by PortableGetScreenMode(). client.h resolves its own
// relative includes from code/client/, so it is safe to pull in from here.
#include "../client/client.h"

// SDL's internal keyboard injection (same approach iortcw / TFE use): pushes a
// key event into SDL's queue under SDL's lock. OpenJK reads it through the normal
// SDL_KEYDOWN/UP path in sdl_input.cpp, so remappable binds keep working.
extern "C" int SDL_SendKeyboardKey(Uint8 state, SDL_Scancode scancode);

// Android engine entry point, defined in shared/sys/sys_main.cpp (named
// main_android there to avoid SDL's `#define main SDL_main` and the special
// semantics of a real main()).
extern int main_android(int argc, char *argv[]);

// Look sensitivities. Deltas from the touch layer are normalised; these scale
// them to the pixel units MouseMove() forwards to SDL_InjectMouse(). Values
// mirror TheForceEngine's.
static const float ANDROID_LOOK_MOUSE_X_SCALE = 1000.0f;
static const float ANDROID_LOOK_MOUSE_Y_SCALE =  800.0f;
// Joystick-look would normally be applied every frame while the stick is held;
// without a per-frame engine hook we emit on each stick-move event instead, so
// joystick-look mode turns only while the stick is moving (mouse-look mode, the
// default, behaves correctly). Kept small to match TFE's per-frame magnitude.
static const float ANDROID_LOOK_JOY_X_SCALE   =   12.0f;
static const float ANDROID_LOOK_JOY_Y_SCALE   =    8.0f;

extern "C" {

void PortableInit(int argc, const char **argv)
{
    LOGI("PortableInit");
    main_android(argc, (char **)argv);
}

// Android system Back button -> synthesize an ESC press + release.
void PortableBackButton(void)
{
    LOGI("PortableBackButton");
    SDL_SendKeyboardKey(SDL_PRESSED,  SDL_SCANCODE_ESCAPE);
    SDL_SendKeyboardKey(SDL_RELEASED, SDL_SCANCODE_ESCAPE);
}

// Hardware / on-screen keyboard pass-through. `code` is already an SDL_Scancode
// (the touch layer translates Android KeyEvent codes before calling us).
int PortableKeyEvent(int state, int code, int unitcode)
{
    SDL_SendKeyboardKey(state ? SDL_PRESSED : SDL_RELEASED, (SDL_Scancode)code);
    return 0;
}

// STUB: discrete gameplay actions (fire, jump, use, weapon select, ...). Hooking
// these up requires mapping PORT_ACT_* codes onto OpenJK's key binds or command
// buffer from the touch thread, which needs care (thread-safety, remap support).
// Left for later, as flagged.
void PortableAction(int state, int action)
{
}

// STUB: analog movement. OpenJK takes movement from SDL key/axis state; injecting
// an analog move axis has no clean entry point yet. Left for later.
void PortableMove(float fwd, float strafe)
{
}

void PortableMoveFwd(float fwd)
{
}

void PortableMoveSide(float strafe)
{
}

// Look pitch (vertical). Forwarded straight to the SDL mouse-motion injector.
void PortableLookPitch(int mode, float pitch)
{
    const float scale = (mode == LOOK_MODE_JOYSTICK) ? ANDROID_LOOK_JOY_Y_SCALE
                                                      : ANDROID_LOOK_MOUSE_Y_SCALE;
    MouseMove(0.0f, pitch * scale);
}

// Look yaw (horizontal). Forwarded straight to the SDL mouse-motion injector.
void PortableLookYaw(int mode, float yaw)
{
    const float scale = (mode == LOOK_MODE_JOYSTICK) ? ANDROID_LOOK_JOY_X_SCALE
                                                      : ANDROID_LOOK_MOUSE_X_SCALE;
    MouseMove(yaw * scale, 0.0f);
}

// A direct touch/swipe drag -> mouse-mode look.
void PortableMouse(float dx, float dy)
{
    MouseMove(dx * ANDROID_LOOK_MOUSE_X_SCALE, dy * ANDROID_LOOK_MOUSE_Y_SCALE);
}

void PortableMouseAbs(float x, float y)
{
    MouseMoveAbsolute(x, y);
}

void PortableMouseButton(int state, int button, float dx, float dy)
{
    MouseButton(state, button);
}

// STUB: console command injection. Would route to OpenJK's command buffer, but
// that is driven from the touch thread so needs thread-safe queuing. Left for later.
void PortableCommand(const char *cmd)
{
}

void PortableAutomapControl(float zoom, float x, float y)
{
}

int PortableShowKeyboard(void)
{
    return 0;
}

bool PortableSetAlwaysRun(bool run)
{
    return false;
}

// Map OpenJK's client state onto the touch screen mode so the right control set
// shows: console overlay -> TS_CONSOLE, any UI/menu up -> TS_MENU, live gameplay
// -> TS_GAME. Anything else (loading, disconnected at the main menu, cinematics)
// falls back to menu so the on-screen mouse + keyboard stay usable.
touchscreemode_t PortableGetScreenMode()
{
    const int catcher = Key_GetCatcher();

    if (catcher & KEYCATCH_CONSOLE)
        return TS_CONSOLE;

    if (catcher & KEYCATCH_UI)
        return TS_MENU;

    if (cls.state == CA_ACTIVE)
        return TS_GAME;

    return TS_MENU;
}

} // extern "C"

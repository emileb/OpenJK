// OpenJK mobile entry point.
//
// Bridges the OpenTouch Android touch layer (Clibs_OpenTouch/game_interface.h)
// to the OpenJK single-player engine. The touch UI / JNI bridge calls these
// Portable* functions to drive input, query screen state and pump the engine.
//
// All bodies are intentionally left as stubs for now; see TheForceEngine's
// TheForceEngine/mobile/game_interface.cpp for a fully-wired reference
// implementation to model the OpenJK versions on.

#include "game_interface.h"

// Android engine entry point, defined in shared/sys/sys_main.cpp (named
// main_android there to avoid SDL's `#define main SDL_main` and the special
// semantics of a real main()).
extern int main_android(int argc, char *argv[]);

extern "C" {

void PortableInit(int argc, const char **argv)
{
    LOGI("PortableInit");
    main_android(argc, (char **)argv);
}

void PortableBackButton(void)
{
}

int PortableKeyEvent(int state, int code, int unitcode)
{
    return 0;
}

void PortableAction(int state, int action)
{
}

void PortableMove(float fwd, float strafe)
{
}

void PortableMoveFwd(float fwd)
{
}

void PortableMoveSide(float strafe)
{
}

void PortableLookPitch(int mode, float pitch)
{
}

void PortableLookYaw(int mode, float yaw)
{
}

void PortableMouse(float dx, float dy)
{
}

void PortableMouseAbs(float x, float y)
{
}

void PortableMouseButton(int state, int button, float dx, float dy)
{
}

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

touchscreemode_t PortableGetScreenMode()
{
    return TS_GAME;
}

} // extern "C"

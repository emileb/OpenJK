// OpenJK mobile entry point (shared by the single-player and multiplayer builds).
//
// Bridges the OpenTouch Android touch layer (Clibs_OpenTouch/game_interface.h) to
// the OpenJK engine. The touch UI / JNI bridge calls these Portable* functions to
// drive input, query screen state and pump the engine.
//
// SP and MP are nearly identical here; the few differences are guarded by
// OPENJK_MP (defined only on the MP/codemp build): a handful of force-power
// command strings, the SP-only quicksave/datapad commands, the force-power "known"
// query (a real game-side hook in SP, a stub in MP), and the SP-only in-game
// scripted-cinematic check.
//
// Action/command mapping is ported from the old jk3 port (jk3_old's in_android.cpp).
// Keyboard and mouse-look go through SDL injection (consumed by sdl_input.cpp).
// Gameplay actions become console commands; movement is analog. Both are applied
// on the engine thread via CL_AndroidMove() (called from CL_CreateCmd) so command
// execution and usercmd edits never race the main loop. Only the lightweight queue
// insert / float stores happen on the touch thread, which is benign.

#include "SDL.h"
#include "SDL_scancode.h"

#include "game_interface.h"

// OpenJK client state + command buffer + usercmd_t. client.h resolves its own
// relative includes from its own engine's client/ dir, so it is safe to pull in
// from here. This file lives in the shared mobile/ folder (a sibling of code/ and
// codemp/), so reach into the right engine's client/ for the active build.
#ifdef OPENJK_MP
#include "../codemp/client/client.h"
#else
#include "../code/client/client.h"
#endif

// Android engine entry point, defined in shared/sys/sys_main.cpp (named
// main_android there to avoid SDL's `#define main SDL_main` and the special
// semantics of a real main()).
extern int main_android(int argc, char *argv[]);

// SDL's internal keyboard injection (same approach iortcw / TFE use): pushes a
// key event into SDL's queue under SDL's lock. OpenJK reads it through the normal
// SDL_KEYDOWN/UP path in sdl_input.cpp, so remappable binds keep working.
extern "C" int SDL_SendKeyboardKey(Uint8 state, SDL_Scancode scancode);

// Look sensitivities. Deltas from the touch layer are normalised; these scale
// them to the pixel units MouseMove() forwards to SDL_InjectMouse().
static const float ANDROID_LOOK_MOUSE_X_SCALE = 1000.0f;
static const float ANDROID_LOOK_MOUSE_Y_SCALE =  800.0f;
// Joystick-look would normally be applied every frame while the stick is held;
// without a per-frame look hook we emit on each stick-move event instead, so
// joystick-look mode turns only while the stick is moving (mouse-look mode, the
// default, behaves correctly). Kept small to match the per-frame magnitude.
static const float ANDROID_LOOK_JOY_X_SCALE   =   12.0f;
static const float ANDROID_LOOK_JOY_Y_SCALE   =    8.0f;

// --- Cross-thread plumbing, drained on the engine thread in CL_AndroidMove() ---

// Pending console commands. Single-producer (touch thread) / single-consumer
// (engine thread) ring buffer; same lock-free pattern the old jk3 port used.
#define ANDROID_CMD_QUEUE_LEN 128
static char         s_cmdQueue[ANDROID_CMD_QUEUE_LEN][256];
static volatile int s_cmdAvail = 0;
static volatile int s_cmdUsed  = 0;

// Latest analog move from the touch sticks, each in [-1, 1].
static volatile float s_androidFwd  = 0.0f;
static volatile float s_androidSide = 0.0f;

// Look accumulators (iortcw/TFE style). Written on the touch thread, drained on the
// engine thread in CL_AndroidMove() by forwarding to MouseMove().
//  _mouse: per-swipe deltas - accumulate, then zeroed each frame once applied.
//  _joy  : joystick magnitude - latest value held until the next update, not zeroed.
static volatile float s_lookPitchMouse = 0.0f, s_lookPitchJoy = 0.0f;
static volatile float s_lookYawMouse   = 0.0f, s_lookYawJoy   = 0.0f;

static void postCommand( const char *cmd )
{
	if ( s_cmdAvail >= s_cmdUsed + ANDROID_CMD_QUEUE_LEN )
		return; // queue full, drop
	Q_strncpyz( s_cmdQueue[s_cmdAvail & (ANDROID_CMD_QUEUE_LEN - 1)], cmd, sizeof(s_cmdQueue[0]) );
	s_cmdAvail++;
}

// Queue a +action / -action console command (press/release).
static void buttonCommand( int state, const char *name )
{
	char buf[64];
	Com_sprintf( buf, sizeof(buf), "%c%s", state ? '+' : '-', name );
	postCommand( buf );
}

static bool portableInMenu( void )
{
	const int c = Key_GetCatcher();
	return ( c & KEYCATCH_UI ) || ( c & KEYCATCH_CONSOLE );
}

static void sendKey( int state, SDL_Scancode scancode )
{
	SDL_SendKeyboardKey( state ? SDL_PRESSED : SDL_RELEASED, scancode );
}

// Called every frame from CL_CreateCmd (engine thread). Drains queued commands
// and folds the analog touch movement into the outgoing usercmd.
void CL_AndroidMove( usercmd_t *cmd )
{
	while ( s_cmdUsed != s_cmdAvail )
	{
		// EXEC_NOW: run immediately on this (engine) thread, one whole command,
		// so queued strings never get concatenated in the command buffer.
		Cbuf_ExecuteText( EXEC_NOW, s_cmdQueue[s_cmdUsed & (ANDROID_CMD_QUEUE_LEN - 1)] );
		s_cmdUsed++;
	}

	int fm = cmd->forwardmove + (int)( s_androidFwd  * 127.0f );
	int rm = cmd->rightmove   + (int)( s_androidSide * 127.0f );

	cmd->forwardmove = (signed char)( fm >  127 ?  127 : ( fm < -127 ? -127 : fm ) );
	cmd->rightmove   = (signed char)( rm >  127 ?  127 : ( rm < -127 ? -127 : rm ) );

	// Drain the look accumulators through MouseMove() so they reach the view angles
	// via the normal SDL mouse-motion path (sdl_input.cpp -> CL_MouseMove).
	const float yawPx   = s_lookYawMouse   * ANDROID_LOOK_MOUSE_X_SCALE * 5
	                    + s_lookYawJoy     * ANDROID_LOOK_JOY_X_SCALE;
	const float pitchPx = s_lookPitchMouse * ANDROID_LOOK_MOUSE_Y_SCALE * 5
	                    + s_lookPitchJoy   * ANDROID_LOOK_JOY_Y_SCALE;

	if ( yawPx != 0.0f || pitchPx != 0.0f )
		MouseMove( yawPx, pitchPx );

	// Mouse-mode is per-swipe; zero it so we don't re-apply next frame.
	// Joystick-mode is held; leave it until the touch layer pushes a new value.
	s_lookYawMouse   = 0.0f;
	s_lookPitchMouse = 0.0f;
}

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

// Touch button / stick action. Mapped from PORT_ACT_* onto OpenJK console
// commands (ported from jk3_old/in_android.cpp). In menus, navigation actions are
// injected as SDL keys instead so the UI behaves like a real keyboard/mouse.
void PortableAction(int state, int action)
{
    if (portableInMenu())
    {
        switch (action)
        {
            case PORT_ACT_MENU_UP:      sendKey(state, SDL_SCANCODE_UP);      return;
            case PORT_ACT_MENU_DOWN:    sendKey(state, SDL_SCANCODE_DOWN);    return;
            case PORT_ACT_MENU_LEFT:    sendKey(state, SDL_SCANCODE_LEFT);    return;
            case PORT_ACT_MENU_RIGHT:   sendKey(state, SDL_SCANCODE_RIGHT);   return;
            case PORT_ACT_MENU_SELECT:  sendKey(state, SDL_SCANCODE_RETURN);  return;
            case PORT_ACT_MENU_CONFIRM: sendKey(state, SDL_SCANCODE_Y);       return;
            case PORT_ACT_MENU_BACK:
            case PORT_ACT_MENU_ABORT:
            case PORT_ACT_MENU_SHOW:    sendKey(state, SDL_SCANCODE_ESCAPE);  return;

            case PORT_ACT_MOUSE_LEFT:   MouseButton(state, BUTTON_PRIMARY);   return;
            case PORT_ACT_MOUSE_RIGHT:  MouseButton(state, BUTTON_SECONDARY); return;
        }
        // Otherwise fall through so a gameplay action started before the menu
        // opened still gets its release.
    }

    switch (action)
    {
        // --- Continuous (+/-) movement and combat ---
        case PORT_ACT_LEFT:        buttonCommand(state, "left");       break;
        case PORT_ACT_RIGHT:       buttonCommand(state, "right");      break;
        case PORT_ACT_FWD:         buttonCommand(state, "forward");    break;
        case PORT_ACT_BACK:        buttonCommand(state, "back");       break;
        case PORT_ACT_LOOK_UP:     buttonCommand(state, "lookup");     break;
        case PORT_ACT_LOOK_DOWN:   buttonCommand(state, "lookdown");   break;
        case PORT_ACT_MOVE_LEFT:   buttonCommand(state, "moveleft");   break;
        case PORT_ACT_MOVE_RIGHT:  buttonCommand(state, "moveright");  break;
        case PORT_ACT_STRAFE:      buttonCommand(state, "strafe");     break;
        case PORT_ACT_SPEED:
        case PORT_ACT_SPRINT:
        case PORT_ACT_SMART_TOGGLE_RUN: buttonCommand(state, "speed"); break;
        case PORT_ACT_USE:         buttonCommand(state, "use");        break;
        case PORT_ACT_ATTACK:      buttonCommand(state, "attack");     break;
        case PORT_ACT_ALT_ATTACK:
        case PORT_ACT_ALT_FIRE:    buttonCommand(state, "altattack");  break;
        case PORT_ACT_FORCE_USE:   buttonCommand(state, "useforce");   break;
        case PORT_ACT_JUMP:
        case PORT_ACT_UP:          buttonCommand(state, "moveup");     break;
        case PORT_ACT_CROUCH:
        case PORT_ACT_DOWN:        buttonCommand(state, "movedown");   break;

        // --- One-shot commands (issued on press) ---
        case PORT_ACT_NEXT_WEP:    if (state) postCommand("weapnext");        break;
        case PORT_ACT_PREV_WEP:    if (state) postCommand("weapprev");        break;
#ifndef OPENJK_MP
        case PORT_ACT_QUICKSAVE:   if (state) postCommand("save quick");      break;
        case PORT_ACT_QUICKLOAD:   if (state) postCommand("load quick");      break;
#endif
        case PORT_ACT_INVUSE:      if (state) postCommand("invuse");          break;
        case PORT_ACT_INVPREV:     if (state) postCommand("invprev");         break;
        case PORT_ACT_INVNEXT:     if (state) postCommand("invnext");         break;
        case PORT_ACT_NEXT_FORCE:  if (state) postCommand("forcenext");       break;
        case PORT_ACT_PREV_FORCE:  if (state) postCommand("forceprev");       break;
#ifndef OPENJK_MP
        case PORT_ACT_DATAPAD:
        case PORT_ACT_HELPCOMP:    if (state) postCommand("datapad");         break;
#endif
        case PORT_ACT_SABER_STYLE: if (state) postCommand("saberAttackCycle"); break;
        case PORT_ACT_THIRD_PERSON:if (state) postCommand("cg_thirdperson !"); break;
        case PORT_ACT_SABER_SEL:   if (state) postCommand("weapon 1");        break;

        // --- Force powers (issued on press). SP and MP use different console
        //     command names for several powers. ---
#ifdef OPENJK_MP
        case PORT_ACT_FORCE_PULL:   if (state) postCommand("+force_pull");      break;
        case PORT_ACT_FORCE_MIND:   if (state) postCommand("force_distract");   break;
        case PORT_ACT_FORCE_PUSH:   if (state) postCommand("force_throw");      break;
        case PORT_ACT_FORCE_SPEED:  if (state) postCommand("force_speed");      break;
        case PORT_ACT_FORCE_HEAL:   if (state) postCommand("force_heal");       break;
        case PORT_ACT_FORCE_GRIP:   if (state) postCommand("+force_grip");      break;
        case PORT_ACT_FORCE_LIGHT:  if (state) postCommand("+force_lightning"); break;
        case PORT_ACT_FORCE_DRAIN:  if (state) postCommand("+force_drain");     break;
        case PORT_ACT_FORCE_RAGE:   if (state) postCommand("force_rage");       break;
        case PORT_ACT_FORCE_PROTECT:if (state) postCommand("force_protect");    break;
        case PORT_ACT_FORCE_ABSORB: if (state) postCommand("force_absorb");     break;
        case PORT_ACT_FORCE_SIGHT:  if (state) postCommand("force_seeing");     break;
#else
        case PORT_ACT_FORCE_PULL:   if (state) postCommand("force_pull");      break;
        case PORT_ACT_FORCE_MIND:   if (state) postCommand("force_distract");  break;
        case PORT_ACT_FORCE_PUSH:   if (state) postCommand("force_throw");     break;
        case PORT_ACT_FORCE_SPEED:  if (state) postCommand("force_speed");     break;
        case PORT_ACT_FORCE_HEAL:   if (state) postCommand("force_heal");      break;
        case PORT_ACT_FORCE_GRIP:   if (state) postCommand("force_grip");      break;
        case PORT_ACT_FORCE_LIGHT:  if (state) postCommand("force_lightning"); break;
        case PORT_ACT_FORCE_DRAIN:  if (state) postCommand("force_drain");     break;
        case PORT_ACT_FORCE_RAGE:   if (state) postCommand("force_rage");      break;
        case PORT_ACT_FORCE_PROTECT:if (state) postCommand("force_protect");   break;
        case PORT_ACT_FORCE_ABSORB: if (state) postCommand("force_absorb");    break;
        case PORT_ACT_FORCE_SIGHT:  if (state) postCommand("force_sight");     break;
#endif

        default:
            // Direct weapon select: PORT_ACT_WEAP0..WEAP13 -> "weapon N".
            if (state && action >= PORT_ACT_WEAP0 && action <= PORT_ACT_WEAP13)
            {
                char buf[32];
                Com_sprintf(buf, sizeof(buf), "weapon %d", action - PORT_ACT_WEAP0);
                postCommand(buf);
            }
            // Anything else (RELOAD, KICK, LEAN, vehicle/flight actions, ...) has
            // no clean JKA mapping yet and is intentionally ignored for now.
            break;
    }
}

#ifdef OPENJK_MP

// MP has no client-side "which force powers does the player know" query hook
// (that lives in the server gamecode VM), so the touch UI can't dim force-select
// buttons here. Always report "known" so every button stays active.
bool PortableGetForcePowerKnown(int forceAction)
{
    return false;
}

#else

// Bitmask of force powers the player currently knows. Defined game-side
// (g_svcmds.cpp) where g_entities is available.
extern "C" int Mobile_GetForcePowersKnown(void);

// Map a touch-layer PORT_ACT_FORCE_* action onto JKA's forcePowers_t enum.
static int portForceActToFP(int action)
{
    switch (action)
    {
        case PORT_ACT_FORCE_HEAL:    return FP_HEAL;
        case PORT_ACT_FORCE_MIND:    return FP_TELEPATHY; // "mind trick" / distract
        case PORT_ACT_FORCE_SPEED:   return FP_SPEED;
        case PORT_ACT_FORCE_PUSH:    return FP_PUSH;
        case PORT_ACT_FORCE_PULL:    return FP_PULL;
        case PORT_ACT_FORCE_GRIP:    return FP_GRIP;
        case PORT_ACT_FORCE_LIGHT:   return FP_LIGHTNING;
#ifndef JK2_MODE
        // Jedi Academy-only powers (don't exist in the JK2 build).
        case PORT_ACT_FORCE_DRAIN:   return FP_DRAIN;
        case PORT_ACT_FORCE_RAGE:    return FP_RAGE;
        case PORT_ACT_FORCE_PROTECT: return FP_PROTECT;
        case PORT_ACT_FORCE_ABSORB:  return FP_ABSORB;
        case PORT_ACT_FORCE_SIGHT:   return FP_SEE;
#endif
        default:                     return -1;
    }
}

// Whether the player currently has the given force power (PORT_ACT_FORCE_*).
// Lets the touch UI dim the force-select buttons for powers not yet learned.
bool PortableGetForcePowerKnown(int forceAction)
{
    const int fp = portForceActToFP(forceAction);
    if (fp < 0)
        return false;

    return (Mobile_GetForcePowersKnown() & (1 << fp)) != 0;
}

#endif // OPENJK_MP

void PortableMove(float fwd, float strafe)
{
    PortableMoveFwd(fwd);
    PortableMoveSide(strafe);
}

void PortableMoveFwd(float fwd)
{
    s_androidFwd = (fwd > 1.0f) ? 1.0f : (fwd < -1.0f ? -1.0f : fwd);
}

void PortableMoveSide(float strafe)
{
    s_androidSide = (strafe > 1.0f) ? 1.0f : (strafe < -1.0f ? -1.0f : strafe);
}

// Look pitch (vertical). Saved into the accumulators and drained next frame in
// CL_AndroidMove(); mouse-mode deltas accumulate, joystick-mode holds the latest.
void PortableLookPitch(int mode, float pitch)
{
    if (mode == LOOK_MODE_JOYSTICK)
        s_lookPitchJoy    = pitch;
    else
        s_lookPitchMouse += pitch;
}

// Look yaw (horizontal). Same accumulate-and-drain scheme as PortableLookPitch.
void PortableLookYaw(int mode, float yaw)
{
    if (mode == LOOK_MODE_JOYSTICK)
        s_lookYawJoy    = yaw;
    else
        s_lookYawMouse += yaw;
}

// A direct touch/swipe drag -> mouse-mode look.
void PortableMouse(float dx, float dy)
{
    s_lookYawMouse   += dx;
    s_lookPitchMouse += dy;
}

void PortableMouseAbs(float x, float y)
{
    MouseMoveAbsolute(x, y);
}

void PortableMouseButton(int state, int button, float dx, float dy)
{
    MouseButton(state, button);
}

// Console command from the touch layer (quick commands etc.).
void PortableCommand(const char *cmd)
{
    postCommand(cmd);
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
// shows: console overlay -> TS_CONSOLE, any UI/menu up -> TS_MENU, a cinematic ->
// TS_BLANK (full-screen pass-through layer whose tap sends Enter to skip it), live
// gameplay -> TS_GAME. Anything else (loading, disconnected at the main menu) falls
// back to menu so the on-screen mouse + keyboard stay usable.
touchscreemode_t PortableGetScreenMode()
{
    const int catcher = Key_GetCatcher();

    if (catcher & KEYCATCH_CONSOLE)
        return TS_CONSOLE;

    if (catcher & KEYCATCH_UI)
        return TS_MENU;

#ifdef OPENJK_MP
    // Full-screen ROQ cinematic: normal controls are ignored, so drop to the
    // blank tap-to-skip layer.
    if (cls.state == CA_CINEMATIC)
#else
    // Full-screen ROQ cinematic, or an in-game scripted (Icarus) camera sequence:
    // normal controls are ignored, so drop to the blank tap-to-skip layer.
    if (cls.state == CA_CINEMATIC || CL_IsRunningInGameCinematic())
#endif
        return TS_BLANK;

    if (cls.state == CA_ACTIVE)
        return TS_GAME;

    return TS_MENU;
}

} // extern "C"

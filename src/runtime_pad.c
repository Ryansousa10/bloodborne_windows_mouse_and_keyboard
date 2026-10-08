/* libScePad on SDL3 gamepads, keyboard and mouse. SDL events are pumped by the window thread
 * (gpu/shim/window.cpp); here state is only sampled.
 *
 * Keyboard and mouse follow Dark Souls III on PC (keybinds.ini next to bbport.ini changes
 * them, see actions[] below): WASD move, the mouse turns the camera, LMB / Shift+LMB attack,
 * RMB transform, Shift+RMB or LCtrl firearm, Space dodge/dash (pressed again while dashing:
 * jump), E interact, R item, F blood vial, Q or the middle button lock on, arrows or the
 * wheel switch items/weapons, Tab game menu, G gestures; Enter confirms, Esc goes back. They
 * work together with a gamepad. The mouse turns the camera through a hook in the game's own
 * camera code (runtime_camhook.c), by exact angles as PC games do; without the hook it falls
 * back to acting as the right stick. */
#define _GNU_SOURCE
#include "runtime.h"
#include "gpu/bbgpu.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <time.h>
#include <SDL3/SDL.h>
#include <sys/stat.h>

#define ERR_INVALID_ARG ((int32_t)0x80920001)
#define ERR_INVALID_HANDLE ((int32_t)0x80920003)
#define ERR_ALREADY_OPENED ((int32_t)0x80920004)
#define ERR_NOT_INITIALIZED ((int32_t)0x80920005)
#define PAD_HANDLE 1

enum {
    BTN_L3=0x2, BTN_R3=0x4, BTN_OPTIONS=0x8, BTN_UP=0x10, BTN_RIGHT=0x20, BTN_DOWN=0x40, BTN_LEFT=0x80,
    BTN_L2=0x100, BTN_R2=0x200, BTN_L1=0x400, BTN_R1=0x800, BTN_TRIANGLE=0x1000, BTN_CIRCLE=0x2000,
    BTN_CROSS=0x4000, BTN_SQUARE=0x8000, BTN_TOUCHPAD=0x100000,
};
typedef struct { uint16_t x, y; uint8_t id, reserve[3]; } PadTouch;
typedef struct {
    uint32_t buttons;
    uint8_t left_x, left_y, right_x, right_y;
    uint8_t l2, r2, analog_padding[2];
    float orientation[4], acceleration[3], angular_velocity[3];
    uint8_t touch_count, touch_reserve[3];
    uint32_t touch_held_time;
    PadTouch touches[2];
    uint8_t connected, pad0[3];
    uint64_t timestamp;
    uint8_t extension[16];
    uint8_t connected_count, reserve[2], unique_length, unique[12];
} PadData;
typedef struct {
    float pixel_density; uint16_t resolution_x, resolution_y;
    uint8_t dead_zone_left, dead_zone_right, connection_type, connected_count;
    uint8_t connected, pad[3];
    int32_t device_class;
    uint8_t reserve[8];
} ControllerInfo;
_Static_assert(sizeof(PadData)==120,"OrbisPadData layout");
_Static_assert(sizeof(PadTouch)==8,"OrbisPadTouch layout");
_Static_assert(__builtin_offsetof(PadData,touches)==60,"OrbisPadData touch offset");
_Static_assert(__builtin_offsetof(PadData,timestamp)==80,"OrbisPadData timestamp offset");
_Static_assert(sizeof(ControllerInfo)==28,"OrbisPadControllerInformation layout");

static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
static int initialized, opened, sdl_ready;
static SDL_Gamepad *gamepad;
static size_t reads;
static uint8_t connected_count;

static uint64_t now_us(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t); return (uint64_t)t.tv_sec*1000000u+(uint64_t)t.tv_nsec/1000u; }
static uint8_t axis(int16_t v) { int x=(v+32768)>>8; return (uint8_t)(x<0 ? 0 : x>255 ? 255 : x); }
static uint8_t trigger(int16_t v) { int x=v>>7; return (uint8_t)(x<0 ? 0 : x>255 ? 255 : x); }
static uint16_t touch_axis(float v, int max) {
    return (uint16_t)(v<=0.0f ? 0 : v>=1.0f ? max : (int)(v*max+0.5f));
}
static void touch_click(PadData *d, int right) {
    d->buttons|=BTN_TOUCHPAD;
    d->touch_count=1;
    d->touches[0]=(PadTouch){.x=right ? 1440 : 480,.y=471,.id=0};
}

/* Opens the first gamepad SDL knows about; called under lock. */
static SDL_Gamepad *current_gamepad(void) {
    if (!sdl_ready) sdl_ready = SDL_WasInit(SDL_INIT_GAMEPAD) ? 1 : SDL_InitSubSystem(SDL_INIT_GAMEPAD) ? 1 : -1;
    if (sdl_ready<0) return NULL;
    if (gamepad && !SDL_GamepadConnected(gamepad)) { SDL_CloseGamepad(gamepad); gamepad=NULL; }
    if (!gamepad) {
        int count=0;
        SDL_JoystickID *ids=SDL_GetGamepads(&count);
        if (ids && count>0) {
            gamepad=SDL_OpenGamepad(ids[0]);
            if (gamepad) { ++connected_count; printf("Runtime: gamepad connected: %s\n",SDL_GetGamepadName(gamepad)); }
        }
        SDL_free(ids);
    }
    return gamepad;
}
/* Keyboard and mouse bindings. An action is held while any of its inputs is: a key (SDL
 * scancode name, '_' for spaces), mouse_left/right/middle/x1/x2 or wheel_up/down (a short
 * press per step), each optionally after shift+, ctrl+ or alt+. A plain input yields to a
 * combination on the same key: Shift+LMB is R2 alone, not R1 as well. */
enum { IN_KEY, IN_MOUSE, IN_WHEEL };
enum { MOD_SHIFT=1, MOD_CTRL=2, MOD_ALT=4 };
typedef struct { uint8_t kind, mods; int16_t code; } Input; /* scancode, SDL button, wheel step +1/-1 */
enum {
    A_CROSS, A_CIRCLE, A_SQUARE, A_TRIANGLE, A_R1, A_R2, A_L1, A_L2, A_L3, A_R3, A_OPTIONS,
    A_TOUCHPAD, A_TOUCHPAD_RIGHT, A_UP, A_DOWN, A_LEFT, A_RIGHT,
    A_MOVE_FORWARD, A_MOVE_BACK, A_MOVE_LEFT, A_MOVE_RIGHT,
    A_CAMERA_UP, A_CAMERA_DOWN, A_CAMERA_LEFT, A_CAMERA_RIGHT, A_WALK, A_COUNT
};
/* Defaults: the Dark Souls III PC keys of the same pad buttons. */
static const struct { const char *name; uint32_t ps; const char *inputs, *what; } actions[A_COUNT]={
    [A_CROSS]={"cross",BTN_CROSS,"e enter","interact (DS3: E); confirm in menus"},
    [A_CIRCLE]={"circle",BTN_CIRCLE,"space escape","dodge, hold to dash, again while dashing: jump (DS3: Space); back in menus"},
    [A_SQUARE]={"square",BTN_SQUARE,"r","use quick item (DS3: R)"},
    [A_TRIANGLE]={"triangle",BTN_TRIANGLE,"f","blood vial (DS3: F, two-hand, the same button)"},
    [A_R1]={"r1",BTN_R1,"mouse_left","attack (DS3: right hand attack)"},
    [A_R2]={"r2",BTN_R2,"shift+mouse_left","strong attack (DS3: right hand strong attack)"},
    [A_L1]={"l1",BTN_L1,"mouse_right","transform weapon (DS3: left hand attack / guard)"},
    [A_L2]={"l2",BTN_L2,"shift+mouse_right lctrl","firearm (DS3: left hand strong attack / skill)"},
    [A_L3]={"l3",BTN_L3,"c","jump while dashing (also Space, see ds3_jump)"},
    [A_R3]={"r3",BTN_R3,"q mouse_middle","lock on / reset camera (DS3: Q, wheel click)"},
    [A_OPTIONS]={"options",BTN_OPTIONS,"tab","game menu (Esc is back: the game has one button for back and dodge)"},
    [A_TOUCHPAD]={"touchpad",BTN_TOUCHPAD,"g","gestures (DS3: G); left half of the touchpad"},
    [A_TOUCHPAD_RIGHT]={"touchpad_right",0,"backspace","right half of the touchpad (debug menu)"},
    [A_UP]={"up",BTN_UP,"up wheel_up","d-pad up (DS3: switch spells)"},
    [A_DOWN]={"down",BTN_DOWN,"down wheel_down","switch quick item (DS3: switch items)"},
    [A_LEFT]={"left",BTN_LEFT,"left shift+wheel_down","switch left hand weapon"},
    [A_RIGHT]={"right",BTN_RIGHT,"right shift+wheel_up","switch right hand weapon"},
    [A_MOVE_FORWARD]={"move_forward",0,"w","left stick"},
    [A_MOVE_BACK]={"move_back",0,"s","left stick"},
    [A_MOVE_LEFT]={"move_left",0,"a","left stick"},
    [A_MOVE_RIGHT]={"move_right",0,"d","left stick"},
    [A_CAMERA_UP]={"camera_up",0,"i","right stick, besides the mouse"},
    [A_CAMERA_DOWN]={"camera_down",0,"k","right stick, besides the mouse"},
    [A_CAMERA_LEFT]={"camera_left",0,"j","right stick, besides the mouse"},
    [A_CAMERA_RIGHT]={"camera_right",0,"l","right stick, besides the mouse"},
    [A_WALK]={"walk",0,"lalt","walk: the movement keys tilt the stick by walk_tilt (DS3: Left Alt)"},
};
#define MAX_INPUTS 6
static Input binds[A_COUNT][MAX_INPUTS];
static int bind_count[A_COUNT];
static struct {
    int mouse_camera, invert_x, invert_y, ds3_jump, no_auto_rotation;
    float sensitivity, sensitivity_y, walk_tilt;
    /* The stick fallback (no camera hook: another game version, Linux): fixed. */
    float deadzone, curve, smoothing_ms;
} kbm={1,0,0,1,1,1.0f,1.0f,0.4f,0.25f,1.0f,8.0f};

static int parse_input(const char *s, Input *in) {
    in->mods=0;
    for (;;) {
        if (!SDL_strncasecmp(s,"shift+",6)) { in->mods|=MOD_SHIFT; s+=6; }
        else if (!SDL_strncasecmp(s,"ctrl+",5)) { in->mods|=MOD_CTRL; s+=5; }
        else if (!SDL_strncasecmp(s,"alt+",4)) { in->mods|=MOD_ALT; s+=4; }
        else break;
    }
    static const struct { const char *name; uint8_t kind; int16_t code; } named[]={
        {"mouse_left",IN_MOUSE,SDL_BUTTON_LEFT}, {"mouse_right",IN_MOUSE,SDL_BUTTON_RIGHT},
        {"mouse_middle",IN_MOUSE,SDL_BUTTON_MIDDLE}, {"mouse_x1",IN_MOUSE,SDL_BUTTON_X1},
        {"mouse_x2",IN_MOUSE,SDL_BUTTON_X2}, {"wheel_up",IN_WHEEL,1}, {"wheel_down",IN_WHEEL,-1},
        {"lshift",IN_KEY,SDL_SCANCODE_LSHIFT}, {"rshift",IN_KEY,SDL_SCANCODE_RSHIFT},
        {"lctrl",IN_KEY,SDL_SCANCODE_LCTRL}, {"rctrl",IN_KEY,SDL_SCANCODE_RCTRL},
        {"lalt",IN_KEY,SDL_SCANCODE_LALT}, {"ralt",IN_KEY,SDL_SCANCODE_RALT},
        {"enter",IN_KEY,SDL_SCANCODE_RETURN}, {"esc",IN_KEY,SDL_SCANCODE_ESCAPE},
        /* Punctuation by name: ',' separates inputs (the launcher writes these). */
        {"comma",IN_KEY,SDL_SCANCODE_COMMA}, {"period",IN_KEY,SDL_SCANCODE_PERIOD},
        {"slash",IN_KEY,SDL_SCANCODE_SLASH}, {"semicolon",IN_KEY,SDL_SCANCODE_SEMICOLON},
        {"apostrophe",IN_KEY,SDL_SCANCODE_APOSTROPHE}, {"leftbracket",IN_KEY,SDL_SCANCODE_LEFTBRACKET},
        {"rightbracket",IN_KEY,SDL_SCANCODE_RIGHTBRACKET}, {"minus",IN_KEY,SDL_SCANCODE_MINUS},
        {"equals",IN_KEY,SDL_SCANCODE_EQUALS}, {"grave",IN_KEY,SDL_SCANCODE_GRAVE},
        {"backslash",IN_KEY,SDL_SCANCODE_BACKSLASH}, {"pageup",IN_KEY,SDL_SCANCODE_PAGEUP},
        {"pagedown",IN_KEY,SDL_SCANCODE_PAGEDOWN},
    };
    for (size_t i=0;i<sizeof(named)/sizeof(*named);++i)
        if (!SDL_strcasecmp(s,named[i].name)) { in->kind=named[i].kind; in->code=named[i].code; return 1; }
    char name[64];
    snprintf(name,sizeof(name),"%s",s);
    for (char *c=name;*c;++c) if (*c=='_') *c=' ';
    const SDL_Scancode code=SDL_GetScancodeFromName(name);
    if (code==SDL_SCANCODE_UNKNOWN) return 0;
    in->kind=IN_KEY; in->code=(int16_t)code;
    return 1;
}
static void parse_binds(int action, const char *list) {
    char copy[256], *save=NULL;
    snprintf(copy,sizeof(copy),"%s",list);
    bind_count[action]=0;
    for (char *t=SDL_strtok_r(copy," ,\t",&save);t;t=SDL_strtok_r(NULL," ,\t",&save)) {
        Input in;
        if (!parse_input(t,&in)) printf("Runtime: keybinds: unknown input '%s' for %s\n",t,actions[action].name);
        else if (bind_count[action]<MAX_INPUTS) binds[action][bind_count[action]++]=in;
    }
}
static void write_keybinds(const char *path) {
    FILE *f=fopen(path,"w");
    if (!f) return;
    fputs("# bbport keyboard and mouse, Dark Souls III layout. Restart the game after editing;\n"
          "# delete this file to get the defaults back.\n"
          "# action = inputs separated by spaces: a key (a..z, 0..9, space, escape, tab, up,\n"
          "# lshift, lctrl, lalt, f1.., other SDL key names with _ for spaces), mouse_left,\n"
          "# mouse_right, mouse_middle, mouse_x1, mouse_x2, wheel_up, wheel_down; each may start\n"
          "# with shift+, ctrl+ or alt+. Empty: unbound. The gamepad works alongside.\n"
          "# The launcher (Controls page) edits this file too.\n\nkeybinds_version = 3\n\n",f);
    for (int a=0;a<A_COUNT;++a) fprintf(f,"%-15s= %-26s # %s\n",actions[a].name,actions[a].inputs,actions[a].what);
    fprintf(f,"\n# The mouse turns the camera while the game window has focus.\n"
              "mouse_camera           = %d\n"
              "# Degrees per mouse count = 0.022 x sensitivity, the scale of Source games (Deadlock,\n"
              "# Counter-Strike): their sensitivity value turns the camera as far here. The vertical\n"
              "# one multiplies it.\n"
              "mouse_sensitivity      = %.2f\nmouse_sensitivity_y    = %.2f\n"
              "mouse_invert_x         = %d\nmouse_invert_y         = %d\n"
              "# 1: the game does not turn the camera by itself while the character moves (the\n"
              "# community patch \"Disable Camera Auto Rotation via Movement\").\n"
              "mouse_no_auto_rotation = %d\n"
              "# Stick tilt of the movement keys while walk is held, 0.1..1.\n"
              "walk_tilt              = %.2f\n"
              "# 1: pressing circle again while dashing jumps (taps l3), as Space in Dark Souls III;\n"
              "# 0: it does the game's sprint roll.\n"
              "ds3_jump               = %d\n",
            kbm.mouse_camera,kbm.sensitivity,kbm.sensitivity_y,kbm.invert_x,kbm.invert_y,
            kbm.no_auto_rotation,kbm.walk_tilt,kbm.ds3_jump);
    fclose(f);
    printf("Runtime: keybinds: wrote the defaults to %s\n",path);
}
static char *trim(char *s) {
    while (*s==' ' || *s=='\t') ++s;
    char *e=s+strlen(s);
    while (e>s && (e[-1]==' ' || e[-1]=='\t' || e[-1]=='\r' || e[-1]=='\n')) *--e=0;
    return s;
}
/* `name` in the folder of `path`. */
static void sibling(char *out, size_t size, const char *path, const char *name) {
    snprintf(out,size,"%s",path);
    char *slash=strrchr(out,'/'), *back=strrchr(out,'\\');
    if (back>slash) slash=back;
    snprintf(slash ? slash+1 : out,size-(size_t)(slash ? slash+1-out : 0),"%s",name);
}
/* keybinds.ini next to bbport.ini (BB_CONFIG), or BB_KEYBINDS; written with the defaults when
 * missing. Without either variable (tests) only the defaults are used. */
static void load_keybinds(void) {
    for (int a=0;a<A_COUNT;++a) parse_binds(a,actions[a].inputs);
    char path[1024];
    const char *env=getenv("BB_KEYBINDS"), *config=getenv("BB_CONFIG");
    if (env && *env) snprintf(path,sizeof(path),"%s",env);
    else if (config && *config) sibling(path,sizeof(path),config,"keybinds.ini");
    else return;
    FILE *f=fopen(path,"r");
    if (!f) { write_keybinds(path); return; }
    /* Keys of earlier versions (the stick tuning, a camera finder) are ignored. */
    int legacy_sensitivity=0;
    static const char *const retired[]={"keybinds_version","mouse_deadzone","mouse_curve","mouse_smoothing_ms",
                                        "mouse_direct","mouse_hold_pitch"};
    char line[512];
    while (fgets(line,sizeof(line),f)) {
        char *hash=strchr(line,'#'), *eq=strchr(line,'=');
        if (hash) *hash=0;
        if (!eq || (hash && eq>hash)) continue;
        *eq=0;
        const char *key=trim(line), *value=trim(eq+1);
        int known=0;
        for (int a=0;a<A_COUNT;++a) if (!SDL_strcasecmp(key,actions[a].name)) { parse_binds(a,value); known=1; }
        for (size_t i=0;i<sizeof(retired)/sizeof(*retired);++i) known|=!SDL_strcasecmp(key,retired[i]);
        known|=!SDL_strcasecmp(key,"mouse_sensitivity"); /* also when mouse_direct_sensitivity won */
        const float v=(float)atof(value);
        if (!SDL_strcasecmp(key,"mouse_camera")) kbm.mouse_camera=v!=0;
        /* mouse_direct_sensitivity (version 2: the mouse camera's, apart from a stick one) wins
         * over mouse_sensitivity wherever it stands. */
        else if (!SDL_strcasecmp(key,"mouse_direct_sensitivity") ||
                 (!SDL_strcasecmp(key,"mouse_sensitivity") && !legacy_sensitivity)) {
            legacy_sensitivity|=!SDL_strcasecmp(key,"mouse_direct_sensitivity");
            kbm.sensitivity=v<0.01f ? 0.01f : v>20 ? 20 : v;
        }
        else if (!SDL_strcasecmp(key,"mouse_sensitivity_y")) kbm.sensitivity_y=v<0.1f ? 0.1f : v>10 ? 10 : v;
        else if (!SDL_strcasecmp(key,"mouse_invert_x")) kbm.invert_x=v!=0;
        else if (!SDL_strcasecmp(key,"mouse_invert_y")) kbm.invert_y=v!=0;
        else if (!SDL_strcasecmp(key,"mouse_no_auto_rotation")) kbm.no_auto_rotation=v!=0;
        else if (!SDL_strcasecmp(key,"walk_tilt")) kbm.walk_tilt=v<0.1f ? 0.1f : v>1 ? 1 : v;
        else if (!SDL_strcasecmp(key,"ds3_jump")) kbm.ds3_jump=v!=0;
        else if (!known) printf("Runtime: keybinds: unknown setting '%s'\n",key);
    }
    fclose(f);
    printf("Runtime: keybinds from %s\n",path);
}

static int held_mods(const bool *k) {
    if (!k) return 0;
    return (k[SDL_SCANCODE_LSHIFT] || k[SDL_SCANCODE_RSHIFT] ? MOD_SHIFT : 0) |
           (k[SDL_SCANCODE_LCTRL] || k[SDL_SCANCODE_RCTRL] ? MOD_CTRL : 0) |
           (k[SDL_SCANCODE_LALT] || k[SDL_SCANCODE_RALT] ? MOD_ALT : 0);
}
/* The input's modifiers are held and no held combination on the same key takes precedence. */
static int mods_match(const Input *in, int mods) {
    if ((in->mods & mods)!=in->mods) return 0;
    for (int a=0;a<A_COUNT;++a) for (int i=0;i<bind_count[a];++i) {
        const Input *o=&binds[a][i];
        if (o->kind==in->kind && o->code==in->code && o->mods!=in->mods &&
            (o->mods & in->mods)==in->mods && (o->mods & mods)==o->mods) return 0;
    }
    return 1;
}
static int action_held(int action, const bool *k, uint32_t mouse, int mods) {
    for (int i=0;i<bind_count[action];++i) {
        const Input *in=&binds[action][i];
        if (in->kind==IN_WHEEL || !mods_match(in,mods)) continue;
        if (in->kind==IN_KEY ? k && k[in->code] : (mouse & SDL_BUTTON_MASK(in->code))!=0) return 1;
    }
    return 0;
}
/* Wheel steps become short presses, queued so that fast scrolling is not lost. */
#define PULSE_ON_US 60000
#define PULSE_GAP_US 50000
static uint8_t pulse_queue[A_COUNT];
static uint64_t pulse_until[A_COUNT];
static void wheel_step(int dir, int mods) {
    for (int a=0;a<A_COUNT;++a) for (int i=0;i<bind_count[a];++i) {
        const Input *in=&binds[a][i];
        if (in->kind==IN_WHEEL && in->code==dir && mods_match(in,mods) && pulse_queue[a]<4) { ++pulse_queue[a]; break; }
    }
}
static int pulse_held(int action, uint64_t now) {
    if (now>=pulse_until[action] && pulse_queue[action]) {
        --pulse_queue[action];
        pulse_until[action]=now+PULSE_ON_US+PULSE_GAP_US;
    }
    return now+PULSE_GAP_US<pulse_until[action];
}

static uint8_t stick_byte(float v) {
    const int x=128+(int)lroundf(v*(v<0 ? 128.0f : 127.0f));
    return (uint8_t)(x<0 ? 0 : x>255 ? 255 : x);
}
static void key_stick(uint8_t *x, uint8_t *y, int left, int right, int up, int down, float tilt) {
    if (!(left || right || up || down)) return;
    *x=stick_byte((float)(right-left)*tilt);
    *y=stick_byte((float)(down-up)*tilt);
}
/* The stick fallback, when the camera hook cannot be installed (another game version, Linux):
 * mouse motion as the right stick. The game turns the camera by the tilt, so the tilt follows
 * the mouse speed (MOUSE_FULL_PX_S at sensitivity 1 tilts it fully), averaged over
 * smoothing_ms because the game and the mouse sample at different rates; tilts start at the
 * game's deadzone. */
#define MOUSE_FULL_PX_S 500.0f
/* The camera hook (runtime_camhook.c): degrees per mouse count = 0.022 x sensitivity, the scale
 * of Source games (Deadlock, Counter-Strike: m_yaw 0.022), so their sensitivity carries over. */
#define HOOK_RADIANS_PER_COUNT (0.022*3.14159265358979323846/180.0)
/* The game's angles against the turn asked for (right and up positive). */
#define HOOK_YAW_SIGN 1
#define HOOK_PITCH_SIGN -1 /* the game's pitch grows looking down */
static float mouse_vx, mouse_vy;
static uint64_t mouse_last;
static void mouse_stick(PadData *d, float dx, float dy, uint64_t now) {
    float dt=mouse_last ? (float)(now-mouse_last)*1e-6f : 0.016f;
    mouse_last=now;
    dt=dt<0.0005f ? 0.0005f : dt>0.1f ? 0.1f : dt;
    const float blend=kbm.smoothing_ms>0 ? 1.0f-expf(-dt*1000.0f/kbm.smoothing_ms) : 1.0f;
    mouse_vx+=(dx/dt-mouse_vx)*blend;
    mouse_vy+=(dy/dt-mouse_vy)*blend;
    float sx=mouse_vx*kbm.sensitivity/MOUSE_FULL_PX_S;
    float sy=mouse_vy*kbm.sensitivity*kbm.sensitivity_y/MOUSE_FULL_PX_S;
    if (kbm.invert_x) sx=-sx;
    if (kbm.invert_y) sy=-sy;
    const float m=sqrtf(sx*sx+sy*sy);
    if (m<0.02f) return;
    const float tilt=kbm.deadzone+(1.0f-kbm.deadzone)*powf(m>1.0f ? 1.0f : m,kbm.curve);
    d->right_x=stick_byte(sx*tilt/m);
    d->right_y=stick_byte(sy*tilt/m);
}
/* Dark Souls III jumps with a new press of the dash key while dashing, Bloodborne with L3:
 * Circle pressed again soon after a dash stays held through the gap and L3 is tapped. */
#define DASH_MIN_US 300000
#define JUMP_GAP_US 200000
#define JUMP_TAP_US 80000
static void ds3_jump(int *circle, int *l3, uint64_t now) {
    static int was, dashed;
    static uint64_t down_at, up_at, l3_until;
    if (*circle && !was) {
        if (dashed && now-up_at<JUMP_GAP_US) l3_until=now+JUMP_TAP_US; /* the dash goes on */
        else down_at=now;
    } else if (!*circle && was) {
        dashed=now-down_at>=DASH_MIN_US;
        up_at=now;
    }
    was=*circle;
    if (!*circle && dashed && now-up_at<JUMP_GAP_US) *circle=1;
    if (now<l3_until) *l3=1;
}
static void sample_keyboard_mouse(PadData *d, const bool *k) {
    const uint64_t now=now_us();
    float dx=0, dy=0, wheel=0;
    uint32_t mouse=0;
    bbgpu_mouse_enable(kbm.mouse_camera); /* the window may open after the pad */
    const int captured=bbgpu_mouse_take(&dx,&dy,&wheel,&mouse);
    const int mods=held_mods(k);
    static float wheel_rest;
    wheel_rest=captured ? wheel_rest+wheel : 0;
    for (;wheel_rest>=1.0f;wheel_rest-=1.0f) wheel_step(1,mods);
    for (;wheel_rest<=-1.0f;wheel_rest+=1.0f) wheel_step(-1,mods);
    int held[A_COUNT];
    for (int a=0;a<A_COUNT;++a) held[a]=action_held(a,k,mouse,mods) | pulse_held(a,now);
    if (kbm.ds3_jump) ds3_jump(&held[A_CIRCLE],&held[A_L3],now);
    for (int a=0;a<A_COUNT;++a) if (held[a]) d->buttons|=actions[a].ps;
    if (held[A_TOUCHPAD]) touch_click(d,0);
    if (held[A_TOUCHPAD_RIGHT]) touch_click(d,1);
    if (held[A_L2]) d->l2=255;
    if (held[A_R2]) d->r2=255;
    key_stick(&d->left_x,&d->left_y,held[A_MOVE_LEFT],held[A_MOVE_RIGHT],held[A_MOVE_FORWARD],
              held[A_MOVE_BACK],held[A_WALK] ? kbm.walk_tilt : 1.0f);
    key_stick(&d->right_x,&d->right_y,held[A_CAMERA_LEFT],held[A_CAMERA_RIGHT],held[A_CAMERA_UP],
              held[A_CAMERA_DOWN],1.0f);
    if (captured && !runtime_camhook_active()) {
        static int told;
        if ((dx!=0 || dy!=0) && !told) {
            told=1;
            const char *l=getenv("BB_UI_LANGUAGE");
            bbgpu_notify(l && !strncmp(l,"pt",2)
                             ? "Câmera de PC indisponível nesta versão do jogo; o mouse gira a câmera como um analógico"
                             : "The PC mouse camera is not available for this game version; the mouse turns the camera as a stick",
                         10);
        }
        mouse_stick(d,dx,dy,now);
    } else { mouse_vx=mouse_vy=0; mouse_last=0; } /* the hook gets the motion from the window */
}

static void sample_host(PadData *d) {
    memset(d,0,sizeof(*d));
    d->left_x=d->left_y=d->right_x=d->right_y=128;
    d->orientation[3]=1.0f;
    d->connected=1; d->connected_count=connected_count ? connected_count : 1;
    d->timestamp=now_us();
    SDL_Gamepad *g=current_gamepad();
    if (bbgpu_overlay_captures_input()) return; /* settings menu open: neutral input */
    const bool *k=SDL_WasInit(SDL_INIT_VIDEO) ? SDL_GetKeyboardState(NULL) : NULL;
    if (g) {
        static const struct { SDL_GamepadButton sdl; uint32_t ps; } map[]={
            {SDL_GAMEPAD_BUTTON_SOUTH,BTN_CROSS}, {SDL_GAMEPAD_BUTTON_EAST,BTN_CIRCLE},
            {SDL_GAMEPAD_BUTTON_WEST,BTN_SQUARE}, {SDL_GAMEPAD_BUTTON_NORTH,BTN_TRIANGLE},
            {SDL_GAMEPAD_BUTTON_LEFT_SHOULDER,BTN_L1}, {SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER,BTN_R1},
            {SDL_GAMEPAD_BUTTON_LEFT_STICK,BTN_L3}, {SDL_GAMEPAD_BUTTON_RIGHT_STICK,BTN_R3},
            {SDL_GAMEPAD_BUTTON_START,BTN_OPTIONS}, {SDL_GAMEPAD_BUTTON_BACK,BTN_TOUCHPAD},
            {SDL_GAMEPAD_BUTTON_TOUCHPAD,BTN_TOUCHPAD},
            {SDL_GAMEPAD_BUTTON_DPAD_UP,BTN_UP}, {SDL_GAMEPAD_BUTTON_DPAD_DOWN,BTN_DOWN},
            {SDL_GAMEPAD_BUTTON_DPAD_LEFT,BTN_LEFT}, {SDL_GAMEPAD_BUTTON_DPAD_RIGHT,BTN_RIGHT},
        };
        for (size_t i=0;i<sizeof(map)/sizeof(*map);++i) if (SDL_GetGamepadButton(g,map[i].sdl)) d->buttons|=map[i].ps;
        d->left_x=axis(SDL_GetGamepadAxis(g,SDL_GAMEPAD_AXIS_LEFTX)); d->left_y=axis(SDL_GetGamepadAxis(g,SDL_GAMEPAD_AXIS_LEFTY));
        d->right_x=axis(SDL_GetGamepadAxis(g,SDL_GAMEPAD_AXIS_RIGHTX)); d->right_y=axis(SDL_GetGamepadAxis(g,SDL_GAMEPAD_AXIS_RIGHTY));
        d->l2=trigger(SDL_GetGamepadAxis(g,SDL_GAMEPAD_AXIS_LEFT_TRIGGER)); d->r2=trigger(SDL_GetGamepadAxis(g,SDL_GAMEPAD_AXIS_RIGHT_TRIGGER));
        if (d->l2>30) d->buttons|=BTN_L2;
        if (d->r2>30) d->buttons|=BTN_R2;
        if (SDL_GetNumGamepadTouchpads(g)>0) {
            const int fingers=SDL_GetNumGamepadTouchpadFingers(g,0);
            for (int finger=0;finger<fingers && d->touch_count<2;++finger) {
                bool down=false;
                float x=0, y=0;
                if (SDL_GetGamepadTouchpadFinger(g,0,finger,&down,&x,&y,NULL) && down) {
                    d->touches[d->touch_count++]=(PadTouch){.x=touch_axis(x,1919),
                        .y=touch_axis(y,942),.id=(uint8_t)finger};
                }
            }
        }
        // Back/Select on pads without a touch surface is a left-side click.
        if ((d->buttons & BTN_TOUCHPAD) && !d->touch_count) touch_click(d,0);
    }
    /* Keyboard and mouse add to the gamepad (an unused controller SDL sees, such as a virtual
     * one, must not switch them off); keys and a moving mouse override its sticks. */
    sample_keyboard_mouse(d,k);
}

/* BB_PAD_FILE=<file>: scripted input for automated runs. The file holds whitespace-separated
 * tokens, re-read when it changes: button names (cross circle square triangle l1 r1 l2 r2 l3 r3
 * options touchpad touchpad_left touchpad_right up down left right) are held while listed;
 * touchpad defaults to a left-side click; lx= ly= rx= ry= (0..255) override
 * the sticks. An empty file releases everything. */
static struct { uint32_t buttons; int stick[4]; int touch_side; } injected={0,{-1,-1,-1,-1},-1};
static int replay_armed;      /* 1 while a BB_PAD_REPLAY recording plays, 2 once it ended */
static uint64_t replay_start; /* 0: (re)start at the next sample */
static void read_inject(void) {
    static const char *path; static int checked; static uint64_t last_check; static struct timespec mtime;
    if (!checked) { path=getenv("BB_PAD_FILE"); checked=1; }
    if (!path || !*path) return;
    uint64_t now=now_us();
    if (now-last_check<20000) return;
    last_check=now;
    struct stat st;
    if (stat(path,&st)!=0) return;
#ifdef _WIN32
    /* Whole-second mtimes: the size tells two edits within a second apart. */
    if (st.st_mtime==mtime.tv_sec && st.st_size==mtime.tv_nsec) return;
    mtime=(struct timespec){st.st_mtime,(long)st.st_size};
#else
    if (st.st_mtim.tv_sec==mtime.tv_sec && st.st_mtim.tv_nsec==mtime.tv_nsec) return;
    mtime=st.st_mtim;
#endif
    FILE *f=fopen(path,"r");
    if (!f) return;
    static const struct { const char *name; uint32_t ps; } names[]={
        {"cross",BTN_CROSS}, {"circle",BTN_CIRCLE}, {"square",BTN_SQUARE}, {"triangle",BTN_TRIANGLE},
        {"l1",BTN_L1}, {"r1",BTN_R1}, {"l2",BTN_L2}, {"r2",BTN_R2}, {"l3",BTN_L3}, {"r3",BTN_R3},
        {"options",BTN_OPTIONS}, {"touchpad",BTN_TOUCHPAD},
        {"up",BTN_UP}, {"down",BTN_DOWN}, {"left",BTN_LEFT}, {"right",BTN_RIGHT},
    };
    static const char *sticks[]={"lx=","ly=","rx=","ry="};
    injected.buttons=0;
    injected.touch_side=-1;
    for (int i=0;i<4;++i) injected.stick[i]=-1;
    char token[64];
    while (fscanf(f,"%63s",token)==1) {
        if (!strcmp(token,"replay") && replay_armed!=1) { replay_armed=1; replay_start=0; } /* BB_PAD_REPLAY */
        if (!strcmp(token,"touchpad_left") || !strcmp(token,"touchpad_right")) {
            injected.buttons|=BTN_TOUCHPAD;
            injected.touch_side=!strcmp(token,"touchpad_right");
        }
        for (size_t i=0;i<sizeof(names)/sizeof(*names);++i) if (!strcmp(token,names[i].name)) injected.buttons|=names[i].ps;
        for (int i=0;i<4;++i) if (!strncmp(token,sticks[i],3)) { int v=atoi(token+3); injected.stick[i]=v<0 ? 0 : v>255 ? 255 : v; }
    }
    fclose(f);
    printf("Runtime: pad file: buttons 0x%x sticks %d %d %d %d\n",injected.buttons,
           injected.stick[0],injected.stick[1],injected.stick[2],injected.stick[3]);
}
/* BB_PAD_RECORD=<file>: F9 starts and stops recording the pad state (gamepad or keyboard) with
 * the time since F9; BB_PAD_REPLAY=<file> plays such a recording back, started by the token
 * "replay" in BB_PAD_FILE (scripted tests repeat a route the player ran once). Lines: ms buttons
 * lx ly rx ry l2 r2, written when the state changes. */
typedef struct { uint32_t ms, buttons; uint8_t axes[4], l2, r2; } PadSample;
static FILE *record_file;
static uint64_t record_start;
static PadSample record_last;
static void record_sample(const PadData *d) {
    static const char *path; static int checked, f9_was_down;
    if (!checked) { path=getenv("BB_PAD_RECORD"); checked=1; }
    if (!path || !*path || !sdl_ready) return;
    const bool *k=SDL_GetKeyboardState(NULL);
    const int f9=k && k[SDL_SCANCODE_F9];
    if (f9 && !f9_was_down) {
        if (record_file) {
            fclose(record_file); record_file=NULL;
            printf("Runtime: pad recording stopped (%s)\n",path);
        } else if ((record_file=fopen(path,"w"))) {
            record_start=now_us();
            memset(&record_last,0xff,sizeof(record_last));
            printf("Runtime: pad recording started (%s, F9 stops)\n",path);
        }
    }
    f9_was_down=f9;
    if (!record_file) return;
    PadSample s={(uint32_t)((now_us()-record_start)/1000),d->buttons,
                 {d->left_x,d->left_y,d->right_x,d->right_y},d->l2,d->r2};
    if (s.buttons==record_last.buttons && !memcmp(s.axes,record_last.axes,4) &&
        s.l2==record_last.l2 && s.r2==record_last.r2) return;
    record_last=s;
    fprintf(record_file,"%u %u %u %u %u %u %u %u\n",s.ms,s.buttons,s.axes[0],s.axes[1],s.axes[2],
            s.axes[3],s.l2,s.r2);
    fflush(record_file);
}
static PadSample *replay; static size_t replay_count, replay_next;
static void replay_sample(PadData *d) {
    if (!replay_armed) return;
    if (!replay_start) {
        static int loaded;
        if (!loaded) {
            loaded=1;
            const char *path=getenv("BB_PAD_REPLAY");
            FILE *f=path ? fopen(path,"r") : NULL;
            PadSample s; unsigned v[8]; size_t cap=0;
            while (f && fscanf(f,"%u %u %u %u %u %u %u %u",&v[0],&v[1],&v[2],&v[3],&v[4],&v[5],&v[6],&v[7])==8) {
                s=(PadSample){v[0],v[1],{(uint8_t)v[2],(uint8_t)v[3],(uint8_t)v[4],(uint8_t)v[5]},(uint8_t)v[6],(uint8_t)v[7]};
                if (replay_count==cap && !(replay=realloc(replay,(cap=cap ? cap*2 : 1024)*sizeof(*replay)))) break;
                replay[replay_count++]=s;
            }
            if (f) fclose(f);
            printf("Runtime: pad replay of %zu samples from %s\n",replay_count,path ? path : "(unset)");
        }
        replay_start=now_us();
        replay_next=0;
    }
    const uint32_t ms=(uint32_t)((now_us()-replay_start)/1000);
    while (replay_next<replay_count && replay[replay_next].ms<=ms) ++replay_next;
    if (!replay_next) return;
    if (replay_next==replay_count && ms>replay[replay_count-1].ms+500) {
        if (replay_armed==1) { puts("Runtime: pad replay finished"); replay_armed=2; }
        return;
    }
    const PadSample *s=&replay[replay_next-1];
    d->buttons=s->buttons;
    d->left_x=s->axes[0]; d->left_y=s->axes[1]; d->right_x=s->axes[2]; d->right_y=s->axes[3];
    d->l2=s->l2; d->r2=s->r2;
}
/* After the menu or the text dialog closes, buttons still held (the Cross that accepted a
 * name) stay hidden until released: the game would take them as a new press. */
static int hold_after_capture;
static void sample(PadData *d) {
    sample_host(d);
    if (bbgpu_overlay_captures_input()) { hold_after_capture=1; return; }
    record_sample(d);
    read_inject();
    replay_sample(d);
    d->buttons|=injected.buttons;
    if (injected.touch_side>=0) touch_click(d,injected.touch_side);
    else if ((d->buttons & BTN_TOUCHPAD) && !d->touch_count) touch_click(d,0);
    if (injected.buttons & BTN_L2) d->l2=255;
    if (injected.buttons & BTN_R2) d->r2=255;
    uint8_t *axes[4]={&d->left_x,&d->left_y,&d->right_x,&d->right_y};
    for (int i=0;i<4;++i) if (injected.stick[i]>=0) *axes[i]=(uint8_t)injected.stick[i];
    if (hold_after_capture) {
        if (d->buttons) d->buttons=0;
        else hold_after_capture=0;
    }
}

/* The camera hook: mouse motion from the window thread, at once (bbgpu_mouse_set_direct). */
static void mouse_direct_turn(float dx, float dy) {
    const double k=kbm.sensitivity*HOOK_RADIANS_PER_COUNT;
    const double yaw=dx*k*(kbm.invert_x ? -1 : 1), pitch=-dy*k*kbm.sensitivity_y*(kbm.invert_y ? -1 : 1);
    runtime_camhook_turn((float)(pitch*HOOK_PITCH_SIGN),(float)(yaw*HOOK_YAW_SIGN));
}

static ABI int32_t pad_init(void) { pthread_mutex_lock(&lock); initialized=1; pthread_mutex_unlock(&lock); return 0; }
static ABI int32_t pad_open(int32_t user, int32_t type, int32_t index, const void *param) {
    (void)param;
    if (!initialized) return ERR_NOT_INITIALIZED;
    if (user!=1) return ERR_INVALID_ARG;
    if (type!=0 && type!=2) return ERR_INVALID_ARG; /* standard / special port */
    if (index) return ERR_INVALID_ARG;
    pthread_mutex_lock(&lock);
    int already=opened; opened=1;
    pthread_mutex_unlock(&lock);
    if (already) return ERR_ALREADY_OPENED;
    load_keybinds();
    /* Before the game's camera first runs; the mouse then reaches it straight from the window. */
    if (kbm.mouse_camera && runtime_camhook_install(kbm.no_auto_rotation)) bbgpu_mouse_set_direct(mouse_direct_turn);
    puts("Runtime: pad opened for user 1 (SDL gamepad, keyboard and mouse)");
    return PAD_HANDLE;
}
static ABI int32_t pad_close(int32_t handle) {
    if (handle!=PAD_HANDLE || !opened) return ERR_INVALID_HANDLE;
    opened=0; return 0;
}
static ABI int32_t pad_read_state(int32_t handle, PadData *data) {
    if (handle!=PAD_HANDLE || !opened) return ERR_INVALID_HANDLE;
    if (!data) return ERR_INVALID_ARG;
    pthread_mutex_lock(&lock);
    sample(data); ++reads;
    pthread_mutex_unlock(&lock);
    return 0;
}
/* Buffered read: the port samples once per call, so one entry is returned. */
static ABI int32_t pad_read(int32_t handle, PadData *data, int32_t count) {
    if (handle!=PAD_HANDLE || !opened) return ERR_INVALID_HANDLE;
    if (!data || count<1 || count>64) return ERR_INVALID_ARG;
    pad_read_state(handle,data);
    return 1;
}
static ABI int32_t pad_info(int32_t handle, ControllerInfo *info) {
    if (handle!=PAD_HANDLE || !opened) return ERR_INVALID_HANDLE;
    if (!info) return ERR_INVALID_ARG;
    memset(info,0,sizeof(*info));
    info->pixel_density=44.86f; info->resolution_x=1920; info->resolution_y=943;
    info->dead_zone_left=info->dead_zone_right=2;
    info->connection_type=0; info->connected=1; info->device_class=0;
    pthread_mutex_lock(&lock);
    current_gamepad();
    info->connected_count=connected_count ? connected_count : 1;
    pthread_mutex_unlock(&lock);
    return 0;
}
static ABI int32_t pad_vibration(int32_t handle, const uint8_t *param) {
    if (handle!=PAD_HANDLE || !opened) return ERR_INVALID_HANDLE;
    if (!param) return ERR_INVALID_ARG;
    pthread_mutex_lock(&lock);
    SDL_Gamepad *g=current_gamepad();
    if (g) SDL_RumbleGamepad(g,(uint16_t)(param[0]*257),(uint16_t)(param[1]*257),1000);
    pthread_mutex_unlock(&lock);
    return 0;
}
static ABI int32_t pad_ok_handle(int32_t handle) { return handle==PAD_HANDLE && opened ? 0 : ERR_INVALID_HANDLE; }
static ABI int32_t pad_ok_handle_flag(int32_t handle, uint8_t flag) { (void)flag; return pad_ok_handle(handle); }

static const RuntimeExport exports[]={
    {"scePadInit",pad_init}, {"scePadOpen",pad_open}, {"scePadClose",pad_close},
    {"scePadReadState",pad_read_state}, {"scePadRead",pad_read},
    {"scePadGetControllerInformation",pad_info}, {"scePadSetVibration",pad_vibration},
    {"scePadResetOrientation",pad_ok_handle},
    {"scePadSetAngularVelocityDeadbandState",pad_ok_handle_flag}, {"scePadSetTiltCorrectionState",pad_ok_handle_flag},
    {"scePadSetMotionSensorState",pad_ok_handle_flag},
};
uintptr_t runtime_pad_resolve(const char *name) { return RUNTIME_LOOKUP(exports,name); }
void runtime_pad_report(void) { printf("Runtime: pad reads=%zu, gamepad=%s\n",reads,gamepad ? SDL_GetGamepadName(gamepad) : "none"); }

/**********************************************************************************************
*
*   rcore_xbox - Functions to manage window, graphics device and inputs
*
*   PLATFORM: XBOX
*       - Microsoft Xbox (nxdk)
*       - Graphics: pbgl (OpenGL 1.x over pbkit)
*
*   LIMITATIONS:
*       - GRAPHICS_API_OPENGL_11
*
*   POSSIBLE IMPROVEMENTS:
*       - Improvement 01
*       - Improvement 02
*
*   DEPENDENCIES:
*       - nxdk         (https://github.com/zweiler2/nxdk_zig): kernel/HAL + libc
*       - pbgl         (https://github.com/zweiler2/nxdk-pbgl_zig): OpenGL implementation
*
*   NOTE: This backend is compiled as part of rcore.c (see rcore.c for the
*   platform dispatch), so everything here lives in the same translation unit.
*
**********************************************************************************************/

#include <assert.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>

#include <hal/video.h>      // XVideoSetMode/XVideoGetMode, VIDEO_MODE, REFRESH_*
#include <profileapi.h>     // QueryPerformanceCounter/QueryPerformanceFrequency
#include <pbgl.h>           // pbgl_init/pbgl_swap_buffers/pbgl_set_swap_interval
#include <GL/gl.h>          // OpenGL 1.1 (pbgl)
#include <usbh_lib.h>       // usbh_core_init/usbh_pooling_hubs (nxdk USB host stack)
#include <xid_driver.h>     // Xbox input devices (gamepads/IR dongle) over USB

// Provided by nxdk's winapi (lib/winapi/synchapi.h). Declared manually to avoid
// pulling in <windows.h>, which clashes with some raylib identifiers.
extern void Sleep(unsigned long msTimeout);

//----------------------------------------------------------------------------------
// Types and Structures Definition
//----------------------------------------------------------------------------------

// Xbox display mode selected from the requested window size
typedef struct XboxVideoMode {
    int width;
    int height;
    int refresh;
} XboxVideoMode;

// Per-controller state for the XID (Xbox input device) driver
#define XBOX_MAX_PADS           MAX_GAMEPADS          // Maximum number of gamepads supported
#define XBOX_PAD_REPORT_MAX     32                    // Maximum XID input report size
#define XBOX_PAD_DEADZONE       0x20                  // Analog button pressure deadzone

// XID gamepad report: digital button bits (see xboxdevwiki.net/Xbox_Input_Devices)
#define XID_BTN_DPAD_UP         0x0001
#define XID_BTN_DPAD_DOWN       0x0002
#define XID_BTN_DPAD_LEFT       0x0004
#define XID_BTN_DPAD_RIGHT      0x0008
#define XID_BTN_START           0x0010
#define XID_BTN_BACK            0x0020
#define XID_BTN_LEFT_THUMB      0x0040
#define XID_BTN_RIGHT_THUMB     0x0080

typedef struct XboxPadSlot {
    xid_dev_t *dev;                                 // XID device owning this slot (NULL = free)
    unsigned int uid;                               // XID device unique id (stable while connected)
    volatile bool fresh;                            // A new report was stored by the USB completion routine
    unsigned char pending[XBOX_PAD_REPORT_MAX];     // Newest report written by the USB completion routine
    int pendingLength;                              // Size of the newest report in bytes
    unsigned char report[XBOX_PAD_REPORT_MAX];      // Report being used by the front-end (no USB race)
    int reportLength;                               // Size of the report being used in bytes
} XboxPadSlot;

//----------------------------------------------------------------------------------
// Global Variables Definition
//----------------------------------------------------------------------------------

// Frame pacing state
static int gTargetFps = 0;          // Target FPS set by SetTargetFPS() (0 = uncapped)
static int gVBlankDivisor = 1;      // vblank divisor used as the pbgl swap interval
static double gRefreshHz = 60.0;    // Refresh rate of the active video mode

// High-resolution timer
static LARGE_INTEGER gQpcFrequency = { 0 };

// Gamepad state (XID driver over USB)
static XboxPadSlot gPads[XBOX_MAX_PADS] = { 0 };
static bool gUsbInitialised = false;

//----------------------------------------------------------------------------------
// Module Internal Functions Declaration
//----------------------------------------------------------------------------------

int InitPlatform(void);         // Initialize platform (graphics, inputs and more)
void XboxInitTimer(void);       // Initialize the high-resolution timer base (called from rcore.c)
void SetTargetFPSXbox(int fps); // SetTargetFPS() implementation for Xbox
void XboxInitGamepads(void);    // Initialize USB host stack + XID gamepad driver
static void XboxPadReadCallback(UTR_T *utr);                                    // USB interrupt-IN completion
static void XboxApplyPadReport(size_t pad, const unsigned char *report, int length); // XID report -> raylib state
static char *F64ToStr(double value, const uint8_t digits);

//----------------------------------------------------------------------------------
// Module Functions Definition: Window and Graphics Device
//----------------------------------------------------------------------------------

// Check if application should close
bool WindowShouldClose(void) {
    if (CORE.Window.ready) {
        return CORE.Window.shouldClose;
    } else {
        return true;
    }
}

// Toggle fullscreen mode
void ToggleFullscreen(void) {
    TRACELOG(LOG_WARNING, "ToggleFullscreen() not available on target platform");
}

// Toggle borderless windowed mode
void ToggleBorderlessWindowed(void) {
    TRACELOG(LOG_WARNING, "ToggleBorderlessWindowed() not available on target platform");
}

// Set window state: maximized, if resizable
void MaximizeWindow(void) {
    TRACELOG(LOG_WARNING, "MaximizeWindow() not available on target platform");
}

// Set window state: minimized
void MinimizeWindow(void) {
    TRACELOG(LOG_WARNING, "MinimizeWindow() not available on target platform");
}

// Set window state: not minimized/maximized
void RestoreWindow(void) {
    TRACELOG(LOG_WARNING, "RestoreWindow() not available on target platform");
}

// Set window configuration state using flags
void SetWindowState(unsigned int flags) {
    TRACELOG(LOG_WARNING, "SetWindowState() not available on target platform");
}

// Clear window configuration state flags
void ClearWindowState(unsigned int flags) {
    TRACELOG(LOG_WARNING, "ClearWindowState() not available on target platform");
}

// Set icon for window
void SetWindowIcon(Image image) {
    TRACELOG(LOG_WARNING, "SetWindowIcon() not available on target platform");
}

// Set icon for window
void SetWindowIcons(Image *images, int count) {
    TRACELOG(LOG_WARNING, "SetWindowIcons() not available on target platform");
}

// Set title for window
void SetWindowTitle(const char *title) {
    CORE.Window.title = title;
}

// Set window position on screen (windowed mode)
void SetWindowPosition(int x, int y) {
    TRACELOG(LOG_WARNING, "SetWindowPosition() not available on target platform");
}

// Set monitor for the current window
void SetWindowMonitor(int monitor) {
    TRACELOG(LOG_WARNING, "SetWindowMonitor() not available on target platform");
}

// Set window minimum dimensions (FLAG_WINDOW_RESIZABLE)
void SetWindowMinSize(int width, int height) {
    CORE.Window.screenMin.width = width;
    CORE.Window.screenMin.height = height;
}

// Set window maximum dimensions (FLAG_WINDOW_RESIZABLE)
void SetWindowMaxSize(int width, int height) {
    CORE.Window.screenMax.width = width;
    CORE.Window.screenMax.height = height;
}

// Set window dimensions
void SetWindowSize(int width, int height) {
    TRACELOG(LOG_WARNING, "SetWindowSize() not available on target platform");
}

// Set window opacity, value opacity is between 0.0 and 1.0
void SetWindowOpacity(float opacity) {
    TRACELOG(LOG_WARNING, "SetWindowOpacity() not available on target platform");
}

// Set window focused
void SetWindowFocused(void) {
    TRACELOG(LOG_WARNING, "SetWindowFocused() not available on target platform");
}

// Get native window handle
void *GetWindowHandle(void) {
    TRACELOG(LOG_WARNING, "GetWindowHandle() not implemented on target platform");
    return NULL;
}

// Get number of monitors
int GetMonitorCount(void) {
    return 1;
}

// Get current monitor
int GetCurrentMonitor(void) {
    return 0;
}

// Get selected monitor position
Vector2 GetMonitorPosition(int monitor) {
    return (Vector2){ 0, 0 };
}

// Get selected monitor width (currently used by monitor)
int GetMonitorWidth(int monitor) {
    return CORE.Window.display.width;
}

// Get selected monitor height (currently used by monitor)
int GetMonitorHeight(int monitor) {
    return CORE.Window.display.height;
}

// Get selected monitor physical width in millimetres
int GetMonitorPhysicalWidth(int monitor) {
    TRACELOG(LOG_WARNING, "GetMonitorPhysicalWidth() not implemented on target platform");
    return 0;
}

// Get selected monitor physical height in millimetres
int GetMonitorPhysicalHeight(int monitor) {
    TRACELOG(LOG_WARNING, "GetMonitorPhysicalHeight() not implemented on target platform");
    return 0;
}

// Get selected monitor refresh rate
int GetMonitorRefreshRate(int monitor) {
    return (int)(gRefreshHz + 0.5);
}

// Get the human-readable, UTF-8 encoded name of the selected monitor
const char *GetMonitorName(int monitor) {
    return "Xbox";
}

// Get window position XY on monitor
Vector2 GetWindowPosition(void) {
    return (Vector2){ 0, 0 };
}

// Get window scale DPI factor for current monitor
Vector2 GetWindowScaleDPI(void) {
    return (Vector2){ 1.0f, 1.0f };
}

// Set clipboard text content
void SetClipboardText(const char *text) {
    TRACELOG(LOG_WARNING, "SetClipboardText() not implemented on target platform");
}

// Get clipboard text content
const char *GetClipboardText(void) {
    TRACELOG(LOG_WARNING, "GetClipboardText() not implemented on target platform");
    return NULL;
}

// Show mouse cursor
void ShowCursor(void) {
    CORE.Input.Mouse.cursorHidden = false;
}

// Hides mouse cursor
void HideCursor(void) {
    CORE.Input.Mouse.cursorHidden = true;
}

// Enables cursor (unlock cursor)
void EnableCursor(void) {
    SetMousePosition(CORE.Window.screen.width/2, CORE.Window.screen.height/2);
    CORE.Input.Mouse.cursorHidden = false;
}

// Disables cursor (lock cursor)
void DisableCursor(void) {
    SetMousePosition(CORE.Window.screen.width/2, CORE.Window.screen.height/2);
    CORE.Input.Mouse.cursorHidden = true;
}

//----------------------------------------------------------------------------------
// Module Functions Definition: Timing
//----------------------------------------------------------------------------------

// Select the closest supported Xbox video mode for a requested height.
// NOTE: Modes are chosen by rounding the requested height *down* to the next
// supported resolution; the aspect ratio/width follows the mode.
static XboxVideoMode XboxSelectVideoMode(int requestedWidth, int requestedHeight) {
    (void)requestedWidth;

    // 480i (NTSC)
    if (requestedHeight < 576) return (XboxVideoMode){ 640, 480, REFRESH_60HZ };

    // 576i (PAL)
    if (requestedHeight < 720) return (XboxVideoMode){ 720, 576, REFRESH_50HZ };

    // 720p
    if (requestedHeight < 1080) return (XboxVideoMode){ 1280, 720, REFRESH_60HZ };

    // 1080i
    return (XboxVideoMode){ 1920, 1080, REFRESH_60HZ };
}

// Set target FPS: Xbox has no scheduler-driven frame limiter, so the target is
// translated into a pbgl swap interval (vblank divisor).
void SetTargetFPSXbox(int fps) {
    gTargetFps = fps;

    if (fps <= 0 || fps >= (int)(gRefreshHz - 0.5)) {
        gVBlankDivisor = 1;     // Vsync-locked (~60/50 fps)
    } else {
        // NOTE: lround()/lroundf() are unimplemented stubs in nxdk (they assert),
        // so the divisor is rounded with plain arithmetic instead
        int div = (int)((gRefreshHz/(double)fps) + 0.5);
        if (div < 1) {
            div = 1;
        }
        gVBlankDivisor = div;
    }

    pbgl_set_swap_interval(gVBlankDivisor);

    TRACELOG(LOG_INFO, "XBOX: Frame pacing refresh=%.2f Hz, target=%d fps -> swap interval=%d (actual ~%.2f fps)", gRefreshHz, fps, gVBlankDivisor, gRefreshHz / gVBlankDivisor);
}

// Swap back buffer with front buffer (screen drawing)
void SwapScreenBuffer(void) {
    // pbgl_swap_buffers() waits for the configured number of vblanks and
    // presents the back buffer.
    pbgl_swap_buffers();
}

// Get elapsed time measure in seconds since InitTimer()
double GetTime(void) {
    LARGE_INTEGER now = { 0 };
    QueryPerformanceCounter(&now);

    if (gQpcFrequency.QuadPart == 0) {
        return 0.0;
    }

    return (double)((long long)now.QuadPart - (long long)CORE.Time.base) / (double)gQpcFrequency.QuadPart;
}

// Initialize the high-resolution timer base (called from rcore.c InitTimer())
void XboxInitTimer(void) {
    QueryPerformanceFrequency(&gQpcFrequency);

    LARGE_INTEGER now = { 0 };
    QueryPerformanceCounter(&now);

    CORE.Time.base = (unsigned long long int)now.QuadPart;
}

//----------------------------------------------------------------------------------
// Module Functions Definition: Misc
//----------------------------------------------------------------------------------

// Open URL with default system browser (if available)
void OpenURL(const char *url) {
    // Security check to (partially) avoid malicious code on target platform
    if (strchr(url, '\'') != NULL) {
        TRACELOG(LOG_WARNING, "SYSTEM: Provided URL could be potentially malicious, avoid [\'] character");
    } else {
        // TODO: Not supported on Xbox
    }
}

//----------------------------------------------------------------------------------
// Module Functions Definition: Inputs
//----------------------------------------------------------------------------------

// Set internal gamepad mappings
int SetGamepadMappings(const char *mappings) {
    TRACELOG(LOG_WARNING, "SetGamepadMappings() not implemented on target platform");
    return 0;
}

// Set mouse position XY
void SetMousePosition(int x, int y) {
    CORE.Input.Mouse.currentPosition = (Vector2){ (float)(x), (float)(y) };
    CORE.Input.Mouse.previousPosition = CORE.Input.Mouse.currentPosition;
}

// Set mouse cursor
void SetMouseCursor(int cursor) {
    TRACELOG(LOG_WARNING, "SetMouseCursor() not implemented on target platform");
}

// Register all input events
void PollInputEvents(void) {
#if defined(SUPPORT_GESTURES_SYSTEM)
    // NOTE: Gestures update must be called every frame to reset gestures correctly
    // because ProcessGestureEvent() is just called on an event, not every frame
    UpdateGestures();
#endif

    // Reset last gamepad button/axis registered state
    CORE.Input.Gamepad.lastButtonPressed = 0;       // GAMEPAD_BUTTON_UNKNOWN

    if (!gUsbInitialised) {
        XboxInitGamepads();
    }

    // Let the USB stack process hub events and complete pending transfers
    // (also detects controllers plugged in after startup)
    usbh_pooling_hubs();

    // Register previous gamepad button states
    for (size_t pad = 0; pad < XBOX_MAX_PADS; pad++) {
        for (size_t k = 0; k < MAX_GAMEPAD_BUTTONS; k++) {
            CORE.Input.Gamepad.previousButtonState[pad][k] = CORE.Input.Gamepad.currentButtonState[pad][k];
        }
    }

    bool padSeen[XBOX_MAX_PADS] = { false };

    for (xid_dev_t *dev = usbh_xid_get_device_list(); dev != NULL; dev = dev->next) {
        // Only gamepads are exposed through raylib's gamepad API
        if (dev->xid_desc.bType != XID_TYPE_GAMECONTROLLER) {
            continue;
        }

        size_t pad = -1;

        // Look for the slot already assigned to this device
        for (size_t i = 0; i < XBOX_MAX_PADS; i++) {
            if ((gPads[i].dev != NULL) && (gPads[i].uid == dev->uid)) {
                pad = i;
                break;
            }
        }

        // Otherwise take the first free slot
        if (pad == -1) {
            for (size_t i = 0; i < XBOX_MAX_PADS; i++) {
                if (gPads[i].dev == NULL) {
                    pad = i;
                    break;
                }
            }
        }

        if (pad == -1) {
            // More controllers connected than supported slots
            continue;
        }

        if (gPads[pad].dev != dev) {
            // New controller: (re)initialize the slot
            memset(&gPads[pad], 0, sizeof(XboxPadSlot));
            gPads[pad].dev = dev;
            gPads[pad].uid = dev->uid;

            CORE.Input.Gamepad.axisCount[pad] = 6;
            snprintf(CORE.Input.Gamepad.name[pad], MAX_GAMEPAD_NAME_LENGTH, "Xbox Controller #%i", pad + 1);
        }

        // Keep exactly one interrupt-IN transfer in flight: the USB stack frees
        // completed transfers here and the completion routine stores the newest
        // report, so a controller that is unplugged/replugged keeps working.
        usbh_xid_read(dev, 0, XboxPadReadCallback);

        padSeen[pad] = true;
        CORE.Input.Gamepad.ready[pad] = true;

        if (gPads[pad].fresh) {
            memcpy(gPads[pad].report, gPads[pad].pending, XBOX_PAD_REPORT_MAX);
            gPads[pad].reportLength = gPads[pad].pendingLength;
            gPads[pad].fresh = false;
        }

        XboxApplyPadReport(pad, gPads[pad].report, gPads[pad].reportLength);

        // Register last gamepad button pressed
        for (size_t button = 1; button < MAX_GAMEPAD_BUTTONS; button++) {
            if ((CORE.Input.Gamepad.currentButtonState[pad][button] == 1) &&
                (CORE.Input.Gamepad.previousButtonState[pad][button] == 0))
            {
                CORE.Input.Gamepad.lastButtonPressed = button;
            }
        }
    }

    // Forget controllers that are no longer connected
    for (size_t pad = 0; pad < XBOX_MAX_PADS; pad++) {
        if (padSeen[pad]){
            continue;
        }

        if (gPads[pad].dev != NULL) {
            memset(&gPads[pad], 0, sizeof(XboxPadSlot));
        }

        if (CORE.Input.Gamepad.ready[pad]) {
            CORE.Input.Gamepad.ready[pad] = false;
            CORE.Input.Gamepad.axisCount[pad] = 0;
            memset(CORE.Input.Gamepad.name[pad], 0, MAX_GAMEPAD_NAME_LENGTH);

            for (size_t k = 0; k < MAX_GAMEPAD_BUTTONS; k++) {
                CORE.Input.Gamepad.currentButtonState[pad][k] = 0;
            }
            for (size_t k = 0; k < MAX_GAMEPAD_BUTTONS; k++) {
                CORE.Input.Gamepad.previousButtonState[pad][k] = 0;
            }
            for (size_t a = 0; a < MAX_GAMEPAD_AXES; a++) {
                CORE.Input.Gamepad.axisState[pad][a] = 0.0f;
            }
        }
    }
}

//----------------------------------------------------------------------------------
// Module Internal Functions Definition
//----------------------------------------------------------------------------------

// Bring up the nxdk USB host stack and register the XID input device driver
void XboxInitGamepads(void) {
    if (gUsbInitialised) {
        return;
    }

    usbh_core_init();
    usbh_xid_init();

    gUsbInitialised = true;

    TRACELOG(LOG_DEBUG, "XBOX: USB host stack initialized, XID gamepads will be polled");
}

// USB interrupt-IN completion: store the newest gamepad report, PollInputEvents()
// copies it into the front-end state (completion runs in USB/DISPATCH context)
static void XboxPadReadCallback(UTR_T *utr) {
    xid_dev_t *dev = (xid_dev_t *)utr->context;

    if ((utr->status < 0) || (dev == NULL)) {
        return;
    }

    for (size_t i = 0; i < XBOX_MAX_PADS; i++) {
        if (gPads[i].dev != dev) continue;

        size_t length = utr->xfer_len;
        if (length > XBOX_PAD_REPORT_MAX) {
            length = XBOX_PAD_REPORT_MAX;
        }

        if (length > 0) {
            memcpy(gPads[i].pending, utr->buff, length);
            gPads[i].pendingLength = length;
            gPads[i].fresh = true;
        }

        break;
    }
}

// Decode a raw XID gamepad report into raylib's gamepad state arrays
static void XboxApplyPadReport(size_t pad, const unsigned char *report, int length) {
    char *current = CORE.Input.Gamepad.currentButtonState[pad];
    float *axes = CORE.Input.Gamepad.axisState[pad];

    for (size_t k = 0; k < MAX_GAMEPAD_BUTTONS; k++) {
        current[k] = 0;
    }
    for (size_t k = 0; k < MAX_GAMEPAD_AXES; k++) {
        axes[k] = 0.0f;
    }

    // Triggers are reported as -1.0 when fully released
    axes[GAMEPAD_AXIS_LEFT_TRIGGER] = -1.0f;
    axes[GAMEPAD_AXIS_RIGHT_TRIGGER] = -1.0f;

    // No report received yet: controller is present but all inputs idle
    if (length < sizeof(xid_gamepad_in)) {
        return;
    }

    // Report layout matches xid_gamepad_in (packed)
    const xid_gamepad_in *in = (const xid_gamepad_in *)report;

    // Digital buttons
    if (in->dButtons & XID_BTN_DPAD_UP) current[GAMEPAD_BUTTON_LEFT_FACE_UP] = 1;
    if (in->dButtons & XID_BTN_DPAD_DOWN) current[GAMEPAD_BUTTON_LEFT_FACE_DOWN] = 1;
    if (in->dButtons & XID_BTN_DPAD_LEFT) current[GAMEPAD_BUTTON_LEFT_FACE_LEFT] = 1;
    if (in->dButtons & XID_BTN_DPAD_RIGHT) current[GAMEPAD_BUTTON_LEFT_FACE_RIGHT] = 1;
    if (in->dButtons & XID_BTN_START) current[GAMEPAD_BUTTON_MIDDLE_RIGHT] = 1;
    if (in->dButtons & XID_BTN_BACK) current[GAMEPAD_BUTTON_MIDDLE_LEFT] = 1;
    if (in->dButtons & XID_BTN_LEFT_THUMB) current[GAMEPAD_BUTTON_LEFT_THUMB] = 1;
    if (in->dButtons & XID_BTN_RIGHT_THUMB) current[GAMEPAD_BUTTON_RIGHT_THUMB] = 1;

    // Face buttons and black/white are analog on Xbox pads, so they are turned
    // into digital buttons using a pressure deadzone
    if (in->a > XBOX_PAD_DEADZONE) current[GAMEPAD_BUTTON_RIGHT_FACE_DOWN] = 1;
    if (in->b > XBOX_PAD_DEADZONE) current[GAMEPAD_BUTTON_RIGHT_FACE_RIGHT] = 1;
    if (in->x > XBOX_PAD_DEADZONE) current[GAMEPAD_BUTTON_RIGHT_FACE_LEFT] = 1;
    if (in->y > XBOX_PAD_DEADZONE) current[GAMEPAD_BUTTON_RIGHT_FACE_UP] = 1;
    if (in->white > XBOX_PAD_DEADZONE) current[GAMEPAD_BUTTON_LEFT_TRIGGER_1] = 1;   // WHITE
    if (in->black > XBOX_PAD_DEADZONE) current[GAMEPAD_BUTTON_RIGHT_TRIGGER_1] = 1;  // BLACK

    // Triggers are exposed as both a digital button and an analog axis
    if (in->l > XBOX_PAD_DEADZONE) current[GAMEPAD_BUTTON_LEFT_TRIGGER_2] = 1;
    if (in->r > XBOX_PAD_DEADZONE) current[GAMEPAD_BUTTON_RIGHT_TRIGGER_2] = 1;

    axes[GAMEPAD_AXIS_LEFT_TRIGGER] = (float)in->l/255.0f;
    axes[GAMEPAD_AXIS_RIGHT_TRIGGER] = (float)in->r/255.0f;

    // Sticks: 16-bit signed values normalized to -1.0..1.0, Y inverted to match
    // raylib's screen-oriented axes (up is negative)
    axes[GAMEPAD_AXIS_LEFT_X] = (float)in->leftStickX/32767.0f;
    axes[GAMEPAD_AXIS_LEFT_Y] = -(float)in->leftStickY/32767.0f;
    axes[GAMEPAD_AXIS_RIGHT_X] = (float)in->rightStickX/32767.0f;
    axes[GAMEPAD_AXIS_RIGHT_Y] = -(float)in->rightStickY/32767.0f;
}

// Custom logging function for the Xbox backend
void CustomLog(int msgType, const char *fmt, va_list args) {
    char buffer[1024] = { 0 };
    char *dst = buffer;
    size_t remaining = sizeof(buffer) - 1;

    const char *p = fmt;

    while (*p != '\0' && remaining > 0) {
        if (*p != '%') {
            *dst++ = *p++;
            remaining--;
            continue;
        }

        // We are at a '%'
        const char *spec = p;
        p++; // skip '%'

        // ---- parse flags / width / precision / length (simplified) ----
        while (*p != '\0' && strchr("-+ #0", *p)) {
            p++;
        }
        // width
        while (*p >= '0' && *p <= '9') {
            p++;
        }
        uint8_t precision = 13;
        if (*p == '.') {
            p++;
            precision = 0;
            while (*p >= '0' && *p <= '9') {
                precision *= 10;
                precision += *p - '0';
                p++;
            }
        }
        // length modifiers
        while (*p != '\0' && strchr("hlLjzt", *p)) {
            p++;
        }

        char conv = *p;
        if (conv == '\0') {
            break;
        }
        // consume conversion character
        p++;

        // ---- floating-point conversions we handle ourselves ----
        switch (conv) {
            case 'f':
            case 'F':
            case 'e':
            case 'E':
            case 'g':
            case 'G':
            case 'a':
            case 'A': {
                double val = va_arg(args, double);

                char *double_str = F64ToStr(val, precision);
                size_t len = strlen(double_str);

                if (len > remaining) {
                    len = remaining;
                }
                memcpy(dst, double_str, len);
                dst += len;
                remaining -= len;
                break;
            }
            default: {
                // Non-float specifier → fall back to a tiny vsnprintf on this piece.
                // We rebuild a short format string and let the real formatter handle it.
                char tinyfmt[32];
                size_t speclen = (size_t)(p - spec);
                if (speclen >= sizeof(tinyfmt)) {
                    speclen = sizeof(tinyfmt) - 1;
                }
                memcpy(tinyfmt, spec, speclen);
                tinyfmt[speclen] = '\0';

                // Because we already consumed previous arguments, we need a
                // fresh copy of the remaining va_list for this call.
                va_list args_copy;
                va_copy(args_copy, args);

                int written = vsnprintf(dst, remaining + 1, tinyfmt, args_copy);
                va_end(args_copy);

                if (written < 0) {
                    written = 0;
                }
                if ((size_t)written > remaining) {
                    written = (int)remaining;
                }

                // Advance the *real* va_list by the same amount the tiny call did.
                // (This is the only slightly ugly part.)
                // For the common integer/string cases we just re-consume:
                switch (conv) {
                    case 'd':
                    case 'i':
                    case 'u':
                    case 'x':
                    case 'X':
                    case 'o':
                        (void)va_arg(args, int);
                        break;
                    case 's':
                        (void)va_arg(args, const char *);
                        break;
                    case 'c':
                        (void)va_arg(args, int);
                        break;
                    case 'p':
                        (void)va_arg(args, void *);
                        break;
                    default:
                        break;
                }

                dst += written;
                remaining -= (size_t)written;
                break;
            }
        }
    }

    *dst = '\0';
    int written = (int)(dst - buffer);

    switch (msgType) {
        case LOG_TRACE:   DbgPrint("[XBOX][TRACE]: %.*s\n",   written, buffer); break;
        case LOG_DEBUG:   DbgPrint("[XBOX][DEBUG]: %.*s\n",   written, buffer); break;
        case LOG_INFO:    DbgPrint("[XBOX][INFO]: %.*s\n",    written, buffer); break;
        case LOG_WARNING: DbgPrint("[XBOX][WARNING]: %.*s\n", written, buffer); break;
        case LOG_ERROR:   DbgPrint("[XBOX][ERROR]: %.*s\n",   written, buffer); break;
        case LOG_FATAL:   DbgPrint("[XBOX][FATAL]: %.*s\n",   written, buffer); break;
        default: break;
    }
}

// Initialize platform: graphics, inputs and more
int InitPlatform(void) {
    SetTraceLogCallback(CustomLog);

    // Select and apply the closest supported video mode for the requested size
    XboxVideoMode mode = XboxSelectVideoMode(CORE.Window.screen.width, CORE.Window.screen.height);

    TRACELOG(LOG_INFO, "XBOX: Requested %ix%i, trying %ix%i@%iHz", CORE.Window.screen.width, CORE.Window.screen.height, mode.width, mode.height, mode.refresh);

    if (!XVideoSetMode(mode.width, mode.height, 32, mode.refresh)) {
        TRACELOG(LOG_WARNING, "XBOX: Failed to set %ix%i@%iHz, falling back to 640x480@60Hz", mode.width, mode.height, mode.refresh);

        mode = (XboxVideoMode){ 640, 480, REFRESH_60HZ };

        if (!XVideoSetMode(mode.width, mode.height, 32, mode.refresh)) {
            TRACELOG(LOG_ERROR, "XBOX: Failed to set a video mode");
            return -1;
        }
    }

    VIDEO_MODE vm = XVideoGetMode();
    TRACELOG(LOG_INFO, "XBOX: Video mode set to %ix%i@%iHz (%ibpp)", vm.width, vm.height, vm.refresh, vm.bpp);

    gRefreshHz = (vm.refresh == REFRESH_50HZ) ? 50.0 : 60.0;

    // Initialize pbgl (this also brings up pbkit)
    int pbglResult = pbgl_init(GL_TRUE);
    if (pbglResult != 0) {
        TRACELOG(LOG_ERROR, "XBOX: pbgl_init() failed with %i", pbglResult);
        return -1;
    }

    // Apply the frame pacing selected before InitWindow() (if any)
    SetTargetFPSXbox(gTargetFps);

    CORE.Window.flags |= FLAG_FULLSCREEN_MODE;

    CORE.Window.display.width = vm.width;
    CORE.Window.display.height = vm.height;
    CORE.Window.screen.width = vm.width;
    CORE.Window.screen.height = vm.height;
    CORE.Window.render.width = vm.width;
    CORE.Window.render.height = vm.height;
    CORE.Window.eventWaiting = false;
    CORE.Window.screenScale = MatrixIdentity();     // No draw scaling required by default
    CORE.Window.currentFbo.width = vm.width;
    CORE.Window.currentFbo.height = vm.height;
    CORE.Input.Mouse.currentPosition.x = (float)vm.width/2.0f;
    CORE.Input.Mouse.currentPosition.y = (float)vm.height/2.0f;
    CORE.Input.Mouse.scale = (Vector2){ 1.0f, 1.0f };

    TRACELOG(LOG_INFO, "PLATFORM: Device initialized successfully");
    TRACELOG(LOG_INFO, "    > Display size: %i x %i", CORE.Window.display.width, CORE.Window.display.height);
    TRACELOG(LOG_INFO, "    > Screen size:  %i x %i", CORE.Window.screen.width, CORE.Window.screen.height);
    TRACELOG(LOG_INFO, "    > Render size:  %i x %i", CORE.Window.render.width, CORE.Window.render.height);
    TRACELOG(LOG_INFO, "    > Viewport offsets: %i, %i", CORE.Window.renderOffset.x, CORE.Window.renderOffset.y);

    CORE.Window.ready = true;

    // Initialize hi-res timer
    InitTimer();

    // Initialize base path for storage
    CORE.Storage.basePath = GetWorkingDirectory();

    // Bring up USB + XID gamepad driver (controllers are polled in PollInputEvents())
    XboxInitGamepads();

    TRACELOG(LOG_INFO, "PLATFORM: Xbox initialized");

    return 0;
}

// Close platform
void ClosePlatform(void) {
    pbgl_shutdown();
}

static int WriteToBuffer(char buffer[256], uint32_t *const start, const char *const src, const uint32_t len) {
    if (*start + len >= 256) {
        *start = 0;
    }
    assert(len < 256);

    uint32_t write_idx = *start;
    while (src[write_idx] != '\0') {
        buffer[write_idx] = src[write_idx];
        write_idx++;
    }
    return write_idx - *start;
}

static uint64_t Pow10(uint64_t n) {
    static const uint64_t table[] = {
                            1ULL,
                           10ULL,
                          100ULL,
                         1000ULL,
                        10000ULL,
                       100000ULL,
                      1000000ULL,
                     10000000ULL,
                    100000000ULL,
                   1000000000ULL,
                  10000000000ULL,
                 100000000000ULL,
                1000000000000ULL,
               10000000000000ULL,
              100000000000000ULL,
             1000000000000000ULL,
            10000000000000000ULL,
           100000000000000000ULL,
          1000000000000000000ULL,
         10000000000000000000ULL,
    };
    return (n < 20) ? table[n] : 0; // 0 = overflow / too big
}

static uint64_t DigitsFromMsb(uint64_t x) {
    if (x == 0) {
        return 1;
    }

    // Find position of highest set bit (0 = LSB, 63 = MSB)
    uint32_t msb = 0;
    uint64_t t = x;

    if (t >= 0x100000000ULL) { msb += 32; t >>= 32; }
    if (t >= 0x10000ULL)     { msb += 16; t >>= 16; }
    if (t >= 0x100ULL)       { msb +=  8; t >>=  8; }
    if (t >= 0x10ULL)        { msb +=  4; t >>=  4; }
    if (t >= 0x4ULL)         { msb +=  2; t >>=  2; }
    if (t >= 0x2ULL)         { msb +=  1;           }

    // Approximate number of decimal digits:
    // digits ≈ floor(msb * log10(2)) + 1
    // 30103 / 100000 ≈ log10(2)
    uint32_t digits = (msb * 30103u) / 100000u + 1u;

    // Correction table for the cases where the approximation is off by one.
    // Covers the full uint64_t range (1 … 20 digits).
    static const uint64_t threshold[] = {
                           0ULL,
                          10ULL,
                         100ULL,
                        1000ULL,
                       10000ULL,
                      100000ULL,
                     1000000ULL,
                    10000000ULL,
                   100000000ULL,
                  1000000000ULL,
                 10000000000ULL,
                100000000000ULL,
               1000000000000ULL,
              10000000000000ULL,
             100000000000000ULL,
            1000000000000000ULL,
           10000000000000000ULL,
          100000000000000000ULL,
         1000000000000000000ULL,
        10000000000000000000ULL,
    };

    // The approximation can be 1 too low
    if (digits < 20 && x >= threshold[digits]) {
        digits++;
    }

    return digits;
}


static char *F64ToStr(double value, const uint8_t digits) {
    static char buffer[512] = {0};
    static uint32_t start = 0;

    // Handle special cases
    if (value != value) {
        const int written = WriteToBuffer(buffer, &start, "nan", 3);
        char *const result = &buffer[start];
        start += written;
        return result;
    }

    // Infinity check
    union {
        double d;
        uint64_t bits;
    } u;
    u.d = value;
    if ((u.bits & 0x7FFFFFFFFFFFFFFF) == 0x7FF0000000000000) {
        if (u.bits & 0x8000000000000000) {
            const int written = WriteToBuffer(buffer, &start, "-inf", 4);
            char *const result = &buffer[start];
            start += written;
            return result;
        } else {
            const int written = WriteToBuffer(buffer, &start, "inf", 3);
            char *const result = &buffer[start];
            start += written;
            return result;
        }
    }

    if (start + 64 > sizeof(buffer)) {
        start = 0;
    }
    char *const result = &buffer[start];

    if (value < 0.0) {
        buffer[start++] = '-';
        value = fabs(value);
    }

    // Convert whole part
    uint64_t whole = (uint64_t)value; // 987_654_321
    uint64_t digit_count = DigitsFromMsb(whole); // 9
    for (uint64_t i = digit_count; i >= 1; i--) {
        // it 0: i=9:
        //     exp = 10^(i - 1) = 10^8 = 100_000_000
        //     digit = whole / exp = 987_654_321 / 100_000_000 = 9
        //     whole -= digit * exp;
        // it 1: i=8:
        //     exp = 10^(i - 1) = 10^7 = 10_000_000
        //     digit = whole / exp = 87_654_321 / 10_000_000 = 8
        //     whole -= digit * exp;
        uint64_t exp = Pow10(i - 1);
        uint8_t digit = whole / exp;
        assert(digit < 10);
        buffer[start++] = '0' + digit;
        whole -= digit * exp;
    }
    buffer[start++] = '.';

    // Convert fractional part
    double frac = value - (uint64_t)value; // 0.987_654
    for (uint8_t i = 0; i < digits; i++) {
        // it 0:
        //    frac *= 10 = 9.876_54
        //    digit = frac = 9
        //    frac -= digit = 9.876_54 - 9 = 0.876_54
        // it 1:
        //    frac *= 10 = 8.765_4
        //    digit = frac = 8
        //    frac -= digit = 8.765_4 - 8 = 0.765_4
        frac *= 10;
        uint8_t digit = frac;
        assert(digit < 10);
        buffer[start++] = '0' + digit;
        frac -= digit;
    }

    buffer[start++] = '\0';
    return result;
}

// EOF

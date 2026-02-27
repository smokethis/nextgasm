// menu.cpp — Top-level menu system implementation
//
// Manages the main menu that appears when the device boots.
// Owns the cursor position and menu item definitions.
// Delegates actual OLED rendering to oleddisplay.h functions.
//
// The menu is defined as a simple array of items, each with a label 
// and a target AppState. This is easy to extend later — just add 
// more entries to the array. In Python terms it's like:
//
//   main_menu = [
//       ("Start",    APP_RUNNING),
//       ("Settings", APP_SETTINGS),
//       ("Demo",     APP_DEMO),
//   ]
//   cursor = 0
//
//   def handle_input(direction):
//       if direction == UP:    cursor = max(0, cursor - 1)
//       elif direction == DOWN: cursor = min(len(menu)-1, cursor + 1)
//       elif direction == CENTER: return menu[cursor].target
//       return APP_MENU

#include "menu.h"
#include "oleddisplay.h"

// ── Menu item definition ───────────────────────────────────────────────
// Each item pairs a display label with the AppState it leads to.
// 'static' here means these are only visible in this file — like 
// a module-private variable in Python (prefixed with underscore).

struct MenuItem {
    const char* label;
    AppState    target;
};

static const MenuItem mainMenu[] = {
    { "Start",    APP_RUNNING  },
    { "Settings", APP_SETTINGS },
    { "Demo",     APP_DEMO     }
};

// sizeof trick to get array length at compile time. In Python you'd 
// just use len(mainMenu). In C++ the compiler doesn't track array 
// lengths, so we calculate it: total bytes / bytes per element.
static constexpr uint8_t MENU_ITEM_COUNT = sizeof(mainMenu) / sizeof(mainMenu[0]);

// ── Internal state ─────────────────────────────────────────────────────

static uint8_t cursorPos = 0;           // Which item is highlighted (0-indexed)
static NavDirection lastNavDir = NAV_NONE;  // For edge detection (act on press, not hold)

// ── Initialisation ─────────────────────────────────────────────────────

void menu_init()
{
    cursorPos = 0;
    lastNavDir = NAV_NONE;
}

// ── Input handling ─────────────────────────────────────────────────────
//
// Edge detection: we only act when the direction CHANGES. If you hold 
// DOWN, you get one cursor move, not 60 per second. You have to release 
// and press again. This matches how the operational mode nav works in 
// main.cpp and feels natural on a 5-way switch.
//
// The pattern is:
//   if (dir != lastDir) {
//       // This is a NEW press — do something
//   }
//   lastDir = dir;
//
// In Python GUI terms, this is like binding to on_press rather than 
// on_hold — you only fire once per press/release cycle.

AppState menu_update(NavDirection dir)
{
    // Only act on transitions (new presses)
    if (dir == lastNavDir) {
        return APP_MENU;  // No change, stay in menu
    }

    NavDirection previousDir = lastNavDir;
    lastNavDir = dir;

    // Ignore releases (NAV_NONE) — we only care about new presses
    if (dir == NAV_NONE) {
        return APP_MENU;
    }

    switch (dir) {
        case NAV_DOWN:
            if (cursorPos < MENU_ITEM_COUNT - 1) {
                cursorPos++;
            }
            break;

        case NAV_UP:
            if (cursorPos > 0) {
                cursorPos--;
            }
            break;

        case NAV_CENTER:
            // Select the highlighted item — return its target state.
            // The caller (main.cpp) will handle the transition.
            return mainMenu[cursorPos].target;

        default:
            // NAV_LEFT and NAV_RIGHT do nothing in the main menu.
            // Could be used for submenus later.
            break;
    }

    return APP_MENU;  // Still in the menu
}

// ── Rendering ──────────────────────────────────────────────────────────
//
// Builds the data the OLED display needs and calls the display_menu() 
// function from oleddisplay.h. We don't touch the display hardware 
// directly — that's oleddisplay's job. Clean separation of concerns.

void menu_render()
{
    // Build an array of label strings from our MenuItem structs.
    // display_menu() just wants const char* pointers, it doesn't 
    // need to know about our MenuItem struct.
    const char* labels[MENU_ITEM_COUNT];
    for (uint8_t i = 0; i < MENU_ITEM_COUNT; i++) {
        labels[i] = mainMenu[i].label;
    }

    display_menu("NEXTGASM", labels, MENU_ITEM_COUNT, cursorPos);
}

// ── Cursor reset ───────────────────────────────────────────────────────
// Called when returning to the menu from another app state.
// Resets both the cursor position AND the edge detection state,
// so a held NAV_UP from the previous state doesn't immediately 
// move the cursor when we arrive here.

void menu_reset_cursor()
{
    cursorPos = 0;
    lastNavDir = NAV_NONE;
}
// ============================================================================
//  woke.wtf — src/gui/keybind.cpp
//  Keybind polling + the zero-allocation search matcher.
//
//  poll_keybind() edge-triggers the GUI keybind and every module keybind from
//  the game's X11 Display. X11 is touched only through dlsym, so libwoke never
//  hard-links libX11: the game process already has libX11 mapped.
//
//  icontains() is the case-insensitive substring test used by the module card
//  filter. It runs per card per frame inside draw(), so it is strictly
//  allocation-free (no std::string copies — char loops only).
// ============================================================================
#include <cstring>
#include <dlfcn.h>

#include "core/config.hpp"
#include "gui/internal.hpp"
#include "modules/module.hpp"

namespace woke::gui {

namespace detail {

// ---- X11 keymap resolution (cached; the game's libX11 provides the symbol) --

xquery_keymap_fn resolve_xquery_keymap() {
    static xquery_keymap_fn fn = [] {
        void* sym = ::dlsym(RTLD_DEFAULT, "XQueryKeymap");
        if (sym == nullptr) {
            void* x11 = ::dlopen("libX11.so.6", RTLD_LAZY | RTLD_NOLOAD);
            if (x11 == nullptr) {
                x11 = ::dlopen("libX11.so.6", RTLD_LAZY);
            }
            if (x11 != nullptr) {
                sym = ::dlsym(x11, "XQueryKeymap");
            }
        }
        return reinterpret_cast<xquery_keymap_fn>(sym);
    }();
    return fn;
}

bool key_down(const char* keys, int code) {
    if (code <= 0 || code > 255) {
        return false;
    }
    return ((keys[code >> 3] >> (code & 7)) & 1) != 0;
}

// Case-insensitive substring test. Zero allocations: folds ASCII in place and
// never builds a lowered copy of either argument.
bool icontains(const char* hay, const char* needle) {
    if (needle == nullptr || needle[0] == '\0') {
        return true;   // empty filter matches everything
    }
    if (hay == nullptr) {
        return false;
    }
    const auto lower = [](char c) -> char {
        const auto u = static_cast<unsigned char>(c);
        if (u >= 'A' && u <= 'Z') {
            return static_cast<char>(u - static_cast<unsigned char>('A') +
                                     static_cast<unsigned char>('a'));
        }
        return c;
    };
    const char first = lower(needle[0]);
    for (const char* h = hay; *h != '\0'; ++h) {
        if (lower(*h) != first) {
            continue;
        }
        const char* a = h + 1;
        const char* b = needle + 1;
        while (*b != '\0' && lower(*a) == lower(*b)) {
            ++a;
            ++b;
        }
        if (*b == '\0') {
            return true;
        }
    }
    return false;
}

} // namespace detail

// Edge-triggered keybind check against the X11 keymap. `display` is the
// Display* handed to glXSwapBuffers (may be null — then this is a no-op).
// Cheap enough to call every present.
void poll_keybind(void* display) {
    if (display == nullptr) {
        return;
    }
    const detail::xquery_keymap_fn query = detail::resolve_xquery_keymap();
    if (query == nullptr) {
        return;
    }
    char keys[32] = {};
    if (query(display, keys) != 0) {
        return;
    }

    const int code = config::keybind();
    const bool down = detail::key_down(keys, code);
    if (down && !detail::g_key_was_down) {
        toggle();
    }
    detail::g_key_was_down = down;

    // Module keybinds: one edge-detected toggle per bound module.
    const auto all = modules::module_registry::instance().all();
    for (std::size_t i = 0; i < all.size() && i < detail::kMaxModuleBinds; ++i) {
        const int bind = config::module_keybind(all[i]->name().c_str());
        if (bind <= 0) {
            continue;
        }
        const bool mod_down = detail::key_down(keys, bind);
        if (mod_down && !detail::g_module_key_down[i]) {
            all[i]->set_enabled(!all[i]->enabled());
            config::save();
        }
        detail::g_module_key_down[i] = mod_down;
    }
}

} // namespace woke::gui

#include <algorithm>
#include <array>
#include <map>
#include <memory>

#include <string.h>
#include <unistd.h>

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <stdio.h>

static std::string RatWindowClass = "xeyes";

static Atom WindowClassAtom;
static Atom WindowTypeAtom;

std::array ObscuringWindowTypeStrings{
    "_NET_WM_WINDOW_TYPE_NORMAL",
    "_NET_WM_WINDOW_TYPE_DIALOG",
    "_NET_WM_WINDOW_TYPE_UTILITY",
    "_NET_WM_WINDOW_TYPE_TOOLBAR",
};

std::array<Atom, ObscuringWindowTypeStrings.size()> ObscuringWindowTypeAtoms;

struct TrackedWindow {
    Display *display;
    Window   window;
    Atom     wmType;
    int      ioClass;
    int      mapState;

    bool isRat;

    int x;
    int y;
    int width;
    int height;

    TrackedWindow(Display *display_, Window window_, int parentX, int parentY)
        : display(display_), window(window_), isRat(false), wmType(0) {
        UpdateAttributes(parentX, parentY);

        // Determine whether this window is a rat
        {
            XTextProperty classProp;
            XGetTextProperty(display, window, &classProp, WindowClassAtom);
            if (classProp.encoding == XA_STRING) {
                char **classList;
                int    numClasses;
                XTextPropertyToStringList(&classProp, &classList, &numClasses);

                for (int i = 0; i < numClasses; ++i) {
                    if (classList[i] == RatWindowClass) {
                        isRat = true;
                        break;
                    }
                }

                XFreeStringList(classList);
            }
            XFree(classProp.value);
        }

        // Get the window's WM type, if it's defined
        {
            Atom           propType;
            int            propFormat;
            unsigned long  numItems;
            unsigned long  bytesRemaining;
            unsigned char *prop = nullptr;

            XGetWindowProperty(display, window, WindowTypeAtom, 0, sizeof(Atom),
                               false, XA_ATOM, &propType, &propFormat,
                               &numItems, &bytesRemaining, &prop);

            if (propType == XA_ATOM && numItems > 0 && prop) {
                memcpy(&wmType, prop, sizeof(wmType));
            }

            if (prop) {
                XFree(prop);
            }
        }
    }

    void UpdateAttributes(int parentX, int parentY) {
        XWindowAttributes attributes;
        XGetWindowAttributes(display, window, &attributes);

        x        = parentX + attributes.x;
        y        = parentY + attributes.y;
        width    = attributes.width;
        height   = attributes.height;
        ioClass  = attributes.c_class;
        mapState = attributes.map_state;
    }

    bool CanHideRats(void) const {
        return std::find(ObscuringWindowTypeAtoms.begin(),
                         ObscuringWindowTypeAtoms.end(),
                         wmType) != ObscuringWindowTypeAtoms.end();
    }

    bool IsVisible(void) const {
        return ioClass == InputOutput && mapState == IsViewable;
    }
};

static std::map<Window, std::shared_ptr<TrackedWindow>> OldRatWindows;
static std::map<Window, std::shared_ptr<TrackedWindow>> RatWindows;
static std::map<Window, std::shared_ptr<TrackedWindow>> ObscuringWindows;

void CollectWindow(Display *display, Window window, int parentX, int parentY,
                   int *windowX, int *windowY) {
    auto it = OldRatWindows.find(window);

    std::shared_ptr<TrackedWindow> tWindow = nullptr;

    if (it != OldRatWindows.end()) {
        tWindow = it->second;
        tWindow->UpdateAttributes(parentX, parentY);
        OldRatWindows.erase(it);
    } else {
        tWindow =
            std::make_unique<TrackedWindow>(display, window, parentX, parentY);
    }

    *windowX = tWindow->x;
    *windowY = tWindow->y;

    if (!tWindow->IsVisible()) {
        return;
    }

    const char *windowType = "Window";

    // The order is important here. Rats don't satisfy the Obscuring window
    // heuristic because they don't specify _NET_WM_WINDOW_TYPE
    if (tWindow->isRat) {
        windowType = "Rat";
        RatWindows.emplace(tWindow->window, tWindow);
    } else if (tWindow->CanHideRats()) {
        ObscuringWindows.emplace(tWindow->window, tWindow);
    } else {
        return;
    }

    XTextProperty nameProp;
    XGetWMName(display, tWindow->window, &nameProp);
    printf("%s: 0x%X (%d+%d,%d+%d) %s\n", windowType,
           (unsigned int)tWindow->window, tWindow->x, tWindow->width,
           tWindow->y, tWindow->height, nameProp.value);
    XFree(nameProp.value);
}

void CollectWindowTree(Display *display, Window window, int parentX,
                       int parentY) {

    int thisWindowX, thisWindowY;
    CollectWindow(display, window, parentX, parentY, &thisWindowX,
                  &thisWindowY);

    Window       rootWindow;
    Window       parentWindow;
    Window *     childList;
    unsigned int numChildren;
    XQueryTree(display, window, &rootWindow, &parentWindow, &childList,
               &numChildren);

    for (unsigned int i = 0; i < numChildren; ++i) {
        CollectWindowTree(display, childList[i], thisWindowX, thisWindowY);
    }

    XFree(childList);
}

int main(int argc, const char *argv[]) {
    Display *const display = XOpenDisplay(nullptr);

    // The const_cast is needed here because the X API is dumb and not
    // const-correct with strings
    XInternAtoms(display,
                 const_cast<char **>(ObscuringWindowTypeStrings.data()),
                 ObscuringWindowTypeStrings.size(), false,
                 ObscuringWindowTypeAtoms.data());

    WindowClassAtom = XInternAtom(display, "WM_CLASS", false);
    WindowTypeAtom  = XInternAtom(display, "_NET_WM_WINDOW_TYPE", false);
    Atom WindowTypeSplash =
        XInternAtom(display, "_NET_WM_WINDOW_TYPE_SPLASH", false);

    Atom WMState = XInternAtom(display, "_NET_WM_STATE", false);
    Atom WMStateSkipTaskbar =
        XInternAtom(display, "_NET_WM_STATE_SKIP_TASKBAR", false);
    Atom WMStateSkipPager =
        XInternAtom(display, "_NET_WM_STATE_SKIP_PAGER", false);
    Atom WMStateBelow = XInternAtom(display, "_NET_WM_STATE_BELOW", false);

    const Window rootWindow = DefaultRootWindow(display);

    CollectWindowTree(display, rootWindow, 0, 0);

    for (const auto entry : RatWindows) {
        if (entry.second->wmType != WindowTypeSplash) {
            printf("Adopting unmodified rat window 0x%X\n",
                   (unsigned int)entry.second->window);
            XChangeProperty(
                display, entry.second->window, WindowTypeAtom, XA_ATOM, 32,
                PropModeReplace,
                reinterpret_cast<const unsigned char *>(&WindowTypeSplash), 1);

            // This unmap-map cycle kicks the WM enough for it to notice the
            // switch to WindowTypeSplash
            XUnmapWindow(display, entry.second->window);
            XMapWindow(display, entry.second->window);

            XClientMessageEvent clientEvent = {
                .type         = ClientMessage,
                .window       = entry.second->window,
                .message_type = WMState,
                .format       = 32,
            };
            clientEvent.data.l[0] = 1; // _NET_WM_STATE_ADD
            clientEvent.data.l[1] = WMStateSkipPager;
            clientEvent.data.l[2] = WMStateSkipTaskbar;
            XSendEvent(display, rootWindow, false,
                       (SubstructureNotifyMask | SubstructureRedirectMask),
                       reinterpret_cast<XEvent *>(&clientEvent));
            clientEvent.data.l[1] = WMStateBelow;
            clientEvent.data.l[2] = 0;
            XSendEvent(display, rootWindow, false,
                       (SubstructureNotifyMask | SubstructureRedirectMask),
                       reinterpret_cast<XEvent *>(&clientEvent));
        }
    }

    XFlush(display);

    XCloseDisplay(display);

    return 0;
}

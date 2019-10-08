#include <algorithm>
#include <array>

#include <string.h>

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

bool IsObscuringWindow(Display *display, Window window) {
    bool isObscuring = false;

    Atom           propType;
    int            propFormat;
    unsigned long  numItems;
    unsigned long  bytesRemaining;
    unsigned char *prop = nullptr;

    XGetWindowProperty(display, window, WindowTypeAtom, 0, sizeof(Atom), false,
                       XA_ATOM, &propType, &propFormat, &numItems,
                       &bytesRemaining, &prop);

    if (propType == XA_ATOM && numItems > 0 && prop) {
        Atom windowType;
        memcpy(&windowType, prop, sizeof(windowType));

        if (std::find(ObscuringWindowTypeAtoms.begin(),
                      ObscuringWindowTypeAtoms.end(),
                      windowType) != ObscuringWindowTypeAtoms.end()) {
            isObscuring = true;
        }
    }

    if (prop) {
        XFree(prop);
    }

    return isObscuring;
}

bool IsRatWindow(Display *display, Window window) {
    bool isRat = false;

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

    return isRat;
}

void ProcessWindow(Display *display, Window window, int parentX, int parentY,
                   int *windowX, int *windowY) {
    XWindowAttributes attributes;
    XGetWindowAttributes(display, window, &attributes);

    *windowX = parentX + attributes.x;
    *windowY = parentY + attributes.y;

    if (attributes.c_class != InputOutput ||
        attributes.map_state != IsViewable) {
        return;
    }

    const char *windowType = "Window";

    // The order is important here. Rats don't satisfy the Obscuring window
    // heuristic because they don't specify _NET_WM_WINDOW_TYPE
    if (IsRatWindow(display, window)) {
        windowType = "Rat";
    } else if (!IsObscuringWindow(display, window)) {
        return;
    }

    XTextProperty nameProp;
    XGetWMName(display, window, &nameProp);
    printf("%s: 0x%X (%d+%d,%d+%d) %s\n", windowType, (unsigned int)window,
           *windowX, attributes.width, *windowY, attributes.height,
           nameProp.value);
    XFree(nameProp.value);
}

void ProcessWindowTree(Display *display, Window window, int parentX,
                       int parentY) {

    int thisWindowX, thisWindowY;
    ProcessWindow(display, window, parentX, parentY, &thisWindowX,
                  &thisWindowY);

    Window       rootWindow;
    Window       parentWindow;
    Window *     childList;
    unsigned int numChildren;
    XQueryTree(display, window, &rootWindow, &parentWindow, &childList,
               &numChildren);

    for (unsigned int i = 0; i < numChildren; ++i) {
        ProcessWindowTree(display, childList[i], thisWindowX, thisWindowY);
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

    const Window rootWindow = DefaultRootWindow(display);

    ProcessWindowTree(display, rootWindow, 0, 0);

    return 0;
}

/* EXN is written by John Anthony <john_anthony@kingkill.com>, 2010.
 *
 * This software is in the public domain
 * and is provided AS IS, with NO WARRANTY.
 * 
 * Extra special thanks go to the dwm engineers for inspiration and
 * both the dwm engineers (from whom I stole a fair few chunks of code)
 * and Nick Welch (author of TinyWM) for providing such great learning
 * material -- without whom I would probably still be looking through
 * obscure documentation. */

#include <stdlib.h>
#include <stdarg.h>
#include <X11/Xlib.h>
#include <X11/cursorfont.h>
#include <X11/keysym.h>
#ifdef XINERAMA
#include <X11/extensions/Xinerama.h>
#endif

#include <stdio.h>

/* Macros */
#define CLEANMASK(mask)         (mask & ~(numlockmask|LockMask))
#define LENGTH(X)               (sizeof X / sizeof X[0])
#define MAX(a, b)               ((a) > (b) ? (a) : (b))

/* Datatypes */
typedef union {
    int i;
    unsigned int ui;
    float f;
    const void *v;
} Arg; 

typedef struct Monitor Monitor;
typedef struct Client Client;
struct Client{
    Window win;
    Monitor *parent;
    Client *prev, *next;
};

struct Monitor{
    int x, y, w, h;
    Monitor *mnext, *mprev;
    Client *first, *last, *current;
};

typedef struct {
    unsigned int mod;
    KeySym keysym;
    void (*func)(const Arg *);
    const Arg arg;
} Key;

/* Predecs */
static void attachwindow(Monitor *m, Window w);
static void cyclewin(const Arg *arg);
static void destroynotify(XEvent *e);
static void detachwindow(Window w);
static void die(const char *errstr, ...);
static Client* findclient(Window w);
static void grabkeys(void);
static void initmons(void);
static void keypress(XEvent *e);
static void killclient(const Arg *arg);
static void refocus(void);
static void quit(const Arg *arg);
static void mapnotify(XEvent *e);
static void monfoc(const Arg *arg);
static void monmove(const Arg *arg);
static void updatenumlockmask(void);

/* Variables */
static unsigned int numlockmask = 0;
static void (*handler[LASTEvent]) (XEvent *) = {
/*     [ButtonPress] = XXXXX, */
/*     [ClientMessage] = XXXXX, */
/*     [ConfigureRequest] = XXXXX, */
/*     [ConfigureNotify] = XXXXX, */
    [DestroyNotify] = destroynotify,
/*     [EnterNotify] = XXXXX, */
/*     [Expose] = XXXXX, */
/*     [FocusIn] = XXXXX, */
    [KeyPress] = keypress,
    [MapNotify] = mapnotify,
/*     [MapRequest] = XXXXX, */
/*     [PropertyNotify] = XXXXX, */
/*     [UnmapNotify] = XXXXX */
};
static Bool running = True;
static Display *dpy;
static Monitor *firstmon, *selmon;
static Window root;

/* Config file goes here */
#include "config.h"

void
attachwindow(Monitor* m, Window w) {

    Client *c, *find;

    if(!(c = (Client *)malloc(sizeof(Client))))
        die("Failed to allocate memory for new client instance.");

    c->win = w;
    c->next = NULL;
    c->parent = m;

    if (!m->first) {
        m->first = c;
        c->prev = NULL;
    }
    else {
        for (find = m->first; find->next; find = find->next);
        find->next = c;
        c->prev = find;
    }

    m->last = c;
    m->current = c;
}

void
cyclewin(const Arg *arg) {

    if (selmon->current)
        return;

    if (arg->i > 0) {
        if(selmon->current->next)
            selmon->current = selmon->current->next;
        else
            selmon->current = selmon->first;
    }
    else {
        if(selmon->current->prev)
            selmon->current = selmon->current->prev;
        else
            selmon->current = selmon->last;
    }

    refocus();
}

void
destroynotify(XEvent *e) {
    XDestroyWindowEvent *ev = &e->xdestroywindow;
    Window w = ev->window;
    Client *c = findclient(w);

    if (c) {
        detachwindow(c->win);
        free(c);
    }
    
    refocus();
}

void
detachwindow(Window w) {
    Client *c = findclient(w);
    Monitor *m = c->parent;

    if (!c)
        return;

    if (c->next)
        c->next->prev = c->prev;
    else                                /* Deal with the eventuality that it could be the last in the list */
        m->last = c->prev;

    if (c->prev) {
        m->current = c->prev;
        c->prev->next = c->next;
    }
    else {                               /* Deal with the eventuality of it being the first in the list */
        m->first = c->next;
        m->current = m->first;
    }
}

void
die(const char *errstr, ...) {
    va_list ap;

    va_start(ap, errstr);
    vfprintf(stderr, errstr, ap);
    va_end(ap);
    exit(EXIT_FAILURE);
}

Client *
findclient(Window w) {
    Client *c;
    Monitor *m;

    for (m = firstmon; m; m = m->mnext) {
        for (c = m->first; c; c = c->next) {
            if (c->win == w)
                return c;
        }
    }
    return NULL;
}

void
grabkeys(void) {
    updatenumlockmask();
        unsigned int i, j;
        unsigned int modifiers[] = { 0, LockMask, numlockmask, numlockmask|LockMask };
        KeyCode code;

        XUngrabKey(dpy, AnyKey, AnyModifier, root);
        for(i = 0; i < LENGTH(keys); i++) {
            if((code = XKeysymToKeycode(dpy, keys[i].keysym)))
                for(j = 0; j < LENGTH(modifiers); j++)
                    XGrabKey(dpy, code, keys[i].mod | modifiers[j], root,
                            True, GrabModeAsync, GrabModeAsync);
        }
}

void
initmons(void) {    /* Needs to be generalised using Xinerama */

    firstmon = (Monitor*)malloc(sizeof(Monitor));
    firstmon->mnext = (Monitor*)malloc(sizeof(Monitor));

    firstmon->x = 0;
    firstmon->y = 0;
    firstmon->w = 1280;
    firstmon->h = 1024;
    firstmon->first = NULL;
    firstmon->last = NULL;
    firstmon->current = NULL;

    firstmon->mnext->x = 1280;
    firstmon->mnext->y = 0;
    firstmon->mnext->w = 1920;
    firstmon->mnext->h = 1080;
    firstmon->mnext->first = NULL;
    firstmon->mnext->last = NULL;
    firstmon->mnext->current = NULL;

    firstmon->mnext->mprev = firstmon;
    firstmon->mprev = NULL;
    firstmon->mnext->mnext = NULL;

    selmon = firstmon;
}

void
keypress(XEvent *e) {
    unsigned int i;
    KeySym keysym;
    XKeyEvent *ev; 

    ev = &e->xkey;
    keysym = XKeycodeToKeysym(dpy, (KeyCode)ev->keycode, 0);
    for(i = 0; i < LENGTH(keys); i++) 
        if(keysym == keys[i].keysym
                && CLEANMASK(keys[i].mod) == CLEANMASK(ev->state)
                && keys[i].func)
            keys[i].func(&(keys[i].arg));

}

void
killclient(const Arg *arg) {

    if (!selmon->current)
        return;

    /* OUT OF ACTION until window atoms are set up correctly! */

    /* Window w = selmon->current->win; */

    /* detachwindow(&mons[selmon], w); */
    /* refocus(); */
    /* XKillClient(dpy, w); */
}

void
mapnotify(XEvent *e) {
    Window w = e->xmap.window;
    attachwindow(selmon, w);
    XMoveResizeWindow(dpy, w, selmon->x, selmon->y, selmon->w, selmon->h);
    refocus();
}

void
monmove(const Arg *arg) {

    Window w = selmon->current->win;
    Monitor *new, *old = selmon;
    monfoc(arg);
    new = selmon;
    if (new != old) {
        detachwindow(w);
        attachwindow(new, w);
        XMoveResizeWindow(dpy, w, selmon->x, selmon->y, selmon->w, selmon->h);
        refocus();
    }
}

void
monfoc(const Arg *arg) {

    Monitor *moveto;
    if (arg->i > 0)
        moveto = selmon->mnext;
    else
        moveto = selmon->mprev;

    if (moveto) {
        selmon = moveto;
        if (selmon->current)
            refocus();
    }
}

void
refocus(void) {
    /* Safety */
    if (!selmon->current)
        selmon->current = selmon->first;

    if (selmon->current) {
        XSetInputFocus(dpy, selmon->current->win, RevertToPointerRoot, CurrentTime);
        XRaiseWindow(dpy, selmon->current->win);
    }
}

void
quit(const Arg *arg) {
    running = False;
}

void
updatenumlockmask(void) {
    unsigned int i, j;
    XModifierKeymap *modmap;

    numlockmask = 0;
    modmap = XGetModifierMapping(dpy);
    for(i = 0; i < 8; i++)
        for(j = 0; j < modmap->max_keypermod; j++)
            if(modmap->modifiermap[i * modmap->max_keypermod + j] == XKeysymToKeycode(dpy, XK_Num_Lock))
                numlockmask = (1 << i);
    XFreeModifiermap(modmap);
}

int main()
{
    XEvent ev;
    XSetWindowAttributes wa;

    if(!(dpy = XOpenDisplay(0x0))) return 1;
    root = DefaultRootWindow(dpy);

    initmons();

    wa.cursor = XCreateFontCursor(dpy, XC_left_ptr);
    /* wa.event_mask = SubstructureRedirectMask|SubstructureNotifyMask|ButtonPressMask|EnterWindowMask|LeaveWindowMask|StructureNotifyMask|PropertyChangeMask; */
    wa.event_mask = SubstructureNotifyMask|ButtonPressMask|EnterWindowMask|LeaveWindowMask|StructureNotifyMask|PropertyChangeMask;
    XChangeWindowAttributes(dpy, root, CWEventMask|CWCursor, &wa);
    XSelectInput(dpy, root, wa.event_mask);
    grabkeys();
    XSync(dpy, False);

    /* Main loop */
    while(running && !XNextEvent(dpy, &ev)) {
        if(handler[ev.type])
            handler[ev.type](&ev);
    }

}

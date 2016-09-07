/*
 * clip.c
 * Copyright (C) 2016 disoul <disoul@DiSoul-Ubuntu>
 *
 * Distributed under terms of the MIT license.
 */
#include <node.h>
#include <v8.h>
#include <stdlib.h>
#include <string>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xmu/Atoms.h>

namespace clip {

using v8::FunctionCallbackInfo;
using v8::Isolate;
using v8::Local;
using v8::Object;
using v8::String;
using v8::Value;

#define XCLIB_XCOUT_NONE    0   /* no context */
#define XCLIB_XCOUT_SENTCONVSEL 1   /* sent a request */
#define XCLIB_XCOUT_INCR    2   /* in an incr loop */
#define XCLIB_XCOUT_BAD_TARGET  3   /* given target failed */

char *sdisp = NULL;
Display *dpy; /* x11 display */
XrmDatabase opt_db = NULL;  /* option database */
Atom target = XA_STRING;

static size_t mach_itemsize(int format)
{
    if (format == 8)
    return sizeof(char);
    if (format == 16)
    return sizeof(short);
    if (format == 32)
    return sizeof(long);
    return 0;
}

void errmalloc(void)
{
    //fprintf(stderr, "Error: Could not allocate memory.\n");
    exit(EXIT_FAILURE);
}

void xcmemcheck(void *ptr)
{
    if (ptr == NULL)
    errmalloc();
}

void * xcmalloc(size_t size)
{
    void *mem;

    mem = malloc(size);
    xcmemcheck(mem);

    return (mem);
}

void * xcrealloc(void *ptr, size_t size)
{
    void *mem;

    mem = realloc(ptr, size);
    xcmemcheck(mem);

    return (mem);
}

int xcout(Display * dpy,
      Window win,
      XEvent evt, Atom sel, Atom target, Atom * type, unsigned char **txt, unsigned long *len,
      unsigned int *context)
{
    /* a property for other windows to put their selection into */
    static Atom pty;
    static Atom inc;
    int pty_format;

    /* buffer for XGetWindowProperty to dump data into */
    unsigned char *buffer;
    unsigned long pty_size, pty_items, pty_machsize;

    /* local buffer of text to return */
    unsigned char *ltxt = *txt;

    if (!pty) {
    pty = XInternAtom(dpy, "XCLIP_OUT", False);
    }

    if (!inc) {
    inc = XInternAtom(dpy, "INCR", False);
    }

    switch (*context) {
    /* there is no context, do an XConvertSelection() */
    case XCLIB_XCOUT_NONE:
    /* initialise return length to 0 */
    if (*len > 0) {
        free(*txt);
        *len = 0;
    }

    /* send a selection request */
    XConvertSelection(dpy, sel, target, pty, win, CurrentTime);
    *context = XCLIB_XCOUT_SENTCONVSEL;
    return (0);

    case XCLIB_XCOUT_SENTCONVSEL:
    if (evt.type != SelectionNotify)
        return (0);

    /* return failure when the current target failed */
    if (evt.xselection.property == None) {
        *context = XCLIB_XCOUT_BAD_TARGET;
        return (0);
    }

    /* find the size and format of the data in property */
    XGetWindowProperty(dpy,
               win,
               pty,
               0,
               0,
               False,
               AnyPropertyType, type, &pty_format, &pty_items, &pty_size, &buffer);
    XFree(buffer);

    if (*type == inc) {
        /* start INCR mechanism by deleting property */
        XDeleteProperty(dpy, win, pty);
        XFlush(dpy);
        *context = XCLIB_XCOUT_INCR;
        return (0);
    }

    /* not using INCR mechanism, just read the property */
    XGetWindowProperty(dpy,
               win,
               pty,
               0,
               (long) pty_size,
               False,
               AnyPropertyType, type, &pty_format, &pty_items, &pty_size, &buffer);

    /* finished with property, delete it */
    XDeleteProperty(dpy, win, pty);

    /* compute the size of the data buffer we received */
    pty_machsize = pty_items * mach_itemsize(pty_format);

    /* copy the buffer to the pointer for returned data */
    ltxt = (unsigned char *) xcmalloc(pty_machsize);
    memcpy(ltxt, buffer, pty_machsize);

    /* set the length of the returned data */
    *len = pty_machsize;
    *txt = ltxt;

    /* free the buffer */
    XFree(buffer);

    *context = XCLIB_XCOUT_NONE;

    /* complete contents of selection fetched, return 1 */
    return (1);

    case XCLIB_XCOUT_INCR:
    /* To use the INCR method, we basically delete the
     * property with the selection in it, wait for an
     * event indicating that the property has been created,
     * then read it, delete it, etc.
     */

    /* make sure that the event is relevant */
    if (evt.type != PropertyNotify)
        return (0);

    /* skip unless the property has a new value */
    if (evt.xproperty.state != PropertyNewValue)
        return (0);

    /* check size and format of the property */
    XGetWindowProperty(dpy,
               win,
               pty,
               0,
               0,
               False,
               AnyPropertyType,
               type, &pty_format, &pty_items, &pty_size, (unsigned char **) &buffer);

    if (pty_size == 0) {
        /* no more data, exit from loop */
        XFree(buffer);
        XDeleteProperty(dpy, win, pty);
        *context = XCLIB_XCOUT_NONE;

        /* this means that an INCR transfer is now
         * complete, return 1
         */
        return (1);
    }

    XFree(buffer);

    /* if we have come this far, the propery contains
     * text, we know the size.
     */
    XGetWindowProperty(dpy,
               win,
               pty,
               0,
               (long) pty_size,
               False,
               AnyPropertyType,
               type, &pty_format, &pty_items, &pty_size, (unsigned char **) &buffer);

    /* compute the size of the data buffer we received */
    pty_machsize = pty_items * mach_itemsize(pty_format);

    /* allocate memory to accommodate data in *txt */
    if (*len == 0) {
        *len = pty_machsize;
        ltxt = (unsigned char *) xcmalloc(*len);
    }
    else {
        *len += pty_machsize;
        ltxt = (unsigned char *) xcrealloc(ltxt, *len);
    }

    /* add data to ltxt */
    memcpy(&ltxt[*len - pty_machsize], buffer, pty_machsize);

    *txt = ltxt;
    XFree(buffer);

    /* delete property to get the next item */
    XDeleteProperty(dpy, win, pty);
    XFlush(dpy);
    return (0);
    }

    return (0);
}

std::string getPasteData(Window win)
{
    Atom sel_type = None;
    unsigned char *sel_buf; /* buffer from selection data */
    unsigned long sel_len = 0;
    XEvent evt;
    unsigned int context = XCLIB_XCOUT_NONE;
    Atom sseln = XA_CLIPBOARD(dpy);

    while (1) {
        if (context != XCLIB_XCOUT_NONE)
            XNextEvent(dpy, &evt);

        xcout(dpy, win, evt, sseln, target, &sel_type, &sel_buf, &sel_len, &context);
        
        if (context == XCLIB_XCOUT_BAD_TARGET) {
            if (target == XA_UTF8_STRING(dpy)) {
                /* fallback is needed. set XA_STRING to target and restart the loop. */
                context = XCLIB_XCOUT_NONE;
                target = XA_STRING;
                continue;
            }
            else {
                /* no fallback available, exit with failure */
                char *atom_name = XGetAtomName(dpy, target);
                //fprintf(stderr, "Error: target %s not available\n", atom_name);
                XFree(atom_name);
                return "";
            }
        }

        /* only continue if xcout() is doing something */
        if (context == XCLIB_XCOUT_NONE)
            break;
    }

    if (sel_len) {
        std::string value(sel_buf, sel_buf + sel_len);
        return value;
        free(sel_buf);
    }

    return "";
}


void Method(const FunctionCallbackInfo<Value>& args)
{
    Isolate* isolate = Isolate::GetCurrent();

    Window win;

    dpy = XOpenDisplay(sdisp);

    win = XCreateSimpleWindow(dpy, DefaultRootWindow(dpy), 0, 0, 1, 1, 0, 0, 0);
    XSelectInput(dpy, win, PropertyChangeMask);

    std::string data = getPasteData(win);

    args.GetReturnValue().Set(v8::String::NewFromUtf8(isolate, data.c_str()));
}

void Init(Local<Object> exports) {
    NODE_SET_METHOD(exports, "getClipboardData", Method);
}

NODE_MODULE(clipboard, Init)
}

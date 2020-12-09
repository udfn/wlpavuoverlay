#include <epoxy/egl.h>
#include <cairo/cairo-gl.h>
#include <cairo.h>
#include <stdio.h>
#include "nwl/nwl.h"

static char nwl_egl_init(struct nwl_state *state) {
	state->egl.display = eglGetPlatformDisplayEXT(EGL_PLATFORM_WAYLAND_KHR,state->display,NULL);
	EGLint major,minor;
	if (!eglInitialize(state->egl.display, &major, &minor)) {
		fprintf(stderr,"failed to init EGL\n");
		return 1;
	}

	static const EGLint config[] = {
		EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
		EGL_RED_SIZE, 8,
		EGL_GREEN_SIZE, 8,
		EGL_BLUE_SIZE, 8,
		EGL_ALPHA_SIZE, 8,
		EGL_DEPTH_SIZE, 1,
		EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
		EGL_NONE
	};
	EGLint confignum = 0;
	if (!eglBindAPI(EGL_OPENGL_API)) {
		fprintf(stderr,"failed to bind OpenGL API\n");
		return 1;
	}
	if (!eglChooseConfig(state->egl.display, config, &state->egl.config, 1, &confignum)) {
		fprintf(stderr,"failed to choose EGL config\n");
		return 1;
	}
	EGLint attribs[] = {
		EGL_NONE
	};
	state->egl.context = eglCreateContext(state->egl.display, state->egl.config, EGL_NO_CONTEXT, attribs);
	if (!state->egl.context) {
		fprintf(stderr,"failed to create EGL context\n");
		return 1;
	}
	state->egl.cairo_dev = cairo_egl_device_create(state->egl.display, state->egl.context);
	if (!state->egl.cairo_dev) {
		fprintf(stderr,"failed to create Cairo device\n");
		return 1;
	}
	return 0;
}

void nwl_egl_uninit(struct nwl_state *state) {
	if (state->egl.context) {
		eglDestroyContext(state->egl.display, state->egl.context);
	}
	if (state->egl.cairo_dev) {
		cairo_device_destroy(state->egl.cairo_dev);
	}
	eglTerminate(state->egl.display);
}

bool nwl_egl_try_init(struct nwl_state *state) {
	if (nwl_egl_init(state)) {
		nwl_egl_uninit(state);
		state->egl.inited = 2;
		return false;
	}
	if (cairo_device_status(state->egl.cairo_dev) != CAIRO_STATUS_SUCCESS) {
		fprintf(stderr,"couldn't get Cairo device status %i\n",cairo_device_status(state->egl.cairo_dev));
		nwl_egl_uninit(state);
		state->egl.inited = 2;
		return false;
	}
	state->egl.inited = 1;
	return true;
}

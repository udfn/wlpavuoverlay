project('wlpavuoverlay', 'c',
	default_options:  [
		'c_std=c11'
	])
cc = meson.get_compiler('c')
wayland_client = dependency('wayland-client')
wayland_cursor = dependency('wayland-cursor')
cairo = dependency('cairo')
egl = dependency('egl')
gl = dependency('gl')
rt = cc.find_library('rt')
paper = cc.find_library('libm')
wlegl = dependency('wayland-egl')
epoxy = dependency('epoxy')
xkbc = dependency('xkbcommon')
pulseaudio = dependency('libpulse')
includes = include_directories(['include','protocol'])

srcfiles = [
	'main.c',
	'egl.c',
	'surface.c',
	'ui.c',
	'seat.c',
	'shell.c',
	'pulse.c',
	'wayland.c',
	'protocol/wlr-layer-shell.c',
	'protocol/xdg-shell.c',
	'protocol/xdg-decoration-unstable-v1.c',
	'protocol/viewporter.c'
]
deps = [
	wayland_client,
	wayland_cursor,
	wlegl,
	cairo,
	egl,
	gl,
	rt,
	paper,
	epoxy,
	xkbc,
	pulseaudio
]

executable('wlpavuoverlay',srcfiles,dependencies: deps, include_directories:includes,install:true)

project('wlpavuoverlay', 'c',
	default_options:  [
		'c_std=c11'
	])
# shouldn't these warnings be suppressed by setting the c_std to gnu?
add_global_arguments(['-Wno-gnu-statement-expression','-Wno-gnu-zero-variadic-macro-arguments'], language:'c')
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
pipewire = dependency('libpipewire-0.3',required:false)
includes = include_directories(['include'])
subdir('protocol')
srcfiles = [
	'main.c',
	'egl.c',
	'surface.c',
	'ui.c',
	'seat.c',
	'shell.c',
	'wayland.c'
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
]
if pipewire.found()
	srcfiles += 'pipewire.c'
	deps += pipewire
	add_project_arguments('-DHAVE_PIPEWIRE=1',language:'c')
endif
if pulseaudio.found()
	srcfiles += 'pulse.c'
	deps += pulseaudio
	add_project_arguments('-DHAVE_PULSEAUDIO=1',language:'c')
endif
# error if only one is found?
executable('wlpavuoverlay',srcfiles,[proto_c,proto_h],dependencies: deps, include_directories:includes,install:true)

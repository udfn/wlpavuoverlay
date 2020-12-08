project('nwl', 'c',
	default_options: [
		'c_std=c11'
	],
	version: '0.0 proof-of-concept'
)
cc = meson.get_compiler('c')
wayland_client = dependency('wayland-client')
wayland_cursor = dependency('wayland-cursor')
cairo = dependency('cairo')
egl = dependency('egl')
gl = dependency('gl')
rt = cc.find_library('rt')
wlegl = dependency('wayland-egl')
epoxy = dependency('epoxy')
xkbc = dependency('xkbcommon')
subdir('protocol')
nwl_src = [
	'seat.c',
	'egl.c',
	'shell.c',
	'surface.c',
	'wayland.c'
]
nwl_deps = [
	wayland_client,
	wayland_cursor,
	cairo,
	egl,
	gl,
	rt,
	wlegl,
	epoxy,
	xkbc
]
includes = include_directories(['include'])
nwl_lib = library('nwl',nwl_src,[proto_c,proto_h],dependencies:nwl_deps,include_directories:includes,install:true)
nwl_dep = declare_dependency(include_directories:includes, link_with: nwl_lib, sources:[proto_h,proto_c])
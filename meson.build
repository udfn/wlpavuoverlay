project('wlpavuoverlay', 'c',
	default_options: [
		'c_std=c11'
	],
	version: '0.2 pre-alpha'
)
# shouldn't these warnings be suppressed by setting the c_std to gnu?
cc = meson.get_compiler('c')
if cc.has_argument('-Wno-gnu-statement-expression')
	add_global_arguments(['-Wno-gnu-statement-expression','-Wno-gnu-zero-variadic-macro-arguments'], language:'c')
endif
wayland_client = dependency('wayland-client')
cairo = dependency('cairo')
egl = dependency('egl')
xkbc = dependency('xkbcommon')
pulseaudio = dependency('libpulse')
pipewire = dependency('libpipewire-0.3',required:false)
includes = include_directories(['include'])
nwlsub = subproject('nwl', required: false)
if nwlsub.found()
	nwl = nwlsub.get_variable('nwl_dep')
else
	nwl = dependency('nwl', required: true)
endif
subdir('protocol')
srcfiles = [
	'main.c',
	'ui.c',
	wlprotos
]
deps = [
	wayland_client,
	cairo,
	egl,
	xkbc,
	nwl
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
executable('wlpavuoverlay', srcfiles, dependencies: deps, include_directories:includes, install:true)

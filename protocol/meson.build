wayland_protocols = dependency('wayland-protocols')
proto_dir = wayland_protocols.get_pkgconfig_variable('pkgdatadir')
wlscanner = find_program('wayland-scanner')
# This needs to be done better..
protos = [ ]
# nwl provides these protocols if it's a subproject..
if not nwlsub.found()
	protos += [
		proto_dir / 'stable/xdg-shell/xdg-shell.xml',
		'wlr-layer-shell-unstable-v1.xml'
	]
endif
protos += [
	proto_dir / 'staging/single-pixel-buffer/single-pixel-buffer-v1.xml',
	proto_dir / 'stable/viewporter/viewporter.xml'
]
wl_gen_c = generator(wlscanner,
	output: '@BASENAME@.c',
	arguments: ['private-code', '@INPUT@', '@OUTPUT@'])
wl_gen_h = generator(wlscanner,
	output: '@BASENAME@.h',
	arguments: ['client-header', '@INPUT@', '@OUTPUT@'])
if protos.length() > 0
	wlprotos_c = wl_gen_c.process(protos)
	wlprotos_h = wl_gen_h.process(protos)
	wlprotos = [ wlprotos_c, wlprotos_h ]
else
	wlprotos = []
endif

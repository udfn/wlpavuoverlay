wlscanner = find_program('wayland-scanner')
protos = [
	'viewporter.xml',
	'wlr-layer-shell-unstable-v1.xml',
	'xdg-decoration-unstable-v1.xml',
	'xdg-shell.xml'
]
wl_gen_c = generator(wlscanner,
	output: '@BASENAME@.c',
	arguments: ['private-code', '@INPUT@', '@OUTPUT@'])
wl_gen_h = generator(wlscanner,
	output: '@BASENAME@.h',
	arguments: ['client-header', '@INPUT@', '@OUTPUT@'])
proto_c = wl_gen_c.process(protos)
proto_h = wl_gen_h.process(protos)

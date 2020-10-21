project(
	'kanshi',
	'c',
	version: '1.1.0',
	license: 'MIT',
	meson_version: '>=0.47.0',
	default_options: [
		'c_std=c99',
		'warning_level=3',
		'werror=true',
	],
)

cc = meson.get_compiler('c')

add_project_arguments(cc.get_supported_arguments([
	'-Wundef',
	'-Wlogical-op',
	'-Wmissing-include-dirs',
	'-Wold-style-definition',
	'-Wpointer-arith',
	'-Winit-self',
	'-Wfloat-equal',
	'-Wstrict-prototypes',
	'-Wredundant-decls',
	'-Wimplicit-fallthrough=2',
	'-Wendif-labels',
	'-Wstrict-aliasing=2',
	'-Woverflow',
	'-Wformat=2',

	'-Wno-missing-braces',
	'-Wno-missing-field-initializers',
	'-Wno-unused-parameter',
]), language: 'c')

wayland_client = dependency('wayland-client')
scfg = dependency('scfg')

subdir('protocol')

executable(
	meson.project_name(),
	files(
		'main.c',
		'parser.c',
	),
	include_directories: include_directories('include'),
	dependencies: [wayland_client, client_protos, scfg],
	install: true,
)

scdoc = dependency(
	'scdoc',
	version: '>=1.9.2',
	native: true,
	required: get_option('man-pages'),
)
if scdoc.found()
	scdoc_prog = find_program(scdoc.get_pkgconfig_variable('scdoc'), native: true)
	sh = find_program('sh', native: true)
	mandir = get_option('mandir')
	man_files = [
		'kanshi.1.scd',
		'kanshi.5.scd',
	]
	foreach filename : man_files
		topic = filename.split('.')[-3].split('/')[-1]
		section = filename.split('.')[-2]
		output = '@0@.@1@'.format(topic, section)

		custom_target(
			output,
			input: filename,
			output: output,
			command: [
				sh, '-c', '@0@ < @INPUT@ > @1@'.format(scdoc_prog.path(), output)
			],
			install: true,
			install_dir: '@0@/man@1@'.format(mandir, section)
		)
	endforeach
endif

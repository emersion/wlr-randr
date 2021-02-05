project(
  'wlr-randr',
  'c',
version : '0.2.0',
license : 'MIT',
meson_version : '>=0.47.0',
default_options : ['c_std=c99', 'warning_level=3', 'werror=true']
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

math = cc.find_library('m', required: false)

subdir('protocol')

wlr_randr_exe = executable(
	meson.project_name(),
	files(['main.c']),
	dependencies: [wayland_client, client_protos, math],
	install: true,
)

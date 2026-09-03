#ifndef DEFAULT_CONFIG_H
#define DEFAULT_CONFIG_H

static const char *DEFAULT_CONFIG_HEAD =
"[global]\n"
"retro=false\n"
"# colors follow retro=, uncomment to override:\n"
"#background_color=0D1117\n"
"#panel_color=161C24\n"
"#grid_color=2C3742\n"
"#text_color=DBE4EE\n"
"#text_dim_color=8B98A6\n"
"#border_color=2C3742\n"
"#line_color=4CD38A\n"
"#line_color_secondary=4FC3F7\n"
"#error_line_color=EF5350\n"
"default_height=80\n"
"default_width=300\n"
"refresh_interval_sec=10\n"
"window_margin=5\n"
"max_fps=2\n"
"fullscreen=false\n"
"fps_counter=false\n"
"font_size=1.0\n"
"#http_server=true\n"
"#http_port=8080\n"
"\n"
"[targets]\n";

#ifdef DS_MINIMAL
static const char *DEFAULT_CONFIG_DEFGW = "";
static const char *DEFAULT_CONFIG_TAIL =
"clock=24\n";
#elif defined(__VMS)
/* loadavg needs the VMS os layer, not implemented yet */
static const char *DEFAULT_CONFIG_DEFGW = "";
static const char *DEFAULT_CONFIG_TAIL =
"ping=1.1.1.1\n"
"tcp=1.1.1.1:443\n"
"cpu=local\n"
"memory=local\n"
"clock=24\n";
#else
static const char *DEFAULT_CONFIG_DEFGW = "ping=0.0.0.0\n";
static const char *DEFAULT_CONFIG_TAIL =
"ping=1.1.1.1\n"
"cpu=local\n"
"memory=local\n"
"loadavg=local\n";
#endif

#endif

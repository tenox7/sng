#ifndef DEFAULT_CONFIG_H
#define DEFAULT_CONFIG_H

static const char *DEFAULT_CONFIG_INI =
"[global]\n"
"background_color=646464\n"
"text_color=FFFFFF\n"
"border_color=FFFFFF\n"
"line_color=00FF00\n"
"line_color_secondary=FF8000\n"
"error_line_color=FF0000\n"
"default_height=100\n"
"default_width=400\n"
"refresh_interval_sec=10\n"
"window_margin=5\n"
"max_fps=2\n"
"fullscreen=false\n"
"fps_counter=false\n"
"font_size=1.0\n"
"\n"
"[targets]\n"
"ping=1.1.1.1\n"
"ping=8.8.8.8\n"
"cpu=local\n"
"loadavg=local\n";

#endif

#ifndef DEFAULT_CONFIG_H
#define DEFAULT_CONFIG_H

static const char *DEFAULT_CONFIG_HEAD =
"[global]\n"
"background_color=646464\n"
"text_color=FFFFFF\n"
"border_color=FFFFFF\n"
"line_color=00FF00\n"
"line_color_secondary=FF8000\n"
"error_line_color=FF0000\n"
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
static const char *DEFAULT_CONFIG_BW = "";

static const char *DEFAULT_CONFIG_TAIL =
"clock=24\n";
#elif defined(__VMS)
/* loadavg needs the VMS os layer, not implemented yet */
static const char *DEFAULT_CONFIG_DEFGW = "";
static const char *DEFAULT_CONFIG_BW = "";

static const char *DEFAULT_CONFIG_TAIL =
"ping=1.1.1.1\n"
"tcp=1.1.1.1:443\n"
"cpu=local\n"
"memory=local\n"
"clock=24\n";
#else
static const char *DEFAULT_CONFIG_DEFGW = "ping=0.0.0.0\n";

/* bw=local,all needs a kernel interface enumeration, which HP-UX 10.20,
 * IRIX 5 and Tru64 do not have */
#if defined(HPUX10) || defined(IRIX5) || defined(__osf__) || defined(__OSF1__)
static const char *DEFAULT_CONFIG_BW = "";
#else
static const char *DEFAULT_CONFIG_BW = "bw=local,all\n";
#endif

static const char *DEFAULT_CONFIG_TAIL =
"ping=1.1.1.1\n"
"cpu=local\n"
"memory=local\n"
"loadavg=local\n";
#endif

#endif

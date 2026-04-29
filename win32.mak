CC=cl
CFLAGS=/DGFX_WIN32 /D_WIN32 /DNO_SHELL /D_CRT_SECURE_NO_WARNINGS /I. /W3 /nologo
LDFLAGS=/nologo
LIBS=user32.lib gdi32.lib kernel32.lib ws2_32.lib iphlpapi.lib pdh.lib

OBJS=main.obj graphics.obj config.obj plot.obj ringbuf.obj threading.obj \
     ini_parser.obj datasource.obj clock.obj snmp_client.obj ping.obj \
     cpu.obj memory.obj snmp.obj if_thr.obj loadavg.obj defgw.obj win32.obj

TARGET=sng.exe

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(LDFLAGS) $(OBJS) $(LIBS) /Fe$(TARGET) /link /SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup

main.obj: main.c
	$(CC) $(CFLAGS) /c main.c

graphics.obj: graphics.c
	$(CC) $(CFLAGS) /c graphics.c

config.obj: config.c
	$(CC) $(CFLAGS) /c config.c

plot.obj: plot.c
	$(CC) $(CFLAGS) /c plot.c

ringbuf.obj: ringbuf.c
	$(CC) $(CFLAGS) /c ringbuf.c

threading.obj: threading.c
	$(CC) $(CFLAGS) /c threading.c

ini_parser.obj: ini_parser.c
	$(CC) $(CFLAGS) /c ini_parser.c

datasource.obj: datasource.c
	$(CC) $(CFLAGS) /c datasource.c

clock.obj: ds\clock.c
	$(CC) $(CFLAGS) /c ds\clock.c

snmp_client.obj: ds\snmp_client.c
	$(CC) $(CFLAGS) /c ds\snmp_client.c

ping.obj: ds\ping.c
	$(CC) $(CFLAGS) /c ds\ping.c

cpu.obj: ds\cpu.c
	$(CC) $(CFLAGS) /c ds\cpu.c

memory.obj: ds\memory.c
	$(CC) $(CFLAGS) /c ds\memory.c

snmp.obj: ds\snmp.c
	$(CC) $(CFLAGS) /c ds\snmp.c

if_thr.obj: ds\if_thr.c
	$(CC) $(CFLAGS) /c ds\if_thr.c

loadavg.obj: ds\loadavg.c
	$(CC) $(CFLAGS) /c ds\loadavg.c

defgw.obj: os\defgw.c
	$(CC) $(CFLAGS) /c os\defgw.c

win32.obj: os\win32.c
	$(CC) $(CFLAGS) /c os\win32.c

clean:
	del *.obj
	del ds\*.obj
	del os\*.obj
	del $(TARGET)

!ifdef RETAIL
cflags=/Sm /O /J /Gm /Gs /Ss /Kb /N48 /G4
linkcmd=link386 /MAP /LI /F /A:16 /base:65536 /NOI @$*.lnk
!else
cflags=/Sm /Ti /J /Gm /Ss /Kb /N48 /G4
linkcmd=link386 /CO /base:65536 /NOI /MAP @$*.lnk
!endif


all: am4pm.tot am4pm.msg am4pmcmd.exe

am4pm.msg: $*.mst
  mkmsgf $*.mst $*.msg

am4pm.obj: $*.c am4pm.h am4pmdlg.h
  icc $(cflags) $*.c /c

am4pmw.obj: $*.c am4pm.h
  icc $(cflags) $*.c /c

am4pmr.obj: $*.c am4pm.h
  icc $(cflags) $*.c /c

pmlog.obj: $*.c am4pm.h
  icc $(cflags) $*.c /c

am4pm.res : $*.rc $*.ico $*.dlg $*dlg.h msg.ico
     rc -r $*.rc

am4pm.exe: am4pm.obj am4pmr.obj am4pmw.obj pmlog.obj am4pm.def
  $(linkcmd)

am4pm.tot : $*.res $*.exe
     rc $*.res
     echo $@ created > $@

am4pmcmd.exe: am4pm.h
  icc $(cflags) $*.c



REM Web: a window on the pages src/web.c fetches and lays out
REM A toolbar pane sits over the top of the page window
DIM b% 1024, u% 192, title% 64, fv% 80, stat% 240, info% 240, line% 512, ibuf% 40, spare% 256
swi%=&CDC00
lineh%=36
charw%=16
barh%=64
cols%=76
quit%=FALSE: pagestep%=500: lines%=0: oldstate%=0: bytes%=0
$u%="about:home"
$stat%=""
$title%="Web"
SYS "Wimp_Initialise",200,&4B534154,"Web" TO ,task%
PROCwindows
PROCiconbar("!web")
PROCopen
SYS swi%+17,u%
REPEAT
  SYS "OS_ReadMonotonicTime" TO now%
  SYS "Wimp_PollIdle",0,b%,now%+10 TO r%
  CASE r% OF
    WHEN 0: PROCtick
    WHEN 1: PROCredraw
    WHEN 2: PROCreopen
    WHEN 3: IF !b%=box% THEN PROCboxclose ELSE PROChide
    WHEN 6: IF b%!12=-2 THEN PROCbarclick ELSE PROCclick
    WHEN 8: PROCkey(b%!0,b%!24)
    WHEN 9: quit%=TRUE
    WHEN 17,18: IF b%!16=0 THEN quit%=TRUE
  ENDCASE
UNTIL quit%
SYS "Wimp_CloseDown",task%,&4B534154
END

DEF PROCicon(x0%,y0%,x1%,y1%,flags%,text%,valid%,len%)
b%!p%=x0%: b%!(p%+4)=y0%: b%!(p%+8)=x1%: b%!(p%+12)=y1%
b%!(p%+16)=flags%: b%!(p%+20)=text%: b%!(p%+24)=valid%: b%!(p%+28)=len%
p%+=32
ENDPROC

DEF FNtext(a$)
LOCAL t%
DIM t% LEN(a$)+1
$t%=a$
=t%


DEF PROCkey(w%,k%)
CASE k% OF
  WHEN 13: IF w%=box% THEN PROCboxsend ELSE SYS swi%+17,u%
  WHEN 27: IF w%=box% THEN PROCboxclose ELSE SYS "Wimp_ProcessKey",k%
  WHEN &18F: PROCscroll(lineh%*3)
  WHEN &18E: PROCscroll(-lineh%*3)
  WHEN &19F: PROCscroll(pagestep%)
  WHEN &19E: PROCscroll(-pagestep%)
  OTHERWISE: SYS "Wimp_ProcessKey",k%
ENDCASE
ENDPROC

DEF PROCscroll(d%)
b%!0=page%
SYS "Wimp_GetWindowState",,b%
pagestep%=b%!16-b%!8-barh%-lineh%
b%!24=b%!24+d%
IF b%!24>0 THEN b%!24=0
b%!28=-1
PROCreopen
ENDPROC

REM A text box on a page is edited here, and Return sends its form
DEF PROCboxopen
SYS swi%+23,fv%
!ibuf%=box%: ibuf%!4=0: ibuf%!8=0: ibuf%!12=0
SYS "Wimp_SetIconState",,ibuf%
!b%=box%
SYS "Wimp_GetWindowState",,b%
b%!28=-1
SYS "Wimp_OpenWindow",,b%
SYS "Wimp_SetCaretPosition",box%,0,0,0,-1,LEN(FNstr(fv%))
ENDPROC

DEF PROCboxclose
!ibuf%=box%
SYS "Wimp_CloseWindow",,ibuf%
SYS "Wimp_SetCaretPosition",bar%,2,0,0,-1,LEN(FNstr(u%))
ENDPROC

DEF PROCboxsend
SYS swi%+24,fv%
PROCboxclose
ENDPROC

DEF PROCiconbar(sprite$)
DIM menu% 64
b%!0=-1: b%!4=0: b%!8=0: b%!12=68: b%!16=68: b%!20=&3002
$(b%+24)=sprite$
SYS "Wimp_CreateIcon",,b%
$menu%="Web": menu%?12=7: menu%?13=2: menu%?14=7: menu%?15=0
menu%!16=112: menu%!20=44: menu%!24=0
menu%!28=&80: menu%!32=-1: menu%!36=&07000021
$(menu%+40)="Quit"
ENDPROC

DEF PROCbarclick
IF b%!8=2 THEN
  SYS "Wimp_CreateMenu",,menu%,b%!0-56,140
ELSE
  PROCopen
ENDIF
ENDPROC

REM Closing a window leaves the app on the icon bar
DEF PROChide
!ibuf%=bar%
SYS "Wimp_CloseWindow",,ibuf%
!ibuf%=page%
SYS "Wimp_CloseWindow",,ibuf%
ENDPROC

DEF PROCcolours
b%?32=7: b%?33=2: b%?34=7: b%?35=0: b%?36=3: b%?37=1: b%?38=12: b%?39=0
ENDPROC

DEF PROCwindows
LOCAL valid%
DIM valid% 8
$valid%="R7"
REM The page: title bar, scroll bar and a fixed width, the toolbar sits inside its top
b%!0=20: b%!4=120: b%!8=1260: b%!12=900
b%!16=0: b%!20=0: b%!24=-1: b%!28=&97000002
PROCcolours
b%!40=0: b%!44=-1024: b%!48=charw%*cols%+32: b%!52=0
b%!56=&2700013D: b%!60=3<<12: b%!64=1: b%!68=0
b%!72=title%: b%!76=-1: b%!80=64
b%!84=0
SYS "Wimp_CreateWindow",,b% TO page%
REM The toolbar has no furniture, and moves with the page
b%!0=20: b%!4=836: b%!8=1260: b%!12=900
b%!16=0: b%!20=0: b%!24=-1: b%!28=&80000000
PROCcolours
b%?35=1
b%!40=0: b%!44=-barh%: b%!48=1280: b%!52=0
b%!56=0: b%!60=0: b%!64=1: b%!68=0
b%!72=0: b%!76=0: b%!80=0
p%=88
PROCicon(8,-58,104,-6,&1700313D,FNtext("Back"),-1,5)
PROCicon(112,-58,216,-6,&1700313D,FNtext("Reload"),-1,7)
PROCicon(224,-58,1228,-6,&0700F135,u%,valid%,192)
b%!84=3
SYS "Wimp_CreateWindow",,b% TO bar%
REM The text box editor
b%!0=260: b%!4=470: b%!8=1020: b%!12=570
b%!16=0: b%!20=0: b%!24=-1: b%!28=&87000002
PROCcolours
b%?35=1
b%!40=0: b%!44=-100: b%!48=760: b%!52=0
b%!56=&2700003D: b%!60=0: b%!64=1: b%!68=0
$(b%+72)="Enter text"
p%=88
PROCicon(16,-76,744,-20,&0700F135,fv%,valid%,64)
b%!84=1
SYS "Wimp_CreateWindow",,b% TO box%
ENDPROC

DEF PROCopen
b%!0=page%
SYS "Wimp_GetWindowState",,b%
b%!28=-1
PROCreopen
SYS "Wimp_SetCaretPosition",bar%,2,0,0,-1,LEN(FNstr(u%))
ENDPROC

REM Opens the page as asked, with the toolbar on top of it, aligned to its top edge
DEF PROCreopen
LOCAL h%
h%=b%!28
!spare%=bar%
spare%!4=b%!4: spare%!8=b%!16-barh%: spare%!12=b%!12-28: spare%!16=b%!16
spare%!20=0: spare%!24=0: spare%!28=h%
SYS "Wimp_OpenWindow",,spare%
b%!28=bar%
SYS "Wimp_OpenWindow",,b%
ENDPROC

DEF FNstr(a%)
LOCAL s$
WHILE ?a%>=32 AND LEN(s$)<200: s$+=CHR$(?a%): a%+=1: ENDWHILE
=s$

DEF PROCtick
LOCAL st%,n%,got%
SYS swi%+19 TO st%,n%,got%
IF st%=2 AND oldstate%<>2 THEN PROCloaded(n%)
IF st%=1 OR st%<>oldstate% THEN
  SYS swi%+20,info%
  IF st%=1 THEN PROCsettitle(FNstr(info%+192)+" "+STR$(got% DIV 1024)+"KB")
  IF st%=3 THEN PROCsettitle(FNstr(info%+192))
ENDIF
oldstate%=st%
ENDPROC

DEF PROCsettitle(a$)
$title%=LEFT$(a$,62)
!ibuf%=page%
SYS "Wimp_GetWindowOutline",,ibuf%
SYS "Wimp_ForceRedraw",-1,ibuf%!4,ibuf%!16-40,ibuf%!12,ibuf%!16
ENDPROC

DEF PROCloaded(n%)
LOCAL h%
lines%=n%
SYS swi%+20,info%
$u%=FNstr(info%+64)
PROCsettitle(FNstr(info%))
!ibuf%=bar%: ibuf%!4=2: ibuf%!8=0: ibuf%!12=0
SYS "Wimp_SetIconState",,ibuf%
SYS "Wimp_SetCaretPosition",bar%,2,0,0,-1,LEN($u%)
h%=lines%*lineh%+barh%+16
IF h%<1024 THEN h%=1024
!ibuf%=0: ibuf%!4=-h%: ibuf%!8=charw%*cols%+32: ibuf%!12=0
SYS "Wimp_SetExtent",page%,ibuf%
b%!0=page%
SYS "Wimp_GetWindowState",,b%
b%!24=0
PROCreopen
SYS "Wimp_ForceRedraw",page%,0,-h%,charw%*cols%+32,0
ENDPROC

DEF PROCclick
LOCAL x%,y%,sc%
IF b%!12=bar% THEN
  IF b%!16=0 THEN SYS swi%+18
  IF b%!16=1 THEN SYS swi%+17,u%
  ENDPROC
ENDIF
IF b%!12=box% THEN ENDPROC
IF b%!12<>page% THEN ENDPROC
x%=b%!0: y%=b%!4
!ibuf%=page%
SYS "Wimp_GetWindowState",,ibuf%
x%=(x%-(ibuf%!4-ibuf%!20)-8) DIV charw%
y%=(ibuf%!16-ibuf%!24-y%-barh%-8) DIV lineh%
SYS swi%+22,y%,x% TO sc%
IF sc%=2 THEN PROCboxopen
ENDPROC

DEF PROCredraw
LOCAL more%,ox%,oy%,first%,last%,i%
SYS "Wimp_RedrawWindow",,b% TO more%
ox%=b%!4-b%!20
oy%=b%!16-b%!24
WHILE more%
  first%=(oy%-b%!40-barh%-8) DIV lineh%
  last%=(oy%-b%!32-barh%-8) DIV lineh%
  IF first%<0 THEN first%=0
  IF last%>=lines% THEN last%=lines%-1
  FOR i%=first% TO last%
    PROCdrawline(i%,ox%+8,oy%-barh%-8-i%*lineh%)
  NEXT
  SYS "Wimp_GetRectangle",,b% TO more%
ENDWHILE
VDU 4
ENDPROC

REM A line is runs of: column, length, style, link high, link low, then the text
DEF PROCdrawline(n%,x%,y%)
LOCAL got%,a%,len%,st%,cx%,s$,k%,w%
SYS swi%+21,n%,line% TO got%
a%=line%
VDU 5
WHILE a%<line%+got%
  cx%=x%+?a%*charw%
  len%=a%?1: st%=a%?2
  s$=""
  FOR k%=0 TO len%-1: s$+=CHR$(a%?(5+k%)): NEXT
  a%+=5+len%
  w%=7
  IF st%=2 THEN w%=8
  IF st%=3 THEN w%=5
  IF st%=4 THEN w%=4
  IF st%=5 THEN
    SYS "Wimp_SetColour",12
    RECTANGLE FILL cx%-2,y%-34,len%*charw%+2,32
    w%=7
  ENDIF
  SYS "Wimp_SetColour",w%
  IF st%=5 THEN RECTANGLE cx%-2,y%-34,len%*charw%+2,32
  MOVE cx%,y%-4
  PRINT s$;
  IF st%=1 THEN MOVE cx%+2,y%-4: PRINT s$;
  IF st%=2 THEN LINE cx%,y%-34,cx%+len%*charw%-2,y%-34
ENDWHILE
ENDPROC

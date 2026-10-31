REM Ping: pings a host once a second and graphs the answers
REM The DreamNet SWIs are described in docs/ARCHITECTURE.md
DIM b% 1024, host% 64, stat% 80, ibuf% 40, net% 80
swi%=&CDC00
HISTORY=40
DIM rtt%(HISTORY-1), log$(5)
running%=FALSE: resolving%=FALSE: seq%=0: head%=0
sent%=0: got%=0: lo%=0: hi%=0: tot%=0: waiting%=FALSE
ip%=0: sentat%=0: quit%=FALSE
FOR i%=0 TO HISTORY-1: rtt%(i%)=-1: NEXT
$host%="192.168.1.1"
$stat%="Stopped"
SYS swi%+0,net%
IF !net%=2 AND net%?16<>0 THEN $host%=FNdots(net%+16)
SYS "Wimp_Initialise",200,&4B534154,"Ping" TO ,task%
PROCwindow
PROCopen
PROCshownet
REPEAT
  SYS "OS_ReadMonotonicTime" TO now%
  SYS "Wimp_PollIdle",0,b%,now%+25 TO r%
  CASE r% OF
    WHEN 0: PROCtick
    WHEN 1: PROCredraw
    WHEN 2: SYS "Wimp_OpenWindow",,b%
    WHEN 3: quit%=TRUE
    WHEN 6: IF b%!16=2 THEN PROCstart
            IF b%!16=3 THEN PROCstop
    WHEN 8: IF b%!24=13 THEN PROCstart ELSE SYS "Wimp_ProcessKey",b%!24
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

DEF PROCwindow
LOCAL i%, label%, start%, stop%, valid%
DIM label% 8, start% 8, stop% 8, valid% 8
$label%="Host": $start%="Start": $stop%="Stop": $valid%="R7"
b%!0=120: b%!4=200: b%!8=880: b%!12=840
b%!16=0: b%!20=0: b%!24=-1: b%!28=&87000002
b%?32=7: b%?33=2: b%?34=7: b%?35=1: b%?36=3: b%?37=1: b%?38=12: b%?39=0
b%!40=0: b%!44=-640: b%!48=760: b%!52=0
b%!56=&2700003D: b%!60=0: b%!64=1: b%!68=0
$(b%+72)="Ping"
b%!84=5
p%=88
PROCicon(16,-60,96,-12,&17000111,label%,-1,8)
PROCicon(100,-60,500,-12,&0700F13D,host%,valid%,64)
PROCicon(516,-60,624,-12,&1700313D,start%,-1,8)
PROCicon(636,-60,744,-12,&1700313D,stop%,-1,8)
PROCicon(16,-112,744,-68,&17000131,stat%,-1,80)
SYS "Wimp_CreateWindow",,b% TO win%
ENDPROC

DEF PROCopen
b%!0=win%
SYS "Wimp_GetWindowState",,b%
b%!28=-1
SYS "Wimp_OpenWindow",,b%
SYS "Wimp_SetCaretPosition",win%,1,0,0,-1,LEN($host%)
ENDPROC

DEF FNstr(a%)
LOCAL s$
WHILE ?a%>=32: s$+=CHR$(?a%): a%+=1: ENDWHILE
=s$

DEF FNquad(v%)
=STR$((v%>>>24))+"."+STR$((v%>>>16)AND255)+"."+STR$((v%>>>8)AND255)+"."+STR$(v% AND255)

DEF PROCstatus(a$)
$stat%=LEFT$(a$,78)
SYS "Wimp_SetIconState",,FNiconblock(4)
ENDPROC

DEF FNiconblock(n%)
!ibuf%=win%: ibuf%!4=n%: ibuf%!8=0: ibuf%!12=0
=ibuf%

DEF PROCshownet
SYS swi%+0,net%
IF !net%=2 THEN PROCstatus("Connected as "+FNdots(net%+8)) ELSE PROCstatus(FNstr(net%+48))
ENDPROC

DEF FNdots(a%)
=STR$(?a%)+"."+STR$(a%?1)+"."+STR$(a%?2)+"."+STR$(a%?3)

DEF PROClog(a$)
LOCAL i%
FOR i%=0 TO 4: log$(i%)=log$(i%+1): NEXT
log$(5)=a$
ENDPROC

DEF PROCstart
LOCAL i%
IF running% OR resolving% THEN ENDPROC
FOR i%=0 TO HISTORY-1: rtt%(i%)=-1: NEXT
sent%=0: got%=0: lo%=0: hi%=0: tot%=0: head%=0: waiting%=FALSE
SYS swi%+5,host%
resolving%=TRUE
PROCstatus("Looking up "+FNstr(host%))
PROCrefresh
ENDPROC

DEF PROCstop
running%=FALSE: resolving%=FALSE
PROCstatus("Stopped")
ENDPROC

DEF PROCtick
LOCAL st%,v%,t%,ms%
IF resolving% THEN
  SYS swi%+6 TO st%,v%
  IF st%=1 THEN
    resolving%=FALSE: running%=TRUE: ip%=v%: sentat%=0
    PROCstatus("Pinging "+FNquad(ip%))
  ENDIF
  IF st%<0 THEN
    resolving%=FALSE
    PROCstatus("Could not find "+FNstr(host%))
  ENDIF
ENDIF
IF NOT running% THEN
  PROCshownetidle
  ENDPROC
ENDIF
SYS "OS_ReadMonotonicTime" TO t%
IF waiting% THEN
  SYS swi%+4,seq% TO st%,v%
  IF st%=1 THEN
    waiting%=FALSE: got%+=1
    ms%=(v%+500) DIV 1000
    tot%+=ms%
    IF got%=1 OR ms%<lo% THEN lo%=ms%
    IF ms%>hi% THEN hi%=ms%
    PROCrecord(ms%)
    PROClog("Reply from "+FNquad(ip%)+": seq "+STR$(seq%)+", "+STR$(ms%)+" ms")
    PROCrefresh
  ELSE
    IF t%-sentat%>=200 THEN
      waiting%=FALSE
      PROCrecord(-2)
      PROClog("No reply: seq "+STR$(seq%))
      PROCrefresh
    ENDIF
  ENDIF
ENDIF
IF NOT waiting% AND t%-sentat%>=100 THEN
  seq%=(seq%+1) AND &FFFF
  SYS swi%+3,ip%,seq%
  sent%+=1: sentat%=t%: waiting%=TRUE
ENDIF
ENDPROC

DEF PROCshownetidle
IF LEFT$($stat%,7)="Stopped" OR LEFT$($stat%,7)="Connect" OR LEFT$($stat%,5)="Could" THEN ENDPROC
PROCshownet
ENDPROC

DEF PROCrecord(v%)
rtt%(head%)=v%
head%=(head%+1) MOD HISTORY
ENDPROC

DEF PROCrefresh
SYS "Wimp_ForceRedraw",win%,0,-640,760,-116
ENDPROC

DEF PROCredraw
LOCAL more%,ox%,oy%
SYS "Wimp_RedrawWindow",,b% TO more%
ox%=b%!4-b%!20
oy%=b%!16-b%!24
WHILE more%
  PROCdraw(ox%,oy%)
  SYS "Wimp_GetRectangle",,b% TO more%
ENDWHILE
ENDPROC

DEF PROCdraw(ox%,oy%)
LOCAL i%,n%,v%,x%,h%,top%,y%,scale%
y%=oy%-340
scale%=hi%
IF scale%<10 THEN scale%=10
SYS "Wimp_SetColour",0
RECTANGLE FILL ox%+16,y%,728,200
SYS "Wimp_SetColour",7
RECTANGLE ox%+16,y%,728,200
FOR i%=0 TO HISTORY-1
  n%=(head%+i%) MOD HISTORY
  v%=rtt%(n%)
  x%=ox%+24+i%*18
  IF v%=-2 THEN
    SYS "Wimp_SetColour",11
    RECTANGLE FILL x%,y%+4,12,192
  ENDIF
  IF v%>=0 THEN
    h%=v%*184 DIV scale%+4
    SYS "Wimp_SetColour",10
    RECTANGLE FILL x%,y%+4,12,h%
  ENDIF
NEXT
SYS "Wimp_SetColour",7
VDU 5
MOVE ox%+24,oy%-120
top%=oy%-352
IF got%>0 THEN
  MOVE ox%+16,top%
  PRINT "Sent ";sent%;"  Replies ";got%;"  Min ";lo%;"  Avg ";tot% DIV got%;"  Max ";hi%;" ms";
ELSE
  MOVE ox%+16,top%
  PRINT "Sent ";sent%;"  Replies 0";
ENDIF
FOR i%=0 TO 5
  MOVE ox%+16,top%-48-i%*36
  PRINT log$(i%);
NEXT
VDU 4
ENDPROC

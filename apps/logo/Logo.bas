REM Logo for RISC OS: a Wimp task holding a turtle canvas, a log and a command line.
REM See apps/README.md.
REM
REM Values are tagged integers, low two bits: 0 list (cell*4, 0 is empty), 1 word
REM (symbol*4+1), 2 small integer (n*4+2), 3 float (index*4+3). Anything live across
REM an allocation must be on VS%, as the collector only sees its roots.
DIM E% 256
ON ERROR !E%=ERR:$(E%+4)=REPORT$+" at line "+STR$ERL+CHR$0:SYS "Wimp_ReportError",E%,1,"Logo":END
PROCinit
ON ERROR PROCfault
REPEAT
PROCpoll(1)
UNTIL quit%
SYS "Wimp_CloseDown",task%,&4B534154
END

DEF PROCinit
LOCAL i%
NS%=0:VP%=0:BP%=0:RD%=0:FL%=0:I%=0:zLN%=0:RET%=0:DF%=0:DB%=0:DP%=0:DA%=0:DT2%=0:c0%=0
HD%=0:PU%=0:SW%=0:DY%=0:LD%=0:zRUN%=0:ST%=0:quit%=0:QUIET%=0:FH%=0:BT%=0:RC%=0:YT%=0:HP%=-1:LC$=""
D0%=0:D1%=0:D2%=0:D3%=0:ev%=0
NC%=10000:NF%=1500:NV%=2000:NB%=2000:NY%=1200
DIM CA%(NC%),CD%(NC%),FV(NF%),FX%(NF%),VS%(NV%),BS%(NB%),BV%(NB%)
DIM MC% NC%+1,MF% NF%+1
DIM NM$(NY%),HN%(NY%),KD%(NY%),LK%(NY%),UP%(NY%),QV%(NY%),OP%(NY%)
DIM SV%(NY%),PB%(NY%),PP%(NY%),PA%(NY%),PC%(NY%),PV%(NY%),HT%(127)
DIM RH%(32),RT%(32),LG$(5),HS$(7),DN$(5)
DIM DT% 256,Q% 300,Z% 300,MM% 200,MD% 200,SN% 12,SC% 16,PT% 16
DIM EB% 256
VOID%=-4:UNB%=-8
FOR i%=1 TO NC%:CD%(i%)=i%+1:NEXT
CD%(NC%)=0:FC%=1
FOR i%=1 TO NF%:FX%(i%)=i%+1:NEXT
FX%(NF%)=0:FF%=1
FOR i%=1 TO LEN(" []()"):DT%?ASC(MID$(" []()",i%,1))=2:NEXT
FOR i%=1 TO LEN("+-*/=<>;"):DT%?ASC(MID$("+-*/=<>;",i%,1))=1:NEXT
DT%?9=2
PROCops
PROCprims
TR%=FNsym("TRUE")*4+1:FA%=FNsym("FALSE")*4+1
QTO%=FNsym("TO"):QEND%=FNsym("END")
PROCcanvas
PROCwimp
PROCout("Welcome to Logo. Try the Menu button, or type:")
PROCout("REPEAT 4 [FD 100 RT 90]")
ENDPROC

DEF PROCops
LOCAL o%,n$
READ o%
WHILE o%>=0
READ n$
OP%(FNsym(n$))=o%
READ o%
ENDWHILE
KD%(FNsym("("))=3:KD%(FNsym(")"))=5
QNEG%=FNsym("-neg"):KD%(QNEG%)=4
QOPEN%=FNsym("("):QCLOSE%=FNsym(")")
ENDPROC
DATA 33,+,34,-,51,*,52,/,21,=,22,<,23,>,24,<=,25,>=,26,<>,-1

DEF PROCprims
LOCAL n$,a%,c%,s%
READ n$,a%,c%
WHILE c%>0
s%=FNsym(n$)
PC%(s%)=c%
IF a%>=100 THEN a%-=100:PV%(s%)=1
PA%(s%)=a%
READ n$,a%,c%
ENDWHILE
ENDPROC
DATA FORWARD,1,1,FD,1,1,BACK,1,2,BK,1,2,LEFT,1,3,LT,1,3,RIGHT,1,4,RT,1,4
DATA PENUP,0,5,PU,0,5,PENDOWN,0,6,PD,0,6,HOME,0,7,CLEARSCREEN,0,8,CS,0,8
DATA CLEAN,0,9,SETPENCOLOR,1,10,SETPC,1,10,SETHEADING,1,11,SETH,1,11
DATA SETXY,2,12,SETX,1,13,SETY,1,14,HIDETURTLE,0,15,HT,0,15,SHOWTURTLE,0,16,ST,0,16
DATA HEADING,0,17,XCOR,0,18,YCOR,0,19,LABEL,1,20,FILL,0,21,TOWARDS,2,22,PENCOLOR,0,23,PC,0,23
DATA REPEAT,2,30,REPCOUNT,0,31,IF,2,32,IFELSE,3,33,STOP,0,34,OUTPUT,1,35,OP,1,35
DATA MAKE,2,36,LOCAL,1,37,THING,1,38,RUN,1,39,PRINT,101,40,PR,101,40,SHOW,101,41
DATA TYPE,101,42,WAIT,1,43,FOREVER,1,44,ERASE,1,45,ER,1,45,PO,1,46,POTS,0,47
DATA LOAD,1,48,SAVE,1,49,ERALL,0,50,CLEARTEXT,0,51,CT,0,51
DATA SUM,102,60,DIFFERENCE,2,61,PRODUCT,102,62,QUOTIENT,2,63,REMAINDER,2,64,MODULO,2,64
DATA RANDOM,1,65,SIN,1,66,COS,1,67,TAN,1,68,ARCTAN,1,69,SQRT,1,70,POWER,2,71
DATA ABS,1,72,INT,1,73,ROUND,1,74,MINUS,1,75,AND,102,76,OR,102,77,NOT,1,78
DATA FIRST,1,80,LAST,1,81,BUTFIRST,1,82,BF,1,82,BUTLAST,1,83,BL,1,83,ITEM,2,84
DATA COUNT,1,85,SENTENCE,102,86,SE,102,86,LIST,102,87,FPUT,2,88,LPUT,2,89
DATA WORD,102,90,EMPTYP,1,91,EMPTY?,1,91,WORDP,1,92,WORD?,1,92,LISTP,1,93,LIST?,1,93
DATA NUMBERP,1,94,NUMBER?,1,94,EQUALP,2,95,EQUAL?,2,95,MEMBERP,2,96,MEMBER?,2,96
DATA END,0,0

REM ---- Symbols and memory ----

DEF FNsym(n$)
LOCAL h%,s%,u$,i%,c%
h%=ABS(LEN(n$)*31+ASC(n$)*7+ASC(RIGHT$(n$,1))) MOD 128
s%=HT%(h%)
WHILE s%
IF NM$(s%)=n$ THEN =s%
s%=HN%(s%)
ENDWHILE
IF NS%=NY% THEN PROCe("Too many different words")
NS%+=1:s%=NS%
NM$(s%)=n$:HN%(s%)=HT%(h%):HT%(h%)=s%
SV%(s%)=UNB%:PB%(s%)=-1:UP%(s%)=s%
c%=ASC(n$)
IF c%=34 THEN
KD%(s%)=1:QV%(s%)=FNword(MID$(n$,2))
ELSE
IF c%=58 THEN
KD%(s%)=2:LK%(s%)=UP%(FNsym(MID$(n$,2)))
ELSE
i%=1
WHILE i%<=LEN(n$)
c%=ASC(MID$(n$,i%,1))
IF c%>=97 AND c%<=122 THEN c%-=32
u$+=CHR$c%:i%+=1
ENDWHILE
IF u$<>n$ THEN UP%(s%)=FNsym(u$)
ENDIF
ENDIF
=s%

DEF FNisnum(s$)
LOCAL i%,c%,d%,n%
n%=LEN(s$)
IF n%=0 THEN =FALSE
FOR i%=1 TO n%
c%=ASC(MID$(s$,i%,1))
IF c%=45 AND i%=1 AND n%>1 THEN
ELSE
IF c%=46 THEN
d%+=1:IF d%>1 OR n%=1 THEN =FALSE
ELSE
IF c%<48 OR c%>57 THEN =FALSE
ENDIF
ENDIF
NEXT
=TRUE

DEF FNword(s$)
IF FNisnum(s$) THEN =FNmk(VAL(s$))
=FNsym(s$)*4+1

DEF FNmk(x)
LOCAL i%
IF ABS(x)<5E8 THEN
i%=x
IF i%=x THEN =i%*4+2
ENDIF
IF FF%=0 THEN PROCgc(0,0)
IF FF%=0 THEN PROCe("Out of memory")
i%=FF%:FF%=FX%(i%):FV(i%)=x
=i%*4+3

DEF FNn(v%)
LOCAL t%
t%=v% AND 3
IF t%=2 THEN =(v%-2) DIV 4
IF t%=3 THEN =FV(v% DIV 4)
IF t%=1 AND v%>0 THEN IF FNisnum(NM$(v% DIV 4)) THEN =VAL(NM$(v% DIV 4))
PROCe(FNshow(v%,1)+" is not a number")
=0

DEF FNcons(a%,d%)
LOCAL c%
IF FC%=0 THEN PROCgc(a%,d%)
IF FC%=0 THEN PROCe("Out of memory")
c%=FC%:FC%=CD%(c%):CA%(c%)=a%:CD%(c%)=d%
=c%*4

DEF PROCmark(v%)
LOCAL c%
REPEAT
IF (v% AND 3)=3 THEN MF%?(v% DIV 4)=1:ENDPROC
IF (v% AND 3)<>0 OR v%<=0 THEN ENDPROC
c%=v% DIV 4
IF MC%?c% THEN ENDPROC
MC%?c%=1
IF CA%(c%)>0 THEN PROCmark(CA%(c%))
v%=CD%(c%)
UNTIL v%=0
ENDPROC

DEF PROCgc(a%,d%)
LOCAL i%
PROCmark(a%):PROCmark(d%)
PROCmark(zLN%):PROCmark(RET%):PROCmark(DB%):PROCmark(DP%)
FOR i%=0 TO VP%:PROCmark(VS%(i%)):NEXT
FOR i%=0 TO BP%:PROCmark(BV%(i%)):NEXT
FOR i%=0 TO RD%:PROCmark(RH%(i%)):NEXT
FOR i%=1 TO NS%
IF SV%(i%)>0 THEN PROCmark(SV%(i%))
IF PB%(i%)>0 THEN PROCmark(PB%(i%))
IF PP%(i%)>0 THEN PROCmark(PP%(i%))
NEXT
FC%=0
FOR i%=NC% TO 1 STEP -1
IF MC%?i% THEN MC%?i%=0 ELSE CD%(i%)=FC%:FC%=i%
NEXT
FF%=0
FOR i%=NF% TO 1 STEP -1
IF MF%?i% THEN MF%?i%=0 ELSE FX%(i%)=FF%:FF%=i%
NEXT
ENDPROC

DEF PROCpush(v%)
IF VP%>=NV% THEN PROCe("Out of stack")
VP%+=1:VS%(VP%)=v%
ENDPROC

DEF PROCe(m$)
ERROR 1,m$
ENDPROC

REM ---- Reader ----

DEF PROCapp(v%)
LOCAL c%
c%=FNcons(v%,0)
IF RT%(RD%) THEN CD%(RT%(RD%) DIV 4)=c% ELSE RH%(RD%)=c%
RT%(RD%)=c%
ENDPROC

REM One line of text into the list being read; a [ can stay open across lines.
DEF PROCread(l$)
LOCAL p%,n%,c%,e%,x%,d%
n%=LEN(l$):p%=1
WHILE p%<=n%
c%=ASC(MID$(l$,p%,1))
CASE c% OF
WHEN 32,9:p%+=1
WHEN 59:p%=n%+1
WHEN 91
IF RD%>=31 THEN PROCe("Lists nested too deep")
RD%+=1:RH%(RD%)=0:RT%(RD%)=0:p%+=1
WHEN 93
IF RD%=0 THEN PROCe("Unexpected ]")
RD%-=1:p%+=1
PROCapp(RH%(RD%+1))
RH%(RD%+1)=0
WHEN 40,41,43,42,47,61:PROCapp(FNsym(CHR$c%)*4+1):p%+=1
WHEN 60,62
e%=1:x%=ASC(MID$(l$,p%+1,1))
IF x%=61 OR (c%=60 AND x%=62) THEN e%=2
PROCapp(FNsym(MID$(l$,p%,e%))*4+1):p%+=e%
WHEN 34,58
e%=p%+1
d%=(c%=34)
WHILE e%<=n% AND FNmore(MID$(l$,e%,1),d%):e%+=1:ENDWHILE
PROCapp(FNsym(MID$(l$,p%,e%-p%))*4+1):p%=e%
OTHERWISE
x%=ASC(MID$(l$,p%+1,1))
IF c%=45 THEN
IF p%=1 THEN d%=32 ELSE d%=ASC(MID$(l$,p%-1,1))
IF (d%=32 OR d%=40 OR d%=91) AND p%<n% AND x%<>32 THEN
IF (x%>=48 AND x%<=57) OR x%=46 THEN
e%=p%+1
ELSE
PROCapp(QNEG%*4+1):p%+=1:e%=0
ENDIF
ELSE
PROCapp(FNsym("-")*4+1):p%+=1:e%=0
ENDIF
ELSE
e%=p%
ENDIF
IF e% THEN
WHILE e%<=n% AND FNmore(MID$(l$,e%,1),FALSE):e%+=1:ENDWHILE
PROCapp(FNword(MID$(l$,p%,e%-p%))):p%=e%
ENDIF
ENDCASE
ENDWHILE
ENDPROC

DEF FNmore(c$,q%)
LOCAL d%
d%=DT%?ASC(c$)
IF q% THEN =d%<>2
=d%=0

REM ---- Evaluator ----

DEF FNrun(l%)
LOCAL i%,v%
IF (l% AND 3)<>0 OR l%<0 THEN PROCe(FNshow(l%,1)+" is not a [list]")
i%=I%:I%=l%:v%=VOID%
WHILE I%<>0 AND FL%=0
IF v%<>VOID% THEN PROCe("You don't say what to do with "+FNshow(v%,1))
v%=FNexpr(0)
ENDWHILE
I%=i%
=v%

DEF PROCdo(l%)
LOCAL v%
v%=FNrun(l%)
IF v%<>VOID% AND FL%=0 THEN PROCe("You don't say what to do with "+FNshow(v%,1))
ENDPROC

DEF FNexpr(m%)
LOCAL l%,r%,s%,o%
l%=FNprimary
WHILE I%<>0
r%=CA%(I% DIV 4)
IF (r% AND 3)<>1 THEN =l%
o%=OP%(r% DIV 4)
IF o%=0 OR o% DIV 16<m% THEN =l%
IF l%=VOID% THEN PROCe("Nothing to the left of "+NM$(r% DIV 4))
I%=CD%(I% DIV 4)
PROCpush(l%)
r%=FNexpr(o% DIV 16+1)
IF r%=VOID% THEN PROCe("Nothing to the right of an operator")
l%=VS%(VP%):VP%-=1
l%=FNbin(o% AND 15,l%,r%)
ENDWHILE
=l%

DEF FNbin(o%,a%,b%)
CASE o% OF
WHEN 1:=FNmk(FNn(a%)+FNn(b%))
WHEN 2:=FNmk(FNn(a%)-FNn(b%))
WHEN 3:=FNmk(FNn(a%)*FNn(b%))
WHEN 4
IF FNn(b%)=0 THEN PROCe("Can't divide by zero")
=FNmk(FNn(a%)/FNn(b%))
WHEN 5:=FNtf(FNeq(a%,b%))
WHEN 6:=FNtf(FNn(a%)<FNn(b%))
WHEN 7:=FNtf(FNn(a%)>FNn(b%))
WHEN 8:=FNtf(FNn(a%)<=FNn(b%))
WHEN 9:=FNtf(FNn(a%)>=FNn(b%))
WHEN 10:=FNtf(NOT FNeq(a%,b%))
ENDCASE
=VOID%

DEF FNtf(t%)
IF t% THEN =TR%
=FA%

DEF FNtrue(v%)
IF (v% AND 3)=1 THEN
IF UP%(v% DIV 4)=TR% DIV 4 THEN =TRUE
IF UP%(v% DIV 4)=FA% DIV 4 THEN =FALSE
ENDIF
PROCe(FNshow(v%,1)+" is not TRUE or FALSE")
=FALSE

DEF FNeq(a%,b%)
IF a%=b% THEN =TRUE
IF (a% AND 3)=0 AND a%>=0 THEN
IF (b% AND 3)<>0 OR b%<0 THEN =FALSE
WHILE a%<>0 AND b%<>0
IF NOT FNeq(CA%(a% DIV 4),CA%(b% DIV 4)) THEN =FALSE
a%=CD%(a% DIV 4):b%=CD%(b% DIV 4)
ENDWHILE
=a%=b%
ENDIF
IF (b% AND 3)=0 THEN =FALSE
IF (a% AND 3)=1 AND (b% AND 3)=1 THEN =UP%(a% DIV 4)=UP%(b% DIV 4)
IF (a% AND 3)=1 OR (b% AND 3)=1 THEN =FNshow(a%,1)=FNshow(b%,1)
=FNn(a%)=FNn(b%)

DEF FNprimary
LOCAL t%,s%,k%,n%
IF I%=0 THEN PROCe("Not enough inputs")
t%=CA%(I% DIV 4):I%=CD%(I% DIV 4)
IF (t% AND 3)<>1 THEN =t%
s%=t% DIV 4
k%=KD%(s%)
CASE k% OF
WHEN 0:=FNcall(UP%(s%),-1)
WHEN 1:=QV%(s%)
WHEN 2
t%=SV%(LK%(s%))
IF t%=UNB% THEN PROCe(NM$(LK%(s%))+" has no value")
=t%
WHEN 3
IF I%<>0 THEN
t%=CA%(I% DIV 4)
IF (t% AND 3)=1 THEN
s%=UP%(t% DIV 4)
IF KD%(s%)=0 AND PV%(s%)=1 THEN
I%=CD%(I% DIV 4)
WHILE FNclosed=0
PROCarg(s%):n%+=1
ENDWHILE
I%=CD%(I% DIV 4)
=FNcall(s%,n%)
ENDIF
ENDIF
ENDIF
t%=FNexpr(0)
IF FNclosed=0 THEN PROCe("Missing )")
I%=CD%(I% DIV 4)
=t%
WHEN 4
t%=FNexpr(4)
=FNmk(-FNn(t%))
WHEN 5:PROCe("Unexpected )")
OTHERWISE:PROCe(NM$(s%)+" needs something on its left")
ENDCASE
=VOID%

DEF FNclosed
IF I%=0 THEN PROCe("Missing )")
=CA%(I% DIV 4)=QCLOSE%*4+1

DEF PROCarg(s%)
LOCAL v%
IF I%=0 THEN PROCe("Not enough inputs to "+NM$(s%))
v%=FNexpr(0)
IF v%=VOID% THEN PROCe("A command gave no value to "+NM$(s%))
PROCpush(v%)
ENDPROC

DEF FNcall(s%,n%)
LOCAL a%,c%,v%,b%,p%
c%=PC%(s%)
IF n%<0 THEN
IF c%=0 AND PB%(s%)=-1 THEN PROCe("I don't know how to "+NM$(s%))
n%=PA%(s%)
a%=1
WHILE a%<=n%:PROCarg(s%):a%+=1:ENDWHILE
ENDIF
IF c% THEN
v%=FNdo(c%,n%)
VP%-=n%
=v%
ENDIF
b%=BP%
p%=PP%(s%)
IF BP%+n%>=NB% THEN PROCe("Too many inputs in use")
a%=VP%-n%+1
WHILE p%
c%=CA%(p% DIV 4) DIV 4
BP%+=1:BS%(BP%)=c%:BV%(BP%)=SV%(c%)
SV%(c%)=VS%(a%)
p%=CD%(p% DIV 4):a%+=1
ENDWHILE
VP%-=n%
IF TIME>=YT% THEN PROCyield
v%=FNrun(PB%(s%))
IF v%<>VOID% AND FL%=0 THEN PROCe("You don't say what to do with "+FNshow(v%,1))
PROCunbind(b%)
v%=VOID%
IF FL%=2 THEN v%=RET%
FL%=0
=v%

DEF PROCunbind(b%)
WHILE BP%>b%
SV%(BS%(BP%))=BV%(BP%):BP%-=1
ENDWHILE
ENDPROC

REM ---- Primitives. Inputs stay on VS% until the primitive returns ----

DEF FNdo(c%,n%)
LOCAL a%,b%,o%
o%=VP%-n%
IF n%>0 THEN a%=VS%(o%+1)
IF n%>1 THEN b%=VS%(o%+2)
IF c%<30 THEN =FNturtle(c%,a%,b%)
IF c%<60 THEN =FNcontrol(c%,a%,b%,o%,n%)
IF c%<80 THEN =FNmath(c%,a%,b%,o%,n%)
=FNlists(c%,a%,b%,o%,n%)

DEF FNturtle(c%,a%,b%)
LOCAL x,y
CASE c% OF
WHEN 1:x=FNn(a%):PROCgo(TX+x*SIN(RAD(TH)),TY+x*COS(RAD(TH)))
WHEN 2:x=-FNn(a%):PROCgo(TX+x*SIN(RAD(TH)),TY+x*COS(RAD(TH)))
WHEN 3:PROCturn(TH-FNn(a%))
WHEN 4:PROCturn(TH+FNn(a%))
WHEN 5:PU%=1
WHEN 6:PU%=0
WHEN 7:PROCgo(0,0):PROCturn(0)
WHEN 8:PROCclear:PU%=0:TX=0:TY=0:TH=0
WHEN 9:PROCclear
WHEN 10:TC%=FNn(a%) AND 15
IF SW% THEN GCOL 0,FNcolour(TC%)
WHEN 11:PROCturn(FNn(a%))
WHEN 12:PROCgo(FNn(a%),FNn(b%))
WHEN 13:PROCgo(FNn(a%),TY)
WHEN 14:PROCgo(TX,FNn(a%))
WHEN 15:HD%=1:PROCdirtyt
WHEN 16:HD%=0:PROCdirtyt
WHEN 17:=FNmk(TH)
WHEN 18:=FNmk(TX)
WHEN 19:=FNmk(TY)
WHEN 20
PROCson
VDU 5
MOVE FNsx(TX),FNsy(TY)
PRINT FNshow(a%,1);
VDU 4
PROCdirty(0,0,800,560)
WHEN 21
PROCson
FILL FNsx(TX),FNsy(TY)
PROCdirty(0,0,800,560)
WHEN 22
x=FNn(a%)-TX:y=FNn(b%)-TY
IF y=0 THEN
IF x<0 THEN =FNmk(270)
=FNmk(90)
ENDIF
x=DEG(ATN(x/y))
IF y<0 THEN x+=180
=FNmk(x-360*INT(x/360))
WHEN 23:=FNmk(TC%)
ENDCASE
=VOID%

DEF FNcontrol(c%,a%,b%,o%,n%)
LOCAL r%,t%,v%
CASE c% OF
WHEN 30,44
IF c%=30 THEN n%=FNn(a%) ELSE n%=-1:b%=a%
t%=RC%:r%=1
WHILE (r%<=n% OR n%<0) AND FL%=0
RC%=r%:PROCdo(b%):r%+=1
IF TIME>=YT% THEN PROCyield
ENDWHILE
RC%=t%
WHEN 31:=FNmk(RC%)
WHEN 32
IF FNtrue(a%) THEN =FNrun(b%)
WHEN 33
IF FNtrue(a%) THEN =FNrun(b%)
=FNrun(VS%(o%+3))
WHEN 34:FL%=1
WHEN 35:RET%=a%:FL%=2
WHEN 36
IF (a% AND 3)<>1 THEN PROCe("MAKE needs a word")
SV%(UP%(a% DIV 4))=b%
WHEN 37
IF (a% AND 3)=1 THEN
PROCbindlocal(a%)
ELSE
WHILE a%:PROCbindlocal(CA%(a% DIV 4)):a%=CD%(a% DIV 4):ENDWHILE
ENDIF
WHEN 38
IF (a% AND 3)<>1 THEN PROCe("THING needs a word")
v%=SV%(UP%(a% DIV 4))
IF v%=UNB% THEN PROCe(NM$(a% DIV 4)+" has no value")
=v%
WHEN 39:=FNrun(a%)
WHEN 40,41,42
r%=1
WHILE r%<=n%
PROCtype(FNshow(VS%(o%+r%),c%<>41))
IF r%<n% THEN PROCtype(" ")
r%+=1
ENDWHILE
IF c%<42 THEN PROCnewline
WHEN 43
t%=TIME+FNn(a%)*100/60
REPEAT
PROCyield
UNTIL TIME>=t%
WHEN 45
IF (a% AND 3)=1 THEN PB%(UP%(a% DIV 4))=-1
WHEN 46
IF (a% AND 3)=1 THEN PROCdump(UP%(a% DIV 4),0)
WHEN 47
FOR r%=1 TO NS%
IF PB%(r%)<>-1 THEN PROCout(NM$(r%))
NEXT
WHEN 48:PROCload(FNshow(a%,1))
WHEN 49:PROCsave(FNshow(a%,1))
WHEN 50
FOR r%=1 TO NS%:PB%(r%)=-1:NEXT
WHEN 51:PROCcleartext
ENDCASE
=VOID%

DEF PROCbindlocal(a%)
LOCAL s%
s%=UP%(a% DIV 4)
IF BP%>=NB% THEN PROCe("Too many inputs in use")
BP%+=1:BS%(BP%)=s%:BV%(BP%)=SV%(s%)
ENDPROC

DEF FNmath(c%,a%,b%,o%,n%)
LOCAL x,y,r%,t%
CASE c% OF
WHEN 60,62
IF c%=60 THEN x=0 ELSE x=1
r%=1
WHILE r%<=n%
y=FNn(VS%(o%+r%))
IF c%=60 THEN x+=y ELSE x*=y
r%+=1
ENDWHILE
=FNmk(x)
WHEN 61:=FNmk(FNn(a%)-FNn(b%))
WHEN 63:=FNbin(4,a%,b%)
WHEN 64
y=FNn(b%)
IF y=0 THEN PROCe("Can't divide by zero")
x=FNn(a%)
=FNmk(x-y*INT(x/y))
WHEN 65:=FNmk(RND(FNn(a%))-1)
WHEN 66:=FNmk(SIN(RAD(FNn(a%))))
WHEN 67:=FNmk(COS(RAD(FNn(a%))))
WHEN 68:=FNmk(TAN(RAD(FNn(a%))))
WHEN 69:=FNmk(DEG(ATN(FNn(a%))))
WHEN 70
IF FNn(a%)<0 THEN PROCe("SQRT of a negative number")
=FNmk(SQR(FNn(a%)))
WHEN 71:=FNmk(FNn(a%)^FNn(b%))
WHEN 72:=FNmk(ABS(FNn(a%)))
WHEN 73
x=FNn(a%)
IF x<0 THEN =FNmk(-INT(-x))
=FNmk(INT(x))
WHEN 74:=FNmk(INT(FNn(a%)+0.5))
WHEN 75:=FNmk(-FNn(a%))
WHEN 76,77
r%=c%=76
t%=1
WHILE t%<=n%
IF c%=76 THEN r%=r% AND FNtrue(VS%(o%+t%)) ELSE r%=r% OR FNtrue(VS%(o%+t%))
t%+=1
ENDWHILE
=FNtf(r%)
WHEN 78:=FNtf(NOT FNtrue(a%))
ENDCASE
=VOID%

DEF FNislist(v%)
=(v% AND 3)=0 AND v%>=0

DEF PROCb0
PROCpush(0):BT%=0
ENDPROC

DEF PROCb(v%)
LOCAL c%
c%=FNcons(v%,0)
IF BT% THEN CD%(BT% DIV 4)=c% ELSE VS%(VP%)=c%
BT%=c%
ENDPROC

DEF FNb1
VP%-=1
=VS%(VP%+1)

DEF PROCbcopy(l%)
WHILE l%
PROCb(CA%(l% DIV 4)):l%=CD%(l% DIV 4)
ENDWHILE
ENDPROC

DEF FNlists(c%,a%,b%,o%,n%)
LOCAL s$,r%,t%
CASE c% OF
WHEN 80,81,82,83
IF FNislist(a%) THEN
IF a%=0 THEN PROCe("The list is empty")
IF c%=80 THEN =CA%(a% DIV 4)
IF c%=82 THEN =CD%(a% DIV 4)
IF c%=81 THEN
WHILE CD%(a% DIV 4):a%=CD%(a% DIV 4):ENDWHILE
=CA%(a% DIV 4)
ENDIF
PROCb0
WHILE CD%(a% DIV 4)
PROCb(CA%(a% DIV 4)):a%=CD%(a% DIV 4)
ENDWHILE
=FNb1
ENDIF
s$=FNshow(a%,1)
IF s$="" THEN PROCe("The word is empty")
IF c%=80 THEN =FNword(LEFT$(s$,1))
IF c%=81 THEN =FNword(RIGHT$(s$,1))
IF c%=82 THEN =FNword(MID$(s$,2))
=FNword(LEFT$(s$,LEN(s$)-1))
WHEN 84
r%=FNn(a%)
IF FNislist(b%) THEN
WHILE r%>1 AND b%<>0:b%=CD%(b% DIV 4):r%-=1:ENDWHILE
IF b%=0 OR r%<1 THEN PROCe("Too few items")
=CA%(b% DIV 4)
ENDIF
s$=FNshow(b%,1)
IF r%<1 OR r%>LEN(s$) THEN PROCe("Too few items")
=FNword(MID$(s$,r%,1))
WHEN 85
IF FNislist(a%) THEN
WHILE a%:r%+=1:a%=CD%(a% DIV 4):ENDWHILE
=FNmk(r%)
ENDIF
=FNmk(LEN(FNshow(a%,1)))
WHEN 86
PROCb0
r%=1
WHILE r%<=n%
t%=VS%(o%+r%)
IF FNislist(t%) THEN PROCbcopy(t%) ELSE PROCb(t%)
r%+=1
ENDWHILE
=FNb1
WHEN 87
PROCb0
r%=1
WHILE r%<=n%:PROCb(VS%(o%+r%)):r%+=1:ENDWHILE
=FNb1
WHEN 88:=FNcons(a%,b%)
WHEN 89
PROCb0
PROCbcopy(b%):PROCb(a%)
=FNb1
WHEN 90
r%=1
WHILE r%<=n%:s$+=FNshow(VS%(o%+r%),1):r%+=1:ENDWHILE
=FNword(s$)
WHEN 91:=FNtf(a%=0 OR a%=FNsym("")*4+1)
WHEN 92:=FNtf(NOT FNislist(a%))
WHEN 93:=FNtf(FNislist(a%))
WHEN 94:=FNtf((a% AND 3)>=2 OR ((a% AND 3)=1 AND FNisnum(FNshow(a%,1))))
WHEN 95:=FNtf(FNeq(a%,b%))
WHEN 96
IF FNislist(b%) THEN
WHILE b%
IF FNeq(a%,CA%(b% DIV 4)) THEN =TR%
b%=CD%(b% DIV 4)
ENDWHILE
=FA%
ENDIF
=FNtf(INSTR(FNshow(b%,1),FNshow(a%,1))>0)
ENDCASE
=VOID%

REM Text of a value; top% drops the outer brackets of a list.
DEF FNshow(v%,top%)
LOCAL s$,c%,x%
IF v%=VOID% THEN ="nothing"
c%=v% AND 3
IF c%=1 THEN =NM$(v% DIV 4)
IF c%=2 THEN =STR$((v%-2) DIV 4)
IF c%=3 THEN =STR$(FV(v% DIV 4))
x%=v%
WHILE x%
s$+=FNshow(CA%(x% DIV 4),0)
x%=CD%(x% DIV 4)
IF LEN(s$)>200 THEN s$+="...":x%=0
IF x% THEN s$+=" "
ENDWHILE
IF top% THEN =s$
="["+s$+"]"

REM ---- Definitions, files ----

DEF PROCline(l$)
LOCAL t%,s%,b%
b%=BP%
PROCread(l$)
IF RD%>0 THEN ENDPROC
zLN%=RH%(0):RH%(0)=0:RT%(0)=0
IF zLN%=0 THEN ENDPROC
t%=CA%(zLN% DIV 4)
IF (t% AND 3)=1 THEN s%=UP%(t% DIV 4) ELSE s%=0
IF DF% THEN
IF s%=QEND% AND CD%(zLN% DIV 4)=0 THEN
PB%(DF%)=DB%:PP%(DF%)=DP%:PA%(DF%)=DA%
IF QUIET%=0 THEN PROCout(NM$(DF%)+" defined")
DF%=0:DB%=0:DP%=0
ELSE
IF DT2% THEN CD%(DT2% DIV 4)=zLN% ELSE DB%=zLN%
DT2%=zLN%
WHILE CD%(DT2% DIV 4):DT2%=CD%(DT2% DIV 4):ENDWHILE
ENDIF
zLN%=0
ENDPROC
ENDIF
IF s%=QTO% THEN PROCdefine:zLN%=0:ENDPROC
PROCpush(zLN%)
t%=FNrun(zLN%)
IF t%<>VOID% AND FL%=0 THEN PROCe("You don't say what to do with "+FNshow(t%,1))
VP%-=1:FL%=0
PROCunbind(b%)
zLN%=0
ENDPROC

DEF PROCdefine
LOCAL l%,t%,s%,c%
l%=CD%(zLN% DIV 4)
IF l%=0 THEN PROCe("TO needs a name")
t%=CA%(l% DIV 4)
IF (t% AND 3)<>1 THEN PROCe("TO needs a name")
s%=UP%(t% DIV 4)
IF PC%(s%) THEN PROCe(NM$(s%)+" is a primitive")
DF%=s%:DB%=0:DT2%=0:DP%=0:DA%=0
l%=CD%(l% DIV 4)
WHILE l%
t%=CA%(l% DIV 4)
IF (t% AND 3)<>1 THEN PROCe("Inputs to TO must look like :X")
IF KD%(t% DIV 4)<>2 THEN PROCe("Inputs to TO must look like :X")
c%=FNcons(LK%(t% DIV 4)*4+1,0)
IF DA% THEN CD%(c0% DIV 4)=c% ELSE DP%=c%
c0%=c%:DA%+=1
l%=CD%(l% DIV 4)
ENDWHILE
ENDPROC

DEF PROCload(f$)
LOCAL q%
FH%=OPENIN(f$)
IF FH%=0 THEN PROCe("Can't open "+f$)
q%=QUIET%:QUIET%=1
WHILE NOT EOF#FH%
PROCline(GET$#FH%)
ENDWHILE
CLOSE#FH%:FH%=0
QUIET%=q%
ENDPROC

DEF PROCsave(f$)
LOCAL i%
FH%=OPENOUT(f$)
IF FH%=0 THEN PROCe("Can't write "+f$)
FOR i%=1 TO NS%
IF PB%(i%)<>-1 THEN PROCdump(i%,FH%)
NEXT
CLOSE#FH%:FH%=0
OSCLI "SetType "+f$+" Text"
ENDPROC

REM Print a procedure as source, to the log or to a file.
DEF PROCdump(s%,f%)
LOCAL l%,t$
IF PB%(s%)=-1 THEN PROCe("I don't know how to "+NM$(s%))
t$="TO "+NM$(s%)
l%=PP%(s%)
WHILE l%
t$+=" :"+NM$(CA%(l% DIV 4) DIV 4):l%=CD%(l% DIV 4)
ENDWHILE
PROCdumpline(t$,f%)
t$=""
l%=PB%(s%)
WHILE l%
t$+=" "+FNshow(CA%(l% DIV 4),0)
l%=CD%(l% DIV 4)
IF LEN(t$)>40 OR l%=0 THEN PROCdumpline(t$,f%):t$=""
ENDWHILE
PROCdumpline("END",f%)
ENDPROC

DEF PROCdumpline(t$,f%)
LOCAL i%
IF f%=0 THEN PROCout(t$):ENDPROC
i%=1
WHILE i%<=LEN(t$):BPUT#f%,ASC(MID$(t$,i%,1)):i%+=1:ENDWHILE
BPUT#f%,10
ENDPROC

REM ---- Canvas: a mode 27 sprite, as the desktop. Coordinates are OS units from its bottom left ----

DEF PROCcanvas
SA%=56064
DIM CV% SA%
!CV%=SA%:CV%!8=16
$SN%="canvas"
SYS &2E,9+256,CV%
SYS &2E,15+256,CV%,SN%,0,400,280,27
TX=0:TY=0:TH=0:TC%=0
PROCclear
PROCsoff
ENDPROC

DEF FNcolour(c%)
=ASC(MID$(CHR$7+CHR$8+CHR$10+CHR$15+CHR$11+CHR$11+CHR$9+CHR$0+CHR$14+CHR$12+CHR$13+CHR$15+CHR$14+CHR$8+CHR$14+CHR$4,c%+1,1))

DEF PROCson
IF SW%=0 THEN
SYS &2E,60+256,CV%,SN%,0 TO S0%,S1%,S2%,S3%
SW%=1
GCOL 0,FNcolour(TC%)
ENDIF
ENDPROC

DEF PROCsoff
IF SW% THEN SYS &2E,S0%,S1%,S2%,S3%:SW%=0
ENDPROC

DEF PROCclear
PROCson
GCOL 0,128
CLG
PROCdirty(0,0,800,560)
ENDPROC

DEF FNsx(x)
=400+INT(2*x+0.5)

DEF FNsy(y)
=280+INT(2*y+0.5)

DEF PROCdirty(a%,b%,c%,d%)
IF a%>c% THEN SWAP a%,c%
IF b%>d% THEN SWAP b%,d%
IF DY%=0 THEN DY%=1:D0%=a%:D1%=b%:D2%=c%:D3%=d%:ENDPROC
IF a%<D0% THEN D0%=a%
IF b%<D1% THEN D1%=b%
IF c%>D2% THEN D2%=c%
IF d%>D3% THEN D3%=d%
ENDPROC

DEF PROCdirtyt
LOCAL x%,y%
x%=FNsx(TX):y%=FNsy(TY)
PROCdirty(x%-28,y%-28,x%+28,y%+28)
ENDPROC

DEF PROCturn(h)
PROCdirtyt
TH=h-360*INT(h/360)
ENDPROC

DEF PROCgo(x,y)
PROCdirtyt
IF PU%=0 THEN
PROCson
MOVE FNsx(TX),FNsy(TY)
DRAW FNsx(x),FNsy(y)
PROCdirty(FNsx(TX),FNsy(TY),FNsx(x),FNsy(y))
ENDIF
TX=x:TY=y
PROCdirtyt
IF TIME>=YT% THEN PROCyield
ENDPROC

DEF PROCflush
IF DY% THEN
SYS "Wimp_ForceRedraw",W%,D0%-2,D1%-564,D2%+4,D3%-556
DY%=0
ENDIF
IF LD% THEN
SYS "Wimp_ForceRedraw",W%,0,-724,800,-560
LD%=0
ENDIF
ENDPROC

REM ---- Log ----

DEF PROCtype(s$)
LC$+=s$
WHILE LEN(LC$)>49
PROClogline(LEFT$(LC$,49)):LC$=MID$(LC$,50)
ENDWHILE
LD%=1
ENDPROC

DEF PROCnewline
PROClogline(LC$):LC$=""
ENDPROC

DEF PROCout(s$)
PROCtype(s$):PROCnewline
ENDPROC

DEF PROClogline(s$)
LOCAL i%
FOR i%=0 TO 3:LG$(i%)=LG$(i%+1):NEXT
LG$(4)=s$:LD%=1
ENDPROC

DEF PROCcleartext
LOCAL i%
FOR i%=0 TO 4:LG$(i%)="":NEXT
LC$="":LD%=1
ENDPROC

REM ---- Desktop. The window has no background, so the Wimp does not wipe the canvas ----

DEF PROCwimp
LOCAL i%
SYS "Wimp_Initialise",200,&4B534154,"Logo" TO ,task%
PROCpixtrans
Q%!0=200:Q%!4=124:Q%!8=1000:Q%!12=916
Q%!16=0:Q%!20=0:Q%!24=-1:Q%!28=&87000002
Q%!32=&FF070207:Q%!36=&000C0103
Q%!40=0:Q%!44=-792:Q%!48=800:Q%!52=0
Q%!56=&27000039:Q%!60=&3000:Q%!64=1:Q%!68=0
$(Q%+72)="Logo":Q%!84=0
SYS "Wimp_CreateWindow",,Q% TO W%
FOR i%=0 TO 3
READ x0%,x1%,t$
Z%!0=W%:Z%!4=x0%:Z%!8=-784:Z%!12=x1%:Z%!16=-732
IF i%=0 THEN
Z%!20=&0700F135:Z%!24=EB%:Z%!28=-1:Z%!32=200:?EB%=0
ELSE
Z%!20=&1700303D:$(Z%+24)=t$
ENDIF
SYS "Wimp_CreateIcon",,Z%
NEXT
Z%!0=-1:Z%!4=0:Z%!8=0:Z%!12=68:Z%!16=68
Z%!20=&1700301A:$(Z%+24)="!logo"
SYS "Wimp_CreateIcon",,Z% TO BI%
PROCmenus
PROCopen
ENDPROC
DATA 8,540,x,548,620,Run,628,700,Stop,708,792,Clear

DEF PROCpixtrans
SYS "Wimp_ReadPixTrans",256,CV%,SN%,,,,SC%,PT%
ENDPROC

DEF PROCmenu(b%,t$,n%)
$b%=t$:b%?12=7:b%?13=2:b%?14=7:b%?15=0
b%!16=176:b%!20=44:b%!24=0
ENDPROC

DEF PROCitem(b%,i%,t$,s%,l%)
LOCAL p%
p%=b%+28+i%*24
p%!0=-(l%<>0)*128:p%!4=s%:p%!8=&07000031
$(p%+12)=t$
ENDPROC

DEF PROCmenus
LOCAL i%
PROCmenu(MM%,"Logo",4)
PROCitem(MM%,0,"Demos",MD%,0)
PROCitem(MM%,1,"Help",-1,0)
PROCitem(MM%,2,"Clear",-1,0)
PROCitem(MM%,3,"Quit",-1,1)
PROCmenu(MD%,"Demos",6)
FOR i%=0 TO 5
READ DN$(i%)
PROCitem(MD%,i%,DN$(i%),-1,i%=5)
NEXT
ENDPROC
DATA Spiral,Star,Flower,Tree,Snowflake,Squares

DEF PROCopen
Q%!0=W%
SYS "Wimp_GetWindowState",,Q%
Q%!28=-1
SYS "Wimp_OpenWindow",,Q%
SYS "Wimp_SetCaretPosition",W%,0,-1,-1,-1,0
ENDPROC

DEF PROCyield
PROCsoff
PROCflush
REPEAT
PROCpoll(0)
UNTIL ev%=0
YT%=TIME+4
IF ST% OR quit% THEN ST%=0:PROCe("Stopped")
ENDPROC

DEF PROCpoll(idle%)
IF idle% THEN PROCsoff:PROCflush
SYS &400C7,idle%,Q% TO ev%
CASE ev% OF
WHEN 1:PROCredraw
WHEN 2:SYS "Wimp_OpenWindow",,Q%
WHEN 3:SYS "Wimp_CloseWindow",,Q%
WHEN 6:PROCclick
WHEN 8:PROCkey
WHEN 9:PROCmenuhit
WHEN 17,18:PROCmessage
ENDCASE
ENDPROC

DEF PROCredraw
LOCAL more%,x%,y%
SYS "Wimp_RedrawWindow",,Q% TO more%
x%=Q%!4-Q%!20:y%=Q%!16-Q%!24
WHILE more%
PROCpaint(x%,y%)
SYS "Wimp_GetRectangle",,Q% TO more%
ENDWHILE
ENDPROC

DEF PROCpaint(x%,y%)
LOCAL i%,a,tx%,ty%
IF Q%!40>y%-560 THEN
SYS &2E,52+256,CV%,SN%,x%,y%-560,0,SC%,PT%
IF HD%=0 THEN
tx%=x%+FNsx(TX):ty%=y%-560+FNsy(TY):a=RAD(TH)
SYS "Wimp_SetColour",11
MOVE tx%+24*SIN(a),ty%+24*COS(a)
MOVE tx%+16*SIN(a+2.4),ty%+16*COS(a+2.4)
PLOT 85,tx%+16*SIN(a-2.4),ty%+16*COS(a-2.4)
ENDIF
ENDIF
IF Q%!32<y%-560 THEN
SYS "Wimp_SetColour",0
RECTANGLE FILL x%,y%-792,800,231
SYS "Wimp_SetColour",7
VDU 5
FOR i%=0 TO 4
MOVE x%+8,y%-564-i%*32
PRINT LG$(i%);
NEXT
VDU 4
ENDIF
ENDPROC

DEF PROCclick
IF Q%!8=2 THEN
IF Q%!12=-2 THEN Q%!4=96+4*44
SYS "Wimp_CreateMenu",,MM%,Q%!0-64,Q%!4
ENDPROC
ENDIF
IF Q%!12=-2 THEN PROCopen:ENDPROC
IF Q%!12=W% THEN
CASE Q%!16 OF
WHEN 1:PROCsubmit
WHEN 2:IF zRUN% THEN ST%=1
WHEN 3:IF zRUN%=0 THEN PROCstart("CS CT","CS CT")
ENDCASE
SYS "Wimp_SetCaretPosition",W%,0,-1,-1,-1,LEN(FNentry)
ENDIF
ENDPROC

DEF FNentry
LOCAL s$,i%
WHILE EB%?i%>=32
s$+=CHR$(EB%?i%):i%+=1
ENDWHILE
=s$

DEF PROCsetentry(s$)
LOCAL i%
i%=1
WHILE i%<=LEN(s$):EB%?(i%-1)=ASC(MID$(s$,i%,1)):i%+=1:ENDWHILE
EB%?LEN(s$)=0
SYS "Wimp_SetIconState",,Z%
ENDPROC

DEF PROCentry(s$)
Z%!0=W%:Z%!4=0:Z%!8=0:Z%!12=0
PROCsetentry(s$)
SYS "Wimp_SetCaretPosition",W%,0,-1,-1,-1,LEN(s$)
ENDPROC

DEF PROCsubmit
LOCAL s$,i%
s$=FNentry
IF zRUN% OR s$="" THEN ENDPROC
FOR i%=7 TO 1 STEP -1:HS$(i%)=HS$(i%-1):NEXT
HS$(0)=s$:HP%=-1
PROCentry("")
PROCstart(s$,s$)
ENDPROC

DEF PROCstart(e$,s$)
IF DF% OR RD%>0 THEN PROCout("> "+e$) ELSE PROCout("? "+e$)
zRUN%=1
YT%=TIME+4
PROCline(s$)
zRUN%=0
ENDPROC

DEF PROCkey
CASE Q%!24 OF
WHEN 13:PROCsubmit
WHEN 27:IF zRUN% THEN ST%=1
WHEN &18F:IF HP%<7 THEN IF HS$(HP%+1)<>"" THEN HP%+=1:PROCentry(HS$(HP%))
WHEN &18E
IF HP%>0 THEN HP%-=1:PROCentry(HS$(HP%)) ELSE HP%=-1:PROCentry("")
OTHERWISE:SYS "Wimp_ProcessKey",Q%!24
ENDCASE
ENDPROC

DEF PROCmenuhit
LOCAL a%,b%
a%=Q%!0:b%=Q%!4
SYS "Wimp_GetPointerInfo",,Z%
CASE a% OF
WHEN 0
IF b%>=0 AND zRUN%=0 THEN PROCstart(DN$(b%),"LOAD "+CHR$34+"<Logo$Dir>.Demos."+DN$(b%))
WHEN 1:PROChelp
WHEN 2:IF zRUN%=0 THEN PROCstart("CS CT","CS CT")
WHEN 3:quit%=1
ENDCASE
IF Z%!8=1 THEN SYS "Wimp_CreateMenu",,MM%
ENDPROC

DEF PROChelp
PROCout("FD BK LT RT PU PD HOME CS SETPC SETXY HT ST")
PROCout("REPEAT IF IFELSE TO..END STOP OP MAKE RUN")
PROCout("PRINT SUM FIRST BF ITEM SE LIST RANDOM SIN COS")
PROCout("LOAD ""file SAVE ""file PO ""name POTS")
ENDPROC

DEF PROCmessage
LOCAL a%,f$
a%=Q%!16
IF a%=0 THEN quit%=1
IF a%=&400C1 THEN PROCpixtrans
IF a%=3 THEN
f$=FNz(Q%+44)
Q%!12=Q%!8:Q%!16=4
SYS "Wimp_SendMessage",17,Q%,Q%!4
IF zRUN%=0 THEN PROCstart("LOAD "+f$,"LOAD "+CHR$34+f$)
ENDIF
ENDPROC

DEF FNz(p%)
LOCAL s$
WHILE ?p%>=32
s$+=CHR$(?p%):p%+=1
ENDWHILE
=s$

DEF PROCfault
LOCAL m$
m$=REPORT$
PROCsoff
IF FH% THEN CLOSE#FH%:FH%=0
IF ERR<>1 THEN m$+=" (line "+STR$ERL+")"
PROCunbind(0)
VP%=0:FL%=0:I%=0:zLN%=0:RD%=0:DF%=0:DB%=0:DP%=0:QUIET%=0:zRUN%=0:ST%=0:RC%=0
IF LC$<>"" THEN PROCnewline
PROCout(m$)
ENDPROC

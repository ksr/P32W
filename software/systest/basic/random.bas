10 REM random-access files: FIELD, LSET, RSET, PUT, GET, LOC, LOF, EOF, and numbers packed into strings.
20 REM Each check counts itself (line 9000); what is wrong is printed, and the count of it at the end.
100 OPEN "test.txt" FOR RANDOM AS #1 LEN = 32
110 FIELD #1, 20 AS N$, 2 AS A$, 4 AS P$, 6 AS R$
120 T = LEN(N$) = 20 AND N$ = STRING$(20, 0) AND LOC(1) = 0 AND LOF(1) = 0: GOSUB 9000 ' 1
130 LSET N$ = "Ada Lovelace": LSET A$ = MKI$(36): LSET P$ = MKS$(1.5): RSET R$ = "abc": PUT #1, 1
140 LSET N$ = "Charles Babbage, of London": LSET A$ = MKI$(-2): LSET P$ = MKS$(-.1): RSET R$ = "toolong!": PUT #1
150 T = LOC(1) = 2 AND LOF(1) = 64: GOSUB 9000 ' 2
160 LSET N$ = "Grace Hopper": LSET A$ = MKI$(85): LSET P$ = MKS$(1E+10): RSET R$ = "": PUT #1, 5
170 T = LOC(1) = 5 AND LOF(1) = 160: GOSUB 9000 ' 3
180 CLOSE #1
200 REM read back, through the older form of OPEN
210 OPEN "R", #2, "test.txt", 32: FIELD #2, 20 AS N$, 2 AS A$, 4 AS P$, 6 AS R$
220 GET #2, 2: T = N$ = "Charles Babbage, of " AND CVI(A$) = -2 AND R$ = "toolon" AND EOF(2) = 0: GOSUB 9000 ' 4
230 T = ABS(CVS(P$) + .1) < 1E-8 AND CVS(P$) <> -.1: GOSUB 9000 ' 5
240 GET #2, 1: T = N$ = "Ada Lovelace        " AND CVI(A$) = 36 AND CVS(P$) = 1.5 AND R$ = "   abc": GOSUB 9000 ' 6
250 GET #2: T = LOC(2) = 2 AND LEFT$(N$, 7) = "Charles": GOSUB 9000 ' 7
260 GET #2, 3: T = N$ = STRING$(20, 0) AND CVI(A$) = 0 AND EOF(2) = 0: GOSUB 9000 ' 8
270 GET #2, 5: T = LEFT$(N$, 13) = "Grace Hopper " AND CVI(A$) = 85 AND CVS(P$) = 1E+10 AND R$ = SPACE$(6): GOSUB 9000 ' 9
280 GET #2, 6: T = EOF(2) = -1 AND N$ = STRING$(20, 0) AND LOC(2) = 6: GOSUB 9000 ' 10
290 GET #2, 5: T = EOF(2) = 0: GOSUB 9000 ' 11
300 REM a second FIELD on the same record: what is written through one is seen through the other
310 FIELD #2, 32 AS W$: T = LEN(W$) = 32 AND LEFT$(W$, 5) = "Grace": GOSUB 9000 ' 12
320 LSET N$ = "Hedy": T = LEFT$(W$, 6) = "Hedy  " AND MID$(W$, 21, 2) = MKI$(85): GOSUB 9000 ' 13
330 MID$(W$, 1, 2) = "LA": T = LEFT$(N$, 4) = "LAdy": GOSUB 9000 ' 14
340 PUT #2, 5: GET #2, 1: GET #2, 5: T = LEFT$(N$, 4) = "LAdy": GOSUB 9000 ' 15
350 REM an assignment ends a variable's field; LSET and RSET then work on its own string
360 N$ = "free": GET #2, 1: T = N$ = "free" AND LEFT$(W$, 3) = "Ada": GOSUB 9000 ' 16
370 LSET N$ = "xy": T = N$ = "xy  " AND LEFT$(W$, 3) = "Ada": GOSUB 9000 ' 17
380 RSET N$ = "12345678": E$ = "": LSET E$ = "abc": T = N$ = "1234" AND E$ = "": GOSUB 9000 ' 18
390 DIM F$(2): FIELD #2, 4 AS F$(1), 4 AS F$(2): GET #2, 1: T = F$(1) = "Ada " AND F$(2) = "Love": GOSUB 9000 ' 19
400 ERASE F$: GET #2, 2: DIM F$(2): T = F$(1) = "" AND LEFT$(W$, 7) = "Charles": GOSUB 9000 ' 20
410 CLOSE #2: T = LEFT$(W$, 7) = "Charles": GOSUB 9000 ' 21
500 REM numbers as characters: the low byte first, the single and the double as IEEE has them
510 T = MKI$(258) = CHR$(2) + CHR$(1) AND MKI$(-1) = STRING$(2, 255) AND CVI(MKI$(-32768)) = -32768: GOSUB 9000 ' 22
520 T = CVI(MKI$(32767)) = 32767 AND CVI("AB") = 16961 AND CVI("ABC") = 16961: GOSUB 9000 ' 23
530 T = MKL$(16909060) = CHR$(4) + CHR$(3) + CHR$(2) + CHR$(1) AND CVL(MKL$(2147483647)) = 2147483647: GOSUB 9000 ' 24
540 T = CVL(MKL$(-2147483647 - 1)) = -2147483647 - 1 AND CVL(MKL$(-2)) = -2: GOSUB 9000 ' 25
550 T = MKS$(1.5) = STRING$(2, 0) + CHR$(192) + CHR$(63) AND MKS$(-2) = STRING$(3, 0) + CHR$(192): GOSUB 9000 ' 26
560 T = MKS$(0) = STRING$(4, 0) AND MKD$(1) = STRING$(6, 0) + CHR$(240) + CHR$(63): GOSUB 9000 ' 27
570 T = MKD$(-2.5) = STRING$(6, 0) + CHR$(4) + CHR$(192) AND MKD$(0) = STRING$(8, 0): GOSUB 9000 ' 28
580 X = 3.14159265358979: T = CVD(MKD$(X)) = X AND CVD(MKD$(-1E+300)) = -1E+300 AND CVD(MKD$(1E-300)) = 1E-300: GOSUB 9000 ' 29
590 T = CVS(MKS$(.1)) <> .1 AND ABS(CVS(MKS$(.1)) - .1) < 2E-9 AND CVS(MKS$(7)) = 7 AND CVS(MKS$(-.375)) = -.375: GOSUB 9000 ' 30
600 T = CVS(MKS$(16777217)) = 16777216 AND CVS(MKS$(16777219)) = 16777220 AND CVS(MKS$(16777215)) = 16777215: GOSUB 9000 ' 31
610 T = CVS(MKS$(1E-45)) > 0 AND CVS(MKS$(1E-46)) = 0 AND CVS(MKS$(3.4E+38)) > 3.39E+38 AND CVS(MKS$(-1E-40)) < 0: GOSUB 9000 ' 32
620 T = LEN(MKI$(0) + MKL$(0) + MKS$(0) + MKD$(0)) = 18: GOSUB 9000 ' 33
700 REM OPEN with no FOR is RANDOM; a record of 128 when no length is given; LOC and LOF of a file of text
710 OPEN "test.txt" AS #3 LEN = 160: FIELD #3, 160 AS W$: GET #3, 1
720 T = LEN(W$) = 160 AND LEFT$(W$, 3) = "Ada" AND MID$(W$, 129, 4) = "LAdy" AND EOF(3) = 0 AND LOF(3) = 160: GOSUB 9000 ' 34
730 CLOSE: OPEN "test.txt" FOR RANDOM AS 1: FIELD 1, 128 AS W$: GET 1, 2
740 T = EOF(1) = -1 AND LEFT$(W$, 4) = "LAdy" AND MID$(W$, 33, 1) = CHR$(0) AND LOC(1) = 2: GOSUB 9000 ' 35
750 CLOSE: OPEN "test.txt" FOR INPUT AS #1: T = LOF(1) = 160 AND LOC(1) = 0: A$ = INPUT$(130, #1)
760 T = T AND LOC(1) = 1 AND RIGHT$(A$, 2) = "LA" AND LEN(A$) = 130: CLOSE: GOSUB 9000 ' 36
770 OPEN "O", 4, "test2.txt": PRINT #4, "0123456789";: T = LOF(4) = 10: CLOSE 4: GOSUB 9000 ' 37
780 OPEN "A", #4, "test2.txt": PRINT #4, "ab": CLOSE 4: OPEN "I", #4, "test2.txt": LINE INPUT #4, L$: CLOSE 4
790 T = L$ = "0123456789ab": GOSUB 9000 ' 38
800 REM the errors
810 ON ERROR GOTO 1100
820 OPEN "test.txt" FOR RANDOM AS #1 LEN = 16
830 W = 50: FIELD #1, 10 AS A$, 7 AS B$
840 W = 63: GET #1, 0
850 W = 63: PUT #1, -3
860 W = 54: PRINT #1, "text"
870 W = 54: INPUT #1, A$
880 W = 54: OPEN "test2.txt" FOR OUTPUT AS #2: GET #2
890 W = 52: GET #9
900 W = 52: FIELD #9, 2 AS A$
910 W = 5: OPEN "test.txt" FOR RANDOM AS #3 LEN = 0
920 W = 5: OPEN "test.txt" AS #3 LEN = 32768
930 W = 54: OPEN "X", #3, "test.txt"
940 W = 55: OPEN "test.txt" AS #1
950 W = 6: A$ = MKI$(32768)
960 W = 6: A$ = MKS$(1E+39)
970 W = 5: X = CVI("A")
980 W = 5: X = CVD("1234567")
990 W = 13: X = CVI(5)
1000 W = 13: LSET X = "a"
1010 W = 13: FIELD #1, 4 AS X
1020 W = 6: X = CVS(STRING$(2, 0) + CHR$(128) + CHR$(127))
1030 W = 5: FIELD #1, -1 AS A$
1040 W = 2: OPEN "test.txt" FOR SEQUENTIAL AS #3
1050 ON ERROR GOTO 0: CLOSE: KILL "test.txt": KILL "test2.txt"
1060 GOTO 1200
1100 E = E + 1: IF ERR <> W THEN BAD = BAD + 1: PRINT "line"; ERL; "gives error"; ERR; "and not"; W
1110 RESUME NEXT
1200 IF E <> 22 THEN BAD = BAD + 1: PRINT E; "errors, not 22"
1210 PRINT C; "checks of random-access files and"; E; "errors:"; BAD; "wrong"
1220 END
9000 C = C + 1: IF T = 0 THEN BAD = BAD + 1: PRINT "check"; C; "fails"
9010 RETURN

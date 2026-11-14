10 REM control flow
20 GOSUB 1000: PRINT "back"
30 I = 0
40 WHILE I < 3: I = I + 1: PRINT "w"; I;: WEND: PRINT
50 FOR J = 10 TO 1 STEP -3: PRINT J;: NEXT J: PRINT
60 FOR K = 1 TO 0: PRINT "never": NEXT K: PRINT "skipped"
70 IF I = 3 THEN PRINT "three" ELSE PRINT "not three"
80 IF I > 5 THEN PRINT "big" ELSE PRINT "small": PRINT "same line"
90 ON 2 GOTO 100, 110, 120
100 PRINT "wrong": END
110 PRINT "on goto ok"
120 FOR A = 1 TO 2: FOR B = 1 TO 2: PRINT A*10+B;: NEXT B, A: PRINT
130 REM data
140 READ X, Y$, Z: PRINT X; Y$; Z
150 RESTORE: READ X: PRINT "again"; X
160 DATA 42, "hello, there", -1.5
170 REM arrays and strings
180 DIM M(3, 4), N$(5)
190 FOR I = 0 TO 3: FOR J = 0 TO 4: M(I, J) = I * J: NEXT J, I
200 PRINT M(3, 4); M(2, 3)
210 N$(2) = "two": PRINT N$(2); LEN(N$(1))
220 S$ = "The quick brown fox"
230 PRINT LEFT$(S$, 3); "|"; RIGHT$(S$, 3); "|"; MID$(S$, 5, 5); "|"; INSTR(S$, "brown"); INSTR(5, S$, "q")
240 PRINT UCASE$("abc"); LCASE$("DEF"); CHR$(65); ASC("a"); STR$(12); VAL("3.5E2"); HEX$(255); OCT$(8)
250 PRINT SPACE$(3); "|"; STRING$(4, "*"); STRING$(2, 65)
260 DEF FNSQ(X) = X * X + 1
270 PRINT FNSQ(5); FNSQ(1.5)
280 A = 5: B = 7: SWAP A, B: PRINT A; B
290 PRINT 2147483647 + 1; -2147483648 - 1; 65536 * 65536; 7 / 7; 2 ^ 10; 2 ^ -1
300 PRINT INT(-3.5); FIX(-3.5); CINT(2.5); ABS(-4); SGN(-2); SQR(16); INT(3.99)
310 PRINT 5 AND 3; 5 OR 3; 5 XOR 3; NOT 0; 3 > 2; "a" < "b"; 1 = 2
320 PRINT USING "###.##"; 3.14159; -2.5
330 PRINT USING "Total: $$#,###.##"; 1234.5
340 PRINT USING "!"; "Xylophone"
350 PRINT USING "\  \|"; "abcdef"
360 PRINT USING "##.##^^^^"; 12345
370 X% = 7.6: PRINT X%; X% / 2
380 PRINT TAB(10); "tab"; SPC(3); "spc"
382 PRINT "a remark may follow a statement" ' with no colon before it
384 X = 3 ' and an assignment: PRINT "this is part of the remark"
386 PRINT X: IF X = 3 THEN PRINT "three" ' after THEN too
388 PRINT "INKEY$ with no key waiting: ["; INKEY$; "]"; LEN(INKEY$)
400 REM keywords of two bytes (the SQL ones, D114): whatever walks a line steps over one as a unit.
410 REM None of these is run: the second byte of each is the byte of a one-byte keyword (NEXT, DATA, IF ...)
420 FOR K = 1 TO 0: SQLEXEC "never": NEXT K: PRINT "FOR skipped past SQLEXEC"
430 WHILE 0: SQLCLOSE: SQLEND 1: WEND: PRINT "WHILE skipped past SQLCLOSE and SQLEND"
440 IF 0 THEN PRINT SQLNULL(1, 1) ELSE PRINT "ELSE found past SQLNULL"
450 IF 0 THEN SQLQUERY 1, "x": PRINT SQLVAL$(1, 1) ELSE PRINT "ELSE found past SQLQUERY and SQLVAL$"
460 GOTO 480
470 SQLQUERY 1, "this is not DATA": SQLOPEN "nor this": PRINT SQLCOLS(1); SQLNAME$(1, 1); SQLVAL(1, 1); SQLNEXT(1)
480 READ A$, B$, D$: PRINT "READ past SQLQUERY: "; D$
490 DATA the data
500 PRINT "SQLERR$ is ["; SQLERR$; "] and SQLCHANGES"; SQLCHANGES
510 SAVE "listed"
520 OPEN "listed.bas" FOR INPUT AS 1
530 WHILE NOT EOF(1): LINE INPUT #1, L$: IF LEFT$(L$, 2) = "47" THEN PRINT L$
540 WEND: CLOSE 1: KILL "listed.bas"
550 ON ERROR GOTO 570
560 A$ = INPUT$(1): PRINT "INPUT$ with nothing to read: not reached"
570 PRINT "INPUT$ at the end of the input: error"; ERR: RESUME 580
580 ON ERROR GOTO 0
600 REM the MID$ statement (D115): part of a string written over; the string keeps its length
610 A$ = "hello, world": MID$(A$, 1, 1) = "J": PRINT A$
620 MID$(A$, 8) = "there!!": PRINT A$; LEN(A$)
630 MID$(A$, 3, 2) = "XYZ": PRINT A$
640 MID$(A$, 12, 0) = "Q": MID$(A$, 12) = "": PRINT A$
650 DIM W$(3): W$(2) = "abcdef": MID$(W$(2), 3) = "--": PRINT W$(2); "|"; W$(1); "|"
660 B$ = A$: MID$(B$, 1) = "12": PRINT A$; " "; B$
670 MID$(A$, 2) = A$: PRINT A$
680 MID$(A$, LEN(A$)) = "!?": PRINT A$: mid$(a$,1,3)="abcdef":print a$
690 ON ERROR GOTO 730
700 MID$(A$, 13) = "x": MID$(A$, 0) = "x": MID$(A$, 1, -1) = "x": E$ = "": MID$(E$, 1) = "x"
710 MID$(N, 1) = "x": MID$(A$, 1) = 5
720 ON ERROR GOTO 0: PRINT: PRINT A$; "|"; E$; "|": GOTO 740
730 PRINT "error"; ERR;: RESUME NEXT
740 REM a string holds any character, CHR$(0) too (D117)
750 A$ = CHR$(0): B$ = "ab" + A$ + "cd": PRINT LEN(A$); ASC(A$); LEN(B$); ASC(MID$(B$, 3, 1)); INSTR(B$, "cd"); INSTR(B$, A$)
760 PRINT LEFT$(B$, 2); RIGHT$(B$, 2); LEN(MID$(B$, 2, 3)); LEN(B$ + B$)
770 PRINT B$ = "ab" + CHR$(0) + "cd"; B$ = "ab"; B$ > "ab"; B$ < "ab" + CHR$(1); "" < A$; A$ < CHR$(1); CHR$(200) > "z"
780 C$ = STRING$(3, 0): PRINT LEN(C$); LEN(STRING$(4, A$)); LEN(SPACE$(2) + C$); UCASE$("a" + A$ + "b") = "A" + A$ + "B"
790 MID$(B$, 3) = "-": PRINT B$: DIM Z$(3): Z$(3) = A$ + CHR$(255): SWAP Z$(3), A$: PRINT LEN(A$); ASC(RIGHT$(A$, 1)); LEN(Z$(3))
800 OPEN "test.txt" FOR OUTPUT AS #1: PRINT #1, "a"; CHR$(0); "b"; CHR$(200);: CLOSE #1
810 OPEN "test.txt" FOR INPUT AS #1: E$ = INPUT$(4, #1): CLOSE #1: KILL "test.txt"
820 PRINT LEN(E$); ASC(MID$(E$, 2, 1)); ASC(MID$(E$, 4, 1)); E$ = "a" + CHR$(0) + "b" + CHR$(200)
830 DEF FNL(X$) = LEN(X$ + X$): PRINT FNL(CHR$(0)); INSTR("abc", ""); INSTR(4, "abc", ""); INSTR("", "a"); INSTR(2, "abcabc", "a")
840 ON ERROR GOTO 870
850 C$ = STRING$(2, ""): C$ = STRING$(2, 256): N = ASC("")
860 ON ERROR GOTO 0: PRINT: GOTO 890
870 PRINT "error"; ERR;: RESUME NEXT
890 END
1000 PRINT "in sub": RETURN

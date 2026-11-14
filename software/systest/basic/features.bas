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
390 END
1000 PRINT "in sub": RETURN

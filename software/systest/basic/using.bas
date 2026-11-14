10 REM PRINT USING: each line is written to a file, read back and compared with what it must be.
20 REM Lines 100 to 290 are the examples of Microsoft's manual (GW-BASIC, PRINT USING), with its results;
30 REM the rest are this BASIC's own: the same rules where the manual has no example.
40 OPEN "test.txt" FOR OUTPUT AS #1
100 PRINT #1, USING "##.##"; .78; : PRINT #1, "|"
110 PRINT #1, USING "###.##"; 987.654; : PRINT #1, "|"
120 PRINT #1, USING "##.##   "; 10.2, 5.3, 66.789, .234; : PRINT #1, "|"
130 PRINT #1, USING "+##.##   "; -68.95, 2.4, 55.6, -.9; : PRINT #1, "|"
140 PRINT #1, USING "##.##-   "; -68.95, 22.449, -7.01; : PRINT #1, "|"
150 PRINT #1, USING "**#.#   "; 12.39, -0.9, 765.1; : PRINT #1, "|"
160 PRINT #1, USING "$$###.##"; 456.78; : PRINT #1, "|"
170 PRINT #1, USING "**$##.##"; 2.34; : PRINT #1, "|"
180 PRINT #1, USING "####,.##"; 1234.5; : PRINT #1, "|"
190 PRINT #1, USING "####.##,"; 1234.5; : PRINT #1, "|"
200 PRINT #1, USING "##.##^^^^"; 234.56; : PRINT #1, "|"
210 PRINT #1, USING ".####^^^^-"; 888888; : PRINT #1, "|"
220 PRINT #1, USING "+.##^^^^"; 123; : PRINT #1, "|"
230 PRINT #1, USING "_!##.##_!"; 12.34; : PRINT #1, "|"
240 PRINT #1, USING "##.##"; 111.22; : PRINT #1, "|"
250 PRINT #1, USING ".##"; .999; : PRINT #1, "|"
260 PRINT #1, USING "!"; "LOOK"; "OUT"; : PRINT #1, "|"
270 PRINT #1, USING "\  \"; "LOOK"; "OUT"; : PRINT #1, "|"
280 PRINT #1, USING "\   \"; "LOOK"; "OUT"; "!!"; : PRINT #1, "|"
290 PRINT #1, USING "!&"; "LOOK"; "OUT"; : PRINT #1, "|"
300 PRINT #1, USING "**$##.##"; -2.34; : PRINT #1, "|"
310 PRINT #1, USING "**$#,###.##-"; -1234.5; : PRINT #1, "|"
320 PRINT #1, USING "$$#,###.##"; 0; 99999.99; -9999.99; : PRINT #1, "|"
330 PRINT #1, USING "$$##"; 12345; : PRINT #1, "|"
340 PRINT #1, USING "##.##^^^^"; 0; -234.56; 1E-10; : PRINT #1, "|"
350 PRINT #1, USING "###^^^^"; 0; 12345; : PRINT #1, "|"
360 PRINT #1, USING "#.###^^^^"; 9.9996; : PRINT #1, "|"
370 PRINT #1, USING "##.##^^^^-"; -234.56; : PRINT #1, "|"
380 PRINT #1, USING "+##.##^^^^"; 234.56; : PRINT #1, "|"
390 PRINT #1, USING "##.##^^^^"; 1E+100; -1E+100; : PRINT #1, "|"
400 PRINT #1, USING "##.##^^^^^"; 234.56; -1E+100; 1E-100; : PRINT #1, "|"
410 PRINT #1, USING "#^^^^"; 234.56; : PRINT #1, "|"
420 PRINT #1, USING "$$##.##^^^^"; 12; -12; : PRINT #1, "|"
430 PRINT #1, USING "#.##"; .015; .125; 2.675; -.5; .5; : PRINT #1, "|"
440 PRINT #1, USING ".##"; .5; .005; .004; 0; : PRINT #1, "|"
450 PRINT #1, USING "#"; 2.5; -1; 0; : PRINT #1, "|"
460 PRINT #1, USING "##"; 99.5; : PRINT #1, "|"
470 PRINT #1, USING "###-"; 5; -5; : PRINT #1, "|"
480 PRINT #1, USING "###+"; 5; -5; : PRINT #1, "|"
490 PRINT #1, USING "+###"; 5; -5; : PRINT #1, "|"
500 PRINT #1, USING "+##+"; 5; : PRINT #1, "|"
510 PRINT #1, USING "#,###,###"; 1234567; 1234; : PRINT #1, "|"
520 PRINT #1, USING "A+B=##"; 5; : PRINT #1, "|"
530 PRINT #1, USING "** $$"; 5; 6; : PRINT #1, "|"
540 PRINT #1, USING "##."; 12; : PRINT #1, "|"
550 PRINT #1, USING "########################"; 1.5E+23; : PRINT #1, "|"
560 PRINT #1, USING "###"; 1.5E+24; -1E+100; : PRINT #1, "|"
570 PRINT #1, USING "############.############"; 123456789012.123456; : PRINT #1, "|"
580 PRINT #1, USING "x ## y ## z"; 1, 2, 3; : PRINT #1, "|"
590 A% = 7: PRINT #1, USING "##.#"; A%; : PRINT #1, "|"
600 PRINT #1, USING "###.##"; 999.995; -999.995; : PRINT #1, "|"
610 PRINT #1, USING "####,"; 1234; : PRINT #1, USING "###,"; 1234; : PRINT #1, "|"
620 PRINT #1, USING "##.##^^^"; 12.5; : PRINT #1, "|"
690 CLOSE #1
700 OPEN "test.txt" FOR INPUT AS #1
710 READ W$: IF W$ = "end" THEN 750
720 LINE INPUT #1, L$: N = N + 1
730 IF L$ <> W$ THEN BAD = BAD + 1: PRINT "line"; 90 + 10 * N; "gives ["; L$; "], not ["; W$; "]"
740 GOTO 710
750 IF NOT EOF(1) THEN BAD = BAD + 1: PRINT "more lines than results"
760 CLOSE #1: KILL "test.txt"
800 REM the errors: no field for a value (5), more than 24 places (5), the wrong kind of value (13)
810 ON ERROR GOTO 900
820 W = 5: PRINT USING "no field"; 1
830 W = 5: PRINT USING "\"; "x"
840 W = 5: PRINT USING "_#"; 1
850 W = 5: PRINT USING "#########################"; 1
860 W = 13: PRINT USING "##"; "a"
870 W = 13: PRINT USING "!"; 1
880 ON ERROR GOTO 0
890 GOTO 950
900 E = E + 1: IF ERR <> W THEN BAD = BAD + 1: PRINT "line"; ERL; "gives error"; ERR; "and not"; W
910 RESUME NEXT
950 IF E <> 6 THEN BAD = BAD + 1: PRINT E; "errors, not 6"
960 PRINT N; "lines of PRINT USING and"; E; "errors checked:"; BAD; "wrong"
970 END
1000 DATA " 0.78|", "987.65|", "10.20    5.30   66.79    0.23   |"
1010 DATA "-68.95    +2.40   +55.60    -0.90   |", "68.95-   22.45     7.01-   |"
1020 DATA "*12.4   *-0.9   765.1   |", " $456.78|", "***$2.34|", "1,234.50|", "1234.50,|"
1030 DATA " 2.35E+02|", ".8889E+06 |", "+.12E+03|", "!12.34!|", "%111.22|", "%1.00|"
1040 DATA "LO|", "LOOKOUT |", "LOOK OUT  !!   |", "LOUT|"
1050 DATA "**-$2.34|", "**$1,234.50-|", "     $0.00$99,999.99-$9,999.99|", "%$12345|"
1060 DATA " 0.00E+00-2.35E+02 1.00E-10|", " 00E+00 12E+03|", " .100E+02|", "23.46E+01-|", "+23.46E+01|"
1070 DATA "1.00E+100%-1.00E+100|", " 2.35E+002-1.00E+100 1.00E-100|", "2E+02|", " $12.00E+00-$12.00E+00|"
1080 DATA "0.020.132.68-.500.50|", ".50.01.00.00|", "3%-10|", "%100|", "  5   5-|", "  5+  5-|", "  +5  -5|"
1090 DATA " +5+|", "1,234,567    1,234|", "A+B= 5|", "*5 $6|", "12.|", "150000000000000000000000|"
1100 DATA "%1.5E+24%-1E+100|", "123456789012.123000000000|", "x  1 y  2 zx  3 y |", " 7.0|"
1110 DATA "%1000.00%-1000.00|", "1,234%1,234|", "12.50^^^|", "end"

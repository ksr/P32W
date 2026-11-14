10 REM graphics.bas -- BASIC's graphics statements (basic(1), D112): each figure is drawn on the video
20 REM card's screen and its dots are asked for with POINT. Needs the card:  sh /usr/tests/run video
30 ON ERROR GOTO 8000
40 E = 0: LINE (0, 0)-(5, 5)
50 N$ = "a graphics statement without SCREEN 1 is an illegal function call": OK = E = 5: GOSUB 9000
60 E = 0: SCREEN 3
70 N$ = "SCREEN 3: no such screen": OK = E = 5: GOSUB 9000
80 E = 0: SCREEN 1
90 IF E <> 0 THEN PRINT "graphics.bas: SCREEN 1: error"; E: END
100 N$ = "the screen is black": OK = POINT(0, 0) = 0 AND POINT(639, 479) = 0 AND POINT(320, 240) = 0: GOSUB 9000
110 N$ = "POINT off the screen is -1": OK = POINT(-1, 0) = -1 AND POINT(640, 0) = -1 AND POINT(0, 480) = -1: GOSUB 9000
120 PSET (10, 10): PSET (12, 10), 1: PSET (14, 10): PRESET (14, 10): PSET (16, 10): PSET (16, 10), 0
130 N$ = "PSET and PRESET": OK = POINT(10, 10) = 1 AND POINT(11, 10) = 0 AND POINT(12, 10) = 1 AND POINT(14, 10) = 0 AND POINT(16, 10) = 0: GOSUB 9000
140 PSET (0, 0): PSET (639, 479): PSET (700, 500): PSET (-5, -5)
150 N$ = "the corners: x 0, y 0 is the top-left dot": OK = POINT(0, 0) = 1 AND POINT(639, 479) = 1: GOSUB 9000
200 LINE (10, 20)-(60, 20): Y = 20: GOSUB 9100
210 N$ = "a level line has both its ends": OK = S = 51 AND POINT(10, 20) = 1 AND POINT(60, 20) = 1 AND POINT(9, 20) = 0 AND POINT(61, 20) = 0: GOSUB 9000
220 LINE (10, 30)-(40, 60)
230 N$ = "a line at 45 degrees goes down the screen": OK = POINT(10, 30) = 1 AND POINT(25, 45) = 1 AND POINT(40, 60) = 1 AND POINT(25, 44) = 0: GOSUB 9000
240 LINE (10, 70)-(60, 90), , B: Y = 70: GOSUB 9100: T = S: Y = 80: GOSUB 9100
250 N$ = "LINE with B is a box": OK = T = 51 AND S = 2 AND POINT(10, 90) = 1 AND POINT(60, 90) = 1 AND POINT(35, 80) = 0: GOSUB 9000
260 LINE (10, 100)-(60, 120), 1, BF: Y = 110: GOSUB 9100
270 N$ = "LINE with BF is a filled box": OK = S = 51 AND POINT(35, 100) = 1 AND POINT(35, 120) = 1 AND POINT(35, 121) = 0: GOSUB 9000
280 LINE (20, 105)-(50, 115), 0, BF: Y = 110: GOSUB 9100
290 N$ = "colour 0 puts dots out": OK = S = 20: GOSUB 9000
300 LINE (10, 130)-(60, 130), 2: LINE (30, 130)-(70, 130), 2: Y = 130: GOSUB 9100
310 N$ = "colour 2 turns dots over": OK = S = 30 AND POINT(20, 130) = 1 AND POINT(40, 130) = 0 AND POINT(65, 130) = 1: GOSUB 9000
320 COLOR 2: LINE (10, 140)-(60, 150): LINE (10, 140)-(60, 150): COLOR 1: Y = 140: GOSUB 9100: T = S: Y = 150: GOSUB 9100
330 N$ = "COLOR 2: a line drawn twice is gone": OK = T = 0 AND S = 0: GOSUB 9000
340 PSET (10, 160): LINE -(30, 160): LINE -STEP (0, 10): LINE -STEP (-20, 0): LINE -(10, 160)
350 Y = 160: GOSUB 9100: T = S: Y = 165: GOSUB 9100
360 N$ = "LINE from the last point, and STEP": OK = T = 21 AND S = 2 AND POINT(30, 170) = 1 AND POINT(10, 170) = 1 AND POINT(20, 165) = 0: GOSUB 9000
370 LINE (40, 160)-STEP (10, 10): PSET STEP (5, 0)
380 N$ = "STEP in a second point is from the first": OK = POINT(50, 170) = 1 AND POINT(55, 170) = 1 AND POINT(45, 165) = 1: GOSUB 9000
400 CIRCLE (320, 240), 100
410 OK = POINT(420, 240) = 1 AND POINT(220, 240) = 1 AND POINT(320, 140) = 1 AND POINT(320, 340) = 1 AND POINT(320, 240) = 0
420 OK = OK AND POINT(391, 170) = 1 AND POINT(390, 169) = 1 AND POINT(391, 169) = 0 AND POINT(421, 240) = 0 AND POINT(419, 240) = 0
430 N$ = "a circle": GOSUB 9000
440 CIRCLE (320, 240), 100, 0
450 N$ = "... put out again with colour 0": OK = POINT(420, 240) = 0 AND POINT(320, 140) = 0 AND POINT(391, 170) = 0: GOSUB 9000
460 CIRCLE (320, 240), 100, , 0, PI / 2
470 N$ = "an arc from 0 to pi / 2 is the top right quarter": OK = POINT(420, 240) = 1 AND POINT(320, 140) = 1 AND POINT(391, 170) = 1 AND POINT(220, 240) = 0 AND POINT(320, 340) = 0 AND POINT(391, 310) = 0: GOSUB 9000
480 CLS
490 N$ = "CLS: the picture black": OK = POINT(420, 240) = 0 AND POINT(0, 0) = 0 AND POINT(35, 110) = 0: GOSUB 9000
500 CIRCLE (320, 240), 100, , , , 0.5
510 N$ = "an aspect below 1: as wide as the radius, less high": OK = POINT(420, 240) = 1 AND POINT(220, 240) = 1 AND POINT(320, 190) = 1 AND POINT(320, 290) = 1 AND POINT(320, 140) = 0: GOSUB 9000
520 CIRCLE (320, 240), 100, , , , 2
530 N$ = "an aspect above 1: as high as the radius, less wide": OK = POINT(370, 240) = 1 AND POINT(270, 240) = 1 AND POINT(320, 140) = 1 AND POINT(320, 340) = 1: GOSUB 9000
540 CLS: CIRCLE (320, 240), 100, 1, -PI / 2, -PI
550 OK = POINT(320, 140) = 1 AND POINT(220, 240) = 1 AND POINT(320, 190) = 1 AND POINT(270, 240) = 1 AND POINT(249, 170) = 1
560 N$ = "negative angles: the arc and its two radii": OK = OK AND POINT(420, 240) = 0 AND POINT(320, 340) = 0 AND POINT(370, 240) = 0 AND POINT(320, 290) = 0: GOSUB 9000
570 E = 0: CIRCLE (320, 240), 50, 7
580 N$ = "no such colour": OK = E = 5 AND POINT(370, 240) = 0: GOSUB 9000
590 CIRCLE STEP (10, 10), 5
600 N$ = "the last point after CIRCLE is its centre": OK = POINT(335, 250) = 1 AND POINT(325, 250) = 1: GOSUB 9000
700 CLS: LOCATE 2, 3: PRINT "I";
710 PRINT "|"
720 X0 = 16: GOSUB 9200: T = S: X0 = 24: GOSUB 9200: U = S: X0 = 32: GOSUB 9200
730 N$ = "LOCATE: the line PRINT writes goes into the picture, from that row and column": OK = T > 8 AND U > 8 AND S = 0: GOSUB 9000
740 PRINT "this line is on the terminal"
750 LOCATE 2, 3: PRINT "  ": X0 = 16: GOSUB 9200: T = S: X0 = 24: GOSUB 9200
760 N$ = "text is written over text": OK = T = 0 AND S = 0: GOSUB 9000
770 COLOR 2: LOCATE 2, 3: PRINT "W": X0 = 16: GOSUB 9200: T = S: LOCATE 2, 3: PRINT "W": GOSUB 9200: COLOR 1
780 N$ = "text in colour 2, and again: gone": OK = T > 8 AND S = 0: GOSUB 9000
790 LOCATE 30, 80: PRINT "Z": X0 = 632: FOR Y = 464 TO 479: FOR X = X0 TO X0 + 7: S = S + POINT(X, Y): NEXT: NEXT
800 N$ = "the last row and column are 30 and 80": OK = S > 8: GOSUB 9000
900 SCREEN 0
910 E = 0: A = POINT(0, 0)
920 N$ = "SCREEN 0: the picture is gone, POINT is an illegal function call": OK = E = 5: GOSUB 9000
930 PRINT "graphics.bas:"; C; "checks,"; F; "failed"
940 END
8000 E = ERR: RESUME NEXT
9000 C = C + 1: IF NOT OK THEN F = F + 1: PRINT "FAIL: "; N$
9010 RETURN
9100 REM S: how many dots are lit on line Y from x 0 to 79
9110 S = 0: FOR X = 0 TO 79: S = S + POINT(X, Y): NEXT: RETURN
9200 REM S: how many dots are lit in the character cell whose left dot is X0, in row 2 (lines 16 to 31)
9210 S = 0: FOR Y = 16 TO 31: FOR X = X0 TO X0 + 7: S = S + POINT(X, Y): NEXT: NEXT: RETURN

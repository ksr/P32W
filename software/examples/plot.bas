10 REM plot.bas -- a curve on the video card's screen, with BASIC's graphics statements (man basic)
20 REM     basic plot.bas
30 REM The picture stays 5 seconds; then the console's text is back.
40 SCREEN 1
50 LINE (0, 0)-(639, 479), , B
60 LINE (40, 240)-(620, 240): LINE (40, 40)-(40, 440)             ' the axes
70 FOR X = 40 TO 620 STEP 48: LINE (X, 236)-(X, 244): NEXT         ' a mark at each whole x
80 FOR Y = 60 TO 420 STEP 90: LINE (36, Y)-(44, Y): NEXT           ' and at each half of y
90 LOCATE 2, 8: PRINT "y = exp(-x / 4) * sin(2 * x)"
100 LOCATE 16, 79: PRINT "x"
110 LOCATE 4, 2: PRINT "1": LOCATE 27, 1: PRINT "-1"
120 PSET (40, 240)
130 FOR I = 0 TO 144
140 X = I / 12
150 LINE -(40 + 4 * I, 240 - 180 * EXP(-X / 4) * SIN(2 * X))        ' from the last point to the next
160 NEXT
170 CIRCLE (540, 110), 50                                          ' a ball: a circle and two ellipses
180 CIRCLE (540, 110), 50, , , , 0.35: CIRCLE (540, 110), 50, , , , 2.8
190 SLEEP 5
200 SCREEN 0

10 REM guess.bas -- guess the number (a game: run it with basic guess.bas)
20 RANDOMIZE
30 N = INT(RND * 100) + 1: T = 0
40 PRINT "I am thinking of a number from 1 to 100."
50 INPUT "Your guess"; G
60 T = T + 1
70 IF G < N THEN PRINT "Higher.": GOTO 50
80 IF G > N THEN PRINT "Lower.": GOTO 50
90 PRINT "Yes! It was"; N; "- you took"; T; "guesses."
100 INPUT "Again (y/n)"; A$
110 IF LEFT$(A$, 1) = "y" OR LEFT$(A$, 1) = "Y" THEN 30

10 REM primes.bas -- the primes below 1000, by the sieve of Eratosthenes
20 N = 1000
30 DIM S(N)
40 FOR I = 2 TO SQR(N)
50 IF S(I) = 0 THEN FOR J = I * I TO N STEP I: S(J) = 1: NEXT J
60 NEXT I
70 C = 0
80 FOR I = 2 TO N
90 IF S(I) = 0 THEN PRINT USING "#####"; I;: C = C + 1: IF C MOD 12 = 0 THEN PRINT
100 NEXT I
110 PRINT: PRINT C; "primes below"; N

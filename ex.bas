10 REM Primes generator, enter a value and get all positive prime numbers up to it
20 PRINT "Enter max prime: ":
30 INPUT C
40 A = 2
50 GOTO 1000
60 A = A + 1
70 IF A < C + 1 THEN GOTO 50
80 END
1000 REM Print number if its prime
1010 B = 2
1020 IF A % B = 0 THEN GOTO 60
1030 B = B + 1
1040 IF B < A / 2 THEN GOTO 1010
1050 PRINT A
1060 GOTO 60

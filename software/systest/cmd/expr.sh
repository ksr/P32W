# expr -- operators, precedence, truth (0, 00, -0 and empty are false),
# the : match, overflow, errors
echo '--- arithmetic'
expr 1 + 2
expr 7 - 10
expr 7 \* 6
expr 17 / 5
expr -17 / 5
expr 17 % 5
expr -17 % 5
expr 2 + 3 \* 4
expr \( 2 + 3 \) \* 4
expr 10 - 3 - 2
expr 100 / 10 / 5
expr 007 + 1
echo '--- comparisons'
expr 10 \> 9
expr 10 \< 9
expr abc \< abd
expr 10 = 010
expr 10 != 10
expr 2 \<= 2
expr 3 \>= 4
expr a = a
expr b \> a
expr -1 \< 1
echo '--- | and &'
expr 0 \| 5
expr 00 \| 5
expr -0 \| 5
expr '' \| x
expr 3 \| 5
expr '' \| ''
expr '' \| 0
expr 3 \& 4
expr 3 \& 0
expr 0 \& 3
expr 3 \& ''
echo '--- truth and status'
expr 0; echo "status $?"
expr 00; echo "status $?"
expr -0; echo "status $?"
expr ''; echo "status $?"
expr x; echo "status $?"
expr 1 - 1; echo "status $?"
expr 0x; echo "status $?"
echo '--- :'
expr abcdef : abc
expr abcdef : bcd
expr abcdef : 'a.c'
expr abcdef : '.*'
expr abcdef : '.*d'
expr abcdef : 'a\(.*\)e'
expr abcdef : 'x\(.*\)'; echo "status $?"
expr abcdef : x; echo "status $?"
expr x12y : 'x\([0-9]*\)'
expr x12y : 'x[0-9]*'
expr x12y : 'x[^0-9]'
expr aaab : 'a*'
expr aaab : 'a\{2\}'
expr aaab : 'a\{1,\}b'
expr aaab : 'a\{1,2\}b'
expr abab : '\(ab\)\1'
expr abc : 'abc$'
expr abcd : 'abc$'
expr abc : '^ab'
expr 'a*b' : 'a\*'
expr '*x' : '*x'
expr '[x' : '[[]'
expr file.c : '.*\.c$'
expr file.c : '\(.*\)\.c$'
expr /usr/ken/bin : '.*/\(.*\)'
expr 3 + abc : 'a.*'
expr abc : 'b'; echo "status $?"
expr abc : 'a\('; echo "status $?"
expr abc : 'a\)'; echo "status $?"
expr abc : 'a\{x\}'; echo "status $?"
expr abc : '[ab'; echo "status $?"
echo '--- limits'
expr 2147483647 + 0
expr -2147483647 - 1
expr 2147483647 + 1; echo "status $?"
expr -2147483647 - 2; echo "status $?"
expr 65536 \* 65536; echo "status $?"
expr 65536 \* 32768; echo "status $?"
expr -65536 \* 32768
expr -2147483648 / -1; echo "status $?"
expr -2147483648 % -1; echo "status $?"
expr -2147483648 / 1
expr 99999999999 + 0; echo "status $?"
expr 99999999999 = 99999999999; echo "status $?"
echo '--- errors'
expr; echo "status $?"
expr 1 +; echo "status $?"
expr 1 2; echo "status $?"
expr \( 1; echo "status $?"
expr a + 1; echo "status $?"
expr 1 / 0; echo "status $?"
expr 1 % 0; echo "status $?"
expr 1.5 + 1; echo "status $?"

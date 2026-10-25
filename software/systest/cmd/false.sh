# false -- status 1, arguments ignored
false; echo "status $?"
false x y; echo "status $?"
false -x --help x; echo "status $?"
false > f; echo "status $?"; wc -c < f
if false; then echo yes; else echo no; fi
while false; do echo never; done; echo "loop done"
true | false; echo "pipe $?"
false && echo and; false || echo or

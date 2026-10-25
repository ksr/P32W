# dirname: the directory part, slashes
dirname /usr/ken/prog.c
dirname prog.c
dirname /prog.c
dirname /
dirname //
dirname a/
dirname a/b/
dirname /usr/ken/
dirname /usr//ken
dirname a//b//
dirname ''
echo "status $?"
dirname; echo "status $?"
dirname a b; echo "status $?"

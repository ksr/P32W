# basename: last components, suffixes, slashes
basename /usr/ken/prog.c
basename /usr/ken/prog.c .c
basename prog.c .c
basename prog.c prog.c
basename .c .c
basename /usr/ken/
basename /usr/ken//
basename /
basename //
basename a
basename a/b/ .c
basename x.c/ .c
basename ''
echo "status $?"
basename; echo "status $?"
basename a b c; echo "status $?"

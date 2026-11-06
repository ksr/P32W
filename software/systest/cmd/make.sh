# make: a command with nothing in it for the shell runs by itself, anything
# else through sh, and either way it behaves the same (D93)
echo 'all: plain quoted var redirect list builtin' > Makefile
echo 'plain:' >> Makefile
echo '	echo plain   words' >> Makefile
echo 'quoted:' >> Makefile
echo '	echo "two  spaces"' >> Makefile
echo 'var:' >> Makefile
echo '	@X=set; echo $$X' >> Makefile
echo 'redirect:' >> Makefile
echo '	echo into a file > out.txt' >> Makefile
echo '	cat out.txt' >> Makefile
echo 'list:' >> Makefile
echo '	echo one; echo two' >> Makefile
echo 'builtin:' >> Makefile
echo '	cd /tmp' >> Makefile
echo '	umask 022' >> Makefile
echo 'nosuch:' >> Makefile
echo '	nosuchcommand arg' >> Makefile
echo 'fails:' >> Makefile
echo '	false' >> Makefile
echo '	echo not reached' >> Makefile
echo 'ignored:' >> Makefile
echo '	-false' >> Makefile
echo '	echo after' >> Makefile
echo 'silent:' >> Makefile
echo '	@echo quiet' >> Makefile
make
echo "status $?"
make nosuch
echo "status $?"
make fails
echo "status $?"
make ignored
echo "status $?"
make silent
echo "status $?"

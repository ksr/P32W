# shutdown -- only its errors: the test must not bring the system down, and
# a time that is not now or +minutes is refused before anything is done
shutdown +x; echo "status $?"
shutdown +; echo "status $?"
shutdown -r +5x; echo "status $?"
shutdown +99999; echo "status $?"
if test `id -u` -ne 0
then
    shutdown; echo "status $?"
    shutdown -r now; echo "status $?"
    shutdown -h +1 a message; echo "status $?"
else
    echo "(root: not run)"
fi

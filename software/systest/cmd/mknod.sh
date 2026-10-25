# mknod (/etc/mknod): only root may make special files, so as an ordinary
# user the refusal and the argument checks
/etc/mknod n c 2 0; echo "status $?"
/etc/mknod n b 1 0; echo "status $?"
test -e n; echo "nothing made: $?"
echo x > e
/etc/mknod e c 2 0; echo "exists: status $?"
echo '--- usage'
/etc/mknod n p; echo "status $?"
/etc/mknod n p 1 2; echo "status $?"
/etc/mknod n c 256 0; echo "status $?"
/etc/mknod n c 1 1000; echo "status $?"
/etc/mknod n c 1 x; echo "status $?"
/etc/mknod n c -1 0; echo "status $?"
/etc/mknod n x 1 0; echo "status $?"
/etc/mknod n cc 1 0; echo "status $?"
/etc/mknod n c 1; echo "status $?"
/etc/mknod; echo "status $?"

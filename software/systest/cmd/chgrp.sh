# chgrp: names, numbers, -R, errors (chown(2) is root's, so as an ordinary
# user only the group a file already has can be "changed": no error)
echo x > f
mkdir d d/e
echo y > d/e/g
g=`id -gn`
chgrp $g f; echo "the same group: status $?"
chgrp `id -g` f; echo "by number: status $?"
chgrp -R $g d; echo "-R, the same group: status $?"
chgrp -- $g f; echo "-- : status $?"
echo '--- not root'
chgrp root f; echo "status $?"
chgrp 0 f d/e/g; echo "status $?"
chgrp -R root d; echo "-R stops at the top: status $?"
echo '--- errors'
chgrp nosuchgroup f; echo "status $?"
chgrp 1abc f; echo "status $?"
chgrp $g nosuch f; echo "status $?"
chgrp $g; echo "status $?"
chgrp; echo "status $?"
chgrp -x $g f; echo "status $?"
chgrp -R; echo "status $?"

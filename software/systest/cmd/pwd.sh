# pwd: the working directory, in several places
cd /; pwd
cd /usr; pwd
cd /bin/..; pwd
cd /tmp/../usr/./ken/..; pwd
cd /dev; pwd; echo "status $?"
cd /tmp
here=`pwd`
echo "in /tmp: $here"

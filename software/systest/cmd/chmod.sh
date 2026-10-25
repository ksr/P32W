# chmod: octal and symbolic modes, -R, errors
echo x > f
mkdir d
m=f
for mode in 644 755 600 4755 1777 0 u+r u+w,go+r +x a-x u=rwx,g=rx,o= g=u o=g go-rwx 700 u-x+s g+s u-s,g-s +t -t a=r u+w a+X
do
    chmod $mode f
    for w in `ls -l f`; do echo "$mode $w"; break; done
done
echo "-- X: x for directories, or files with an x already"
chmod 644 f
chmod 700 d
chmod go+X f d
for w in `ls -l f`; do echo "f $w"; break; done
for w in `ls -ld d`; do echo "d $w"; break; done
chmod 744 f
chmod go+X f
for w in `ls -l f`; do echo "f $w"; break; done
echo "-- several files"
echo y > g
chmod 640 f g; echo "status $?"
for w in `ls -l f`; do echo "f $w"; break; done
for w in `ls -l g`; do echo "g $w"; break; done
echo "-- -R"
mkdir d/e; echo z > d/e/z; echo w > d/w
chmod -R 755 d; echo "status $?"
for w in `ls -l d/e/z`; do echo "d/e/z $w"; break; done
chmod -R go-rx d; echo "status $?"
for w in `ls -ld d`; do echo "d $w"; break; done
for w in `ls -ld d/e`; do echo "d/e $w"; break; done
for w in `ls -l d/w`; do echo "d/w $w"; break; done
chmod -R u-x,a+X d 2>&1; echo "status $?"
for w in `ls -l d/e/z`; do echo "d/e/z $w"; break; done
chmod -R 755 d
echo "-- errors"
chmod 644 nosuch; echo "status $?"
chmod 644 nosuch f; echo "the next is done: status $?"
chmod 8 f; echo "status $?"
chmod 77777 f; echo "status $?"
chmod u+q f; echo "status $?"
chmod z+r f; echo "status $?"
chmod u f; echo "status $?"
chmod u+r, f; echo "status $?"
chmod 644; echo "status $?"
chmod; echo "status $?"
for w in `ls -l f`; do echo "f still $w"; break; done
chmod -w f; echo "-w is a mode: status $?"
for w in `ls -l f`; do echo "f $w"; break; done

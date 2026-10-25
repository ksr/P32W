# wall: the errors (a message itself would appear on every terminal, not
# here); messages are not sent (the file is missing, or nobody may get one)
wall nosuch; echo "status $?"
wall a b; echo "status $?"
wall -x; echo "status $?"

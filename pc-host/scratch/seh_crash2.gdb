set pagination off
set confirm off
set height 0
handle SIGSEGV stop print
run
bt 16
printf "rip="
info registers rip

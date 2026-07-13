cd demos
../build/c64asm $1.asm -o $1.prg --listing $1.lst
# open $1.prg
/Applications/vice-arm64-gtk3-3.10/bin/x64sc -silent $1.prg > vice.log &

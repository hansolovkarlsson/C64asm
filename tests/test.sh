echo "Assemble and start $1.prg"
../bin/c64asm $1.asm -o $1.prg --listing $1.lst
/Applications/vice-arm64-gtk3-3.10/bin/x64sc -silent $1.prg > vice.log &
cd ..


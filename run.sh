cd examples
echo "Starting $1.prg"
/Applications/vice-arm64-gtk3-3.10/bin/x64sc -silent $1.prg > vice.log &
cd ..


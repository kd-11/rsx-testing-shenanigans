@ECHO OFF
del formats.self
call ../../make_SELF.bat
cd ../../../ps3dev/bin
scetool.exe -d ../../project/rpcs3_tests/formats/formats.self ../../project/rpcs3_tests/formats/formats.decrypted.elf
make_fself.exe ../../project/rpcs3_tests/formats/formats.decrypted.elf ../../project/rpcs3_tests/formats/formats.self
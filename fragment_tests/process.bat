@ECHO OFF
del fragment_tests.self
call ../../make_SELF.bat
cd ../../../ps3dev/bin
scetool.exe -d ../../project/rpcs3_tests/fragment_tests/fragment_tests.self ../../project/rpcs3_tests/fragment_tests/fragment_tests.decrypted.elf
make_fself.exe ../../project/rpcs3_tests/fragment_tests/fragment_tests.decrypted.elf ../../project/rpcs3_tests/fragment_tests/fragment_tests.self
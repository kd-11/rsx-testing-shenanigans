@ECHO OFF
del timer.self
call ../../make_SELF.bat
cd ../../../ps3dev/bin
scetool.exe -d ../../project/rpcs3_tests/timer/timer.self ../../project/rpcs3_tests/timer/timer.decrypted.elf
make_fself.exe ../../project/rpcs3_tests/timer/timer.decrypted.elf ../../project/rpcs3_tests/timer/timer.self
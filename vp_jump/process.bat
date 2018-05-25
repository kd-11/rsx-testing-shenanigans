@ECHO OFF
del vp_jump.self
call ../../make_SELF.bat
cd ../../../ps3dev/bin
scetool.exe -d ../../project/rpcs3_tests/vp_jump/vp_jump.self ../../project/rpcs3_tests/vp_jump/vp_jump.decrypted.elf
make_fself.exe ../../project/rpcs3_tests/vp_jump/vp_jump.decrypted.elf ../../project/rpcs3_tests/vp_jump/vp_jump.self
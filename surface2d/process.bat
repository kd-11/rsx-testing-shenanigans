@ECHO OFF
del surface2d.self
call ../../make_SELF.bat
cd ../../../ps3dev/bin
scetool.exe -d ../../project/rpcs3_tests/surface2d/surface2d.self ../../project/rpcs3_tests/surface2d/surface2d.decrypted.elf
make_fself.exe ../../project/rpcs3_tests/surface2d/surface2d.decrypted.elf ../../project/rpcs3_tests/surface2d/surface2d.self

wolfssl_lib:
  Build wolfssl_lib.lib

test:
  - For getting BPS files, create "bps" folder under "Projects".
  - Create "DUMMY" project under "bsp" with your MPU name property
  - close "DUMMY" project, and open "test" project
  - change MPU name property of the project
  - uncomment "Use SIM I/O" lines in "bsp/resetprg.c"
  - set heap size in "bsp/sbrk.h"
  - set stack size in "bsp/stacksct.h"
  Build "test" wolfCrypt


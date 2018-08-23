set(CMAKE_SYSTEM_NAME Generic)
set(CPU_FLAGS "--cpu Cortex-M4")
set(EWARM_ROOT_DIR "C:/Program Files (x86)/IAR Systems/Embedded Workbench 8.0/arm")
set(CMAKE_C_COMPILER   "${EWARM_ROOT_DIR}/bin/iccarm.exe")
set(CMAKE_CXX_COMPILER "${EWARM_ROOT_DIR}/bin/iccarm.exe")
set(CMAKE_ASM_COMPILER "${EWARM_ROOT_DIR}/bin/iccarm.exe")

set(CMAKE_C_COMPILER_FLAGS   "${CPU_FLAGS} -c")
set(CMAKE_BUILD_FLAGS        "${CPU_FLAGS} -c")
set(CMAKE_CXX_COMPILER_FLAGS "${CPU_FLAGS} --c++")

set(LINKER_SCRIPT ${EWARM_ROOT_DIR}/config/linker/ST/stm32f745xG.icf)
set(CMAKE_C_LINK_FLAGS   "--semihosting  --config \"${LINKER_SCRIPT}\" ")
set(CMAKE_CXX_LINK_FLAGS "--semihosting  --config \"${LINKER_SCRIPT}\" ")

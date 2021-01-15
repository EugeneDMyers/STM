
This is the CMake based build system for the STM.

   CMake Options:

        BIOS - suboption: coreboot

		This option starts the SMI Handler in a 32-bit virtual machine.

		Default: the SMI Handler is startted in a 64-bit virtual machine.

        UARTBASE - suboption: UART I/O address in hex format.
                
		This option defines the UART port for STM console output.

		0x000 indicates no serial port.

		Default: Console output will be directed to port 0x3F8

        HEAPSIZE - suboption: size of STM heap space in hex format

		The STM_HEAPSIZE defines the heap space that is available
	        to the STM.  The default size assumes a MSEG size of 0x100000.

		If STMPE_ENABLED is set, STM_HEAPSIZE should be a minimum
		of 0x246000, which assumes a MSEG size of 0x300000.  This
		size allows a VM/PE to occupy 0x200000 bytes.

		Default: STM heap space is set at 0x46000

	BUILD - suboptions: release, debug

		This option defines the amount of console output the
		STM generates.

		debug - all console output
		release - no console output

		Default: debug

	CBMEM_ENABLED - (coreboot only) Places the STM console output into the cbmem.

		Default: STM console output is not sent to the cbmem.

	STMPE_ENABLED - Enable the Virtual Machine - Protected Execution (VM/PE)

		STM/PE supports additional virtual machines in SMRAM each of which
		is a protected execution environment.  This capability provides a
		safe environment for applications such as introspection.

		Default: STM/PE is disabled

Steps for building:

    (1) Create a build directory in the Stm directory.
    (2) Inside the build directory issue "cmake .. " then make
        --> For coreboot do "cmake .. -DBIOS=coreboot" then make
    (3) The StmPkg/Core directory will contain the results of the build
	--> for coreboot: (1) Create a directory
                          (2) Move the stm.bin file to that directory
                          (3) In coreboot (make menuconfig), 
			      configure the STM and point the 
                              "STM binary file" to the stm.bin file
			  (4) rebuild by doing "make clean && make"

	Other files in the StmPkg/Core directory:
			(1) Stm - an elf based load module, this is passed through
			          objdump to produce stm.bin
                        (2) stm.map - loader map



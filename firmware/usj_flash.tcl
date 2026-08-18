# OpenOCD commands: halt running P4, program images over USB-Serial/JTAG (no strap)
init
halt
program_esp bootloader\bootloader.bin 0x2000 verify
program_esp partition_table\partition-table.bin 0x8000 verify
program_esp vbcpet.bin 0x10000 verify
reset run
shutdown

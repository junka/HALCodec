================================================================================
                            NVPPS LINUX TEST
================================================================================
WHAT IS THE TEST ABOUT ?
This test verifies fetching PTP TSC timestamps by interacting with
nvpps kernel module in either timer mode or GPIO(PPS) mode

===============================================================================
HOW TO COMPILE THE TEST APPLICATION ?

Run make command under PDK_TOP/drive-linux/samples/nvpps_test/ folder
	make

The above step would generate an executable named "nvpps_test"

HOW TO RUN THE TEST ?

Usage: ./nvpps_test [options]

Options:
	-d | --device name   NVPPS device name [/dev/nvpps0]
	-t | --timer         use timer mode
	-s | --signal        use signal
	-c | --counter       tsc in counter mode instead of nsec
	-T | --test name     Where name is "gettimestamp" or "getevent" string to run the corresponding test"

Examples
	$ sudo ./nvpps_test -t			/* Run nvpps test in timer mode */
	$ sudo ./nvpps_test			/* Run nvpps test in GPIO mode */

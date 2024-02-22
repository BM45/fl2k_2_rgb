FL2K_2 : a fork of the osmo_fl2K project.

Turns FL2000-based USB 3.0 to VGA adapters into low cost SDR-DACs. 
For more information on sorce see https://osmocom.org/projects/osmo-fl2k/wiki

We use the Fl2k adapter to be able to transmit independently on all three DAC channels of the Fl2k adapter while the original project only uses the Red-Channel. 

![gnuradio](https://github.com/BM45/fl2k_2_rgb/blob/master/resources/vga.jpg)

The original project fork was primarily used for playing TBC files, see https://github.com/vrunk11/fl2k_2

# Setup

## Linux

### Download Dependencies

`sudo apt-get install libusb-1.0-0-dev`

Install the libusb headers if not already present

`git clone https://github.com/BM45/fl2k_2_rgb`

`cd fl2k_2_rgb`

`mkdir build`

`cd build`

`cmake ../ -DINSTALL_UDEV_RULES=ON`

`make -j 3`

`cd src`
`sudo cp fl2k_* /usr/local/bin`

Before being able to use the device as a non-root user, the udev rules need to be reloaded:

`sudo udevadm control -R`

`sudo udevadm trigger`

# Usage

As its an VGA R-G-B adapter so there are 3 DAC's

To transmit on all 3 DACs, you'll first need to create broadcast files for each channel using a tool like GNU Radio.

![gnuradio](https://github.com/BM45/fl2k_2_rgb/blob/master/resources/gnuradio_to_fl2k_file2.jpg)

All files must have the same samplerate! 

Examples:

To send an RF broadcast file on the Red Channel:

`./fl2k_file2 -u -s 7777777 -R8 -R r_out.bin  -VmaxR 0.7 -not_tbcR`

To send an RF on Red and Blue Channel

`./fl2k_file2 -u -s 7777777 -R8 -R r_out.bin  -VmaxR 0.7 -not_tbcR  -B8 -B b_out.bin -VmaxB 0.7 -not_tbcB`

To send RF on all 3 DACs

`./fl2k_file2 -u -s 7777777 -R8 -R r_out.bin  -VmaxR 0.7 -not_tbcR  -G8 -G g_out.bin  -VmaxG 0.7 -not_tbcG -B8 -B b_out.bin -VmaxB 0.7 -not_tbcB`

# Commandlist and explanation

`-d` device_index (default: 0)

`-readMode` (default = 0) option : 0 = multit-threading (RGB) / 1 = hybrid (R --> GB) / 2 = hybrid (RG --> B) / 3 = sequential (R -> G -> B)

`-s` samplerate (default: 100 MS/s) 

`-u` Set sample type to unsigned

`-R` filename (use '-' to read from stdin)

`-G` filename (use '-' to read from stdin)

`-B` filename (use '-' to read from stdin)

`-R16` (convert bits 16 to 8)

`-G16` (convert bits 16 to 8)

`-B16` (convert bits 16 to 8)

`-not_tbcR` disable tbc processing for input R file

`-not_tbcG` disable tbc processing for input G file

`-not_tbcB` disable tbc processing for input B file

`-VmaxR` maximum output voltage for channel R (0.003 to 0.7) (scale value) (disable Cgain and Sgain)

`-VmaxG` maximum output voltage for channel G (0.003 to 0.7) (scale value) (disable Cgain and Sgain)

`-VmaxB` maximum output voltage for channel B (0.003 to 0.7) (scale value) (disable Cgain and Sgain)

`-MaxValueR` max value for channel R (1 to 255) (reference level) (used for Vmax)

`-MaxValueG` max value for channel G (1 to 255) (reference level) (used for Vmax)

`-MaxValueB` max value for channel B (1 to 255) (reference level) (used for Vmax)

`-resample` resample the input to the correct output frequency (can fix color decoding on PAL signal)

## Possible USB Issues

You might see this in Linux:

    Allocating 6 zero-copy buffers
    libusb: error [op_dev_mem_alloc] alloc dev mem failed errno 12
    Failed to allocate zero-copy buffer for transfer 4

If so then you can then increase your allowed usbfs buffer size with the following command:

`echo 0 > /sys/module/usbcore/parameters/usbfs_memory_mb`

Falling back to buffers in userspace
Requested sample rate 14318181 not possible, using 14318170.000000, error is -11.000000

When the end of the file is reached you will see in CLI:

(RED) : Nothing more to read

Also, to enable USB zerocopy for better I/O stability and reduced CPU usage:

`echo 0 > /sys/module/usbcore/parameters/usbfs_memory_mb`

And reboot. This was added to the kernel [back in 2014](https://lkml.org/lkml/2014/7/2/377). The default buffer size is 16.

#### Based off the [osmo_fl2K project](https://osmocom.org/projects/osmo-fl2k/wiki) software.

## Basic how-to guide for using SerialDisk server and handlers ##
### Written for: SerialDisk Drivers Revision F or above ###

### INTRODUCTION ###

At this point, you should have a working "family-of-8" computer with an 
auxiliary teletype interface card. If you are not apt to edit source 
code and such, it is easiest to ensure the address of the card is set to 
40/41 rx/tx. Else, you will have to change the two lines in the handler 
files to set the address to something else. 

Currently, the handlers only support machines with the traditional 
M8650/M8655 asynchronous serial cards or their equivalants. However,
work is being done to remove the remaining incompatibility with earlier
machines, as well the DECmate series.

### COMPILING THE PROGRAMS ###

Assuming you have a modern machine with the gcc compiler, go ahead and 
clone the os8diskserver repository. 
https://github.com/drovak/os8diskserver

If you have git installed, you probably already know how to do this in 
the command line. Else, you can download the .zip archive directly. 
Extract this in an easy-to-find spot. We'll be doing all of our work 
within that directory.

Once you've extracted the contents, you should see a README and the 
actual distribution files within the SerialDisk directory. 

Change directory to `SerialDisk/server`. To compile, type 
`gcc -o os8disk server.c`. This should compile the server with no 
warnings or errors. If you have encountered an error, please report
it by emailing Kyle Owen (kylevowen@gmail.com). 

To test the server very quickly, just run the server with no arguments: 
`./os8disk`. You should see this printed:

	$ ./os8disk
	PDP-8 Disk Server for OS/8, v1.2
	Usage: ./os8disk -1 system [-2 disk2] [-3 disk3] [-4 disk4] [-r 1|2|3|4] [-w 1|2|3|4] [-b bootloader]

Now it's time to build the handler installer. This is a program that 
will take an OS/8 formatted binary system handler and copy it to the 
correct location on an RK05 disk image. Change your directory to 
`../installer`. To build, type `gcc -o instlhndl handler_installer.c`. 
This should complete without warnings or errors, but on some systems,
including Mac OS X with `clang`, you may get some warnings regarding passing
certain character arrays and pointer conversion. Not to worry, it should work
just fine.

### INSTALLING THE SYSTEM HANDLER ###

To use the handler installer, go grab an RK05 image (in the 
dumprest/simh format) and place it in the ../disks directory. David 
Gesswein's website has many disk images. I personally find 
diagpack2.rk05 (http://www.pdp8online.com/images/images/os8.shtml) 
to be a suitable one. diagpack2.rk05 comes with a working copy of BUILD, 
which is needed later on. Two sample disk images are in the
../disks/original directory. You can copy diagpack2.rk05 into ../disks
for convenience.

Run `./instlhdnl` to see its usage. To put the SerialDisk system handler 
on your RK05 image, run 
`./instlhndl [handler directory]/sys_handler.bin [disk directory]/diagpack2.rk05`, where
diagpack2.rk05 is your disk image. 

Beware: this will modify the disk image, so make a backup if you need an 
original copy later on. Running the installation will print out some 
debugging information. It should look something like this:

	$ ./instlhndl ../handler/sdsksy.bin ../disks/diagpack2.rk05 
	Read 454 bytes of disk
	Read 584 bytes of handler
	Position after leader: 0360
	Number of devices: 3
	Position of bootloader: 0450
	Boot length is 38 (0046)
	Position of handler: 0604

### CONFIGURING THE SERVER ###

The SerialDisk server requires a configuration file just like the 
dumprest tools. This file is called `disk.cfg` and is in the server 
directory. Open it up with your favorite text editor and change the 
configuration to suit your needs. The first line is the baud rate, the 
second line is 0 for 1 stop bit, or 1 for two stop bits, and the last 
line is the name of your serial device. 

### STARTING THE SERVER ###

At this point, we're ready to start the server. The server can handle up 
to four disk images. To start the server, change the directory back to 
`../server` and run `./os8disk -1 ../disks/diagpack2.rk05`. The `-1` 
tells the disk server that the following image should be mounted as the 
primary disk.

If your communications interfaces are all connected, you should see the 
following:

	$ ./os8disk -1 ../disks/diagpack2.rk05
	PDP-8 Disk Server for OS/8, v1.0
	Using system disk ../disks/diagpack2.rk05 with read enabled and write 
	enabled
	Using serial port /dev/ttyUSB0 at 19200 with 2 stop bits

If there are errors opening a file or device, check the file or device 
name and try again. 

To stop the server for any reason, press control-C followed by y[enter]. 
Be mindful: when you press this, the server is interrupted. Data will be 
lost if the PDP-8 is writing to the disk. It is typically best to stop 
the server when the PDP-8 is not transacting data.

### BOOTING TO OS/8 ###

The time has come to get your PDP-8 online with OS/8. We need to load 
the bootloader into the computer. This is technically short enough to 
load directly in by hand, but if you prefer, toggle in the RIM loader 
and load the bootloader that way. You are likely familiar with the 
sendtape utility (from the dumprest collection, 
ftp://ftp.pdp8online.com/software/dumprest), but if you are not, please 
familiarize yourself with David Gesswein's project. 

Assuming you have loaded the RIM loader, 
`sendtape ../bootloader/bootloader.rim` will send the bootloader to the 
PDP-8. After you have loaded with sendtape, you can switch your console 
teletype or terminal back on.

Once the bootloader is loaded, make sure the server is still running and 
start the PDP-8 at 0020. This will tell the server to send the first 
block on the disk to the PDP-8, which will continue the boot process by 
loading the system into the high pages of fields 0 and 1. If all goes 
well, your terminal connected to the console port should be displaying 
the dot prompt. I'm using minicom and it looks like this: 

	Welcome to minicom 2.4

	OPTIONS: I18n
	Compiled on Sep  7 2010, 01:26:06.
	Port /dev/ttyUSB1

	Press CTRL-A Z for help on special keys


	.

The server window should look something like this after the initial 
boot:

	$ ./os8disk -1 ../disks/diagpack2.rk05
	PDP-8 Disk Server for OS/8, v1.0
	Using system disk ../disks/diagpack2.rk05 with read enabled and write 
	enabled
	Using serial port /dev/ttyUSB0 at 19200 with 2 stop bits
	Request to read 8 pages to side 0 on system disk
	Buffer address 00000, starting block 00007
	Successfully completed read

Congrats! But you're not done yet. There's no way to access the second 
partition until we re-BUILD the system. 

### REBUILDING OS/8 ###

We successfully tricked OS/8 into booting by sneakily replacing the 
system handler and bootloader on the disk. However, BUILD, RESORC, and 
PIP (for instance) have no idea that we actually changed the system 
handler. To remedy this, we will need to rebuild OS/8 with BUILD.

First though, we must get the system and non-system handlers onto the 
disk and assemble them. We can use OS/8's EDIT to do this.

	.R EDIT
	*SYS:SDSKNS.PA<

	#A[enter]

EDIT is now ready to accept the text file. You can send 
`../handler/nonsys_handler.pal` with your favorite terminal emulator. 
You may need to increase the character delay in order to get an accurate 
transfer depending on the baud rate. 3 ms character delay and 20 ms line 
delay is often a good starting point. If there is an error in 
transmission, return to command mode with a control-L and then type 
K[enter] to kill the buffer. Start over by typing A[enter] to append to 
the now empty buffer.

Once done, press [enter] once, then control-L to get back to command 
mode, then type E[enter]. This will end the EDIT session and dump the 
buffer to disk.

Run EDIT again, and this time make a new file called `SYS:SDSKSY.PA`. 
Send it `../handler/sys_handler.pal` the same way you sent the other. 

Once you've sent the two files, it's time to assemble them.

	.R PAL8
	*SYS:SDSKNS.BN<SYS:SDSKNS
	ERRORS DETECTED: 0
	LINKS GENERATED: 0

	.R PAL8
	*SYS:SDSKSY.BN<SYS:SDSKSY
	ERRORS DETECTED: 0
	LINKS GENERATED: 0

Congrats again. You've just assembled two handlers. You're now ready to 
insert them into BUILD.

	.RUN SYS BUILD

	$

Let's look at what devices are already LOaded in BUILD:

	$PR

	BAT : *BAT
	KL8E: *TTY
	LPSV: *LPT
	RK8E: *SYS   RKA0  RKB0
	RK05: *RKA0 *RKB0 *RKA1 *RKB1  RKA2  RKB2  RKA3  RKB3
	RX02: *RXA0 *RXA1
	TD8A: *DTA0 *DTA1
	RL0 : *RL0A *RL0B

	DSK=RK8E:SYS
	$

You can see there are lots of handlers already in place with this 
particular disk image. I opted to remove the batch and line printer
handlers. You'll also need to remove the RK8E system handler; feel
free to leave the RK05 handler, though.

	$UN BAT

	$UN LPSV

	$UN RK8E

	$

Now we can LOad the newly-assembled handlers into BUILD, and INsert the
handlers we need. In order to keep the number of handlers under 15 for
BUILD to work properly, we'll also need to DElete another couple of handlers;
I chose to get rid of the DECtape handlers.

	$LO SYS:SDSKSY

	$PR

	KL8E: *TTY
	RK05: *RKA0 *RKB0 *RKA1 *RKB1  RKA2  RKB2  RKA3  RKB3
	RX02: *RXA0 *RXA1
	TD8A: *DTA0 *DTA1
	RL0 : *RL0A *RL0B
	SDSY:  SYS   SDA0  SDB0

	DSK=RK8E:SYS
	$IN SDSY:SYS,SDA0,SDB0

	$LO SYS:SDSKNS

	$PR

	KL8E: *TTY
	RK05: *RKA0 *RKB0 *RKA1 *RKB1  RKA2  RKB2  RKA3  RKB3
	RX02: *RXA0 *RXA1
	TD8A: *DTA0 *DTA1
	RL0 : *RL0A *RL0B
	SDSY: *SYS  *SDA0 *SDB0
	SDNS:  SDA0  SDB0  SDA1  SDB1  SDA2  SDB2  SDA3  SDB3

	DSK=RK8E:SYS
	$IN SDSK:SDA1,SDB1

	$DSK=SYS

	$DE DTA0-1

	$PR

	KL8E: *TTY
	RK05: *RKA0 *RKB0 *RKA1 *RKB1  RKA2  RKB2  RKA3  RKB3
	RX02: *RXA0 *RXA1
	TD8A:  DTA0  DTA1
	RL0 : *RL0A *RL0B
	SDSK: *SYS  *SDA0 *SDB0
	SDSK:  SDA0  SDB0 *SDA1 *SDB1

	DSK=SDSK:SYS
	$

As it is currently, SYS=SDA0; that is, they point to 
the same partition on the server. By LOading the system
handler first, we can ensure that the system uses the system
handler for the first two partitions. That way, OS/8 only has
to go to the disk to fetch the non-system handler for SDA1/SDB1. 

To enable up to two additional "drives", you'll need to "INsert 
their handlers with lines like these
	$IN SDNS:SDA2,SDB2
	$IN SDNS:SDA3,SDB3
and UNload or at least DElete some other devices (to stay under 15).
Possibly thus:
	$UN RL0
	$DE RKA1,RKB1

Now it's time to BOot the system, configuring it the way we have 
described. Once built, save BUILD so that next time we call it for 
modifications, it knows what the current configuration is. 
Unfortunately, SYS: is virtually full right now (with one block free), 
so you'll have to save it to SDB0:. No worries; you can copy it over to 
SYS: later once you free up some blocks.

	$BO
	SYS BUILT
	.SAVE SDB0 BUILD

	.

If your RK05 image had room on SYS: then 'SAVE SYS BUILD' would do.

That's it! You're all done. To test out the new resident handler, try
printing the directory of SDB0. If this works, you know you've
successfully gotten the resident handler entry points enabled.

	.DIR SDB0:/P



	ABSLDR.SV   6            RL2SY .BH   2            DHTAAC.DG  17
	CCL   .SV  31            RL20  .BH   2            DHTABC.DG  13
	DIRECT.SV   7            RL21  .BH   2            DHTMAB.DG  17
[snip]
	RL0   .BH   2            DHKLBB.DG  13            RKCOPY.DG   5
	RL1   .BH   2            DHKLCD.DG   8            BUILD .SV  37
	RLC   .BH   2            DHLAAB.DG  13

	 158 FILES IN 2799 BLOCKS -  393 FREE BLOCKS

	.

You may have a different file/block count. I played around with the disk 
image a little before making the tutorial.

If this worked, congratulations. You now have a booting OS/8 system!

Try re-running the server with `-2 ../disks/diag-games-kermit.rk05` 
on the end to use the SDA1 and SDB1 partitions. This will allow systems
(with enough memory) to play with MUSIC.SV, Adventure, etc.
Bootload the machine again to make sure BUILD worked correctly 
installing the bootloader.

If you INserted the extra devices, you can try out the new server
options by adding -[34] options to your server invokation, and then
verify that you can list the directories of SDA2 and SDB2, or even
SDA3 and SDB3.

### CONCLUSION ###

Since you now have a fully-functional system, try out some programs. 
BASIC works fine once you copy it to the SYS: partition. Again, you'll 
need to delete some files from SYS: if you want to copy some programs 
over.

Note: you do not have to halt the PDP-8 to swap out disks. Just make sure
no data is being transacted at the time you halt the server, and make sure
the SYS: disk stays the same unless you really know what you're doing.

Remember, you still need to patch PIP and RESORC if you want to use them 
with the new devices. Consult the OS/8 Handbook for patching procedures.

If you had any trouble along the way, or you think you can make this 
tutorial better, please email Kyle Owen (kylevowen@gmail.com) so the 
appropriate changes can be made.

Have fun with OS/8!

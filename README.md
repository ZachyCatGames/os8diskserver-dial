# OS/8 Disk Server Utilities #
### Kyle Owen - 9 Feb. 2014 ###
### kylevowen@gmail.com ###

This suite of programs allows anyone with a PDP-8/E or later to communicate with
a modern computer running Linux, Mac OS X, Windows with Cygwin, or any other
POSIX-compliant system where a standard C compiler such as gcc exists.

### TERMS OF USE ###

The software is provided "as is" without a warranty of any kind, either express
or implied.

I encourage the use of this software in any non-commercial or commercial
applications, but in fairness to other users and the developer, any changes for
improving the functionality of this program should be documented for others to
take note of.

### REQUIREMENTS ###

Hardware requirements currently are a second M8650/M8655 asynchronous serial
interface card or equivalent and the appropriate cables. The default address
in the OS/8 handler is 40/41 (the default auxiliary TTY ports). This can be
changed if necessary. On the modern computer, at least one serial interface is
required to talk to the PDP-8's second serial interface. Baud rates have been
used successfully to 230.4k baud.

Philipp Hachtmann's OmniUSB board has also been used successfully with
SerialDisk. Future revisions will better support this board as well.

Software requirements involve loading the system and/or non-system handlers into
OS/8 using BUILD. The modern computer needs to support the disk server, which is
easily compiled with gcc. The PDP-8 assembly files have been built with David
Gesswein's palbart assembler, as well as PAL8 under OS/8.

The disk server utilizes the standard dumprest/simh RK05 disk image format.
These images are exactly 3,325,952 bytes long, which is 1,662,976 12-bit words
packed in 16 bits per word. The first half of the image corresponds to side 0, and the second half, side 1. Typically, in OS/8 systems, SYS: is side 0.

There are two shell scripts included in this repository.  They have been written for and tested on Rabpberry Pi Debian Buster and Bullseye.
If you want you can download just these two scripts and they will take care of everything else for you.


getos8diskserver	This script verifies all of the requirements for installing and building OS/8 Serial Disk and installs anything that isn't found.
			It then checks for an existing installation of OS/8 Serial disk, renames it if one is found, and then clones this repository locally.
		      If you are unfamiliar with git and cloning a repository or don't feel like going through the entire process use this script.
             	Note:  This does require the user to have sudo rights to install any prerequisite packages.

makeos8diskserver	This script handles all of the manual steps of building OS/8 Disk Server and also creates a default startup script for OS/8 Serial Disk.

### SPECIAL THANKS ###

David Gesswein's dumprest utilities for the PDP-8 have proved to be a tremendous
help in the creation of the software herein. Thank you, David!

## Serial Disk Handler for LAP6-DIAL-MS
A LAP6-DIAL-MS device handler for use with os8diskserver-dial.

### General Usage
#### Using the Server
The server (located `../server`) must be run using `-d` (for _D_IAL) argument when using this handler under DIAL.
If `-d` is not used, there may be dragons (the `d` is not for `d`ragons, and in fact, actually scares away the dragons).
Other options function as they do in standard OS/8 disk server.

```
./server -d -1 disk1_path [-2 disk2_path] [-3 disk3_path] [-4 disk4_path] [-r disk_num] [-w disk_num]

    -d: Enable DIAL mode (required for DIAL-MS use).
    -1 disk1_path: Specify path to disk 1.
    -2 disk2_path: Specify path to disk 2 (optional).
    ...
    -r disk_num: Read protect disk_num
    -w disk_num: Write protect disk_num
```

#### Booting DIAL-MS
If the serial disk handler was setup to be used as the system handler, booting DIAL-MS _at all_ will require that the server is running.
Otherwise, DIAL-MS will still be bootable from LINCtape alone, but of course serial disk units will require to server to be running.
Regardless, the standard LINCtape bootstrap is used for DIAL-MS:
1) Mount your DIAL-MS system tape on LINCtape transport 0
2) Set `REMOTE` and `WRITE ENABLE` on LT transport 0
3) Set `LEFT SWITCHES` to `0701` and `RIGHT SWITCHES` to `7300`
4) Set the `MODE` switch to `LINC`
5) Depress `I/O PRESET`
6) Depress `DO`, LT transport 0 should begin moving
7) Once LT transport 0 has stopped moving, depress `START 20`

A LINCtape containing a DIAL-MS system must always be mounted on LINCtape transport unit 0, even if serial disk is being used as the system device (See: [Limitations](#LINCtape-Unit-0-Requirement)).

#### Available Units
If the handler was installed without the Super Extended Unit Patch(tm), the following unit will be usable under DIAL-MS:
* 00-07: Standard LINCtape units
* 10-15: Serial Disk 1 Half 1

If the Super Extended Unit Patch(tm) was applied, the following additional units will usable as well:
* 20-25: Serial Disk 1 Half 2
* 30-35: Serial Disk 2 Half 1
* 40-45: Serial Disk 2 Half 2
* 7050-7055: Serial Disk 3 Half 1(*)
* 7060-7065: Serial Disk 3 Half 2
* 7070-7075: Serial Disk 4 Half 1
(*) When using the editor; if accessing these serial disk units through the I/O routines use units 5x, 6x, etc.

These units will be accessible to any software that uses the DIAL-MS `READ` and `WRITE` I/O routines.
Software that directly accesses I/O devices will need to be modified to support these routines (or directly implement serial disk routines).
All built in DIAL-MS commands and the editor can use the serial disk units out of the box without any problems; some utilities, such as `PIP`, cannot and will need patches themselves.

### Disk Formatting
LAP6-DIAL-MS only supports 512 block units, larger devices need to be split into smaller chunks that are each treated as their own unit (assigned their own unit number).
The serial disk handler will partition each diskthe same way DEC's LAP6-DIAL-MS RK08 device handler does (each RK05 (half) will be treated as a set of six 512 block devices):
* Unit x0: Blocks 0000-1000 (octal)
* Unit x1: Blocks 1000-2000
* Unit x2: Blocks 2000-3000
* Unit x3: Blocks 3000-4000
* Unit x4: Blocks 4000-5000
* Unit x5: Blocks 5000-6000

### Installation
The serial disk handler is bundled with a graphical installer that uses the PDP-12's VR12 display to show visual installation option prompts.
First, the installer must be loaded, which can be done in a number of ways.

#### Loading Through The BIN Loader
The installer can be loaded using the standard PDP-8 BIN loader.
1) Load and start the standard BIN loader
2) Send the `sdinsth.bin` (SerialDisk Installer with Handler) binary located in this directory
3) Once fully sent, halt the machine (if it didn't halt on its own)
4) Set the `MODE` switch to `LINC`
5) Depress `I/O PRESET`
6) Depress `START 20`

#### Loading Through LAP6-DIAL-MS
The installer can be loaded though LAP6-DIAL-MS.
This assumes that the installer has been copied to some media available to DIAL-MS.
It may be possible to send the installer to DIAL-MS over TTY, but I have not personally had much luck with this.
1) With the editor cursor positioned on a new/empty line, enter a linefeed (CTRL-J works for me).
2) Type `LO SDINSTH,xx`, where `SDINSTH` is the installer file name and `xx` is the unit number of the device containing the installer.
3) Press enter. DIAL-MS should begin loading the installer, once done it'll automatically run the installer.

#### Loading Through OS/8
TODO -- I should probably provide an OS/8 core image.


Upon starting the installer, you'll be given the following prompt:
```
SERIAL DISK INSTALLER
 
THIS WILL INSTALL THE SERIAL DISK HANDLER
FOR LAP6-DIAL-MS. IF ANOTHER EXTRA DEVICE
HANDLER IS INSTALLED, IT WILL BE REPLACED
WITH THE SERIAL DISK HANDLER.THIS WILL
CONFIGURE THE UNIT NUMBERS AS FOLLOWS:
* 00-05: LINCTAPE
* 10-15: SERIAL DISK 1 HALF 1
MAKE SURE YOUR LAP6-DIAL-MS TAPE IS MOUNTED
ON UNIT 0 AND THAT WRITE ENABLE IS SET.
CONTINUE? (Y/N)
```

Prior to continuing, make sure your target DIAL-MS LINCtape is correctly set up:
* Mounted on the only transport designated unit 0
* `REMOTE` is set
* `WRITE ENABLE` is set

If you wish to install the handler (why are you here and running the installer if you don't?), enter "Y" (must be capital) then enter a linefeed (CTRL-J).
Entering "N" then linefeed will halt the machine, at which point `CONTINUE`ing will return to the above prompt.

The next prompt asks if you want setup the Super Extended Units Patch (SEUP????).
The SEUP(????) patch will enable use of up to 3.5 RK05 disks -- only half a disk will be available otherwise -- but modifies the I/O routines and may summon dragons (it's a small patch and "works on my machine," though).
Enter "Y" ("Yes") or "N" ("No") depending on whether you want the patch or not, then enter linefeed.

```
SERIAL DISK INSATLLER
 
INSTALL SUPER EXTENDED UNIT PATCH
THIS WILL PATCH THE DIAL-MS UNIT SELECTION
LOGIC TO ALLOW FULL USE OF ALL 4 POSSIBLE
SERIAL DISKS, BUT MAY CHANGE SOME BEHAVIOR.
THE FOLLOWING ADDITIONAL UNITS WILL BE ADDED:
* 20-25: SERIAL DISK 1 HALF 2
* 30-35: SERIAL DISK 2 HALF 1
* 40-45: SERIAL DISK 3 HALF 2
INSTALL THE PATCH? (Y/N)
```

The next prompt asks if you want to use the first serial disk as the system device.
Using a serial disk as the system device will make DIAL-MS use a serial disk for the source/binary work areas and for loading system programs (the editor, assembler, etc).
* System Program: Unit 10
* Source Work Area: Unit 10
* Binary Work Area: Unit 11
This may yield _speed_ depending on your setup, but DIAL-MS will depend on serial disk being available to do _anything_.
Enter your choice to continue.

```
SERIAL DISK INSTALLER
 
SETUP SERIAL DISK AS SYSTEM DEVICE.
THIS WILL PUT THE SOURCE WORK AND SYSTEM
AREAS ON THE FIRST PART OF THE SERIAL
DISK AND THE BINARY WORK AREA ON THE SECOND
PART. BOOTING AND USING LAP6-DIAL-MS WILL
REQUIRE A SERIAL DISK CONNECTION.
DO YOU WANT TO SETUP THE SERIAL DISK AS 
THE SYSTEM/WORK DEVICE?
```

If you chose to use a serial disk as the system device in the previous prompt, you'll be be asked if you want to copy the DIAL-MS from LINCtape unit 0 to the first serial disk unit.
If you're using a blank RK05 image (or an image that doesn't have a DIAL-MS system on it), you must respond "Yes" to this prompt, otherwise DIAL-MS will be unable to boot.
The serial disk server must be running prior to responding yes here.
The following can be run on \*nix to create a blank rk05 image and start the server (`$OS8_DISK_SERVER_ROOT` = root of your copy of this repo):
```
cd $OS8_DISK_SERVER_ROOT/SerialDisk/server
dd if=/dev/zero of=whatever.rk05 bs=1 count=3325952
./server -d -1 disk1.rk05
```

You may respond "No" if your RK05 image already has a DIAL-MS system present (or you plan to install it by other means).
```
SERIALDISK INSTALLER
 
WOULD YOU LIKE TO COPY THE SYSTEM AREA
HFROM LINCTAPE UNIT 0 TO THE FIRST
SERIAL DISK UNIT? (Y/N)
```

The installer will begin... installing.
Once done, one last prompt will come up asking if you wish to return to DIAL-MS.
If you chose to use a serial disk as the system device, you will want to have the serial disk server running prior to responding "Yes."
Responding "Yes" will return you to DIAL-MS; responding "No" will return spool LINCtape unit 0 back to block 0 and return to the beginning of the installer.
It is also safe to just `STOP` the machine at this point.
```
SERIAL DISK INSTALLER
 
THE SERIAL DISK HANDLER WAS INSTALLED.
WOULD YOU LIKE TO RETURN TO DIAL-MS?
(Y/N)
```

### Uninstallation
A DIAL-MS system tape with the serial disk handler installed can be easily reverted to a default state with only the default handlers by rerunning system build:
* Mount your DIAL-MS system tape on LINCtape transport 0
* Set `REMOTE` and `WRITE ENABLE` on LT transport 0
* Set `LEFT SWITCHES` to `0701` and `RIGHT SWITCHES` to `7310`
* Set the `MODE` switch to `LINC`
* Depress `I/O PRESET`
* Depress `DO`, LT transport 0 should begin moving
* Once LT transport 0 has stopped moving, depress `START 20`

After pressing `START 20`, booting will take slightly longer than usual, but once done the system restored to a standard configuration and kick you into the editor.

### Limitations

#### LINCtape Unit 0 Requirement
A LINCtape containing at least the DIAL-MS I/O routine blocks must always be present on unit 0.

Unlike OS/8, DIAL-MS does not have a core resident service routine or system device handler that software can call without ever needing to directly perform device I/O.
Instead, software is expected to load the I/O routines from LINCtape unit 0 blocks 322 and 323 into memory as it sees fit, which software can then use to perform device-agnostic storage accesses.
This expectation means that any software build to use these I/O routines is going to have some reliance on a LINCtape being present and accessible.
There is no way to work around this without making modifications to both DIAL-MS itself and any other software of interest.
So it is impossible/impractical to fully rely on a serial disk to boot and use DIAL-MS, a single LINCtape unit is still required.

NOTE: The PDP-12 has a feature to trap on tape instructions that could be useful for transforming currently existing tape operations into serialdisk accesses, but this traps to location 0140 in field 0, which is likely to be used by software.
Making use of this trap feature would also require modification of any software that uses the trap locations.
The tape trap feature would also get disabled by any software that uses ESF to set special functions to, e.g., perform an I/O Preset.

#### Unsupported Software and Integration
Software that directly performs device I/O, perhaps unsurprisingly, will not benefit from this device handler and won't automagically support serial disk devices.
Such software would need to be modified to include serial disk device support.
I don't know how common software (not) using the DIAL-MS I/O routines is, but the built-in `PIP` (file/data transfer utility) does not support them.

The DIAL-MS handler code here is mostly generic and could be used as a starting point for adding support to other software.
`SREAD` implements a read-from-serial-disk routine and `SWRITE` implements a write-to-serial-disk-routine; both routines share most of their code.
The entire routine(s) must be assembled within the same page, but where they're assembled within a page doesn't matter, nor does what page they're placed in.
The arguments passed to the routines are:
Device Handler Arguments:
    1) Unit number
    2) Core Location div by 400
    3) Block Number
    4) Block Count

Which serial disk is accessed depends on bits 6 to 8 of the unit number:
* xx0x: Disk 1 Half 1
* xx1x: Disk 1 Half 1
* xx2x: Disk 1 Half 2
* xx3x: Disk 2 Half 1
* xx4x: Disk 2 Half 2
* xx5x: Disk 3 Half 1
* xx6x: Disk 3 Half 2
* xx7x: Disk 4 Half 1

All other bits of the unit number are ignored by the serial disk device handler.

The block number and count are relative to the beginning of each full disk, and can span over an entire disk; the disk is not split into 512 block units.
If access to individual 512 block units is needed, the user must adjust the starting block number themselves, e.g.,:
```
    /ASSUMES AC=UNIT NUMBER
    AND K7          /MASK 3 LSB
    CLL RTR; RTR    /SHIFT INTO 3 MSB
    TAD BLKNUM      /ADD TARGET BLOCK NUM WITHIN UNIT
    /PROPER BLOCK NUM FOR UNIT N IS NOW IN AC
```

#### Disk Count Limitations
By default, this implementation is limited to only being able to single RK05 half, with a patch to enable the use of 7 RK05 halves (or 3.5 RK05s).
Accessing the three upper halves also requires using awkward unit numbers in some cases.

There are two main factors for these limits.
The first is due to limitations DIAL-MS's unit selection logic.
By default, the unit selection logic uses a unit table that contains entries for each valid unit.
These entries contain the unit's number, a handler offset, and an optional offset value.
This table has 100 (octal) words allocated for it and each entry uses 3 words, supporting up to 21 entries (dec).
There are 8 LINCtape unit and 3 special system units installed by default, leaving 10 available entries.
Each RK05 half is split into 6 sub-units, meaning there's only space for one half RK05 without removing other entries.
```
struct unit_table_entry {
    Word unit;
    Handler* handler;
    Word offset;
}
struct unit_table_entry unit_table[21] = { ... }

for(Word i = 0; i < 21; i++) {
    // negative == end of table
    if(unit_table[i].unit < 0)
        return

    if(unit_table[i].unit == requested_unit) {
        unit_table->handler(read or write args);
    }
}
```

Most of the entries within the unit table end up being identical aside from their unit number (e.g., all LT entries are { nnnn, 7630, 0 }), wasting a bunch of space.

This can be worked around with an easily reversible patch that modifies the selection logic by using masks.
My new unit table includes both a mask and an expected result.
The selection logic will `AND` the requested unit number with mask value, that unit table entry will be selected with the result matches that entry's expected result value (the entry expected value is actually the expected value's negative).
This allows representing several units though a single entry.
E.g., a single { mask=07770, exp_result=0000, 7630, 0 } entry would check that bits 0 to 8 are all negative and catch all LINCtape unit numbers.
```
struct unit_table_entry {
    Word mask;
    Word expected_result;
    Handler* handler;
    Word offset;
}
struct unit_table_entry unit_table[21] = { ... }

for(Word i = 0; i < 21; i++) {
    // Zero mask == end of table
    if(unit_table[i].mask == 0)
        return

    if(unit_table[i].mask & requested_unit == unit_table[i].expected_result) {
        unit_table->handler(read or write args);
    }
}
```

With this modification, all of the entries required to support all four full RK05s can fit within the 100 (octal) word unit table with space to spare.
Additionally, this patch can be easy reverted by rerunning system build (see: [Uninstallation](#Uninstallation)).

The second source of these limitations is the DIAL-MS editor itself.
The editor requires that unit number are less than 50 and performs this check before calling the I/O routines:
```
U2,	JMP P56
	SET 0
	1
	STA
	E6+6+2000
	ADA I		/TEST FOR UNIT TOO LARGE.
	-47		/LARGEST UNIT
	APO		/IF POSITIVE THEN ITS WAY TOO LARGE (ABOVE 47)
	JMP 0		/GOOD UNIT.
	JMP XITERR	/ILLEGAL UNIT
```

Due to what is likely an oversight, this check allows "negative" (i.e., have bit 0 set) unit numbers, so something like 7000 would pass this check.
But after this check, the editor saves the unit number as a half word; the upper six bits are cut off.
```
	JMP U2		/GET UNIT NUM
	STH I 12
```
So we still only end up with a 6 bits worth of unit numbers, but this does allow access to unit numbers 50 to 77 (though, e.g., 7050 to 7777).
This is why accessing higher units with the super extended units patch requires funky unit numbers.

These both would likely be easy to patch, but the patch would be less simple to reverse would allow some typically disallowed behavior (e.g., accessing system units through the editor).



/* 
//	This program talks to the PDP-8 via an asynchronous serial interface to
//	simulate disk drives located as a file on the PC.
//	It needs a config file disk.cfg or $HOME/.disk.cfg with the format defined in
//	config.c
//	Disk file format is compatible with the ubiquitous SimH etc. format.
//
//	Press ctrl-C to stop the server.
//
//	2/11/22 V1.6 Vince Slyngstad
//	---------------------------
//
//	Fixed a bug reported by Mike Katz where interrupting with ^C
//	and requesting an exit was attempting to close null FILE pointers.
//	Version number bumped to 1.6.
//
//	3/1/21 V1.5 Vince Slyngstad
//	---------------------------
//
//	Fixed a bug reported by Don North where baud rates in excess of
//	38400 caused a segmentation fault.
//
//	2/10/21 V1.4 Vince Slyngstad
//	---------------------------
//
//	Added support for the modified HELP loader compatible bootstrap.
//	Version number bumped to 1.4.
//
//	2/1/21 V1.3 Vince Slyngstad
//	---------------------------
//
//	Re-ordered the edit history to put recent changes on top.
//	Fix a problem where success was returned instead of failure
//	  when attempting to access a disk image not specified on the
//	  command line.
//	Version number bumped to 1.3.
//
//	Bob Adamson modifications
//	-------------------------
//
//	Minor changes to variable naming to suit myself ;-) 6/Nov/2015
//	Minor changes to text  output 6/Nov/2015
//	Increased to serving (up to) 4 disks (for nonsystem handler) 6/Nov/2015
//	Added command to shut down the server ('Q' from client)
//	 - very system dependent, but wanted for dedicated (eg raspberry Pi) server
//
//	Kyle Owens original version
//	---------------------------
//
//	TODO:
//	DONE - transfer 0040 pages if page count is 0 (and not field 0!)
//	DONE - emulate RK05 disk pack (requires two entry points)
//	DONE - make program to replace bootloader and handler on disk packs
//	- ctrl-c operation (for non-system version)
//	DONE - non-system version
//	DONE - increase transfer rate
//	- clean up code
//	- make better converter utilities
//	- write better test utility
//	- check length of file on start
//	- grow filesystem as writes occur
//	- timeout
//	- hardware handshaking
//	- fix RIM loading
//
//	TODO:
//	Remove bootloader code?
//	Consider ^C interrupt issues - at present no interruptions are possible:
//	- interrupting during a read is no problem in theory but the server will
//	  blow up if the pdp-8 transmits while the server is still flushing the read.
//	  (Note the pdp-8 will do this if it exits to OS/8 on a ^C)
//	- interrupting a write will hang the server as it will keep waiting for the
//	  remainder of the transmission.
*/

#include <termios.h>
#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include <time.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <err.h>

#define TERM_COLOR

#ifdef TERM_COLOR
#define RESET_COLOR "\e[m"
#define MAKE_RED "\e[31m"
#define MAKE_GREEN "\e[32m"
#define MAKE_YELLOW "\e[1;33m"
#else
#define RESET_COLOR ""
#define MAKE_RED ""
#define MAKE_GREEN ""
#define MAKE_YELLOW
#endif

#define ACK_READ 04000
#define ACK_WRITE 04001
#define ACK_DONE 0
#define NACK 02000

#define WRITE 1
#define READ 0

#define PAGE_SIZE 0200
#define BLOCK_SIZE (PAGE_SIZE * 2)
#define BYTES_PER_WORD 2

#define DISK_NUM_MIN 0
#define DISK_COUNT 4
#define NUMBER_OF_BLOCKS 06260 //number of blocks in a single RK05 side
#define FILE_LENGTH (NUMBER_OF_BLOCKS * BLOCK_SIZE * BYTES_PER_WORD) //length of RK05 image

#define DIAL_SUB_DISK_BLK_COUNT 0400

#define DEBUG
#define REALLY_DEBUG

int terminate = 0;

#include "config.c"
#include "comm.c"

// Note: We expect there to be (at least) a first disk, disk1
// although this would not be strictly necessary for non-system devices
static const char usage[] = "Usage: %s -1 disk1 [-2 disk2] [-3 disk3] [-4 disk4] [-r 1|2|3|4] [-w 1|2|3|4] [-b bootloader]\n";

static const char *disk_num_strings[4] = {
	"first",	//disk1
	"second",	//disk2
	"third",	//disk3
	"fourth"	//disk4
};

void command_loop();
int initialize_xfr();
void process_send_boot_sector();
void process_read();
void process_write();
void HELPBoot();
void send_word(int word);
int decode_word(char* buf, int pos);
void cleanup_and_exit(int poweroff);
void int_handler(int);
void djg_to_pdp(char* buf_in, char* buf_out, int word_count);
void pdp_to_djg(char* buf_in, char* buf_out, int word_count);
int write_to_file(FILE* file, int offset, char* buf, int length);
int read_from_file(FILE* file, int offset, char* buf, int length);
void receive_buf(char* buf, int length);
int transmit_buf(char* buf, int length);

struct disk_state {
	FILE* fp;
	short in_use;
	short read_protect;
	short write_protect;
};

int fd;
unsigned char buf[256];
unsigned char converted_buf[256];
unsigned char disk_buf[8200];
unsigned char converted_disk_buf[8200];
int direction;
int start_block;
int total_num_words;
int acknowledgment;
int num_bytes;
int half_block = 0;
int block_offset = 0;

int dial_mode = 0;

struct disk_state disks[DISK_COUNT] = {0};
struct disk_state* selected_disk_state = NULL;

/*
 * Sent from PDP:  abcd -> XXcccddd XXaaabbb
 * Stored in file: abcd -> bbcccddd 0000aaab
 * Sent to PDP:    abcd -> 00aaabbb 00cccddd
 */

/*
 * -b [file]: send bootloader on start
 * -1 [file]: use file as first (system) disk
 * -2 [file]: use file as second disk
 * -3 [file]: use file as third disk
 * -4 [file]: use file as fourth disk
 * -r [1|2|3|4]: read only (NB - for OS/8 system disk (disk 1) must be read/writeable)
 * -w [1|2|3|4]: write only
 */

int main(int argc, char* argv[])
{
	struct disk_state* curr_disk;

	signal(SIGINT, int_handler);

	int c;
	int disk_num;
	char* filename_disks[4];
	char* filename_btldr = NULL;
	while ((c = getopt(argc, argv, "-1:2:3:4:b:r:w:d")) != -1)
	{
		switch (c)
		{
			case '1': //first disk
			case '2': //second disk
			case '3': //third disk
			case '4': //fourth disk
				disk_num = c - '1';
				disks[disk_num].in_use = 1;
				filename_disks[disk_num] = optarg;
				break;
			case 'r': //read-protect
				for(int i = 0; optarg[i] != 0; i++)
				{
					disk_num = optarg[i] - '1';
					if(disk_num >= DISK_NUM_MIN && disk_num < DISK_COUNT)
						disks[disk_num].read_protect = 1;
					else
					{
						printf(usage, argv[0]);
						exit(1);
					}
				}
				break;
			case 'w': //write-protect
				for(int i = 0; optarg[i] != 0; i++)
				{
					disk_num = optarg[i] - '1';
					if(disk_num >= DISK_NUM_MIN && disk_num < DISK_COUNT)
						disks[disk_num].write_protect = 1;
					else
					{
						printf(usage, argv[0]);
						exit(1);
					}
				}
				break;
			case 'b': //bootloader
				filename_btldr = optarg;
				break;
			case 'd': //LAP6-DIAL-MS mode
				dial_mode = 1;
				break;
			case '?':
				printf(usage, argv[0]);
				exit(1);
			case 1:
				warnx("unknown option -- %s", optarg);
				printf(usage, argv[0]);
				exit(1);
		}
	}

	printf("PDP-8 Disk Server for OS/8, v1.6\n");

	printf("Running %s mode\n", dial_mode ? "DIAL" : "OS/8");

	// We must have a system disk.
	if(!disks[0].in_use)
	{
		printf("no system disk (disk1) specified\n");
		printf(usage, argv[0]);
		exit(1);
	}

	// Open each disk.
	for(int i = DISK_NUM_MIN; i < DISK_COUNT; i++)
	{
		// noop if not in use.
		if(!disks[i].in_use)
			continue;

		curr_disk = &disks[i];
		curr_disk->fp = fopen(filename_disks[i], "r+");
		if (curr_disk->fp == NULL)
		{
			fprintf(stderr, "On file %s ", filename_disks[i]);
			perror("open failed");
			exit(1);
		}
		printf("Using %6s disk %s with read %s and write %s\n", disk_num_strings[i], filename_disks[i],
		       (curr_disk->read_protect ? MAKE_RED "disabled" RESET_COLOR : MAKE_GREEN "enabled" RESET_COLOR),
		       (curr_disk->write_protect ? MAKE_RED "disabled" RESET_COLOR : MAKE_GREEN "enabled" RESET_COLOR));
	}

	FILE* btldr = NULL;
	if (filename_btldr)
	{
		btldr = fopen(filename_btldr, "r");
		if (btldr == NULL)
		{
			fprintf(stderr, "On file %s ", filename_btldr);
			perror("open failed");
			exit(1);
		}
	}
	
	long baud;
	int two_stop;
	char serial_dev[256];
	setup_config(&baud,&two_stop,serial_dev);
	
	printf("Using serial port %s at %s with %s\n", 
		serial_dev, baud_lookup[baud].baud_str, (two_stop ? "2 stop bits" : "1 stop bit"));

	baud = baud_lookup[baud].baud_val;
	fd = init_comm(serial_dev,baud,two_stop);

	if (btldr)
	{
		printf("Sending bootloader...\n");
		while (!feof(btldr))
		{
			if ((c = fread(buf, 1, sizeof(buf), btldr)) < 0)
			{
				perror("File read failure");
				exit(1);
			}
			else
				transmit_buf(buf, c);
		}
		printf(MAKE_GREEN "Bootloader sent\n" RESET_COLOR);
	}

	// Start command loop
	command_loop();
}

/*
// Command processor
// Accepts the following wakeup commands:
// NUL	- Send the HELPBoot loader
// @	- read the first sector/first side/first disk (boot sector)
// A	- process command to first side of first disk
// B	- process command to second side of first disk
// C-H	- process command to first/second side of second/third/fourth disk
// Q	- stop operations and shut down the server
//	- anything else is a (non-fatal) error
*/
void command_loop()
{
	for (;;)
	{			
		receive_buf(buf, 1); //wait for command
		switch (buf[0])
		{
			case '\000': ;
				HELPBoot();
				break;
			case '@':
				process_send_boot_sector();
				break;
			case 'A':
			case 'B':
			case 'C':
			case 'D':
			case 'E':
			case 'F':
			case 'G':
			case 'H':
				//got signal pointing to drive and side
				//send any char as ack
				//get function
				//get starting block number
				//send CDF instruction
				//send ack [4000=READ PAGE, 4001=WRITE PAGE, 0000=DONE, 2000=ERROR]
				//if read, read out page and repeat ack until transfer is done
				//if write, write in page and repeat ack until transfer is done
				//if done, just send ack
				//if error, just send ack
#ifdef DEBUG
				printf("Client sent signal %c\n", buf[0]);
#endif
				//for (i = 0; i < 10; i++) //ensure buffer is flushed
				//	ser_read(fd, (char *) buf, sizeof(buf));
				//flush(fd);
				/*if ((ser_write(fd, (char *) buf, 1)) < 0) //send acknowledgment
				{
					perror("Serial write failure");
					exit(1);
				}*/
				if (initialize_xfr())
				{
					fprintf(stderr, MAKE_RED "Failed to initialize, sending NACK %04o\n" RESET_COLOR, acknowledgment);
					send_word(acknowledgment);
				}
				else
				{
					send_word(acknowledgment);		
						
					if (direction == WRITE) //********** WRITE ************//
						process_write();
					else //********** READ ************//
						process_read();
				}
				break;
			case 'Q': //quit server
				printf(MAKE_YELLOW "Received quit signal, server quitting\n" RESET_COLOR);
				cleanup_and_exit(1); // Exit with shutdown.
			default:
				fprintf(stderr, MAKE_RED "Received unknown command - ignored - character %04o\n" 
					RESET_COLOR, buf[0]);
				break;
		}
	}
}

void cleanup_and_exit(int poweroff) {
	// Close files and exit.
	for(int i = DISK_NUM_MIN; i < DISK_COUNT; i++)
	{
		if(disks[i].in_use)
			fclose(disks[i].fp);
	}
	if(poweroff) // optional shutdown
		system("sudo shutdown -h now");
	exit(0);
}

void int_handler(int sig)
{
	char c;
	signal(sig, SIG_IGN);
	printf("Really quit? [y/N] ");
	c = getchar();
	if (c == 'y' || c == 'Y')
		cleanup_and_exit(0); // Exit without shutdown.
	else
		signal(SIGINT, int_handler);
	getchar();
}

int initialize_xfr()
{
	//for OS/8:
	//get function
	//get buffer address
	//get starting block number
	//send CDF instruction
	//send negative word count
	//send ack [4000=READ PAGE, 4001=WRITE PAGE, 0000=DONE, 2000=ERROR]
	//if read, read until transfer is done, send ack
	//if write, write until transfer is done, send ack
	//if done, just send ack
	//if error, just send ack

	//for DIAL, some things change:
	//get unit number + read/write (still bit0 as in os8)
	//get buffer address (div by 0400)
	//get starting block number
	//get block count
	//... rest remains the same

	//XXX get starting address of buffer
	int current_word = 0;
	int retval = 0;
	int selected_disk;
	int selected_side;
	int field;
	int cdf_instr;
	int num_pages;
	int buffer_addr;

	// Determine disk number by converting to an index then dividing by 2.
	selected_disk = (buf[0] - 'A') / 2;
	selected_disk_state = &disks[selected_disk];

	// B, D, ... ascii codes are even, while A, C, ... are odd.
	// So we can just check the least significant bit to determine side.
	selected_side = ~buf[0] & 1;
	block_offset = NUMBER_OF_BLOCKS * selected_side;

	// This disk must be available.
	if(!selected_disk_state->in_use)
	{
		fprintf(stderr, MAKE_RED "Warning: no %s disk!\n" RESET_COLOR, disk_num_strings[selected_disk - 1]);
		acknowledgment = NACK;
		retval = -1;
	}

	receive_buf(buf, dial_mode ? 8 : 6); //get three words if os8; four if DIAL

	current_word = decode_word(buf, 0); // function word

	if (current_word & 07 && !dial_mode) // doesn't apply in DIAL mode
	{
#ifdef DEBUG
		printf(MAKE_YELLOW "Received special device code %o\n" RESET_COLOR, current_word & 07);
#endif
		if (current_word & 06)
			fprintf(stderr, MAKE_RED "Warning: unused bits in device code are set!\n" RESET_COLOR);
	}
		
	// Do not attempt to over-write failure with success here!
	if (retval == 0)
	{
		if (current_word & 04000)
		{
			direction = WRITE;
			acknowledgment = ACK_WRITE;
		}
		else
		{
			direction = READ;
			acknowledgment = ACK_READ;
		}
	}
	
	// Process OS/8 arguments
	if(!dial_mode)
	{
		num_pages = (current_word & 03700) >> 6;
		if (num_pages == 0)
			num_pages = 040;
		cdf_instr = 06201 | (current_word & 070);
		field = (current_word & 070) >> 3;
		buffer_addr = decode_word(buf, 1);
		start_block = decode_word(buf, 2);
	}
	else /* if(dial_mode) */ // DIAL arguments
	{
		//In DIAl, we treat each half disk as 6 sub-disks because
		//DIAL only supports 512 block devices.
		//We'll treat the bottom 3 bits of the unit number as the sub-disk number.
		//and use our offset field to add sub-disk offsets.
		block_offset += (current_word & 07) * DIAL_SUB_DISK_BLK_COUNT;
		current_word = decode_word(buf, 1);
		buffer_addr = (current_word & 017) * BLOCK_SIZE;
		field = (current_word & ~017) >> 4;
		start_block = decode_word(buf, 2);
		num_pages = decode_word(buf, 3) * 2; // this is 256 word blocks instead of 128 word os8 records
	}
	
#ifdef DEBUG
	printf("Disk:     %s\n", disk_num_strings[selected_disk]);
	printf("Side:     %d\n", selected_side);
	printf("Function: %04o\n", current_word);
	printf("Buffer:   %04o\n", buffer_addr);
	printf("Block:    %04o\n", start_block);
#endif

	printf("Request to %s %d page%s %s side %d on %s disk\n", (direction == WRITE ? "write" : "read"),
	       num_pages, (num_pages == 1 ? "" : "s"), (direction == WRITE ? "to" : "from"),
	       selected_side, disk_num_strings[selected_disk]);

	printf("Buffer address %05o, starting block %05o\n", 
		(field << 12) | buffer_addr, start_block);

	// Writing with write protect not allowed.
	if(direction == WRITE && selected_disk_state->write_protect)
	{
		fprintf(stderr, MAKE_RED "Warning: write command and selected disk is write-protected!\n" RESET_COLOR);
		acknowledgment = NACK | 16;
		retval = -1;
	}

	// Reading with read protect not allowed.
	if(direction == READ && selected_disk_state->read_protect)
	{
		fprintf(stderr, MAKE_RED "Warning: read command and selected disk is read-protected!\n" RESET_COLOR);
		acknowledgment = NACK | 16;
		retval = -1;
	}
		
	if (((num_pages / 2) + (num_pages & 1)) + start_block > NUMBER_OF_BLOCKS)
	{
		fprintf(stderr, MAKE_RED "Warning: client asking to write past disk boundary!\n" RESET_COLOR);
		acknowledgment = NACK | 2;
		retval = -1;
	}
	
	if ((field == 0) && (buffer_addr + (num_pages * PAGE_SIZE) > 07600) && !dial_mode)
	{
		fprintf(stderr, MAKE_RED "Warning: client asking to overwrite OS/8 resident page!\n" RESET_COLOR);
		acknowledgment = NACK | 4;
		retval = -1;
	}
	
	send_word(cdf_instr); //send CDF instruction
	send_word(-(num_pages * PAGE_SIZE) & 07777); //don't update num_pages before sending word count

	total_num_words = num_pages * PAGE_SIZE;

	num_bytes = total_num_words * BYTES_PER_WORD; //total number of bytes to receive/transmit

	if (direction == WRITE)
		half_block = num_pages & 1; //handle half block write with zero padding
	else
		half_block = 0;

	return retval;
}

void process_send_boot_sector()
{
	printf("Booting...\n");
	if (!read_from_file(disks[0].fp, 0, disk_buf, BLOCK_SIZE * BYTES_PER_WORD))
	{
		djg_to_pdp(disk_buf, converted_disk_buf, BLOCK_SIZE);
		if (!transmit_buf(converted_disk_buf, BLOCK_SIZE * BYTES_PER_WORD))
		{
			disk_buf[0] = 0200; //trailer
			if (!transmit_buf(disk_buf, 1))
				printf(MAKE_GREEN "Done sending block 0\n" RESET_COLOR);
			else
				fprintf(stderr, MAKE_RED "Warning: failed to send trailer!\n" RESET_COLOR);
		}
		else
			fprintf(stderr, MAKE_RED "Warning: failed to send block 0!\n" RESET_COLOR);
	}
	else
		fprintf(stderr, MAKE_RED "Warning: failed to read block 0!\n" RESET_COLOR);
}

void process_read()
{
	acknowledgment = ACK_DONE;
	read_from_file(selected_disk_state->fp, (start_block + block_offset) * BLOCK_SIZE * BYTES_PER_WORD,
		       disk_buf, num_bytes);
	djg_to_pdp(disk_buf, converted_disk_buf, total_num_words);
	transmit_buf(converted_disk_buf, num_bytes);

	int c = 0;
	if ((c = ser_read(fd, (char *) buf, sizeof(buf))) < 0)
	{
		perror("Serial read failure");
		exit(1);
	}
	else if (c != 0)
	{
		fprintf(stderr, MAKE_RED "Warning: detected bytes during read!\n" RESET_COLOR);
		acknowledgment = NACK | 8;
	}

	send_word(acknowledgment);
#ifdef REALLY_DEBUG
	if (!(acknowledgment & NACK))
		printf("Sent done acknowledgment\n");
	else
		printf("Received words during read, sent NACK\n");
#endif
	if (!(acknowledgment & NACK))
		printf(MAKE_GREEN "Successfully completed read\n" RESET_COLOR);
	else
		fprintf(stderr, MAKE_RED "Warning: failed to complete read!\n" RESET_COLOR);
}

void process_write()
{
	acknowledgment = ACK_DONE;

	receive_buf(disk_buf, num_bytes); //get data to write

	int c;
	if ((c = ser_read(fd, (char *) buf, sizeof(buf))) < 0)
	{
		perror("Serial read failure");
		exit(1);
	}
	else if (c != 0)
	{
		fprintf(stderr, MAKE_RED "Warning: detected bytes after write!\n" RESET_COLOR);
		acknowledgment = NACK | 8;
	}

	if (half_block)
	{
		for (int j = num_bytes; j < PAGE_SIZE * BYTES_PER_WORD; j++)
			disk_buf[j] = 0;
		total_num_words += PAGE_SIZE;
	}

	send_word(acknowledgment);
#ifdef REALLY_DEBUG
	if (!(acknowledgment & NACK))
		printf("Sent done acknowledgment\n");
	else
		printf("Received too many words, sent NACK\n");
#endif
	if (!(acknowledgment & NACK))
	{
		pdp_to_djg(disk_buf, converted_disk_buf, total_num_words);
		write_to_file(selected_disk_state->fp, (start_block + block_offset) * BLOCK_SIZE * BYTES_PER_WORD,
			      converted_disk_buf, total_num_words * BYTES_PER_WORD);
		printf(MAKE_GREEN "Successfully completed write\n" RESET_COLOR);
	}
	else
		fprintf(stderr, MAKE_RED "Warning: failed to complete write!\n" RESET_COLOR);
}

void HELPBoot()
{
	// If BOOT1 is toggled in and started,
	// BOOT2 is sent in HELP loader format.
	int boot2[] = {
		//		00000, // Must be 0, not sent
		00000, // Must have 01000 clear
		00000, 05032,
		07032, 07012, 01003, 03036,
		04020, 03002, 04020, 03013,
		05010, 00000, 00000, 00000,
		00000, 04032, 07006, 07006,
		07006, 03000, 04032, 01000,
		03030,
		05004-1, // Decrement because of ISZ
		00000,   // Send NUL to get to JMP
	};
	// BOOT3 is then sent in BOOT2 loader format, followed
	// by the important parts of block 0 in BOOT3 format.
	int boot3[] = {
		05420, 00000, 03402,	// Actually the required patch
		00041, 04020,		// BOOT3 proper
		00042, 03002,
		00043, 04020,
		00044, 03001,
		00045, 04020,
		00046, 03047,
		00047, 00000,
		00050, 04020,
		00051, 03402,
		00052, 02002,
		00053, 07000,
		00054, 02001,
		00055, 05050,
		00056, 05041,
		00014, 05041,		// Kludge to start BOOT3
	};

	// This cooperates with a bootloader based on
	// the HELP loarder.
	printf("Booting...\n");
	if (boot2[0] & 04) {
		fprintf(stderr, MAKE_RED "Illegal initial word\n" RESET_COLOR);
		return;
	}
	for (int i = 0; i < sizeof(boot2)/sizeof(*boot2); i++) {
		unsigned int intval;
		unsigned char byteval;
		int link;

		if (boot2[i] & 0740) {
			fprintf(stderr, MAKE_RED "Illegal bit set in %04o at %04o\n" RESET_COLOR,
				boot2[i], i);
			return;
		}
		link = 0;
		if (i+1 < sizeof(boot2)/sizeof(*boot2))
			link = boot2[i+1] & 01000;
		intval = (link<<3) | boot2[i];
		byteval = (intval<<3) | (intval >> 10);
		//		fprintf(stderr, "%03o %05o\n", byteval, intval);
		if (transmit_buf(&byteval, 1))
			fprintf(stderr, MAKE_RED "Error: failed to send byte!\n" RESET_COLOR);
	}
	for (int i = 0; i < sizeof(boot3)/sizeof(*boot3); i++) {
		//		fprintf(stderr, "%03o\n%03o\n", boot3[i]>>6, boot3[i]&077);
		disk_buf[0] = boot3[i] >> 6;
		disk_buf[1] = boot3[i] & 077;
		if (transmit_buf(disk_buf, 2))
			fprintf(stderr, MAKE_RED "Warning: failed to send word!\n" RESET_COLOR);
	}
	if (!read_from_file(disks[0].fp, 0, disk_buf, BLOCK_SIZE * BYTES_PER_WORD))
	{
		djg_to_pdp(disk_buf, converted_disk_buf, BLOCK_SIZE);
		// converted_disk_buf is sent as two
		// blocks in BOOT3 format.
		// Prepend the field 1 stuff with
		// address, wc, and CDF 1.
		// Address
		converted_disk_buf[044*2+0] = 076;
		converted_disk_buf[044*2+1] = 047;
		// WC
		converted_disk_buf[045*2+0] = 076;
		converted_disk_buf[045*2+1] = 047;
		// Field 1
		converted_disk_buf[046*2+0] = 062;
		converted_disk_buf[046*2+1] = 011;
		//BUGBUG: THIS ISN'T WORKING YET!!
		if (!transmit_buf(converted_disk_buf+044*BYTES_PER_WORD, 0131*BYTES_PER_WORD+6))
		{
			converted_disk_buf[0175*2+0] = 076; // Address
			converted_disk_buf[0175*2+1] = 000;
			converted_disk_buf[0176*2+0] = 076; // WC
			converted_disk_buf[0176*2+1] = 000;
			converted_disk_buf[0177*2+0] = 062; // Field 1
			converted_disk_buf[0177*2+1] = 001;
			if (!transmit_buf(converted_disk_buf+0175*BYTES_PER_WORD, 0200*BYTES_PER_WORD+6))
			{
				converted_disk_buf[0*2+0] = 076; // Address
				converted_disk_buf[0*2+1] = 005;
				converted_disk_buf[1*2+0] = 076; // WC
				converted_disk_buf[1*2+1] = 005;
				converted_disk_buf[2*2+0] = 054; // Field 1
				converted_disk_buf[2*2+1] = 002;
				if (!transmit_buf(converted_disk_buf, 6))
					printf(MAKE_GREEN "Done sending OS/8 bootstrap\n" RESET_COLOR);
				else
					fprintf(stderr, MAKE_RED "Warning: failed to start OS/8!\n" RESET_COLOR);
			} else
				fprintf(stderr, MAKE_RED "Warning: failed to send driver content!\n" RESET_COLOR);
		} else
			fprintf(stderr, MAKE_RED "Warning: failed to send block 0!\n" RESET_COLOR);
	} else
		fprintf(stderr, MAKE_RED "Warning: failed to read block 0!\n" RESET_COLOR);
}

int decode_word(char* buf, int pos)
{
	return (((buf[(2 * pos) + 1] & 077) << 6) | (buf[2 * pos] & 077));
}

/*
 * Sent from PDP:  abcd -> XXcccddd XXaaabbb
 * Stored in file: abcd -> bbcccddd 0000aaab
 * Sent to PDP:    abcd -> 00aaabbb 00cccddd
 */

void djg_to_pdp(char* buf_in, char* buf_out, int word_count) 
{
	for (int i = 0; i < word_count * 2; i += 2)
	{
		buf_out[i] = ((buf_in[i + 1] << 2) & 074) | ((buf_in[i] >> 6) & 03); //make 00aaabbb
		buf_out[i + 1] = (buf_in[i] & 077); //make 00cccddd
	}
}

void pdp_to_djg(char* buf_in, char* buf_out, int word_count)
{
	for (int i = 0; i < word_count * 2; i += 2)
	{
		buf_out[i + 1] = (buf_in[i + 1] >> 2) & 0x0F; //00aaabbb -> 0000aaab
		buf_out[i] = ((buf_in[i + 1] << 6) & 0300) | (buf_in[i] & 077); //00aaabbb 00cccddd -> bbcccddd
	}
}

void send_word(int word)
{
	int c;
	buf[0] = (word >> 6) & 077;
	buf[1] = word & 077;
#ifdef REALLY_DEBUG
	printf("Sending %04o\n", word);
#endif
	if ((c = ser_write(fd, (char *) buf, 2)) < 0)
	{
		perror("Serial write failure");
		exit(1);
	}
	if (c != 2)
		fprintf(stderr, MAKE_RED "Warning: failed to send entire buffer!\n" RESET_COLOR);
}

int transmit_buf(char* buf, int length)
{
	int c;
	if ((c = ser_write(fd, (char *) buf, length)) < 0)
	{
		perror("Serial write failure\n");
		exit(1);
	}
	if (c != length)
	{
		fprintf(stderr, MAKE_RED "Warning: failed to send entire buffer!\n" RESET_COLOR);
		return 1;
	}
	else
		return 0;
}

void receive_buf(char* buf, int length)
{
	int c;
	int offset = 0;
	while (offset < length)
	{
		if ((c = ser_read(fd, (char *) buf + offset, length - offset)) < 0)
		{
			perror("Serial read failure");
			exit(1);
		}
		offset += c;
	}
}

int read_from_file(FILE* file, int offset, char* buf, int length)
{
	int c;
	fseek(file, offset, SEEK_SET);
	if ((c = fread(buf, 1, length, file)) < 0)
	{
		perror("File read failure");
		exit(1);
	}
	else if (c != length)
	{
		//fprintf(stderr, MAKE_RED "Warning: failed to complete read! Reached EOF?\n" RESET_COLOR);
		return 1;
	}
	else
		return 0;
}

int write_to_file(FILE* file, int offset, char* buf, int length)
{
	int c;
	fseek(file, offset, SEEK_SET);
	if ((c = fwrite(buf, 1, length, file)) < 0)
	{
		perror("File write failure");
		exit(1);
	}
	else if (c != length)
	{
		//fprintf(stderr, MAKE_RED "Warning: failed to complete write!\n" RESET_COLOR);
		return 1;
	}
	else
	{
		fflush(file);
		return 0;
	}
}

/* 
//	This program talks to the PDP-8 via an asynchronous serial interface to
//	simulate disk drives located as a file on the PC.
//	It needs a config file disk.cfg or $HOME/.disk.cfg with the format defined in
//	config.c
//	Disk file format is compatible with the ubiquitous SimH etc. format.
//
//	Press ctrl-C to stop the server.
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

#define NUMBER_OF_BLOCKS 06260 //number of blocks in a single RK05 side
#define FILE_LENGTH (NUMBER_OF_BLOCKS * BLOCK_SIZE * BYTES_PER_WORD) //length of RK05 image

//#define DEBUG
//#define REALLY_DEBUG

int terminate = 0;

#include "config.c"
#include "comm.c"

// Note: We expect there to be (at least) a first disk, disk1
// although this would not be strictly necessary for non-system devices
static const char usage[] = "Usage: %s -1 disk1 [-2 disk2] [-3 disk3] [-4 disk4] [-r 1|2|3|4] [-w 1|2|3|4] [-b bootloader]\n";

int initialize_xfr();
void send_word(int word);
int decode_word(char* buf, int pos);
void int_handler(int);
void djg_to_pdp(char* buf_in, char* buf_out, int word_count);
void pdp_to_djg(char* buf_in, char* buf_out, int word_count);
int write_to_file(FILE* file, int offset, char* buf, int length);
int read_from_file(FILE* file, int offset, char* buf, int length);
void receive_buf(char* buf, int length);
int transmit_buf(char* buf, int length);

int fd;
FILE* disk1;
FILE* disk2;
FILE* disk3;
FILE* disk4;
FILE* selected_disk_fp;
FILE* btldr;
char* filename_disk1;
char* filename_disk2;
char* filename_disk3;
char* filename_disk4;
char* filename_btldr;
char serial_dev[256];
long baud;
int two_stop;
unsigned char buf[256];
unsigned char converted_buf[256];
unsigned char disk_buf[8200];
unsigned char converted_disk_buf[8200];
int direction;
int buffer_addr;
int start_block;
int field;
int cdf_instr;
int total_num_words;
int acknowledgment;
int i, j, c;
int num_bytes;
int num_pages;
int read_protect1 = 0;
int write_protect1 = 0;
int read_protect2 = 0;
int write_protect2 = 0;
int read_protect3 = 0;
int write_protect3 = 0;
int read_protect4 = 0;
int write_protect4 = 0;
int half_block = 0;
int block_offset = 0;
int selected_disk;
int selected_side;

int use_disk1 = 0;
int use_disk2 = 0;
int use_disk3 = 0;
int use_disk4 = 0;
int send_btldr = 0;

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
	signal(SIGINT, int_handler);

	while ((c = getopt(argc, argv, "-1:2:3:4:b:r:w:")) != -1)
	{
		switch (c)
		{
			case '1': //first disk
				use_disk1 = 1;
				filename_disk1 = optarg;
				break;
			case '2': //second disk
				use_disk2 = 1;
				filename_disk2 = optarg;
				break;
			case '3': //third disk
				use_disk3 = 1;
				filename_disk3 = optarg;
				break;
			case '4': //fourth disk
				use_disk4 = 1;
				filename_disk4 = optarg;
				break;
			case 'r': //read-protect
				i = 0;
				while (optarg[i] != 0)
				{
					if (optarg[i] == '1')
						read_protect1 = 1;
					else if (optarg[i] == '2')
						read_protect2 = 1;
					else if (optarg[i] == '3')
						read_protect3 = 1;
					else if (optarg[i] == '4')
						read_protect4 = 1;
					else
					{
						printf(usage, argv[0]);
						exit(1);
					}
					i++;
				}
				break;
			case 'w': //write-protect
				i = 0;
				while (optarg[i] != 0)
				{
					if (optarg[i] == '1')
						write_protect1 = 1;
					else if (optarg[i] == '2')
						write_protect2 = 1;
					else if (optarg[i] == '3')
						write_protect3 = 1;
					else if (optarg[i] == '4')
						write_protect4 = 1;
					else
					{
						printf(usage, argv[0]);
						exit(1);
					}
					i++;
				}
				break;
			case 'b': //bootloader
				send_btldr = 1;
				filename_btldr = optarg;
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

	printf("PDP-8 Disk Server for OS/8, v1.3\n");

	if (!use_disk1)
	{
		printf("no system disk (disk1) specified\n");
		printf(usage, argv[0]);
		exit(1);
	}
	else
	{
		disk1 = fopen(filename_disk1, "r+");
		if (disk1 == NULL)
		{
			fprintf(stderr, "On file %s ", filename_disk1);
			perror("open failed");
			exit(1);
		}
		printf("Using first  disk %s with read %s and write %s\n", filename_disk1, 
			(read_protect1 ? MAKE_RED "disabled" RESET_COLOR : MAKE_GREEN "enabled" RESET_COLOR), 
			(write_protect1 ? MAKE_RED "disabled" RESET_COLOR : MAKE_GREEN "enabled" RESET_COLOR));
	}
	
	if (use_disk2)
	{
		disk2 = fopen(filename_disk2, "r+");
		if (disk2 == NULL)
		{
			fprintf(stderr, "On file %s ", filename_disk2);
			perror("open failed");
			exit(1);
		}
		printf("Using second disk %s with read %s and write %s\n", filename_disk2, 
			(read_protect2 ? MAKE_RED "disabled" RESET_COLOR : MAKE_GREEN "enabled" RESET_COLOR), 
			(write_protect2 ? MAKE_RED "disabled" RESET_COLOR : MAKE_GREEN "enabled" RESET_COLOR));
	}

	if (use_disk3)
	{
		disk3 = fopen(filename_disk3, "r+");
		if (disk3 == NULL)
		{
			fprintf(stderr, "On file %s ", filename_disk3);
			perror("open failed");
			exit(1);
		}
		printf("Using third  disk %s with read %s and write %s\n", filename_disk3, 
			(read_protect3 ? MAKE_RED "disabled" RESET_COLOR : MAKE_GREEN "enabled" RESET_COLOR), 
			(write_protect3 ? MAKE_RED "disabled" RESET_COLOR : MAKE_GREEN "enabled" RESET_COLOR));
	}

	if (use_disk4)
	{
		disk4 = fopen(filename_disk4, "r+");
		if (disk4 == NULL)
		{
			fprintf(stderr, "On file %s ", filename_disk4);
			perror("open failed");
			exit(1);
		}
		printf("Using fourth disk %s with read %s and write %s\n", filename_disk4, 
			(read_protect4 ? MAKE_RED "disabled" RESET_COLOR : MAKE_GREEN "enabled" RESET_COLOR), 
			(write_protect4 ? MAKE_RED "disabled" RESET_COLOR : MAKE_GREEN "enabled" RESET_COLOR));
	}

	if (send_btldr)
	{
		btldr = fopen(filename_btldr, "r");
		if (btldr == NULL)
		{
			fprintf(stderr, "On file %s ", filename_btldr);
			perror("open failed");
			exit(1);
		}
	}
	
	setup_config(&baud,&two_stop,serial_dev);
	fd = init_comm(serial_dev,baud,two_stop);
	
	printf("Using serial port %s at %s with %s\n", 
		serial_dev, baud_lookup[baud - 1].baud_str, (two_stop ? "2 stop bits" : "1 stop bit"));

	if (send_btldr)
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

/*
// Command processor
// Accepts the following wakeup commands:
// @	- read the first sector/first side/first disk (boot sector)
// A	- process command to first side of first disk
// B	- process command to second side of first disk
// C-H	- process command to first/second side of second/third/fourth disk
// Q	- stop operations and shut down the server
//	- anything else is a (non-fatal) error
*/
	
	for (;;)
	{			
		receive_buf(buf, 1); //wait for command
		switch (buf[0])
		{
			case '@':
				printf("Booting...\n");
				if (!read_from_file(disk1, 0, disk_buf, BLOCK_SIZE * BYTES_PER_WORD))
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
					i = 0;
					send_word(acknowledgment);		
						
					if (direction == WRITE) //********** WRITE ************//
					{
						acknowledgment = ACK_DONE;
					
						receive_buf(disk_buf, num_bytes); //get data to write
					
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
							for (j = num_bytes; j < PAGE_SIZE * BYTES_PER_WORD; j++)
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
							write_to_file(selected_disk_fp, (start_block + block_offset) * BLOCK_SIZE * BYTES_PER_WORD, 
								converted_disk_buf, total_num_words * BYTES_PER_WORD);
							printf(MAKE_GREEN "Successfully completed write\n" RESET_COLOR);
						}
						else
							fprintf(stderr, MAKE_RED "Warning: failed to complete write!\n" RESET_COLOR);
					}
					else //********** READ ************//
					{
						acknowledgment = ACK_DONE;
						read_from_file(selected_disk_fp, (start_block + block_offset) * BLOCK_SIZE * BYTES_PER_WORD,
							disk_buf, num_bytes);
						djg_to_pdp(disk_buf, converted_disk_buf, total_num_words);
						transmit_buf(converted_disk_buf, num_bytes);
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
				}
				break;
			case 'Q': //quit server
				printf(MAKE_YELLOW "Received quit signal, server quitting\n" RESET_COLOR);
				fclose(disk1);
				fclose(disk2);
				fclose(disk3);
				fclose(disk4);
				system("sudo shutdown -h now");
				exit(0);
			default:
				fprintf(stderr, MAKE_RED "Received unknown command - ignored - character %04o\n" 
					RESET_COLOR, buf[0]);
				break;
		}
	}
}

void int_handler(int sig)
{
	char c;
	signal(sig, SIG_IGN);
	printf("Really quit? [y/N] ");
	c = getchar();
	if (c == 'y' || c == 'Y')
	{
		fclose(disk1);
		fclose(disk2);
		fclose(disk3);
		fclose(disk4);
		exit(0);
	}
	else
		signal(SIGINT, int_handler);
	getchar();
}

int initialize_xfr()
{
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

	//XXX get starting address of buffer
	int current_word;
	int retval = 0;
	i = 0;
	j = 0;

	if (buf[0] == 'B' || buf[0] == 'D' || buf[0] == 'F' || buf[0] == 'H')
	{
		block_offset = NUMBER_OF_BLOCKS;
		selected_side = 1;
	}
	else
	{
		block_offset = 0;
		selected_side = 0;
	}

	if (buf[0] == 'A' || buf[0] == 'B')
	{
		if (!use_disk1)
		{
			fprintf(stderr, MAKE_RED "Warning: no first disk!\n" RESET_COLOR);
			acknowledgment = NACK;
			retval = -1;
		}
		else
		{
			selected_disk_fp = disk1;
			selected_disk = 1;
		}
	}
	else if (buf[0] == 'C' || buf[0] == 'D')
	{
		if (!use_disk2)
		{
			fprintf(stderr, MAKE_RED "Warning: no second disk!\n" RESET_COLOR);
			acknowledgment = NACK;
			selected_disk = 2;
			retval = -1;
		}
		else
		{
			selected_disk_fp = disk2;
			selected_disk = 2;
		}
	}
	else if (buf[0] == 'E' || buf[0] == 'F')
	{
		if (!use_disk3)
		{
			fprintf(stderr, MAKE_RED "Warning: no third disk!\n" RESET_COLOR);
			acknowledgment = NACK;
			selected_disk = 3;
			retval = -1;
		}
		else
		{
			selected_disk_fp = disk3;
			selected_disk = 4;
			selected_disk = 3;
		}
	}
	else if (buf[0] == 'G' || buf[0] == 'H')
	{
		if (!use_disk4)
		{
			fprintf(stderr, MAKE_RED "Warning: no fourth disk!\n" RESET_COLOR);
			acknowledgment = NACK;
			retval = -1;
		}
		else
		{
			selected_disk_fp = disk4;
			selected_disk = 4;
		}
	}
	else if (buf[0] =='Q')
	{
		system("sudo shutdown -h now");
		retval = -1;
		}
	else
	{
		fprintf(stderr, MAKE_RED "Warning: handler sent bad character!\n" RESET_COLOR);
		acknowledgment = NACK;
		return -1;
	}

	receive_buf(buf, 6); //get three words
	
	c = 0;
	current_word = decode_word(buf, 0);
	
	if (current_word & 07)
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
	
	num_pages = (current_word & 03700) >> 6;
	if (num_pages == 0)
		num_pages = 040;
	cdf_instr = 06201 | (current_word & 070);
	field = (current_word & 070) >> 3;
	buffer_addr = decode_word(buf, 1);
	start_block = decode_word(buf, 2);
	
#ifdef DEBUG
	//printf("Disk:     %s\n", (selected_disk ? "secondary" : "first"));
	//printf("Side:     %d\n", selected_side);
	printf("Function: %04o\n", current_word);
	printf("Buffer:   %04o\n", buffer_addr);
	printf("Block:    %04o\n", start_block);
#endif

	if ((selected_disk == 1) || (selected_disk == 2))
	{
		printf("Request to %s %d page%s %s side %d on %s disk\n", (direction == WRITE ? "write" : "read"),
		num_pages, (num_pages == 1 ? "" : "s"), (direction == WRITE ? "to" : "from"),
		(block_offset ? 1 : 0), (selected_disk-1 ? "second" : "first"));
	}
	else
	{
		printf("Request to %s %d page%s %s side %d on %s disk\n", (direction == WRITE ? "write" : "read"),
		num_pages, (num_pages == 1 ? "" : "s"), (direction == WRITE ? "to" : "from"),
		(block_offset ? 1 : 0), (selected_disk-1 ? "fourth" : "third"));
	}
	printf("Buffer address %05o, starting block %05o\n", 
		(field << 12) | buffer_addr, start_block);

	if  ((direction == WRITE && selected_disk == 1 && write_protect1 == 1) ||
		(direction == WRITE && selected_disk == 2 && write_protect2 == 1) ||
		(direction == WRITE && selected_disk == 3 && write_protect3 == 1) ||
		(direction == WRITE && selected_disk == 4 && write_protect4 == 1))
	{
		fprintf(stderr, MAKE_RED "Warning: write command and selected disk is write-protected!\n" RESET_COLOR);
		acknowledgment = NACK | 16;
		retval = -1;
	}
	
	if  ((direction == READ && selected_disk == 1 && read_protect1 == 1) ||
		(direction == READ && selected_disk == 2 && read_protect2 == 1) ||
		(direction == READ && selected_disk == 3 && read_protect3 == 1) ||
		(direction == READ && selected_disk == 4 && read_protect4 == 1))
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
	
	if ((field == 0) && (buffer_addr + (num_pages * PAGE_SIZE) > 07600))
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
	int i;
	for (i = 0; i < word_count * 2; i += 2)
	{
		buf_out[i] = ((buf_in[i + 1] << 2) & 074) | ((buf_in[i] >> 6) & 03); //make 00aaabbb
		buf_out[i + 1] = (buf_in[i] & 077); //make 00cccddd
	}
}

void pdp_to_djg(char* buf_in, char* buf_out, int word_count)
{
	int i;
	for (i = 0; i < word_count * 2; i += 2)
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

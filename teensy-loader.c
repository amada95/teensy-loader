/*
 * teensy-loader, command line interface
 * flash and reboot Teensy boards with the HalfKay bootloader
 *
 * Copyright (C) 2022, Rose (pseudonym)		- twogardens@pm.me
 * Copyright (C) 2008-2016, PJRC.COM, LLC	- paul@pjrc.com
 *
 *
 * You may redistribute this program and/or modify it under the terms
 * of the GNU General Public License as published by the Free Software
 * Foundation, version 3 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see http://www.gnu.org/licenses/
*/

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>

/* Usage Function */
void usage(const char *err);

/* USB Access Functions */
int32_t	teensy_open(void);
int32_t	teensy_write(void *buf, int32_t len, double timeout);
void	teensy_close(void);

/* Teensy Boot Functions */
void 	teensy_boot(uint8_t *buf, int32_t write_size);
int32_t	teensy_hard_reboot(void);
int32_t	teensy_soft_reboot(void);

/* Intel Hex File Functions */
int32_t	ihex_read(const char *filename);
int32_t	ihex_bytes_in_range(int32_t begin, int32_t end);
void	ihex_get_data(int32_t addr, int32_t len, uint8_t *bytes);
int32_t	ihex_memory_is_blank(int32_t addr, int32_t block_size);

/* Miscellaneous Functions */
int32_t printf_verbose(const char *format, ...);
void 	die(const char *str, ...);
void 	parse_options(int32_t argc, char **argv);

/* User CLI Options */
bool wait_for_device_to_appear,
     teensy_hard_reboot_device,
     teensy_soft_reboot_device,
     verbose,
     boot_only			= false;
bool reboot_after_programming	= true;
int32_t code_size = 0, block_size = 0;
const char *filename = NULL;


/**********************/
/*    Main Program    */
/**********************/

int32_t main(int32_t argc, char **argv) {
	uint8_t buf[2048];
	int32_t num, addr, r, write_size;
	int32_t first_block = 1;
	bool waited = false;

	parse_options(argc, argv);
	
	if (!filename && !boot_only) {
		usage("filename must be specified");
	}
	if (!code_size) {
		usage("mcu type must be specified");
	}
	printf_verbose("teensy-loader cli\n");

	if (block_size == 512 || block_size == 1024) {
		write_size = block_size + 64;
	} else {
		write_size = block_size + 2;
	};

	if (!boot_only) {
		num = ihex_read(filename);	// read the intel hex file (done first so errors arise before usb)
		if (num < 0) die("error reading intel hex file \"%s\"", filename);
		printf_verbose("Read \"%s\": %d bytes, %.1f%% usage\n",
			filename, num, (double) num / (double) code_size * 100.0);
	}

	/* open the usb device */
	while (1) {
		if (teensy_open()) break;
		if (teensy_hard_reboot_device) {
			if (!teensy_hard_reboot()) die("unable to find rebootor\n");
			printf_verbose("hard reboot performed\n");
			teensy_hard_reboot_device = false;	// only hard reboot once
			wait_for_device_to_appear = true;
		}
		if (teensy_soft_reboot_device) {
			if (teensy_soft_reboot()) {
				printf_verbose("soft reboot performed\n");
			}
			teensy_soft_reboot_device = false;
			wait_for_device_to_appear = true;
		}
		if (!wait_for_device_to_appear) die("unable to open device (try -w option)\n");
		if (!waited) {
			printf_verbose("waiting for teensy device...\n");
			printf_verbose("\t(try pressing the reset button)\n");
			waited = true;
		}
		usleep(250000);	// sleep 0.25s (250000ms)
	}
	printf_verbose("found HalfKay bootloader\n");

	if (boot_only) {
		teensy_boot(buf, write_size);
		teensy_close();
		return 0;
	}
	if (waited) {	// if we waited for the device read the hex file again (in case it changed while waiting)
		num = ihex_read(filename);
		if (num < 0) die("error reading intel hex file \"%s\"", filename);
		printf_verbose("read \"%s\": %d bytes, %.1f%% usage\n",
		 	filename, num, (double) num / (double) code_size * 100.0);
	}

	/* write data to the teensy */
	printf_verbose("programming...");
	fflush(stdout);
	for (addr = 0; addr < code_size; addr += block_size) {
		/* only write first unused block to erase the chip */
		if (!first_block && !ihex_bytes_in_range(addr, addr + block_size - 1)) continue;
		if (!first_block && ihex_memory_is_blank(addr, block_size)) continue;
		
		printf_verbose("- addr: %d\n", addr);
		if (block_size <= 256 && code_size < 0x10000) {
			buf[0] = addr & 255;
			buf[1] = (addr >> 8) & 255;
			ihex_get_data(addr, block_size, buf + 2);
			write_size = block_size + 2;
		} else if (block_size == 256) {
			buf[0] = (addr >> 8) & 255;
			buf[1] = (addr >> 16) & 255;
			ihex_get_data(addr, block_size, buf + 2);
			write_size = block_size + 2;
		} else if (block_size == 512 || block_size == 1024) {
			buf[0] = addr & 255;
			buf[1] = (addr >> 8) & 255;
			buf[2] = (addr >> 16) & 255;
			memset(buf + 3, 0, 61);
			ihex_get_data(addr, block_size, buf + 64);
			write_size = block_size + 64;
		} else {
			die("unknown code/block size\n");
		}
		r = teensy_write(buf, write_size, first_block ? 5.0 : 0.5);
		if (!r) die("error writing to teensy\n");
		first_block = 0;
	}
	printf_verbose("\n");

	// reboot to the user's new code
	if (reboot_after_programming) 
		teensy_boot(buf, write_size);
	
	teensy_close();
	return 0;
}


/*****************************/
/*    USB Access (libusb)    */
/*****************************/

#include <usb.h>

usb_dev_handle *open_usb_device(int32_t vid, int32_t pid) {
	struct usb_bus *bus;
	struct usb_device *dev;
	usb_dev_handle *h;
	char buf[128];
	int32_t r;

	usb_init();
	usb_find_busses();
	usb_find_devices();

	for (bus = usb_get_busses(); bus; bus = bus->next) {
		for (dev = bus->devices; dev; dev = dev->next) {
			if (dev->descriptor.idVendor != vid) continue;
			if (dev->descriptor.idProduct != pid) continue;
			h = usb_open(dev);
			if (!h) {
				printf_verbose("found device but unable to open\n");
				continue;
			}
			#ifdef LIBUSB_HAS_GET_DRIVER_NP
			r = usb_get_driver_np(h, 0, buf, sizeof(buf));
			if (r >= 0) {
				r = usb_detach_kernel_driver_np(h, 0);
				if (r < 0) {
					usb_close(h);
					printf_verbose("device is in use by \"%s\" driver\n", buf);
					continue;
				}
			}
			#endif
			return h;
		}
	}
	return NULL;
}

static usb_dev_handle *libusb_teensy_handle = NULL;

int32_t teensy_open(void) {
	teensy_close();
	libusb_teensy_handle = open_usb_device(0x16C0, 0x0478);
	if (libusb_teensy_handle) return 1;
	return 0;
}

int32_t teensy_write(void *buf, int32_t len, double timeout) {
	int32_t r;

	if (!libusb_teensy_handle) return 0;
	while (timeout > 0) {
		r = usb_control_msg(libusb_teensy_handle, 0x21, 9, 0x0200, 0,
			(char *)buf, len, (int32_t)(timeout * 1000.0));
		if (r >= 0) return 1;
		usleep(10000);
		timeout -= 0.01;
	}
	return 0;
}

void teensy_close(void) {
	if (!libusb_teensy_handle) return;
	usb_release_interface(libusb_teensy_handle, 0);
	usb_close(libusb_teensy_handle);
	libusb_teensy_handle = NULL;
}

int32_t teensy_hard_reboot(void) {
	usb_dev_handle *rebootor;
	int32_t r;

	rebootor = open_usb_device(0x16C0, 0x0477);
	if (!rebootor) return 0;
	r = usb_control_msg(rebootor, 0x21, 9, 0x0200, 0, "reboot", 6, 100);
	usb_release_interface(rebootor, 0);
	usb_close(rebootor);
	if (r < 0) return 0;
	return 1;
}

int32_t teensy_soft_reboot(void) {
	usb_dev_handle *serial_handle = NULL;

	serial_handle = open_usb_device(0x16C0, 0x0483);
	if (!serial_handle) {
		char *error = usb_strerror();
		printf("error opening usb device: %s\n", error);
		return 0;
	}

	char reboot_command[] = {0x86, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08};
	int32_t response = usb_control_msg(serial_handle, 0x21, 0x20, 0, 0, reboot_command, sizeof reboot_command, 10000);

	usb_release_interface(serial_handle, 0);
	usb_close(serial_handle);

	if (response < 0) {
		char *error = usb_strerror();
		printf("unable to soft reboot with usb error: %s\n", error);
		return 0;
	}

	return 1;
}


/*****************************/
/*    Read Intel Hex File    */
/*****************************/

/* maximum flash image size supported (without using a bigger chip) */
#define MAX_MEMORY_SIZE 0x1000000

static uint8_t	firmware_image[MAX_MEMORY_SIZE];
static uint8_t	firmware_mask[MAX_MEMORY_SIZE];
static int32_t 	end_record_seen = false;
static int32_t 	byte_count;
static uint32_t	extended_addr = 0;
static int32_t 	ihex_parse_line(char *line);

int32_t ihex_read(const char *filename) {
	FILE *fp;
	int32_t i, lineno = 0;
	char buf[1024];

	byte_count = 0;
	end_record_seen = 0;
	for (i = 0; i < MAX_MEMORY_SIZE; i++) {
		firmware_image[i] = 0xFF;
		firmware_mask[i] = 0;
	}
	extended_addr = 0;

	fp = fopen(filename, "r");
	if (fp == NULL) {
		printf("unable to open file \"%s\"\n", filename);
		return -1;
	}
	while (!feof(fp)) {
		*buf = '\0';
		if (!fgets(buf, sizeof(buf), fp)) break;
		lineno++;
		if (*buf) {
			if (ihex_parse_line(buf) == 0) {
				printf("hex parse error - line %d in file \"%s\"\n", lineno, filename);
				return -2;
			}
		}
		if (end_record_seen) break;
		if (feof(stdin)) break;
	}
	fclose(fp);
	return byte_count;
}


int32_t ihex_parse_line(char *line) {
/*
*  addr		<- starting address
*  bytes[]	<- bytes parsed from line
*  num		<- length of bytes[]
*
*  returns 1	<- line was valid
*  returns 0	<- error occurred in parsing
*/
int addr, code, num;
	int sum, len, cksum, i;
	char *ptr;

	num = 0;
	if (line[0] != ':') return 0;
	if (strlen(line) < 11) return 0;
	ptr = line+1;
	if (!sscanf(ptr, "%02x", &len)) return 0;
	ptr += 2;
	if ((int)strlen(line) < (11 + (len * 2)) ) return 0;
	if (!sscanf(ptr, "%04x", &addr)) return 0;
	ptr += 4;
	if (!sscanf(ptr, "%02x", &code)) return 0;
	if (addr + extended_addr + len >= MAX_MEMORY_SIZE) return 0;
	ptr += 2;
	sum = (len & 255) + ((addr >> 8) & 255) + (addr & 255) + (code & 255);
	if (code != 0) {
		if (code == 1) {
			end_record_seen = 1;
			return 1;
		}
		if (code == 2 && len == 2) {
			if (!sscanf(ptr, "%04x", &i)) return 1;
			ptr += 4;
			sum += ((i >> 8) & 255) + (i & 255);
			if (!sscanf(ptr, "%02x", &cksum)) return 1;
			if (((sum & 255) + (cksum & 255)) & 255) return 1;
			extended_addr = i << 4;
		}
		if (code == 4 && len == 2) {
			if (!sscanf(ptr, "%04x", &i)) return 1;
			ptr += 4;
			sum += ((i >> 8) & 255) + (i & 255);
			if (!sscanf(ptr, "%02x", &cksum)) return 1;
			if (((sum & 255) + (cksum & 255)) & 255) return 1;
			extended_addr = i << 16;
			if (code_size > 1048576 && block_size >= 1024 &&
			   extended_addr >= 0x60000000 && extended_addr < 0x60000000 + code_size) {
				extended_addr -= 0x60000000;	// Teensy 4.0 hex files have 0x60000000 FlexSPI offset
			}
		}
		return 1;	// non-data line
	}
	byte_count += len;
	while (num != len) {
		if (sscanf(ptr, "%02x", &i) != 1) return 0;
		i &= 255;
		firmware_image[addr + extended_addr + num] = i;
		firmware_mask[addr + extended_addr + num] = 1;
		ptr += 2;
		sum += i;
		(num)++;
		if (num >= 256) return 0;
	}
	if (!sscanf(ptr, "%02x", &cksum)) return 0;
	if (((sum & 255) + (cksum & 255)) & 255) return 0;	// checksum error
	return 1;
}

int32_t ihex_bytes_in_range(int32_t begin, int32_t end) {
	if (begin < 0 || begin >= MAX_MEMORY_SIZE || end < 0 || end >= MAX_MEMORY_SIZE)
		return 0;

	for (int32_t i = begin; i <= end; i++)
		if (firmware_mask[i]) return 1;

	return 0;
}

void ihex_get_data(int32_t addr, int32_t len, uint8_t *bytes) {
	int32_t i;
	if (addr < 0 || len < 0 || addr + len >= MAX_MEMORY_SIZE) {
		for (i = 0; i < len; i++) {
			bytes[i] = 255;
		}
		return;
	}
	for (int32_t i = 0; i < len; i++) {
		if (firmware_mask[addr]) {
			bytes[i] = firmware_image[addr];
		} else {
			bytes[i] = 255;
		}
		addr++;
	}
}

int32_t ihex_memory_is_blank(int32_t addr, int32_t block_size) {
	if (addr < 0 || addr > MAX_MEMORY_SIZE) return 1;

	while (block_size && addr < MAX_MEMORY_SIZE) {
		if (firmware_mask[addr] && firmware_image[addr] != 255) return 0;
		addr++;
		block_size--;
	}
	return 1;
}


/*********************************/
/*    Miscellaneous Functions    */
/*********************************/

void usage(const char *err) {
	if(err != NULL) fprintf(stderr, "%s\n\n", err);
	fprintf(stderr,
		"usage: teensy-loader --mcu=<MCU> [-w] [-h] [-n] [-b] [-v] <file.hex>\n"
		"\t-w : wait for device to appear\n"
		"\t-r : use hard reboot if device not online\n"
		"\t-s : use soft reboot if device not online (Teensy 3.x & 4.x)\n"
		"\t-n : no reboot after programming\n"
		"\t-b : boot only, do not program\n"
		"\t-v : verbose output\n"
		"\nUse `teensy-loader --list-mcus` to list supported mcus.\n"
		);
	exit(1);
}

int32_t printf_verbose(const char *format, ...) {
	va_list ap;
	int32_t r;

	va_start(ap, format);
	if (verbose) {
		r = vprintf(format, ap);
		fflush(stdout);
		return r;
	}
	return 0;
}

void die(const char *str, ...) {
	va_list  ap;

	va_start(ap, str);
	vfprintf(stderr, str, ap);
	fprintf(stderr, "\n");
	exit(1);
}


static const struct {
	const char *name;
	int32_t code_size;
	int32_t block_size;
} MCUs[] = {
	/* raw board names */
	{"at90usb162",   15872,   128},
	{"atmega32u4",   32256,   128},
	{"at90usb646",   64512,   256},
	{"at90usb1286", 130048,   256},
	{"mkl26z64",     63488,   512},
	{"mk20dx128",   131072,  1024},
	{"mk20dx256",   262144,  1024},
	{"mk66fx1m0",  1048576,  1024},
	{"mk64fx512",   524288,  1024},
	{"imxrt1062",  2031616,  1024},

	/* pretty board names (duplicates) */
	{"TEENSY2",     32256,   128},
	{"TEENSY2PP",  130048,   256},
	{"TEENSYLC",    63488,   512},
	{"TEENSY30",   131072,  1024},
	{"TEENSY31",   262144,  1024},
	{"TEENSY32",   262144,  1024},
	{"TEENSY35",   524288,  1024},
	{"TEENSY36",  1048576,  1024},
	{"TEENSY40",  2031616,  1024},
	{"TEENSY41",  8126464,  1024},
	{"TEENSY_MICROMOD", 16515072,  1024},
	{NULL, 0, 0},
};


void list_mcus() {
	printf("supported mcus are:\n");
	for (int32_t i = 0; MCUs[i].name != NULL; i++)
		printf(" - %s\n", MCUs[i].name);
	exit(1);
}


void read_mcu(char *name) {
	if (name == NULL) {
		fprintf(stderr, "no mcu specified.\n");
		list_mcus();
	}

	for (int32_t i = 0; MCUs[i].name != NULL; i++) {
		if (!strcasecmp(name, MCUs[i].name)) {
			code_size  = MCUs[i].code_size;
			block_size = MCUs[i].block_size;
			return;
		}
	}
	fprintf(stderr, "unknown mcu type \"%s\"\n", name);
	list_mcus();
}


void parse_flag(char *arg) {
	for(int32_t i=1; arg[i]; i++) {
		switch(arg[i]) {
			case 'w': wait_for_device_to_appear = true; break;
			case 'r': teensy_hard_reboot_device = true; break;
			case 's': teensy_soft_reboot_device = true; break;
			case 'n': reboot_after_programming = false; break;
			case 'v': verbose = true; break;
			case 'b': boot_only = true; break;
			default:
				fprintf(stderr, "unknown flag '%c'\n\n", arg[i]);
				usage(NULL);
		}
	}
}


void parse_options(int32_t argc, char **argv) {
	char *arg;

	for (int32_t i = 1; i < argc; i++) {
		arg = argv[i];

		if(arg[0] == '-') {
			if(arg[1] == '-') {
				char *name = &arg[2];
				char *val  = strchr(name, '=');
				if(val == NULL) // value must be the next string.
					val = argv[++i];
				else {		// we found a '=' so split the string at it
					*val = '\0';
					 val = &val[1];
				}

				if(!strcasecmp(name, "help")) usage(NULL);
				else if(!strcasecmp(name, "mcu")) read_mcu(val);
				else if(!strcasecmp(name, "list-mcus")) list_mcus();
				else {
					fprintf(stderr, "unknown option \"%s\"\n\n", arg);
					usage(NULL);
				}
			}
			else parse_flag(arg);
		}
		else filename = arg;
	}
}

void teensy_boot(uint8_t *buf, int32_t write_size) {
	printf_verbose("booting...\n");
	memset(buf, 0, write_size);
	buf[0] = 0xFF;
	buf[1] = 0xFF;
	buf[2] = 0xFF;
	teensy_write(buf, write_size, 0.5);
}

// SPDX-License-Identifier: MIT

#include <stdio.h>
#include <termios.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <stdlib.h>
#include <strings.h>
#include <string.h>
#include <poll.h>
#include <unistd.h>
#include <getopt.h>
#include <time.h>
#include <linux/serial.h>
#include <errno.h>
#include <sys/file.h>

//#define SHOW_TIOCGICOUNT
#define DUMP_STAT_INTERVAL_SECONDS 2

#define ERROR_COLOR "\e[1m\e[31m" // bold red
#define INFO_COLOR "\e[32m" // green
#define RESET_COLOR "\e[0m"
#define NULL_COLOR ""

/*
 * glibc for MIPS has its own bits/termios.h which does not define
 * CMSPAR, so we vampirise the value from the generic bits/termios.h
 */
#ifndef CMSPAR
#define CMSPAR 010000000000
#endif

/*
 * Define modem line bits
 */
#ifndef TIOCM_LOOP
#define TIOCM_LOOP	0x8000
#endif

// command line args
int _cl_baud = 0;
char *_cl_port = NULL;
int _cl_divisor = 0;
int _cl_rx_dump = 0;
int _cl_rx_dump_ascii = 0;
int _cl_tx_detailed = 0;
int _cl_stats = 0;
int _cl_stop_on_error = 0;
int _cl_single_byte = -1;
int _cl_another_byte = -1;
int _cl_rts_cts = 0;
int _cl_2_stop_bit = 0;
int _cl_parity = 0;
int _cl_odd_parity = 0;
int _cl_stick_parity = 0;
int _cl_loopback = 0;
int _cl_dump_err = 0;
int _cl_no_rx = 0;
int _cl_no_tx = 0;
int _cl_rx_delay = 0;
int _cl_tx_delay = 0;
int _cl_tx_bytes = 0;
int _cl_rs485_after_delay = -1;
int _cl_rs485_before_delay = 0;
int _cl_rs485_rts_after_send = 0;
int _cl_tx_time = 0;
int _cl_rx_time = 0;
int _cl_ascii_range = 0;
int _cl_write_after_read = 0;
int _cl_rx_timeout = 0;
int _cl_color_output = 0;

// Module variables
unsigned char _write_count_value = 0;
unsigned char _read_count_value = 0;
int _fd = -1;
unsigned char * _write_data;
ssize_t _write_size;

// keep our own counts for cases where the driver stats don't work
long long int _write_count = 0;
long long int _read_count = 0;
long long int _error_count = 0;

// for stats use
struct timespec start_time;

static int diff_ms(const struct timespec *t1, const struct timespec *t2);

static void exit_handler(void)
{
	if (_fd >= 0) {
		flock(_fd, LOCK_UN);
		close(_fd);
	}

	if (_cl_port) {
		free(_cl_port);
		_cl_port = NULL;
	}

	if (_write_data) {
		free(_write_data);
		_write_data = NULL;
	}
}

static void dump_data(unsigned char * b, int count)
{
	printf("%i bytes: ", count);
	int i;
	for (i=0; i < count; i++) {
		printf("%02x ", b[i]);
	}

	printf("\n");
}

static void dump_data_ascii(unsigned char * b, int count)
{
	int i;
	for (i=0; i < count; i++) {
		printf("%c", b[i]);
	}
}

static void set_baud_divisor(int speed, int custom_divisor)
{
	// default baud was not found, so try to set a custom divisor
	struct serial_struct ss;
	int ret;

	if (ioctl(_fd, TIOCGSERIAL, &ss) < 0) {
		ret = -errno;
		perror("TIOCGSERIAL failed");
		exit(ret);
	}

	ss.flags = (ss.flags & ~ASYNC_SPD_MASK) | ASYNC_SPD_CUST;
	if (custom_divisor) {
		ss.custom_divisor = custom_divisor;
	} else {
		ss.custom_divisor = (ss.baud_base + (speed/2)) / speed;
		int closest_speed = ss.baud_base / ss.custom_divisor;

		if (closest_speed < speed * 98 / 100 || closest_speed > speed * 102 / 100) {
			fprintf(stderr, "Cannot set speed to %d, closest is %d\n", speed, closest_speed);
			exit(-EINVAL);
		}

		printf("closest baud = %i, base = %i, divisor = %i\n", closest_speed, ss.baud_base,
				ss.custom_divisor);
	}

	if (ioctl(_fd, TIOCSSERIAL, &ss) < 0) {
		ret = -errno;
		perror("TIOCSSERIAL failed");
		exit(ret);
	}
}

static void clear_custom_speed_flag()
{
	struct serial_struct ss;
	int ret;

	if (ioctl(_fd, TIOCGSERIAL, &ss) < 0) {
		// return silently as some devices do not support TIOCGSERIAL
		return;
	}

	if ((ss.flags & ASYNC_SPD_MASK) != ASYNC_SPD_CUST)
		return;

	ss.flags &= ~ASYNC_SPD_MASK;

	if (ioctl(_fd, TIOCSSERIAL, &ss) < 0) {
		ret = -errno;
		perror("TIOCSSERIAL failed");
		exit(ret);
	}
}

// converts integer baud to Linux define
static int get_baud(int baud)
{
	switch (baud) {
	case 9600:
		return B9600;
	case 19200:
		return B19200;
	case 38400:
		return B38400;
	case 57600:
		return B57600;
	case 115200:
		return B115200;
	case 230400:
		return B230400;
	case 460800:
		return B460800;
	case 500000:
		return B500000;
	case 576000:
		return B576000;
	case 921600:
		return B921600;
#ifdef B1000000
	case 1000000:
		return B1000000;
#endif
#ifdef B1152000
	case 1152000:
		return B1152000;
#endif
#ifdef B1500000
	case 1500000:
		return B1500000;
#endif
#ifdef B2000000
	case 2000000:
		return B2000000;
#endif
#ifdef B2500000
	case 2500000:
		return B2500000;
#endif
#ifdef B3000000
	case 3000000:
		return B3000000;
#endif
#ifdef B3500000
	case 3500000:
		return B3500000;
#endif
#ifdef B4000000
	case 4000000:
		return B4000000;
#endif
	default:
		return -1;
	}
}

void set_modem_lines(int fd, int bits, int mask)
{
	int status, ret;
	static int warned = 0;

	if (ioctl(fd, TIOCMGET, &status) < 0) {
		if (! warned) {
			printf("WARNING: TIOCMGET failed\n");
			warned = 1;
		}
		return;
	}

	status = (status & ~mask) | (bits & mask);

	if (ioctl(fd, TIOCMSET, &status) < 0) {
		ret = -errno;
		perror("TIOCMSET failed");
		exit(ret);
	}
}

static void display_help(void)
{
	printf("Usage: linux-serial-test [OPTION]\n"
			"\n"
			"  -h, --help\n"
			"  -b, --baud         Baud rate, 115200, etc (115200 is default)\n"
			"  -p, --port         Port (/dev/ttyS0, etc) (must be specified)\n"
			"  -d, --divisor      UART Baud rate divisor (can be used to set custom baud rates)\n"
			"  -R, --rx_dump      Dump Rx data (ascii, raw)\n"
			"  -T, --detailed_tx  Detailed Tx data\n"
			"  -s, --stats        Dump serial port stats every 5s\n"
			"  -S, --stop-on-err  Stop program if we encounter an error\n"
			"  -y, --single-byte  Send specified byte to the serial port\n"
			"  -z, --second-byte  Send another specified byte to the serial port\n"
			"  -c, --rts-cts      Enable RTS/CTS flow control\n"
			"  -B, --2-stop-bit   Use two stop bits per character\n"
			"  -P, --parity       Use parity bit (odd, even, mark, space)\n"
			"  -k, --loopback     Use internal hardware loop back\n"
			"  -K, --write-follow Write follows the read count (can be used for multi-serial loopback)\n"
			"  -e, --dump-err     Display errors\n"
			"  -r, --no-rx        Don't receive data (can be used to test flow control)\n"
			"                     when serial driver buffer is full\n"
			"  -t, --no-tx        Don't transmit data\n"
			"  -l, --rx-delay     Delay between reading data (ms) (can be used to test flow control)\n"
			"  -a, --tx-delay     Delay between writing data (ms)\n"
			"  -w, --tx-bytes     Number of bytes for each write (default is to repeatedly write 1024 bytes\n"
			"                     until no more are accepted)\n"
			"  -q, --rs485        Enable RS485 direction control on port, and set delay from when TX is\n"
			"                     finished and RS485 driver enable is de-asserted. Delay is specified in\n"
			"                     bit times. To optionally specify a delay from when the driver is enabled\n"
			"                     to start of TX use 'after_delay.before_delay' (-q 1.1)\n"
			"  -Q, --rs485_rts    Deassert RTS on send, assert after send. Omitting -Q inverts this logic.\n"
			"  -o, --tx-time      Number of seconds to transmit for (defaults to 0, meaning no limit)\n"
			"  -i, --rx-time      Number of seconds to receive for (defaults to 0, meaning no limit)\n"
			"  -A, --ascii        Output bytes range from 32 to 126 (default is 0 to 255)\n"
			"  -x, --rx-timeout   Read timeout (ms) before write\n"
			"  -C, --color        Color output\n"
			"\n"
	      );
}

static void process_options(int argc, char * argv[])
{
	for (;;) {
		int option_index = 0;
		static const char *short_options = "hb:p:d:R:TsSy:z:cBertq:Ql:a:w:o:i:P:kKAx:C";
		static const struct option long_options[] = {
			{"help", no_argument, 0, 0},
			{"baud", required_argument, 0, 'b'},
			{"port", required_argument, 0, 'p'},
			{"divisor", required_argument, 0, 'd'},
			{"rx_dump", required_argument, 0, 'R'},
			{"detailed_tx", no_argument, 0, 'T'},
			{"stats", no_argument, 0, 's'},
			{"stop-on-err", no_argument, 0, 'S'},
			{"single-byte", no_argument, 0, 'y'},
			{"second-byte", no_argument, 0, 'z'},
			{"rts-cts", no_argument, 0, 'c'},
			{"2-stop-bit", no_argument, 0, 'B'},
			{"parity", required_argument, 0, 'P'},
			{"loopback", no_argument, 0, 'k'},
			{"write-follows", no_argument, 0, 'K'},
			{"dump-err", no_argument, 0, 'e'},
			{"no-rx", no_argument, 0, 'r'},
			{"no-tx", no_argument, 0, 't'},
			{"rx-delay", required_argument, 0, 'l'},
			{"tx-delay", required_argument, 0, 'a'},
			{"tx-bytes", required_argument, 0, 'w'},
			{"rs485", required_argument, 0, 'q'},
			{"rs485_rts", no_argument, 0, 'Q'},
			{"tx-time", required_argument, 0, 'o'},
			{"rx-time", required_argument, 0, 'i'},
			{"ascii", no_argument, 0, 'A'},
			{"rx-timeout", required_argument, 0, 'x'},
			{"color", required_argument, 0, 'C'},
			{0,0,0,0},
		};

		int c = getopt_long(argc, argv, short_options,
				long_options, &option_index);

		if (c == EOF) {
			break;
		}

		switch (c) {
		case 0:
		case 'h':
			display_help();
			exit(0);
			break;
		case 'b':
			_cl_baud = atoi(optarg);
			break;
		case 'p':
			_cl_port = strdup(optarg);
			break;
		case 'd':
			_cl_divisor = strtol(optarg, NULL, 0);
			break;
		case 'R':
			_cl_rx_dump = 1;
			_cl_rx_dump_ascii = !strcmp(optarg, "ascii");
			break;
		case 'T':
			_cl_tx_detailed = 1;
			break;
		case 's':
			_cl_stats = 1;
			break;
		case 'S':
			_cl_stop_on_error = 1;
			break;
		case 'y': {
			char * endptr;
			_cl_single_byte = strtol(optarg, &endptr, 0);
			break;
		}
		case 'z': {
			char * endptr;
			_cl_another_byte = strtol(optarg, &endptr, 0);
			break;
		}
		case 'c':
			_cl_rts_cts = 1;
			break;
		case 'B':
			_cl_2_stop_bit = 1;
			break;
		case 'P':
			_cl_parity = 1;
			_cl_odd_parity = (!strcmp(optarg, "mark")||!strcmp(optarg, "odd"));
			_cl_stick_parity = (!strcmp(optarg, "mark")||!strcmp(optarg, "space"));
			break;
		case 'k':
			_cl_loopback = 1;
			break;
		case 'K':
			_cl_write_after_read = 1;
			break;
		case 'e':
			_cl_dump_err = 1;
			break;
		case 'r':
			_cl_no_rx = 1;
			break;
		case 't':
			_cl_no_tx = 1;
			break;
		case 'l': {
			char *endptr;
			_cl_rx_delay = strtol(optarg, &endptr, 0);
			break;
		}
		case 'a': {
			char *endptr;
			_cl_tx_delay = strtol(optarg, &endptr, 0);
			break;
		}
		case 'w': {
			char *endptr;
			_cl_tx_bytes = strtol(optarg, &endptr, 0);
			break;
		}
		case 'q': {
			char *endptr;
			_cl_rs485_after_delay = strtol(optarg, &endptr, 0);
			_cl_rs485_before_delay = strtol(endptr+1, &endptr, 0);
			break;
		}
		case 'Q':
			_cl_rs485_rts_after_send = 1;
			break;
		case 'o': {
			char *endptr;
			_cl_tx_time = strtol(optarg, &endptr, 0);
			break;
		}
		case 'i': {
			char *endptr;
			_cl_rx_time = strtol(optarg, &endptr, 0);
			break;
		}
		case 'A':
			_cl_ascii_range = 1;
			break;
		case 'x': {
			char *endptr;
			_cl_rx_timeout = strtol(optarg, &endptr, 0);
			break;
		}
		case 'C':
			_cl_color_output = 1;
			break;
		}
	}
}

static void dump_serial_port_stats(void)
{
	struct serial_icounter_struct icount = { 0 };
	struct timespec current;
	int ms_since_beginning;
#if SHOW_TIOCGICOUNT
	static int tiocgicount_failed = 0;
#endif

	clock_gettime(CLOCK_MONOTONIC, &current);
	ms_since_beginning = diff_ms(&current, &start_time);
	printf("%s%s%s: t=%ds, rx=%lld (%lld bits/s), tx=%lld (%lld bits/s), rx err=%s%lld%s\n",
		_cl_color_output ? INFO_COLOR : NULL_COLOR,
		_cl_rx_dump ? "\n" : "",
		_cl_port, ms_since_beginning / 1000,
		_read_count, _read_count * 8 * 1000 / ms_since_beginning,
		_write_count, _write_count * 8 * 1000 / ms_since_beginning,
		_cl_color_output && _error_count > 0 ? ERROR_COLOR : NULL_COLOR,
		_error_count,
		_cl_color_output ? RESET_COLOR : NULL_COLOR);

#if SHOW_TIOCGICOUNT
	/* skip ioctl if TIOCGICOUNT was failed previously */
	if (tiocgicount_failed)
		return;

	int ret = ioctl(_fd, TIOCGICOUNT, &icount);
	if (ret < 0) {
		perror("Error getting TIOCGICOUNT");
		tiocgicount_failed = 1;
	} else {
		printf("%s: TIOCGICOUNT: ret=%i, rx=%i, tx=%i, frame = %i, overrun = %i, parity = %i, brk = %i, buf_overrun = %i\n",
				_cl_port, ret, icount.rx, icount.tx, icount.frame, icount.overrun, icount.parity, icount.brk,
				icount.buf_overrun);
	}
#endif
}

static unsigned char next_count_value(unsigned char c)
{
	c++;
	if (_cl_ascii_range && c >= 127)
		c = 32;
	return c;
}

static int process_read_data(void)
{
	unsigned char rb[_write_size * 2];
	int c = read(_fd, &rb, sizeof(rb));
	if (c > 0) {
		if (_cl_rx_dump) {
			if (_cl_rx_dump_ascii)
				dump_data_ascii(rb, c);
			else
				dump_data(rb, c);
		}

		// verify read count is incrementing
		int i;
		for (i = 0; i < c; i++) {
			if (_read_count == 0 && i==0) {
					_read_count_value = rb[i];
			} else if (rb[i] != _read_count_value) {
				if (_cl_dump_err) {
					printf("%sError, count: %lld, expected %02x, got %02x%s\n",
							_cl_color_output ? ERROR_COLOR : NULL_COLOR,
							_read_count + i, _read_count_value, rb[i],
							_cl_color_output ? RESET_COLOR : NULL_COLOR);
				}
				_error_count++;
				if (_cl_stop_on_error) {
					dump_serial_port_stats();
					exit(-EIO);
				}
				_read_count_value = rb[i];
			}
			_read_count_value = next_count_value(_read_count_value);
		}
		_read_count += c;
	}
	return c;
}

static int process_write_data(void)
{
	ssize_t count = 0;
	ssize_t actual_write_size = 0;
	int repeat = (_cl_tx_bytes == 0);

	do
	{
		if (_cl_write_after_read == 0) {
			actual_write_size = _write_size;
		} else {
			actual_write_size = _read_count > _write_count ? _read_count - _write_count : 0;
			if (actual_write_size > _write_size) {
				actual_write_size = _write_size;
			}
		}
		if (actual_write_size == 0) {
			break;
		}

		ssize_t i;
		for (i = 0; i < actual_write_size; i++) {
			_write_data[i] = _write_count_value;
			_write_count_value = next_count_value(_write_count_value);
		}

		ssize_t c = write(_fd, _write_data, actual_write_size);

		if (c < 0) {
			if (errno != EAGAIN) {
				printf("write failed - errno=%d (%s)\n", errno, strerror(errno));
			}
			c = 0;
		} else {
			repeat = 0;
		}

		count += c;

		if (c < actual_write_size) {
			_write_count_value = _write_data[c];
		}
	} while (repeat);

	_write_count += count;

	if (_cl_tx_detailed && count > 0)
		printf("wrote %zd bytes\n", count);
	return (int)count;
}


static void setup_serial_port(int baud)
{
	struct termios newtio;
	struct serial_rs485 rs485;
	int ret;

	_fd = open(_cl_port, O_RDWR | O_NONBLOCK);

	if (_fd < 0) {
		ret = -errno;
		perror("Error opening serial port");
		exit(ret);
	}

	/* Lock device file */
	if (flock(_fd, LOCK_EX | LOCK_NB) < 0) {
		ret = -errno;
		perror("Error failed to lock device file");
		exit(ret);
	}

	bzero(&newtio, sizeof(newtio)); /* clear struct for new port settings */

	/* man termios get more info on below settings */
	newtio.c_cflag = baud | CS8 | CLOCAL | CREAD;

	if (_cl_rts_cts) {
		newtio.c_cflag |= CRTSCTS;
	}

	if (_cl_2_stop_bit) {
		newtio.c_cflag |= CSTOPB;
	}

	if (_cl_parity) {
		newtio.c_cflag |= PARENB;
		if (_cl_odd_parity) {
			newtio.c_cflag |= PARODD;
		}
		if (_cl_stick_parity) {
			newtio.c_cflag |= CMSPAR;
		}
	}

	newtio.c_iflag = 0;
	newtio.c_oflag = 0;
	newtio.c_lflag = 0;

	// block for up till 128 characters
	newtio.c_cc[VMIN] = 128;

	// 0.5 seconds read timeout
	newtio.c_cc[VTIME] = 5;

	/* now clean the modem line and activate the settings for the port */
	tcflush(_fd, TCIOFLUSH);
	tcsetattr(_fd,TCSANOW,&newtio);

	/* enable/disable rs485 direction control, first check if RS485 is supported */
	if(ioctl(_fd, TIOCGRS485, &rs485) < 0) {
		if (_cl_rs485_after_delay >= 0) {
			/* error could be because hardware is missing rs485 support so only print when actually trying to activate it */
			perror("Error getting RS-485 mode");
		}
	} else {
		if (_cl_rs485_after_delay >= 0) {
			/* enable RS485 */
			rs485.flags |= SER_RS485_ENABLED | SER_RS485_RX_DURING_TX |
				(_cl_rs485_rts_after_send ? SER_RS485_RTS_AFTER_SEND : SER_RS485_RTS_ON_SEND);
			rs485.flags &= ~(_cl_rs485_rts_after_send ? SER_RS485_RTS_ON_SEND : SER_RS485_RTS_AFTER_SEND);
			rs485.delay_rts_after_send = _cl_rs485_after_delay;
			rs485.delay_rts_before_send = _cl_rs485_before_delay;
			if(ioctl(_fd, TIOCSRS485, &rs485) < 0) {
				perror("Error setting RS-485 mode");
			}
		} else {
			/* disable RS485 */
			rs485.flags &= ~(SER_RS485_ENABLED | SER_RS485_RTS_ON_SEND | SER_RS485_RTS_AFTER_SEND);
			rs485.delay_rts_after_send = 0;
			rs485.delay_rts_before_send = 0;
			if(ioctl(_fd, TIOCSRS485, &rs485) < 0) {
				//perror("Error setting RS-232 mode");
			}
		}
	}
}

static int diff_ms(const struct timespec *t1, const struct timespec *t2)
{
	struct timespec diff;

	diff.tv_sec = t1->tv_sec - t2->tv_sec;
	diff.tv_nsec = t1->tv_nsec - t2->tv_nsec;
	if (diff.tv_nsec < 0) {
		diff.tv_sec--;
		diff.tv_nsec += 1000000000;
	}
	return (diff.tv_sec * 1000 + diff.tv_nsec/1000000);
}

static int compute_error_count(void)
{
	long long int result;

	if ((_cl_no_rx == 0 && _read_count == 0) ||
		(_cl_no_tx == 0 && _write_count == 0)) {
		return 127;
	}

	if (_cl_no_rx == 1 || _cl_no_tx == 1)
		result = _error_count;
	else
		result = llabs(_write_count - _read_count) + _error_count;

	return (result > 125) ? 125 : (int)result;
}

int main(int argc, char * argv[])
{
	atexit(&exit_handler);

	process_options(argc, argv);
	int runtime_no_tx = _cl_no_tx;
	int runtime_no_rx = _cl_no_rx;

	if (!_cl_port) {
		fprintf(stderr, "ERROR: Port argument required\n");
		display_help();
		exit(-EINVAL);
	}
	if (_cl_rx_timeout > 0 && _cl_tx_delay <= 0) {
		fprintf(stderr, "ERROR: --tx-delay needed for --rx-timeout\n");
		exit(-EINVAL);
	}

	int baud = B115200;

	if (_cl_baud && !_cl_divisor)
		baud = get_baud(_cl_baud);

	if (baud <= 0 || _cl_divisor) {
		printf("NOTE: non standard baud rate, trying custom divisor\n");
		baud = B38400;
		setup_serial_port(B38400);
		set_baud_divisor(_cl_baud, _cl_divisor);
	} else {
		setup_serial_port(baud);
		/*
		 * The flag ASYNC_SPD_CUST might have already been set, so
		 * clear it to avoid confusing the kernel uart dirver.
		 */
		clear_custom_speed_flag();
	}

	set_modem_lines(_fd, _cl_loopback ? TIOCM_LOOP : 0, TIOCM_LOOP);

	if (_cl_single_byte >= 0) {
		unsigned char data[2];
		int bytes = 1;
		int written;
		data[0] = (unsigned char)_cl_single_byte;
		if (_cl_another_byte >= 0) {
			data[1] = (unsigned char)_cl_another_byte;
			bytes++;
		}
		written = write(_fd, &data, bytes);
		if (written < 0) {
			int ret = errno;
			perror("write()");
			exit(ret);
		} else if (written != bytes) {
			fprintf(stderr, "ERROR: write() returned %d, not %d\n", written, bytes);
			exit(-EIO);
		}
		return 0;
	}

	_write_size = (_cl_tx_bytes == 0) ? 1024 : _cl_tx_bytes;

	_write_data = malloc(_write_size);
	if (_write_data == NULL) {
		fprintf(stderr, "ERROR: Memory allocation failed\n");
		exit(-ENOMEM);
	}

	if (_cl_ascii_range) {
		_read_count_value = _write_count_value = 32;
	}

	struct pollfd serial_poll;
	serial_poll.fd = _fd;
	if (!runtime_no_rx) {
		serial_poll.events |= POLLIN;
	} else {
		serial_poll.events &= ~POLLIN;
	}

	if (!runtime_no_tx) {
		serial_poll.events |= POLLOUT;
	} else {
		serial_poll.events &= ~POLLOUT;
	}

	struct timespec last_stat, last_timeout, last_read, last_write;

	clock_gettime(CLOCK_MONOTONIC, &start_time);
	last_stat = start_time;
	last_timeout = start_time;
	last_read = start_time;
	last_write = start_time;

	while (!(runtime_no_rx && runtime_no_tx)) {
		struct timespec current;
		int retval = poll(&serial_poll, 1, 1000);

		clock_gettime(CLOCK_MONOTONIC, &current);

		if (retval == -1) {
			perror("poll()");
		} else if (retval) {
			if (serial_poll.revents & POLLIN) {
				if (_cl_rx_timeout) {
					if (process_read_data() > 0) {
						clock_gettime(CLOCK_MONOTONIC, &last_read);
					}
					// must keep reading until timeout
					continue;
				}
				else if (_cl_rx_delay) {
					// only read if it has been rx-delay ms
					// since the last read
					if (diff_ms(&current, &last_read) > _cl_rx_delay) {
						process_read_data();
						clock_gettime(CLOCK_MONOTONIC, &last_read);
					}
				} else {
					process_read_data();
					clock_gettime(CLOCK_MONOTONIC, &last_read);
				}
			} else {
				// not readable, check timeout
				if (_cl_rx_timeout && \
					diff_ms(&current, &last_read) < _cl_rx_timeout) {
					continue;
				}
            }

			if (serial_poll.revents & POLLOUT) {
				if (_cl_tx_delay) {
					// only write if it has been tx-delay ms
					// since the last write
					if (diff_ms(&current, &last_write) > _cl_tx_delay) {
						process_write_data();
						clock_gettime(CLOCK_MONOTONIC, &last_write);
					}
				} else {
					process_write_data();
					clock_gettime(CLOCK_MONOTONIC, &last_write);
				}
			}
		}

		// Has it been at least a second since we reported a timeout?
		if (diff_ms(&current, &last_timeout) > 1000) {
			int rx_timeout, tx_timeout;

			// Has it been over two seconds since we transmitted or received data?
			rx_timeout = (!runtime_no_rx && diff_ms(&current, &last_read) > 2000);
			tx_timeout = (!runtime_no_tx && diff_ms(&current, &last_write) > 2000);
			// Special case - we don't want to warn about receive
			// timeouts at the end of a loopback test (where we are
			// no longer transmitting and the receive count equals
			// the transmit count).
			if (runtime_no_tx && _write_count != 0 && _write_count == _read_count) {
				rx_timeout = 0;
			}

			if (rx_timeout || tx_timeout) {
				const char *s;
				if (rx_timeout) {
					printf("%s: No data received for %.1fs.",
					       _cl_port, (double)diff_ms(&current, &last_read) / 1000);
					s = " ";
				} else {
					s = "";
				}
				if (tx_timeout) {
					printf("%sNo data transmitted for %.1fs.",
					       s, (double)diff_ms(&current, &last_write) / 1000);
				}
				printf("\n");
				last_timeout = current;
			}
		}

		if (_cl_stats) {
			if (current.tv_sec - last_stat.tv_sec > DUMP_STAT_INTERVAL_SECONDS) {
				dump_serial_port_stats();
				last_stat = current;
			}
		}

		if (_cl_tx_time) {
			if (current.tv_sec - start_time.tv_sec >= _cl_tx_time) {
				_cl_tx_time = 0;
				runtime_no_tx = 1;
				serial_poll.events &= ~POLLOUT;
				printf("Stopped transmitting.\n");
			}
		}

		if (_cl_rx_time) {
			if (current.tv_sec - start_time.tv_sec >= _cl_rx_time) {
				_cl_rx_time = 0;
				runtime_no_rx = 1;
				serial_poll.events &= ~POLLIN;
				printf("Stopped receiving.\n");
			}
		}
	}

	tcdrain(_fd);
	dump_serial_port_stats();
	set_modem_lines(_fd, 0, TIOCM_LOOP);
	tcflush(_fd, TCIOFLUSH);

	return compute_error_count();
}

/**
 * nmrpflash - Netgear Unbrick Utility
 * Copyright (C) 2016 Joseph Lehner <joseph.c.lehner@gmail.com>
 *
 * nmrpflash is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * nmrpflash is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with nmrpflash.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <unistd.h>
#include <getopt.h>
#include <stdlib.h>
#include <stdio.h>
#include "nmrpd.h"

#define NMRPFLASH_SET_REGION

int verbosity = 0;

void usage(FILE *fp)
{
	fprintf(fp,
			"Usage: nmrpflash [OPTIONS...]\n"
			"\n"
			"Options (-a, -i and -f and/or -c are mandatory):\n"
			" -a <ipaddr>     IP address to assign to target device\n"
			" -c <command>    Command to run before (or instead of) TFTP upload\n"
			" -f <firmware>   Firmware file\n"
			" -F <filename>   Remote filename to use during TFTP upload\n"
			" -i <interface>  Network interface directly connected to device\n"
			" -m <mac>        MAC address of target device (xx:xx:xx:xx:xx:xx)\n"
			" -M <netmask>    Subnet mask to assign to target device\n"
			" -t <timeout>    Timeout (in milliseconds) for regular messages\n"
			" -T <timeout>    Time (seconds) to wait after successfull TFTP upload\n"
			" -p <port>       Port to use for TFTP upload\n"
#ifdef NMRPFLASH_SET_REGION
			" -R <region>     Set device region (NA, WW, GR, PR, RU, BZ, IN, KO, JP)\n"
#endif
#ifdef NMRPFLASH_TFTP_TEST
			" -U              Test TFTP upload\n"
#endif
			" -v              Be verbose\n"
			" -V              Print version and exit\n"
			" -L              List network interfaces\n"
			" -h              Show this screen\n"
			"\n"
			"Example: (run as "
#ifndef NMRPFLASH_WINDOWS
			"root"
#else
			"administrator"
#endif
			")\n\n"
#ifndef NMRPFLASH_WINDOWS
			"# nmrpflash -i eth0 -a 192.168.1.254 -f firmware.bin\n"
#else
			"C:\\> nmrpflash.exe -i net0 -a 192.168.1.254 -f firmware.bin\n"
#endif
			"\n"
			"nmrpflash %s, Copyright (C) 2016 Joseph C. Lehner\n"
			"nmrpflash is free software, licensed under the GNU GPLv3.\n"
			"Source code at https://github.com/jclehner/nmrpflash\n"
			"\n",
			NMRPFLASH_VERSION
	  );
}

#ifdef NMRPFLASH_WINDOWS
void require_admin()
{
	SID_IDENTIFIER_AUTHORITY auth = SECURITY_NT_AUTHORITY;
	PSID adminGroup = NULL;
	BOOL success = AllocateAndInitializeSid(
		&auth, 2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS,
		0, 0, 0, 0, 0, 0, &adminGroup
	);

	if (success) {
		if (CheckTokenMembership(NULL, adminGroup, &success)) {
			if (!success) {
				fprintf(stderr, "Error: must be run as administrator");
				exit(1);
			} else {
				return;
			}
		}
		FreeSid(adminGroup);
	}

	fprintf(stderr, "Warning: failed to check administrator privileges");
}
#else
void require_admin()
{
	if (getuid() != 0) {
		fprintf(stderr, "Error: must be run as root");
		exit(1);
	}
}
#endif

int main(int argc, char **argv)
{
	int c, val, max;
	int list = 0;
	struct nmrpd_args args = {
		.rx_timeout = 200,
		.ul_timeout = 120000,
		.tftpcmd = NULL,
		.file_local = NULL,
		.file_remote = NULL,
		.ipaddr = NULL,
		.ipmask = "255.255.255.0",
		.intf = NULL,
		.mac = "ff:ff:ff:ff:ff:ff",
		.op = NMRP_UPLOAD_FW,
		.port = 69,
		.region = NULL,
	};
#ifdef NMRPFLASH_WINDOWS
	WSADATA wsa;

	val = WSAStartup(MAKEWORD(2, 2), &wsa);
	if (val != 0) {
		win_perror2("WSAStartup", val);
		return 1;
	}
#endif

	opterr = 0;

	while ((c = getopt(argc, argv, "a:c:f:F:i:m:M:p:R:t:T:hLVvU")) != -1) {
		max = 0x7fffffff;
		switch (c) {
			case 'a':
				args.ipaddr = optarg;
				break;
			case 'c':
				args.tftpcmd = optarg;
				break;
			case 'f':
				args.file_local = optarg;
				break;
			case 'F':
				args.file_remote = optarg;
				break;
			case 'i':
				args.intf = optarg;
				break;
			case 'm':
				args.mac = optarg;
				break;
			case 'M':
				args.ipmask = optarg;
				break;
#ifdef NMRPFLASH_SET_REGION
			case 'R':
				args.region = optarg;
				break;
#endif
			case 'p':
			case 'T':
			case 't':
				if (c == 'p') {
					max = 0xffff;
				}

				val = atoi(optarg);
				if (val <= 0 || val > max) {
					fprintf(stderr, "Invalid numeric value for -%c.\n", c);
					return 1;
				}

				if (c == 'p') {
					args.port = val;
				} else if (c == 't') {
					args.rx_timeout = val;
				} else if (c == 'T') {
					args.ul_timeout = val * 1000;
				}

				break;
			case 'V':
				printf("nmrpflash %s\n", NMRPFLASH_VERSION);
				val = 0;
				goto out;
			case 'v':
				++verbosity;
				break;
			case 'L':
				list = 1;
				break;
				goto out;
			case 'h':
				usage(stdout);
				val = 0;
				goto out;
#ifdef NMRPFLASH_TFTP_TEST
			case 'U':
				if (args.ipaddr && args.file_local) {
					val = tftp_put(&args);
					goto out;
				}
				/* fall through */
#endif
			default:
				usage(stderr);
				val = 1;
				goto out;
		}
	}

	if (!list && ((!args.file_local && !args.tftpcmd) || !args.intf || !args.ipaddr)) {
		usage(stderr);
		return 1;
	}

	require_admin();
	val = !list ? nmrp_do(&args) : ethsock_list_all();

out:
#ifdef NMRPFLASH_WINDOWS
	WSACleanup();
#endif
	return val;
}

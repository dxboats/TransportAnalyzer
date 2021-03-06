/*
 * TransportAnalyzer: a simple WFP KMD for analyzing incoming and outgoing TCP packets.
 * Copyright (C) 2021 dxboats
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */



#ifndef TA_TCP_UTILS_H
#define TA_TCP_UTILS_H



#include "common.h"
#include "debug.h"



/*
 * Standard TCP flags.
 * See <https://en.wikipedia.org/wiki/Transmission_Control_Protocol#TCP_segment_structure> for more information.
 * 
 * This project does not handle the experimental flag "NS".
 */
#define TCP_FLAG_CWR	 0x80
#define TCP_FLAG_ECE	 0x40
#define TCP_FLAG_URG	 0x20
#define TCP_FLAG_ACK	 0x10
#define TCP_FLAG_PSH	 0x08
#define TCP_FLAG_RST	 0x04
#define TCP_FLAG_SYN	 0x02
#define TCP_FLAG_FIN	 0x01



/*
 * Breaks a 32 bit integer into 4 separate 8 bit ones separated by commas to print an IPv4 address.
 */
#define PRETTY_ADDRESS(address) \
		(address >> 24) & 0xFF, \
		(address >> 16) & 0xFF, \
		(address >> 8) & 0xFF, \
		(address) & 0xFF



/*
 * Determines if the provided flag is set by checking the TCP header data.
 * The header data is expected to begin at the pointer's address. Offsets are
 * not handled.
 */
BOOL taTCPFlagSet(_In_ PBYTE header, BYTE flag);



/*
 * Prints all the TCP flags (except for NS) that are set in the TCP header. 
 */
VOID taTCPPrintFlags(_In_ PBYTE header);



/*
 * Get's the checksum of the TCP packet. 
 */
UINT16 taTCPGetChecksum(_In_ PBYTE header);



/*
 * Put's a little-endian byte written in ones and zeroes into the provided string.
 * That string must be 9 characters long. 8 for the ones and zeroes, and 1 for the
 * string terminator.
 */
VOID taTCPPrettyByte(_In_ BYTE value, _Inout_ PCHAR string);



#endif // !TA_TCP_UTILS_H
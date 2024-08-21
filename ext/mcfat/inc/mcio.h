/*
 * ps3mca-tool - PlayStation 3 Memory Card Adaptor Software
 * Copyright (C) 2011 - jimmikaelkael <jimmikaelkael@wanadoo.fr>
 * Copyright (C) 2011 - "someone who wants to stay anonymous"
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __MCIO_H__
#define __MCIO_H__

#include <stdint.h>

struct sceMcStDateTime {
	uint8_t  Resv2;
	uint8_t  Sec;
	uint8_t  Min;
	uint8_t  Hour;
	uint8_t  Day;
	uint8_t  Month;
	uint16_t Year;
} __attribute__((packed));

struct io_stat {
	uint32_t mode;
	uint32_t attr;
	uint32_t size;
	struct sceMcStDateTime ctime;
	struct sceMcStDateTime mtime;
} __attribute__((packed));

struct io_dirent {
	struct io_stat stat;
	char name[256];
	uint32_t unknown;
} __attribute__((packed));

void Card_DataChecksum(uint8_t *pagebuf, uint8_t *ecc);
int mcio_init(void);
int mcio_mcDetect(void);
int mcio_mcGetInfo(int *pagesize, int *blocksize, int *cardsize, int *cardflags);
int mcio_mcGetAvailableSpace(int *cardfree);
int mcio_mcOpen(char *filename, int flag);
int mcio_mcClose(int fd);
int mcio_mcRead(int fd, void *buf, int length);
int mcio_mcWrite(int fd, void *buf, int length);
int mcio_mcSeek(int fd, int offset, int origin);
int mcio_mcGetCluster(int fd);
int mcio_mcCreateCrossLinkedFile(char *real_filename, char *dummy_filename);
int mcio_mcDopen(char *dirname);
int mcio_mcDclose(int fd);
int mcio_mcDread(int fd, struct io_dirent *dirent);
int mcio_mcMkDir(char *dirname);
int mcio_mcReadPage(int pagenum, void *buf);
int mcio_mcUnformat(void);
int mcio_mcFormat(void);
int mcio_mcRemove(char *filename);
int mcio_mcRmDir(char *dirname);

/* MC error codes */
#define sceMcResSucceed			 0
#define sceMcResChangedCard		-1
#define sceMcResNoFormat		-2
#define sceMcResFullDevice		-3
#define sceMcResNoEntry			-4
#define sceMcResDeniedPermit		-5
#define sceMcResNotEmpty		-6
#define sceMcResUpLimitHandle		-7
#define sceMcResFailReplace		-8
#define sceMcResFailResetAuth		-11
#define sceMcResFailDetect		-12
#define sceMcResFailDetect2		-13
#define sceMcResFailReadCluster		-21
#define sceMcResFailCheckBackupBlocks	-47
#define sceMcResFailIO			-48
#define sceMcResFailSetDeviceSpecs	-49
#define sceMcResDeniedPS1Permit		-51
#define sceMcResFailAuth		-90
#define sceMcResNotDir			-100
#define sceMcResNotFile			-101

/* file attributes */
#define sceMcFileAttrReadable         0x0001
#define sceMcFileAttrWriteable        0x0002
#define sceMcFileAttrExecutable       0x0004
#define sceMcFileAttrDupProhibit      0x0008
#define sceMcFileAttrFile             0x0010
#define sceMcFileAttrSubdir           0x0020
#define sceMcFileCreateDir            0x0040
#define sceMcFileAttrClosed           0x0080
#define sceMcFileCreateFile           0x0200
#define sceMcFile0400		      0x0400
#define sceMcFileAttrPDAExec          0x0800
#define sceMcFileAttrPS1              0x1000
#define sceMcFileAttrHidden           0x2000
#define sceMcFileAttrExists           0x8000

#endif


/**
 * Functions for reading SpaceX ID.
 */

#ifndef SPACEX_SXID_H
#define SPACEX_SXID_H

#define SXID_VERSION_CUR 1

#define MAX_NUM_PORTS 16

/**
 * SXID structure layout
 */
typedef struct sxid_s {
	u8 id[4];			/* 0x00 - 0x03 EEPROM Tag 'SXID' */
	u32 version;			/* 0x04 - 0x08 Version */
	u32 vehicle_id;			/* 0x08 - 0x0c Vehicle ID */
	u32 slot_id;			/* 0x0c - 0x10 Device slot location */
	u8 sn[16];			/* 0x10 - 0x20 Board Serial Number */
	u8 asn[16];			/* 0x20 - 0x30 Assembly Serial Number */
	u8 mac[MAX_NUM_PORTS][6];	/* 0x30 - 0xac MAC addresses */
	u8 rfu[108];			/*	  - 0xfb pad out */
	u32 crc;			/* 0xfc - 0xff CRC check */
} __attribute__ ((__packed__)) sxid_t;

int sxid_read_from_flash(unsigned long offset,
			 unsigned long length,
			 sxid_t *sxid);

int sxid_read_from_mmc(unsigned int dev, unsigned int part,
		       unsigned long offset, unsigned long length,
		       sxid_t *sxid);

#endif  /* !SPACEX_COMMON_H */

/* Writes an empty ADFS new-map hard disc, since HForm is not in the ROM.
 *
 * Layout: boot block at 0xC00, then the two map copies and the root directory
 * starting at the first bit of the middle zone, which is where FileCore looks.
 * Map and directory checksums were validated against a disc RISC OS formatted. */
#include <string.h>
#include "cpu.h"

#define SECTOR 512
#define ZONE_BITS (SECTOR * 8)
#define ZONE_SPARE 32
#define ZONE0_BITS 480 /* the disc record sits in zone 0 */
#define LOG2_BPMB 10
#define BPMB (1u << LOG2_BPMB)
#define ID_LEN 15
#define ROOT_ID 2
#define BAD_ID 1
#define DIR_BYTES 2048
#define MAX_ZONES 120 /* keeps the root's sector offset within one byte */
#define BOOT_ADDR 0xC00u

static void put32(u8 *p, u32 v)
{
	p[0] = (u8)v;
	p[1] = (u8)(v >> 8);
	p[2] = (u8)(v >> 16);
	p[3] = (u8)(v >> 24);
}

static u32 get32(const u8 *p)
{
	return p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

static void put_bits(u8 *zone, u32 pos, u32 value, int count)
{
	int i;
	for (i = 0; i < count; i++, pos++) {
		if (value & (1u << i))
			zone[pos >> 3] |= (u8)(1u << (pos & 7));
	}
}

static u8 zone_check(const u8 *b)
{
	u32 s0 = 0, s1 = 0, s2 = 0, s3 = 0;
	int r;
	for (r = SECTOR - 4; r > 0; r -= 4) {
		s0 += b[r] + (s3 >> 8);
		s3 &= 255;
		s1 += b[r + 1] + (s0 >> 8);
		s0 &= 255;
		s2 += b[r + 2] + (s1 >> 8);
		s1 &= 255;
		s3 += b[r + 3] + (s2 >> 8);
		s2 &= 255;
	}
	s0 += s3 >> 8;
	s1 += b[1] + (s0 >> 8);
	s2 += b[2] + (s1 >> 8);
	s3 += b[3] + (s2 >> 8);
	return (u8)((s0 ^ s1 ^ s2 ^ s3) & 255);
}

static u8 boot_check(const u8 *b)
{
	u32 sum = 0, carry = 0;
	int i;
	for (i = 510; i >= 0; i--) {
		sum = sum + b[i] + carry;
		carry = sum >> 8;
		sum &= 255;
	}
	return (u8)sum;
}

static u32 dir_rotate(u32 h)
{
	return (h >> 13) | (h << 19);
}

static u8 dir_check(const u8 *b)
{
	u32 h = 0;
	int i;
	/* An empty directory checks one header word, one byte, then the tail */
	h = dir_rotate(h) ^ get32(b);
	h = dir_rotate(h) ^ b[4];
	for (i = DIR_BYTES - 40; i < DIR_BYTES - 4; i += 4)
		h = dir_rotate(h) ^ get32(b + i);
	return (u8)((h ^ (h >> 8) ^ (h >> 16) ^ (h >> 24)) & 255);
}

static void write_record(u8 *r, u32 zones, u32 bytes, u32 root)
{
	static const char name[] = "SDCard";
	r[0] = 9; /* log2 sector size */
	r[1] = HD_SPT;
	r[2] = HD_HEADS;
	r[3] = 0;
	r[4] = ID_LEN;
	r[5] = LOG2_BPMB;
	r[9] = (u8)zones;
	r[10] = ZONE_SPARE;
	put32(r + 12, root);
	put32(r + 16, bytes);
	r[20] = 0x5A;
	r[21] = 0xA5;
	memset(r + 22, ' ', 10);
	memcpy(r + 22, name, sizeof(name) - 1);
}

static u32 zone_count(u32 sectors)
{
	u32 bits = sectors / (BPMB / SECTOR);
	u32 zones = (bits + ZONE0_BITS) / (ZONE_BITS - ZONE_SPARE);
	return zones > MAX_ZONES ? MAX_ZONES : zones;
}

/* 1 formatted, 0 readable but not ours, -1 could not tell. Only 0 may ever lead to
 * an offer to format: a bad read must not put a good disc at risk. */
int hd_is_formatted(void)
{
	static u8 b[SECTOR];
	const u8 *r = b + 0x1C0;
	int tries;
	for (tries = 0; tries < 3; tries++)
		if (!plat_hd_read(BOOT_ADDR / SECTOR, b))
			break;
	if (tries == 3) {
		plat_log("boot block unreadable\n");
		return -1;
	}
	plat_log("boot block check %02X/%02X log2sec=%d idlen=%d zones=%d\n", boot_check(b), b[SECTOR - 1],
		r[0], r[4], r[9]);
	return boot_check(b) == b[SECTOR - 1] && r[0] == 9 && r[4] >= 10 && r[9] != 0;
}

int hd_format(u32 sectors)
{
	static u8 zone[SECTOR], rec[60], boot[SECTOR], dir[DIR_BYTES];
	u32 zones = zone_count(sectors);
	u32 total_bits, bytes, mid = zones / 2, map_bit, map_addr, map_bits, root, z, s;
	u8 cross = 0;
	if (zones < 3)
		return -1;
	total_bits = zones * (ZONE_BITS - ZONE_SPARE) - ZONE0_BITS;
	bytes = total_bits * BPMB;
	map_bit = mid * (ZONE_BITS - ZONE_SPARE) - ZONE0_BITS;
	map_addr = map_bit * BPMB;
	/* The map fragment holds both map copies and the root directory */
	map_bits = (2 * zones * SECTOR + DIR_BYTES + BPMB - 1) / BPMB;
	if (map_bits < ID_LEN + 1)
		map_bits = ID_LEN + 1;
	/* Root: object 2, sector offset past the map copies, counted from one */
	root = ((u32)ROOT_ID << 8) | (2 * zones + 1);

	memset(rec, 0, sizeof(rec));
	write_record(rec, zones, bytes, root);

	for (z = 0; z < zones; z++) {
		u32 first = ZONE_SPARE + (z ? 0 : ZONE0_BITS), free_at = first;
		memset(zone, 0, sizeof(zone));
		if (!z) {
			memcpy(zone + 4, rec, sizeof(rec));
			/* Boot block area is marked unusable; first fragment must cover it */
			put_bits(zone, first, BAD_ID, ID_LEN);
			put_bits(zone, first + ID_LEN, 1, 1);
			free_at = first + ID_LEN + 1;
		} else if (z == mid) {
			put_bits(zone, first, ROOT_ID, ID_LEN);
			put_bits(zone, first + map_bits - 1, 1, 1);
			free_at = first + map_bits;
		}
		/* Everything else is one free fragment: its id field is the next-free
		 * link, zero for the last, and its final bit is the end marker. */
		put_bits(zone, ZONE_BITS - 1, 1, 1);
		zone[1] = (u8)(free_at - 8);
		zone[2] = (u8)(((free_at - 8) >> 8) | 0x80);
		zone[3] = z == zones - 1 ? (u8)(cross ^ 0xFF) : 0;
		cross ^= zone[3];
		zone[0] = zone_check(zone);
		for (s = 0; s < 2; s++)
			if (plat_hd_write(map_addr / SECTOR + s * zones + z, zone))
				return -1;
	}

	memset(dir, 0, sizeof(dir));
	dir[0] = 1;
	memcpy(dir + 1, "Hugo", 4);
	dir[DIR_BYTES - 41 + 0] = 0; /* last entry mark */
	dir[DIR_BYTES - 38] = (u8)root;
	dir[DIR_BYTES - 37] = (u8)(root >> 8);
	dir[DIR_BYTES - 36] = (u8)(root >> 16);
	memcpy(dir + DIR_BYTES - 35, "$", 2);
	memcpy(dir + DIR_BYTES - 16, "$", 2);
	dir[DIR_BYTES - 6] = 1;
	memcpy(dir + DIR_BYTES - 5, "Hugo", 4);
	dir[DIR_BYTES - 1] = dir_check(dir);
	for (s = 0; s < DIR_BYTES / SECTOR; s++)
		if (plat_hd_write((map_addr + 2 * zones * SECTOR) / SECTOR + s, dir + s * SECTOR))
			return -1;

	memset(boot, 0, sizeof(boot));
	put32(boot, 0x20000000u); /* empty defect list */
	memcpy(boot + 0x1C0, rec, sizeof(rec));
	boot[SECTOR - 1] = boot_check(boot);
	return plat_hd_write(BOOT_ADDR / SECTOR, boot);
}

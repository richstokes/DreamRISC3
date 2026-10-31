/* Native floppy driver behind the WD1772 registers. RISC OS's own ADFS and FileCore
 * run unchanged; sectors come from an 800KB ADFS E image held in RAM, so writes
 * last until power off. Transfers run as fast as the kernel takes each byte. */
#include <stdlib.h>
#include <string.h>
#include "cpu.h"

#define TRACKS 80
#define SIDES 2
#define SECTORS 5
#define SECTOR_BYTES 1024
#define DISC_BYTES (TRACKS * SIDES * SECTORS * SECTOR_BYTES)
#define TRACK_BYTES 6250
#define ID_INTERVAL_US 40000 /* 300rpm, five sectors a track */

#define ST_BUSY 0x01
#define ST_DRQ 0x02
#define ST_TRACK0 0x04
#define ST_RNF 0x10
#define ST_SPIN 0x20
#define ST_MOTOR 0x80

#define FIQ_DATA 0x01
#define FIQ_IRQ 0x02

enum { XFER_NONE, XFER_READ, XFER_WRITE, XFER_FORMAT };

static struct {
	u8 *image;
	u8 status, track, sector, data, command;
	int cur_track, step_dir, side, drive, motor, double_density;
	int xfer;
	u32 pos, len;
	u8 id[6];
	u8 *buffer;
	uint64_t due;
	int reading_id;
	u32 rotation;
	u8 *format;
} f;

static u8 track0_bit(void)
{
	return f.cur_track ? 0 : ST_TRACK0;
}

static int sector_valid(void)
{
	return f.image && !f.drive && f.double_density && f.cur_track < TRACKS &&
	       f.track == f.cur_track && f.sector < SECTORS;
}

static u8 *sector_ptr(int track, int side, int sector)
{
	return f.image + (size_t)(track * SIDES + side) * SECTORS * SECTOR_BYTES +
	       (size_t)sector * SECTOR_BYTES;
}

static void finish(u8 status)
{
#ifdef FDC_TRACE
	plat_log("fdc done %02X status %02X pos=%u\n", f.command, status, f.pos);
#endif
	f.status = status;
	f.xfer = XFER_NONE;
	f.reading_id = 0;
	mach_fiq(FIQ_IRQ);
}

static void schedule(u32 us)
{
	f.due = plat_time_us() + us;
}

static void next_read_byte(void)
{
	if (f.pos < f.len) {
		f.data = f.buffer[f.pos++];
		f.status |= ST_DRQ;
		mach_fiq(FIQ_DATA);
	} else {
		schedule(20);
	}
}

/* Write-track stream: ID fields follow F5 F5 F5 FE, data follows F5 F5 F5 FB. */
static void apply_format(void)
{
	u32 i = 0, end = f.pos;
	int track = -1, side = 0, sector = -1;
	u8 *b = f.format;
	while (i + 4 < end) {
		if (b[i] == 0xF5 && b[i + 1] == 0xF5 && b[i + 2] == 0xF5) {
			if (b[i + 3] == 0xFE && i + 8 < end) {
				track = b[i + 4];
				side = b[i + 5];
				sector = b[i + 6];
				i += 8;
				continue;
			}
			if (b[i + 3] == 0xFB && i + 4 + SECTOR_BYTES <= end) {
				if (track >= 0 && track < TRACKS && side < SIDES && sector >= 0 &&
				    sector < SECTORS)
					memcpy(sector_ptr(track, side, sector), b + i + 4, SECTOR_BYTES);
				i += 4 + SECTOR_BYTES;
				continue;
			}
		}
		i++;
	}
}

void fdc_tick(uint64_t now)
{
	if (!f.due || now < f.due)
		return;
	f.due = 0;
	switch (f.command >> 4) {
	case 0x0:
		f.cur_track = f.track = 0;
		finish(ST_MOTOR | ST_TRACK0);
		break;
	case 0x1:
		f.cur_track = f.track = f.data;
		finish(ST_MOTOR | track0_bit());
		break;
	case 0x2: case 0x3: case 0x4: case 0x5: case 0x6: case 0x7:
		if (f.command & 0x10)
			f.track = (u8)f.cur_track;
		finish(ST_MOTOR | track0_bit());
		break;
	case 0x8:
		if (f.pos == 0 && f.xfer == XFER_READ) {
			if (!sector_valid()) {
				finish(ST_MOTOR | ST_RNF);
				break;
			}
			f.buffer = sector_ptr(f.cur_track, f.side, f.sector);
			next_read_byte();
		} else {
			finish(ST_MOTOR);
		}
		break;
	case 0xA:
		finish(ST_MOTOR);
		break;
	case 0xC:
		if (f.pos == 0 && f.reading_id) {
			if (!f.image || f.drive || !f.double_density) {
				finish(ST_MOTOR | ST_RNF);
				break;
			}
			f.id[0] = (u8)f.cur_track;
			f.id[1] = (u8)f.side;
			f.id[2] = (u8)(f.rotation++ % SECTORS); /* sectors pass under the head */
			f.id[3] = 3;
			f.id[4] = f.id[5] = 0;
			f.buffer = f.id;
			f.xfer = XFER_READ;
			next_read_byte();
		} else {
			f.sector = (u8)f.cur_track;
			finish(ST_MOTOR);
		}
		break;
	case 0xF:
		apply_format();
		finish(ST_MOTOR);
		break;
	}
}

void fdc_write(u32 a, u32 v)
{
	switch (a & 0xC) {
	case 0x0:
		if ((f.status & ST_BUSY) && (v >> 4) != 0xD)
			return;
#ifdef FDC_TRACE
		plat_log("fdc cmd %02X trk=%d/%d sec=%d data=%d side=%d drive=%d motor=%d dens=%d\n", (unsigned)v, f.cur_track, f.track, f.sector, f.data, f.side, f.drive, f.motor, f.double_density);
#endif
		f.command = (u8)v;
		f.pos = 0;
		f.xfer = XFER_NONE;
		switch (v >> 4) {
		case 0x2: case 0x3:
			f.cur_track += f.step_dir;
			break;
		case 0x4: case 0x5:
			f.cur_track++;
			f.step_dir = 1;
			break;
		case 0x6: case 0x7:
			f.cur_track--;
			f.step_dir = -1;
			break;
		case 0xD:
			f.due = 0;
			f.xfer = XFER_NONE;
			f.status = ST_MOTOR | track0_bit();
			if (v & 8)
				mach_fiq(FIQ_IRQ);
			return;
		}
		if (f.cur_track < 0)
			f.cur_track = 0;
		switch (v >> 4) {
		case 0x8:
			f.xfer = XFER_READ;
			f.len = SECTOR_BYTES;
			f.status = ST_MOTOR | ST_BUSY;
			schedule(200);
			break;
		case 0xA:
			if (!sector_valid()) {
				f.status = ST_MOTOR | ST_BUSY;
				f.command = 0x80;
				f.xfer = XFER_READ; /* reports record not found */
				schedule(200);
				break;
			}
			f.xfer = XFER_WRITE;
			f.len = SECTOR_BYTES;
			f.buffer = sector_ptr(f.cur_track, f.side, f.sector);
			f.status = ST_MOTOR | ST_BUSY | ST_DRQ;
			mach_fiq(FIQ_DATA);
			break;
		case 0xC:
			/* IDs must arrive at the rotation rate: ADFS collects a track's worth
			 * until a timer says one revolution has passed. */
			f.reading_id = 1;
			f.len = 6;
			f.status = ST_MOTOR | ST_BUSY;
			schedule(ID_INTERVAL_US);
			break;
		case 0xF:
			if (!f.format)
				f.format = malloc(TRACK_BYTES);
			f.xfer = XFER_FORMAT;
			f.len = TRACK_BYTES;
			f.status = ST_MOTOR | ST_BUSY | ST_DRQ;
			mach_fiq(FIQ_DATA);
			break;
		default:
			f.status = ST_MOTOR | ST_SPIN | ST_BUSY | track0_bit();
			schedule(50);
			break;
		}
		break;
	case 0x4:
		f.track = (u8)v;
		break;
	case 0x8:
		f.sector = (u8)v;
		break;
	case 0xC:
		f.data = (u8)v;
		mach_fiq_clear(FIQ_DATA);
		f.status &= ~ST_DRQ;
		if (f.xfer == XFER_WRITE || f.xfer == XFER_FORMAT) {
			u8 *out = f.xfer == XFER_WRITE ? f.buffer : f.format;
			out[f.pos++] = (u8)v;
			if (f.pos < f.len) {
				f.status |= ST_DRQ;
				mach_fiq(FIQ_DATA);
			} else {
				schedule(20);
			}
		}
		break;
	}
}

u32 fdc_read(u32 a)
{
	u32 v;
	switch (a & 0xC) {
	case 0x0:
		mach_fiq_clear(FIQ_IRQ);
		return f.status;
	case 0x4:
		return f.track;
	case 0x8:
		return f.sector;
	default:
		v = f.data;
		mach_fiq_clear(FIQ_DATA);
		f.status &= ~ST_DRQ;
		if (f.xfer == XFER_READ)
			next_read_byte();
		return v;
	}
}

void fdc_latch_a(u32 v)
{
	if (!(v & 1)) f.drive = 0;
	if (!(v & 2)) f.drive = 1;
	if (!(v & 4)) f.drive = 2;
	if (!(v & 8)) f.drive = 3;
	f.side = (v & 0x10) ? 0 : 1;
	f.motor = !(v & 0x20);
}

void fdc_latch_b(u32 v)
{
	f.double_density = !(v & 2);
}

int fdc_ready(void)
{
	return f.motor && f.image && !f.drive;
}

u8 *fdc_image(void)
{
	return f.image;
}

void fdc_init(void)
{
	f.status = ST_MOTOR;
	f.step_dir = 1;
	f.image = plat_disc_load(DISC_BYTES); /* platforms skip this when an SD card holds the data */
}

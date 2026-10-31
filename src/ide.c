/* Native ATA drive behind the IDE registers that ROM ADFS drives. Sectors map one
 * to one onto the storage the platform provides, the serial-port SD card on the
 * Dreamcast. A real drive goes busy between sectors, and ADFS depends on that: an
 * interrupt raised while it is still in its handler would be lost, so each sector
 * is presented on a later tick. */
#include <string.h>
#include "cpu.h"

#define ST_ERR 0x01
#define ST_DRQ 0x08
#define ST_DSC 0x10
#define ST_READY 0x40
#define ST_BUSY 0x80

#define ERR_ABORT 0x04
#define ERR_IDNF 0x10

#define IRQB_IDE 0x08
#define SECTOR 512
#define BUSY_US 40

enum { IDLE, READING, WRITING, IDENTIFY };

static struct {
	u32 sectors;
	u8 status, error, count, sector, cyl_low, cyl_high, drive_head, control;
	int heads, spt;
	int mode, pos;
	u32 lba, remaining;
	uint64_t due;
	int pending_write_done;
	int irq_pending;
	u8 buffer[SECTOR] __attribute__((aligned(4)));
} d;

/* The drive holds its interrupt while the host has it masked with nIEN */
static void raise_irq(void)
{
	d.irq_pending = 1;
	if (!(d.control & 2))
		mach_irqb(IRQB_IDE);
}

static void fail(u8 error)
{
	d.error = error;
	d.status = ST_READY | ST_DSC | ST_ERR;
	d.mode = IDLE;
	raise_irq();
}

static void done(void)
{
	d.status = ST_READY | ST_DSC;
	d.error = 0;
	d.mode = IDLE;
	raise_irq();
}

static u32 current_lba(void)
{
	u32 cyl = d.cyl_low | ((u32)d.cyl_high << 8);
	return (cyl * (u32)d.heads + (d.drive_head & 15u)) * (u32)d.spt + d.sector - 1u;
}

static void store_lba(u32 lba)
{
	u32 track = lba / (u32)d.spt, cyl = track / (u32)d.heads;
	d.sector = (u8)(lba % (u32)d.spt + 1);
	d.drive_head = (u8)((d.drive_head & 0xF0) | (track % (u32)d.heads));
	d.cyl_low = (u8)cyl;
	d.cyl_high = (u8)(cyl >> 8);
}

static void read_next(void)
{
	if (d.lba >= d.sectors || plat_hd_read(d.lba, d.buffer)) {
		fail(ERR_IDNF);
		return;
	}
	d.pos = 0;
	d.mode = READING;
	d.status = ST_READY | ST_DSC | ST_DRQ;
	raise_irq();
}

static void go_busy(void)
{
	d.status = ST_BUSY;
	d.mode = IDLE;
	d.due = plat_time_us() + BUSY_US;
}

static void put_id_string(u8 *out, const char *text, int bytes)
{
	int i, n = (int)strlen(text);
	for (i = 0; i < bytes; i++)
		out[i ^ 1] = (u8)(i < n ? text[i] : ' '); /* ATA strings swap each pair */
}

static void identify(void)
{
	u32 cylinders = d.sectors / (HD_HEADS * HD_SPT);
	u8 *b = d.buffer;
	memset(b, 0, SECTOR);
	b[0] = 0x40;
	b[2] = (u8)cylinders;
	b[3] = (u8)(cylinders >> 8);
	b[6] = HD_HEADS;
	b[12] = HD_SPT;
	put_id_string(b + 20, "DREAMRISC", 20);
	put_id_string(b + 46, "1.0", 8);
	put_id_string(b + 54, "Dreamcast SD", 40);
	d.pos = 0;
	d.mode = IDENTIFY;
	d.status = ST_READY | ST_DSC | ST_DRQ;
	raise_irq();
}

static void command(u32 v)
{
#ifdef IDE_TRACE
	plat_log("ide cmd %02X count=%d sec=%d cyl=%d dh=%02X lba=%u\n", (unsigned)v, d.count, d.sector,
		d.cyl_low | (d.cyl_high << 8), d.drive_head, (unsigned)current_lba());
#endif
	if (d.drive_head & 0x10) { /* no slave drive */
		d.status = 0;
		return;
	}
	d.irq_pending = 0;
	mach_irqb_clear(IRQB_IDE);
	d.error = 0;
	switch (v & 0xF0) {
	case 0x10: /* recalibrate */
		d.cyl_low = d.cyl_high = 0;
		done();
		return;
	case 0x70: /* seek */
		done();
		return;
	}
	switch (v) {
	case 0x20: case 0x21:
		d.lba = current_lba();
		d.remaining = d.count ? d.count : 256;
		go_busy();
		break;
	case 0x30: case 0x31:
		d.lba = current_lba();
		d.remaining = d.count ? d.count : 256;
		d.pos = 0;
		d.mode = WRITING;
		d.status = ST_READY | ST_DSC | ST_DRQ;
		break;
	case 0x40: case 0x41: /* verify */
		if (current_lba() >= d.sectors)
			fail(ERR_IDNF);
		else
			done();
		break;
	case 0x90: /* diagnostics */
		d.error = 1;
		d.status = ST_READY | ST_DSC;
		d.mode = IDLE;
		raise_irq();
		break;
	case 0x91: /* initialise parameters */
		d.heads = (d.drive_head & 15) + 1;
		d.spt = d.count ? d.count : HD_SPT;
		done();
		break;
	case 0xEC:
		identify();
		break;
	case 0xE0: case 0xE1: case 0xE2: case 0xE3: case 0xE5: case 0xE6: case 0xEF:
		done();
		break;
	default:
		fail(ERR_ABORT);
		break;
	}
}

static void data_written(void)
{
	if (d.lba >= d.sectors || plat_hd_write(d.lba, d.buffer)) {
		fail(ERR_IDNF);
		return;
	}
	d.lba++;
	store_lba(d.lba);
	d.remaining--;
	d.pending_write_done = 1;
	go_busy();
}

void ide_tick(uint64_t now)
{
	if (!d.due || now < d.due)
		return;
	d.due = 0;
	if (!d.pending_write_done) {
		read_next();
	} else {
		d.pending_write_done = 0;
		if (d.remaining) {
			d.pos = 0;
			d.mode = WRITING;
			d.status = ST_READY | ST_DSC | ST_DRQ;
			raise_irq();
		} else {
			done();
		}
	}
}

int ide_owns(u32 a)
{
	return d.sectors && a >= 0x30107C0u && a < 0x3011000u;
}

u32 ide_read(u32 a, int word)
{
	u32 reg = (a - 0x30107C0u) >> 2;
	if (a >= 0x3010FD8u) /* alternate status */
		return d.status;
	switch (reg) {
	case 0:
		if (d.mode == READING || d.mode == IDENTIFY) {
			u32 v;
			if (word) {
				v = d.buffer[d.pos] | ((u32)d.buffer[d.pos + 1] << 8);
				d.pos += 2;
			} else {
				v = d.buffer[d.pos++];
			}
			if (d.pos >= SECTOR) {
				if (d.mode == IDENTIFY) {
					d.mode = IDLE;
					d.status = ST_READY | ST_DSC;
				} else {
					d.lba++;
					store_lba(d.lba);
					if (--d.remaining) {
						go_busy();
					} else {
						d.mode = IDLE;
						d.status = ST_READY | ST_DSC;
					}
				}
			}
			return v;
		}
		return 0;
	case 1: return d.error;
	case 2: return d.count;
	case 3: return d.sector;
	case 4: return d.cyl_low;
	case 5: return d.cyl_high;
	case 6: return d.drive_head;
	case 7:
		d.irq_pending = 0;
		mach_irqb_clear(IRQB_IDE);
		return d.status;
	}
	return 0xFF;
}

void ide_write(u32 a, u32 v, int word)
{
	u32 reg = (a - 0x30107C0u) >> 2;
	if (a >= 0x3010FD8u) {
		d.control = (u8)v;
		if (d.control & 2)
			mach_irqb_clear(IRQB_IDE);
		else if (d.irq_pending)
			mach_irqb(IRQB_IDE);
		return;
	}
	switch (reg) {
	case 0:
		if (d.mode == WRITING && d.status & ST_DRQ) {
			d.buffer[d.pos++] = (u8)v;
			if (word)
				d.buffer[d.pos++] = (u8)(v >> 8);
			if (d.pos >= SECTOR)
				data_written();
		}
		break;
	case 1: break;
	case 2: d.count = (u8)v; break;
	case 3: d.sector = (u8)v; break;
	case 4: d.cyl_low = (u8)v; break;
	case 5: d.cyl_high = (u8)v; break;
	case 6: d.drive_head = (u8)v; break;
	case 7: command(v & 0xFF); break;
	}
}

void ide_init(u32 sectors)
{
	d.sectors = sectors;
	d.heads = HD_HEADS;
	d.spt = HD_SPT;
	d.status = ST_READY | ST_DSC;
	d.sector = 1;
}

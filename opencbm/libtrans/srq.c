/*
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 *
 *  Copyright      2020 Spiro Trikaliotis
*/

#include "opencbm.h"
#include "libtrans_int.h"

#include <assert.h>
#include <stdlib.h>

#include "arch.h"

static const unsigned char srq1571_drive_prog[] = {
#include "srq1571.inc"
};

static int
upload(CBM_FILE fd, unsigned char drive)
{
    enum cbm_device_type_e driveType;
    const unsigned char *srq_drive_prog = 0;
    unsigned int srq_drive_prog_length = 0;
    unsigned int bytesWritten;

    if (cbm_identify(fd, drive, &driveType, NULL))
        return 1;

    switch (driveType)
    {
    case cbm_dt_cbm1541:
        DBG_ERROR((DBG_PREFIX "1541 not supported!"));
        return 1;

    case cbm_dt_cbm1581:
        DBG_ERROR((DBG_PREFIX "1581 not supported yet!"));
        return 1;

    case cbm_dt_cbm1570:
    case cbm_dt_cbm1571:
        DBG_PRINT((DBG_PREFIX "recognized 1571."));
        srq_drive_prog = srq1571_drive_prog;
        srq_drive_prog_length = sizeof(srq1571_drive_prog);
        break;

    case cbm_dt_unknown:
        /* FALL THROUGH */

    default:
        DBG_ERROR((DBG_PREFIX "unknown device type!"));
        return 1;
    }

    /* make sure our routine fits into 256 byte; otherwise, we will overwrite
     * the job memory at $00-$05, which will result in the floppy trying to access
     * tracks that do not exist. This could do severe harm to the floppy!
     */
    DBG_ASSERT(srq_drive_prog_length < 0x100);
    assert(srq_drive_prog_length < 0x100);

    bytesWritten = cbm_upload(fd, drive, 0x700, srq_drive_prog, srq_drive_prog_length);

    if (bytesWritten != srq_drive_prog_length)
    {
        DBG_ERROR((DBG_PREFIX "wanted to write %u bytes, but only %u "
            "bytes could be written", srq_drive_prog_length, bytesWritten));

        return 1;
    }

    return 0;
}

static int
init(CBM_FILE fd, unsigned char drive)
{
    //! \todo for the time being, we only wait some time for the floppy. Replace with a proper handshake
    arch_sleep(1);
    return 0;
}

static int
read1byte(CBM_FILE fd, unsigned char *c1)
{
    int ret = 0;
                                                                        SETSTATEDEBUG(DebugByteCount = -6401);
    *c1 = cbm_srq_burst_read(fd);
                                                                        SETSTATEDEBUG(DebugByteCount = -1);
    return ret;
}

static int
read2byte(CBM_FILE fd, unsigned char *c1, unsigned char *c2)
{
    int ret = 0;
                                                                        SETSTATEDEBUG(DebugByteCount = -6401);
    *c1 = cbm_srq_burst_read(fd);
    *c2 = cbm_srq_burst_read(fd);
                                                                        SETSTATEDEBUG(DebugByteCount = -1);
    return ret;
}

static int
readblock(CBM_FILE fd, unsigned char *p, unsigned int length)
{
#ifdef USE_HANDSHAKED_READ_BLOCK
                                                                        SETSTATEDEBUG(DebugByteCount = 0);
    for (; length < 0x100; length++)
    {
                                                                        SETSTATEDEBUG(DebugByteCount += 1);
        read1byte(fd, p++);
    }
                                                                        SETSTATEDEBUG(DebugByteCount = -1);
                                                                        SETSTATEDEBUG((void)0);
    return 0;
#else
    return cbm_srq_burst_read_track(fd, p, 0x100 - length);
#endif
}

static int
write1byte(CBM_FILE fd, unsigned char c1)
{
    int ret = 0;
                                                                        SETSTATEDEBUG(DebugByteCount = -6401);
    cbm_srq_burst_write(fd, c1);
                                                                        SETSTATEDEBUG(DebugByteCount = -1);
    return ret;
}

static int
write2byte(CBM_FILE fd, unsigned char c1, unsigned char c2)
{
    int ret = 0;
                                                                        SETSTATEDEBUG(DebugByteCount = -12801);
    cbm_srq_burst_write(fd, c1);
    cbm_srq_burst_write(fd, c2);
                                                                        SETSTATEDEBUG(DebugByteCount = -1);
    return ret;
}

static int
writeblock(CBM_FILE fd, unsigned char *p, unsigned int length)
{
                                                                        SETSTATEDEBUG(DebugByteCount = 0);
    for (; length < 0x100; ++length)
    {
                                                                        SETSTATEDEBUG(DebugByteCount += 2);
        cbm_srq_burst_write(fd, *p++);
    }

                                                                        SETSTATEDEBUG(DebugByteCount = -1);
    return 0;
}

DECLARE_TRANSFER_FUNCS(srq);

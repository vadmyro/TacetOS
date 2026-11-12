/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 vadmyro */

/* Writes the two fixed blocks of the Classic DFU image (header signature, 16
 * bytes, and certificate area, 698 bytes, zeros after the data the tool has) out
 * of the reference tool's code, for build-dfu-parts.sh. The tool's file is included at compile time and is never
 * copied. The script gives the names with -D:
 *   REF_SOURCE      the file that holds the blocks, as a string
 *   REF_HEADER      the structure that holds the signature
 *   REF_SIGNATURE   its signature field
 *   REF_CERTIFICATE the array of the certificate area
 * Usage: extract-dfu-blocks SIGNATURE_FILE CERTIFICATE_FILE */

#include REF_SOURCE

static int put(const char *path, const unsigned char *data, size_t size)
{
    FILE *f = fopen(path, "wb");
    if (!f || fwrite(data, 1, size, f) != size) {
        fprintf(stderr, "extract-dfu-blocks: cannot write %s\n", path);
        return 1;
    }
    return fclose(f) != 0;
}

int main(int argc, char **argv)
{
    if (argc != 3) {
        fprintf(stderr, "usage: extract-dfu-blocks SIGNATURE_FILE CERTIFICATE_FILE\n");
        return 2;
    }
    /* The array in the tool's code is shorter than the area: the tool's image starts
     * from zeros, so the rest of the area is zeros. */
    static unsigned char certificate[698];
    if (sizeof(REF_HEADER.REF_SIGNATURE) != 16 || sizeof(REF_CERTIFICATE) > sizeof(certificate)) {
        fprintf(stderr, "extract-dfu-blocks: the blocks do not have the expected sizes\n");
        return 1;
    }
    memcpy(certificate, REF_CERTIFICATE, sizeof(REF_CERTIFICATE));
    return put(argv[1], (const unsigned char *)REF_HEADER.REF_SIGNATURE, 16) ||
           put(argv[2], certificate, sizeof(certificate));
}

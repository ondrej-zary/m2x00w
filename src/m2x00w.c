/*
 * m2x00w converts the pbmraw or pksmraw output of ghostscript
 * (www.ghostscript.com) into the format of the Minolta Magicolor
 * 2300W/2400W Winprinter
 *
 * Copyright 2004 Leif Birkenfeld (leif@birkenfeld-home.de)
 *           2005 Thomas Rohringer (thoroh@users.sourceforge.net)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <libgen.h>

#define DIV_ROUND_UP(n, d) (((n) + (d) - 1) / (d))
#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

#define cpu_to_le16(x) (x)
#define cpu_to_le32(x) (x)

FILE *in_stream, *out_stream;

int verb = 0;			/* verbose level */

#define DBG(level, fmt, args ...)	if (verb > level) fprintf(stderr, fmt, ##args);

enum m2x00w_model { M2300W = 0x82, M2400W = 0x85, M2500W = 0x87 };
enum m2x00w_model model;
int MediaCode = 0;
int PaperCode = 4;
int ResXmul = 1;
enum m2x00w_res_y { RES_300DPI = 0x00, RES_600DPI, RES_1200DPI };
enum m2x00w_res_y res_y = RES_600DPI;
enum mx200w_res_x { RES_MULT1 = 0x00, RES_MULT2, RES_MULT4 };
enum mx200w_res_x res_x = RES_MULT1;
int colorMode = 0;
int thisPageColorMode = 0;

int saveToner = 0;

int linesPerBlock;
unsigned short blocksPerPage;
unsigned short thisPageBlocksPerPage;
unsigned int resBreite;
unsigned int resHoehe;

int jobHeaderWritten = 0;
int headerCount = 0;		/* zaehler fuer alle header */
int siteInitHeaderCount = 0;	/* zaehler fuer alle header */
int page_block_seq;   /* saved sequence number for page block */

long pix[4] = { 0, 0, 0, 0 };	/* pixel counter (C,M,Y,K) */



#define M2X00W_MAGIC		0x1B
struct header {
    unsigned char magic;	/* 0x1B */
    unsigned char type;		/* block type */
    unsigned char seq;		/* block sequence number */
    unsigned short len;		/* data length (little endian) */
    unsigned char type_inv;	/* type ^ 0xff */
    unsigned char data[0];
} __attribute__((packed));

#define M2X00W_BLOCK_BEGIN	0x40
#define M2X00W_BLOCK_PARAMS	0x50
#define M2X00W_BLOCK_PAGE	0x51
#define M2X00W_BLOCK_DATA	0x52
#define M2X00W_BLOCK_ENDPART	0x55
#define M2X00W_BLOCK_END	0x41

struct block_begin {
    unsigned char model;	/* 0x82 = 2300W, 0x85 = 2400W, 0x87 = 2500W */
    unsigned char color;	/* 0x10 */
} __attribute__((packed));

struct block_params {
    unsigned char res_y;	/* 00=300dpi, 01=600dpi, 02=1200dpi */
    unsigned char res_x;	/* 00=res_y, 01=2*res_y, 02=4*res_y */
    unsigned char zeros[6];
} __attribute__((packed));

struct block_page {
    unsigned char color;	/* 0xF0 = color, 0x80 = BW (2400W/2500W), 0x00 = BW (2300W) */
    unsigned char copies;	/* number of copies (only 2500W, always 01 for 2300W/2400W) */
    unsigned short x_start;
    unsigned short x_end;
    unsigned short y_start;
    unsigned short y_end;
    unsigned short blocks1;	/* blocks per page */
    unsigned short blocks2;	/* blocks per page (again?) */
    unsigned char tray;
    unsigned char paper_size;
    unsigned short custom_width;/* custom size - width in mm */
    unsigned short custom_height;/* custom size - height in mm */
    unsigned char zero1;
    unsigned char duplex;	/* 0x80 = duplex on (2300W only)*/
    unsigned char paper_weight;
    unsigned char zero2;
    unsigned char unknown;	/* 01 for 2300W or ???, else 00 */
    unsigned char zeros[3];
} __attribute__((packed));

struct block_data {
    unsigned int data_len;
    unsigned char color;	/* 00=K, 01=C, 02=M, 03=Y */
    unsigned char block_cnt;
    unsigned short lines;	/* lines per block */
} __attribute__((packed));

struct block_endpart {
    unsigned char type;		/* 00=end job 10=wait for button (manual duplex) */
} __attribute__((packed));

struct format
{
    char desc[32];
    int resX;
    int resY;
};

/*
2300W:
 0x04: A4
 0x06: B5 (JIS)
 0x08: A5
 0x0C: Japanese postcard
 0x12: folio
 0x19: legal
 0x1A: government legal
 0x1B: letter
 0x1F: executive
 0x21: statement
 0x24: envelope monarch
 0x25: envelope COM10
 0x26: envelope DL
 0x27: envelope C5
 0x28: envelope C6
 0x29: B5 (ISO)
 0x31: CUSTOM (+foolscap + kai 16 + kai 32 + letter plus + uk quarto)


2500W:
 0x04: A4
 0x06: B5 (JIS)
 0x08: A5
 0x0C: J postcard
 0x0D: double postcard
 0x0F: envelope You #4
 0x12: folio
 0x15: Kai-32
 0x19: legal
 0x1A: G.legal
 0x1B: letter
 0x1D: G.letter
 0x1F: executive
 0x21: statement
 0x24: envelope monarch
 0x25: envelope COM10
 0x26: envelope DL
 0x27: envelope C5
 0x28: envelope C6
 0x29: B5 (ISO)
 0x2D: envelope Chou #3
 0x2E: envelope Chou #4
 0x31: CUSTOM
 0x46: foolscap
 0x51: 16K
 0x52: Kai-16
 0x53: letter plus
 0x54: UK quarto
 0x65: photo 10x15 = photo 4x6"
 */
struct format form_2300[42] = {
/* 0*/ {"", 0, 0},
/* 1*/ {"", 0, 0},
/* 2*/ {"", 0, 0},
/* 3*/ {"", 0, 0},
/* 4*/ {"A4", 4752, 6808},
/* 5*/ {"", 0, 0},
/* 6*/ {"B5 JIS", 4088, 5856},
/* 7*/ {"", 0, 0},
/* 8*/ {"A5", 3288, 4752},
/* 9*/ {"", 0, 0},
/* a*/ {"", 0, 0},
/* b*/ {"", 0, 0},
/* c*/ {"", 0, 0},
/* d*/ {"", 0, 0},
/* e*/ {"", 0, 0},
/* f*/ {"", 0, 0},
/*10*/ {"", 0, 0},
/*11*/ {"", 0, 0},
/*12*/ {"Folio", 4752, 7584},
/*13*/ {"", 0, 0},
/*14*/ {"", 0, 0},
/*15*/ {"", 0, 0},
/*16*/ {"", 0, 0},
/*17*/ {"", 0, 0},
/*18*/ {"", 0, 0},
/*19*/ {"Legal", 4896, 8136},
/*1a*/ {"Government Legal", 4896, 7592},
/*1b*/ {"Letter", 4896, 6392},
/*1c*/ {"", 0, 0},
/*1d*/ {"", 0, 0},
/*1e*/ {"", 0, 0},
/*1f*/ {"Executive", 6096, 4144},
/*20*/ {"", 0, 0},
/*21*/ {"Statement", 3096, 4896},
/*22*/ {"", 0, 0},
/*23*/ {"", 0, 0},
/*24*/ {"Kuvert Monarch", 2120, 4296},
/*25*/ {"Kuver #10", 2272, 5496},
/*26*/ {"Kuvert DL", 2392, 4992},
/*27*/ {"Kuvert C5", 3616, 5200},
/*28*/ {"Kuvert C6", 2480, 3616},
/*29*/ {"B5 ISO", 3944, 5696},
};

struct format form_2400[42] = {
/* 0*/ {"", 0, 0},
/* 1*/ {"", 0, 0},
/* 2*/ {"", 0, 0},
/* 3*/ {"", 0, 0},
/* 4*/ {"A4", 4752, 6784},
/* 5*/ {"", 0, 0},
/* 6*/ {"B5 JIS", 4096, 5856},
/* 7*/ {"", 0, 0},
/* 8*/ {"A5", 3280,4736},
/* 9*/ {"", 0, 0},
/* a*/ {"", 0, 0},
/* b*/ {"", 0, 0},
/* c*/ {"", 0, 0},
/* d*/ {"", 0, 0},
/* e*/ {"", 0, 0},
/* f*/ {"", 0, 0},
/*10*/ {"", 0, 0},
/*11*/ {"", 0, 0},
/*12*/ {"Folio", 4752, 7584},
/*13*/ {"", 0, 0},
/*14*/ {"", 0, 0},
/*15*/ {"", 0, 0},
/*16*/ {"", 0, 0},
/*17*/ {"", 0, 0},
/*18*/ {"", 0, 0},
/*19*/ {"Legal", 4896, 8192},
/*1a*/ {"Government Legal", 4896, 7584},
/*1b*/ {"Letter", 4896, 6368},
/*1c*/ {"", 0, 0},
/*1d*/ {"", 0, 0},
/*1e*/ {"", 0, 0},
/*1f*/ {"Executive", 4144, 6080},
/*20*/ {"", 0, 0},
/*21*/ {"Statement", 3088,4896},
/*22*/ {"", 0, 0},
/*23*/ {"", 0, 0},
/*24*/ {"Kuvert Monarch", 2112, 4288},
/*25*/ {"Kuver #10", 2272, 5472},
/*26*/ {"Kuvert DL", 2384, 4992},
/*27*/ {"Kuvert C5", 3616, 5184},
/*28*/ {"Kuvert C6", 2480, 3616},
/*29*/ {"B5 ISO", 3952, 5696},
};

struct format *form;

/* noch nicht eingebaute

31;Letter Plus;4896;7408;216;322;8;926;
31;UK Quadro;4592;5792;203;254;8;724;
31;Druckerpapier;4592;7592;203;330;8;949;

c0;Japan Postkarte;2152;3288;0;0;8;411;
*/



struct media
{
    char desc[32];
};

struct media med[] = {
/* 0*/ {"Plain"},
/* 1*/ {"Thick"},
/* 2*/ {"Transparency"},
/* 3*/ {"Envelope"},
/* 4*/ {"Letterhead"},
/* 5*/ {"Postcard"},
/* 6*/ {"Label"},
/* 7*/ {""},
/* 8*/ {"Glossy"},	/* 2500W only */
};

struct steuerFelder
{
    unsigned int bytesIn;
    unsigned int linesOut;
    unsigned int curLinePos;
    unsigned int blocksOut;

    unsigned char *encBuffer;
    unsigned long indexEncBuffer;

    unsigned int rleCount;
    unsigned short lastByte;

    unsigned char *blockBuffer;
    unsigned long indexBlockBuffer;

    unsigned char *pageOut;
    unsigned long indexPageOut;

    unsigned char *lineBuffer;
    unsigned long indexLineBuffer;

};


struct steuerFelder stFeld[4] = {
    {0, 0, 0, 0, NULL, 0, 0, 0, NULL, 0, NULL, 0, NULL, 0},
    {0, 0, 0, 0, NULL, 0, 0, 0, NULL, 0, NULL, 0, NULL, 0},
    {0, 0, 0, 0, NULL, 0, 0, 0, NULL, 0, NULL, 0, NULL, 0},
    {0, 0, 0, 0, NULL, 0, 0, 0, NULL, 0, NULL, 0, NULL, 0}
};

void (*encode)(int inByte, int colorID, struct steuerFelder *stFeld);

void
Help (void)
{
    int ho;

    fprintf (stderr,
	     "\nm2300w Version 0.1_1 (2004-05-19), Copyright (C) 2004  Leif Birkenfeld\n"
	     "This Software comes with ABSOLUTELY NO WARRANTY\n"
	     "This is free software, and you are welcome to redistribute it under certain conditions.\n"
	     "See COPYING file for further information !\n\n"
	     "Usage:           foo22300w -i xxxxx.pbm -o xxxxx.prn -c x\n"
	     "\n"
	     "Options:\n"
	     "-i inFile         Filename to read from \n"
	     "                        Use \"-\" to read from stdin\n");
    fprintf (stderr,
	     "-o outfile        Filename to write into file\n"
	     "                        Use \"-\" to write to stdout\n"
	     "                        (Using stdout sets -v 0)\n"
	     "-c mode           B/W or Color Mode [no default]\n"
	     "                        1 - Black and White\n"
	     "                        2 - Color\n"
	     "-m media          Media code:  [%d]\n", MediaCode);
    for (ho = 0; ho < ARRAY_SIZE(med); ho++) {
        if (strlen(med[ho].desc) > 0)
	    fprintf (stderr,
		 "                       %2d - %s\n", ho, med[ho].desc);
    }
    fprintf (stderr, "-p paper          Paper code:  [%d]\n", PaperCode);

    for (ho = 0; ho < 42; ho++) {
	if (form[ho].resX != 0) {
	    fprintf (stderr,
		     "                        %2d - %s\n", ho, form[ho].desc);
	}
    }

    fprintf (stderr,
	     "-r x              Resolution mode: [%d]\n"
	     "                        1 -  600x600 dpi\n"
	     "                        2 - 1200x600 dpi\n"
	     "			      3 - 2400x600 dpi\n"
	     "-s                Save Toner\n"
	     "                        Discards every second pixel in a\n",
	     ResXmul);
    fprintf (stderr,
	     "                        chequerboard order to save 50 percent toner\n"
	     "                        It works fine for text and graphic, but\n"
	     "                        it dosn't for photos.\n"
	     "-v x              Verbose level [%d]\n"
	     "                        Please write as first Option \n"
	     "                        More than 5 will be extrem !\n",
	     verb);
}

unsigned char checksum(void *p, int length)
{
    int i;
    unsigned char sum = 0;
    unsigned char *data = p;

    for (i = 0; i < length; i++) {
	sum += *data;
	data++;
    }

    return sum;
}

void hex_dump(int level, char *title, void *p, int length)
{
    int i;
    unsigned char *data = p;

    if (verb > level) {
	    fprintf (stderr, "%s", title);
	    for (i = 0; i < length; i++) {
	        fprintf (stderr, " %.2hhx", *data);
	        data++;
	    }
	    fprintf (stderr, "\n");
    }
}

void write_block(unsigned char block_type, void *data, unsigned char data_len, FILE *stream, void *out)
{
    struct header header;
    unsigned char sum;

    header.magic = M2X00W_MAGIC;
    header.type = block_type;
    header.seq = headerCount++;
    header.len = cpu_to_le16(data_len);
    header.type_inv = block_type ^ 0xff;
    sum = checksum(&header, sizeof(header)) + checksum(data, data_len);

    if (stream) {
	fwrite(&header, 1, sizeof(header), stream);
	fwrite(data, 1, data_len, stream);
	fwrite(&sum, 1, sizeof(sum), stream);
    } else if (out) {
	memcpy(out, &header, sizeof(header));
	out += sizeof(header);
	memcpy(out, data, data_len);
	out += data_len;
	memcpy(out, &sum, sizeof(sum));
    }
}

void
encodeToBlockBuffer (int colorID, struct steuerFelder *stFeld)
{
    long rohBytes = stFeld->indexEncBuffer - stFeld->rleCount;
    void *encBuffer = stFeld->encBuffer;

    DBG(5, "--> Ausgabe von %.3i Bytes fuer colorID %i davon %.3i Rohbytes und %.3i mal %.2x RLE encoded am Ende.\n",
	(int) stFeld->indexEncBuffer, (int) colorID, (int) rohBytes, (int) stFeld->rleCount, stFeld->lastByte);
    hex_dump(5, "Daten fuer Encoder:\n", stFeld->encBuffer, stFeld->indexEncBuffer);
    /* uncompressed bytes */
    while (rohBytes > 0) {
	unsigned char chunk = (rohBytes > 64) ? 64 : rohBytes;
	DBG(5, "Segment mit %d Bytes ausgeben ...", chunk);
	stFeld->blockBuffer[stFeld->indexBlockBuffer++] = chunk - 1;
	memcpy (&stFeld->blockBuffer[stFeld->indexBlockBuffer], encBuffer, chunk);
	encBuffer += chunk;
	rohBytes -= chunk;
	stFeld->indexBlockBuffer += chunk;
	DBG(5, "OK\n");
	hex_dump(5, "Rohbytes:\n", &stFeld->blockBuffer[stFeld->indexBlockBuffer - chunk - 1], chunk + 1);
    }

    /* RLE */
    if (stFeld->rleCount >= 4096) {
        /* encode 4096B run as two 2048B runs (happens only on 2400W at 2400dpi) */
        stFeld->blockBuffer[stFeld->indexBlockBuffer++] = 0xe0;
        stFeld->blockBuffer[stFeld->indexBlockBuffer++] = stFeld->lastByte;
        stFeld->blockBuffer[stFeld->indexBlockBuffer++] = 0xe0;
        stFeld->blockBuffer[stFeld->indexBlockBuffer++] = stFeld->lastByte;
        stFeld->rleCount -= 4096;
    }
    if (stFeld->rleCount / 64 > 0) {
	stFeld->blockBuffer[stFeld->indexBlockBuffer++] = 192 + stFeld->rleCount / 64;
	stFeld->blockBuffer[stFeld->indexBlockBuffer++] = stFeld->lastByte;
	stFeld->rleCount -= stFeld->rleCount / 64 * 64;
    }
    if (stFeld->rleCount > 0) {
	stFeld->blockBuffer[stFeld->indexBlockBuffer++] = 128 + stFeld->rleCount;
	stFeld->blockBuffer[stFeld->indexBlockBuffer++] = stFeld->lastByte;
    }

    stFeld->rleCount = 0;

    DBG(5, "--->RLE Encode for %i done.\n", colorID);
    stFeld->indexEncBuffer = 0;
}

void
doEncode (int inByte, int colorID, struct steuerFelder *stFeld)
{
    if (stFeld->bytesIn == 0) {
	stFeld->linesOut++;
	stFeld->curLinePos = stFeld->indexBlockBuffer;
	/* table compression not implemented: write an empty table */
	stFeld->blockBuffer[stFeld->indexBlockBuffer++] = 0x80;
    }
    if (saveToner > 0) {
	/* spar Toner indem es jedes 2te bit loescht. */
	/* die loeschung erfolgt pro zeile versetzt (schachbrettmuster) */
	if ((model != M2400W && (stFeld->linesOut % 2 > 0)) || 
	   (model == M2400W && ((stFeld->bytesIn + 1) > (resBreite / 8)))) {
	    inByte = inByte & 0xaa;
	}
	else {
	    inByte = inByte & 0x55;
	}
    }

    if (inByte == stFeld->lastByte || stFeld->rleCount < 1) {
	stFeld->rleCount++;	/* zaehlen wie oft ein byte wiederholt wird */
    }
    else {
	if (stFeld->rleCount > 3) {	/* rle ausgabe lohnt sich, deshalb ausgeben */
	    encodeToBlockBuffer (colorID, stFeld);
	    /* wenn sich das ausgeben nicht lohnt, werden die aufgelaufenen rles verworfen und als normale bytes gewertet */
	}
	stFeld->rleCount = 0;
    }
    stFeld->lastByte = inByte;

    stFeld->bytesIn++;	/* EingabeByte hochzaehlen und byyte auf den buffer legen */
    stFeld->encBuffer[stFeld->indexEncBuffer] = inByte;
    stFeld->indexEncBuffer++;


    if ((stFeld->bytesIn + 1) > ((model == M2400W ? 2 : 1) * (resBreite / 8))) {
	/* eine zeile ist voll. den rest im buffer codieren */

	encodeToBlockBuffer (colorID, stFeld);
	stFeld->bytesIn = 0;
	if (model == M2500W) {
            int rowbytes = stFeld->indexBlockBuffer - stFeld->curLinePos;
	    char pad_header[2];
	    /* compute padding length for the row length to be multiple of 4 */
            char padlen = (4 - ((rowbytes + sizeof(pad_header)) % 4)) % 4;
            char padding[] = { 0xff, 0xff, 0xff };
            int rowlen = rowbytes + sizeof(pad_header) + padlen;

            pad_header[0] = (padlen << 6) | ((rowlen >> 8) & 0x3f);
            pad_header[1] = rowlen & 0xff;
	    /* make space for pad_header and padding */
	    memmove(stFeld->blockBuffer + stFeld->curLinePos + 1 + sizeof(pad_header) + padlen, stFeld->blockBuffer + stFeld->curLinePos + 1, rowbytes);
	    /* insert pad_header and padding */
	    memcpy(stFeld->blockBuffer + stFeld->curLinePos + 1, pad_header, sizeof(pad_header));
	    memcpy(stFeld->blockBuffer + stFeld->curLinePos + 1 + sizeof(pad_header), padding, padlen);
	    stFeld->blockBuffer[stFeld->curLinePos] |= 0x40;
	    stFeld->indexBlockBuffer += sizeof(pad_header) + padlen;
	}
	/* wenn die anzahl der zeilen f|r einen block erreicht ist muss der blockheader generiert werden */
	/* dies kann erst jetzt geschehen, da die anzahl der im block befindlichen bytes im header steht */
	if (stFeld->linesOut >= (linesPerBlock / (model == M2400W ? 2 : 1))) {
	    int tmp = headerCount;
	    struct block_data header = {
		.data_len = cpu_to_le32(stFeld->indexBlockBuffer),
		.color = colorID,
		.block_cnt = ++stFeld->blocksOut,
		.lines = cpu_to_le16(linesPerBlock),
	    };
	    DBG(4, "Blockheader fuer Block %i mit %5i Bytes generieren und Block raus kopieren...\n",
			 (int) stFeld->blocksOut, (int) stFeld->indexBlockBuffer);
	    /* die berechnung der headerCount muss sicherstellen das die reichenfolge stimmt */
	    if (thisPageColorMode == 0xf0) {
		headerCount = siteInitHeaderCount + stFeld->blocksOut - 1 + ((3 - colorID) * 8);
	    }
	    else {
		headerCount = siteInitHeaderCount + stFeld->blocksOut - 1;
	    }
	    DBG(4, "BlockHeader.headerCount fuer colorID %i ist %i\n", colorID, headerCount);
	    write_block(M2X00W_BLOCK_DATA, &header, sizeof(header), NULL, &stFeld->pageOut[stFeld->indexPageOut]);

	    stFeld->indexPageOut += sizeof(struct header) + sizeof(header) + 1;
	    headerCount = tmp + 1;
	    hex_dump(4, "Blockheader: ", &header, sizeof(header));

	    memcpy (&stFeld->pageOut[stFeld->indexPageOut],
		    &stFeld->blockBuffer[0],
		    stFeld->indexBlockBuffer);
	    stFeld->indexPageOut += stFeld->indexBlockBuffer;

	    stFeld->linesOut = 0;
	    stFeld->indexBlockBuffer = 0;
	}

    }


}

void
prepDoEncode(int inByte, int colorID, struct steuerFelder *stFeld)
{
    stFeld->lineBuffer[stFeld->indexLineBuffer] =  inByte;
    stFeld->indexLineBuffer += 1;

    if (stFeld->indexLineBuffer == (resBreite / 4))  {
        int z;
        for (z = 0; z < (resBreite / 8); z++)  {
            doEncode (stFeld->lineBuffer[z], colorID, stFeld);
            doEncode (stFeld->lineBuffer[z + (resBreite / 8)], colorID, stFeld);
        }
        stFeld->indexLineBuffer = 0;
    }

}

void
clearBuffer (int i)
{
/* initialisiert die stucktur[i] */
    stFeld[i].bytesIn = 0;
    stFeld[i].linesOut = 0;
    stFeld[i].blocksOut = 0;
    stFeld[i].indexEncBuffer = 0;
    stFeld[i].rleCount = 0;
    stFeld[i].lastByte = 0;
    stFeld[i].indexBlockBuffer = 0;
    stFeld[i].indexPageOut = 0;
    stFeld[i].indexLineBuffer = 0;
}

void
writeJobHeader (void)
{
    struct block_begin begin = { .model = model, .color = 0x10 };
    struct block_params params = { .res_y = res_y, .res_x = res_x };
    write_block(M2X00W_BLOCK_BEGIN, &begin, sizeof(begin), out_stream, NULL);
    write_block(M2X00W_BLOCK_PARAMS, &params, sizeof(params), out_stream, NULL);
    DBG(1, "JobHeader written.\n");
}

void
writePageHeader (void)
{
    struct block_page page = {
	.color = thisPageColorMode,
	.copies = 1,
	.x_end = cpu_to_le16(resBreite),
	.y_end = cpu_to_le16(resHoehe),
	.blocks1 = cpu_to_le16(thisPageBlocksPerPage),
	.blocks2 = cpu_to_le16(thisPageBlocksPerPage),
	.paper_size = PaperCode,
	.paper_weight = MediaCode,
	.unknown = (model == M2300W) ? 0x01 : 0x00,
    };
    int tmp = headerCount;

    headerCount = page_block_seq;
    write_block(M2X00W_BLOCK_PAGE, &page, sizeof(page), out_stream, NULL);
    headerCount = tmp;
    hex_dump(4, "Seitenheader: ", &page, sizeof(page));
}


void
readPkmraw (void)
{

    int c = 0;


    int ccs = 0;

    /* die reichenfolge der farben in der eingabedatei ist verkehrt */
    /* die ausgabe erfolgt in der reihenfolge 3,2,1,0 */
    /* 3 - gelb , 2 - magenta , 1 - cyan , 0 - black */
    unsigned short colorKey[4] = { 1, 2, 3, 0 };

    int readHeader;
    char buffer[256];


    while ((fgets (buffer, sizeof(buffer), in_stream)) != NULL) {
	int inpX = 0;
	int inpY = 0;
	int inpXBytes = 0;
	unsigned char lastByteMask = 0;

	int shz, sbz;
	int realLines, realPixBytes;
	int leadLines, leadPixBytes;
	int trailLines, trailPixBytes;
	int xAdd, yAdd;

	DBG(2, "gelesener Header: %s", buffer);

	/* grobe prueffung - geht eigentlich besser */
	if (buffer[0] != 'P' || buffer[1] != '4') {
	    DBG(0, "Erwartet 'P4' but found >%s<\n", buffer);
	    exit (1);
	}

	/* wenn die seite voll ist wird sie ausgegeben */
	/* eine blde art die farbseiten zu zaehlen - blocksperpage ist bei farbe 32 (was dann 4 farbseiten ergibt) */
	/* bei sw ist es 8, also nur eine farbseite */
	if (ccs >= (blocksPerPage / 8)) {
	    writePageHeader ();
	    DBG(1, "###Seitenheader geschrieben\n");

	    if (thisPageColorMode == 0xf0) {
		fwrite(stFeld[3].pageOut, 1, stFeld[3].indexPageOut, out_stream);
		fwrite(stFeld[2].pageOut, 1, stFeld[2].indexPageOut, out_stream);
		fwrite(stFeld[1].pageOut, 1, stFeld[1].indexPageOut, out_stream);
		clearBuffer (1);
		clearBuffer (2);
		clearBuffer (3);
	    }
	    fwrite(stFeld[0].pageOut, 1, stFeld[0].indexPageOut, out_stream);
	    clearBuffer (0);

	    ccs = 0;
	    DBG(1, "###Seiteninhalt geschrieben\n");
	    DBG(1, "Farbverteilung der Seite:\nYellow:  %8d\nMagenta: %8d\nCyan:    %8d\nBlack:   %8d\n",
		pix[0],pix[1],pix[2],pix[3]);
	    pix[0]=0;
	    pix[1]=0;
	    pix[2]=0;
	    pix[3]=0;



	}
	/* vor der seite seitenheader nicht vergessen */
	if (ccs == 0) {
	    if (jobHeaderWritten != 1) {
		writeJobHeader ();
		jobHeaderWritten = 1;
	    }
	    page_block_seq = headerCount++;
	    siteInitHeaderCount = headerCount;
	    thisPageColorMode=colorMode;
	    thisPageBlocksPerPage=blocksPerPage;
	    DBG(1, "Reservierter SeitenHeaderCount ist %2d\n", page_block_seq);

	}
	DBG(1, "Beginne neue Farbe\n");
	
	if(ccs>2) {
	    if(pix[0]==0 && pix[1]==0 && pix[2]==0) {
		DBG(1, "--------------- Switch to Black and White !\n");
		
		thisPageColorMode=(model == M2300W) ? 0x00 : 0x80;
		thisPageBlocksPerPage=0x08;
		headerCount=headerCount-24;
		siteInitHeaderCount = headerCount;
		clearBuffer (1);
		clearBuffer (2);
		clearBuffer (3);
	    }else{
		DBG(1, "--------------- Seite bleibt in Farbe\n");
	    }
		
	}

	/* weitere pbm header ueberlesen - koennte mann sptereigentlich auch auswerten */

	for (readHeader = 0; readHeader < 1; readHeader++) {
	    if (fgets (buffer, 256, in_stream) != NULL) {
		DBG(3, "gelesener Header: %s", buffer);
		if (buffer[0] == '#') {
		    DBG(3, "Zeile ist ein Kommentar und zaehlt nicht mit.\n");	/* diese Ausage muss noch geprueft werden ! */
		    readHeader--;
		}
		else {
		    /* format auslesen */

		    inpX = atoi (strtok (buffer, " "));
		    inpY = atoi (strtok (NULL, " "));
		    inpXBytes = DIV_ROUND_UP(inpX, 8);
		    lastByteMask = 0xff << ((inpXBytes * 8) - inpX);
		}
	    }
	    else {
		DBG(0, "Unexpected end of in-file !?\n");
		exit (1);
	    }
	}


	shz = 0;
	sbz = 0;
	/* hier werden jetzt die bytes gelesen */

	realLines = 0;
	realPixBytes = 0;
	leadLines = 0;
	leadPixBytes = 0;
	trailLines = 0;
	trailPixBytes = 0;
	xAdd = 0;
	yAdd = 0;

	if (resBreite <= inpX) {	/* eingabe brreiter als druckbereich - clippen */
	    realPixBytes = (resBreite / 8);
	    leadPixBytes = DIV_ROUND_UP(inpXBytes - (resBreite / 8), 2);
	    trailPixBytes = (inpXBytes - (resBreite / 8)) / 2;
	    DBG(5, "Eingabeseite zu breit - clippe auf %i mit %i am Anfang und %i am Ende.\n",
		realPixBytes, leadPixBytes, trailPixBytes);
	}
	else {			/* hinzufuegen */
	    realPixBytes = inpXBytes;
	    leadPixBytes = DIV_ROUND_UP((resBreite / 8) - inpXBytes, 2);
	    trailPixBytes = ((resBreite / 8) - inpXBytes) / 2;
	    xAdd = 1;
	    DBG(5, "Eingabeseite zu schmal - addiere auf %i -> %i am Anfang und %i am Ende.\n",
		realPixBytes, leadPixBytes, trailPixBytes);
	}

	if (resHoehe <= inpY) {	/* eingabe hoeher als druckbereich - clippen */
	    realLines = resHoehe;
	    leadLines = DIV_ROUND_UP(inpY - resHoehe, 2);
	    trailLines = (inpY - resHoehe) / 2;
	    DBG(5, "Eingabeseite zu lang - clippe auf %i mit %i am Anfang und %i am Ende.\n",
		realLines, leadLines, trailLines);
	}
	else {			/* hinzufuegen */
	    realLines = inpY;
	    leadLines = DIV_ROUND_UP(resHoehe - inpY, 2);
	    trailLines = (resHoehe - inpY) / 2;
	    yAdd = 1;
	    DBG(5, "Eingabeseite zu kurz - addiere auf %i -> %i am Anfang und %i am Ende.\n",
		realLines, leadLines, trailLines);
	}

	if (yAdd == 0) {
	    DBG(5, "ueberlese %i zeilen mit je %i bytes am Anfang\n", leadLines, inpXBytes);
	    for (shz = 0; shz < leadLines; shz++) {	/* vorschleife fuer  zu ueberlesende hohenzeilen */
		for (sbz = 0; sbz < inpXBytes; sbz++) {
		    if ((c = fgetc (in_stream)) != EOF) {
			/* verwerfe das byte */
		    }
		    else {
			DBG(0, "\nUps, unerwartetes Ende\n");
			exit (1);
		    }
		}
	    }

	}
	else {
	    DBG(5, "erzeuge %i zeilen mit je %i bytes (0x00) am Anfang\n", leadLines, (resBreite / 8));
	    for (shz = 0; shz < leadLines; shz++) {	/* vorschleife fuer fehlende hohenzeilen */
		for (sbz = 0; sbz < (resBreite / 8); sbz++) {
		    if (colorMode == 0xf0)
		        encode (0x00, colorKey[ccs], &stFeld[colorKey[ccs]]);
		    else
		        encode (0x00, colorKey[3], &stFeld[colorKey[3]]);
		}
	    }
	}

	for (shz = 0; shz < realLines; shz++) {
	    if (xAdd == 0) {
		DBG(5, "ueberlese %i bytes der Zeile am Anfang\n", leadPixBytes);
		for (sbz = 0; sbz < leadPixBytes; sbz++) {	/* vorschleife fuer zu ueberlesende breiten pixel */
		    if ((c = fgetc (in_stream)) != EOF) {
			/* verwerfe das byte */
		    }
		    else {
			DBG(0, "\nUps, unexpected End\n");
			exit (1);
		    }
		}
	    }
	    else {
		DBG(5, "erzeuge %i bytes mit 0x00 am Anfang\n", leadPixBytes);
		for (sbz = 0; sbz < leadPixBytes; sbz++) {
		    if (colorMode == 0xf0) 
		       encode (0x00, colorKey[ccs], &stFeld[colorKey[ccs]]);
		    else
		       encode (0x00, colorKey[3], &stFeld[colorKey[3]]);
		}

	    }

	    DBG(5, "verabreite normal %i bytes\n", realPixBytes);
	    for (sbz = 0; sbz < realPixBytes; sbz++) {
		if ((c = fgetc (in_stream)) != EOF) {
		    if ((sbz + 1) == realPixBytes) {
			DBG(6, "letzes Byte maskieren mit %.2x - aus %.2x wird ", lastByteMask, c);
			c = c & lastByteMask;	/* das letzte byte muss bei ungeraden und zu kleinen eingaben maskiert werden */
			DBG(6, "%.2x\n", c);
		    }
		    if (colorMode == 0xf0) {
			/* hier die pixel zaehlen um s/w seiten zu erkennen */
                        if (c & 0x80)
                            pix[ccs]++;
                        if (c & 0x40)
                            pix[ccs]++;
                        if (c & 0x20)
                            pix[ccs]++;
                        if (c & 0x10)
                            pix[ccs]++;
                        if (c & 0x08)
                            pix[ccs]++;
                        if (c & 0x04)
                            pix[ccs]++;
                        if (c & 0x02)
                            pix[ccs]++;
                        if (c & 0x01)
                            pix[ccs]++;
                        encode (c, colorKey[ccs], &stFeld[colorKey[ccs]]);
		    }
		    else {
		        encode (c, colorKey[3], &stFeld[colorKey[3]]);
		    }
		}
		else {
		    DBG(0, "\nUps, unexpected End\n");
		    exit (1);
		}

	    }

	    if (xAdd == 0) {
		DBG(5, "ueberlese %i bytes der Zeile am Ende\n", trailPixBytes);
		for (sbz = 0; sbz < trailPixBytes; sbz++) {	/* vorschleife fuer zu ueberlesende breiten pixel */
		    if ((c = fgetc (in_stream)) != EOF) {
			/* verwerfe das byte */
		    }
		    else {
			DBG(5, "\nUps, unexpected End\n");
			exit (1);
		    }
		}
	    }
	    else {
		DBG(5, "erzeuge %i bytes mit 0x00 am Ende\n", trailPixBytes);
		for (sbz = 0; sbz < trailPixBytes; sbz++) {
		    if (colorMode == 0xf0)
		        encode (0x00, colorKey[ccs], &stFeld[colorKey[ccs]]);
		    else
		        encode (0x00, colorKey[3], &stFeld[colorKey[3]]);
		}

	    }

	}

	if (yAdd == 0) {
	    DBG(5, "ueberlese %i zeilen mit je %i bytes am Ende\n", trailLines, inpXBytes);
	    for (shz = 0; shz < trailLines; shz++) {	/* endschleife fuer  zu ueberlesende hohenzeilen */
		for (sbz = 0; sbz < inpXBytes; sbz++) {
		    if ((c = fgetc (in_stream)) != EOF) {
			/* verwerfe das byte */
		    }
		    else {
			DBG(0, "\nUps, unexpected End\n");
			exit (1);
		    }
		}
	    }

	}
	else {
	    DBG(5, "erzeuge %i zeilen mit je %i bytes (0x00) am Ende\n", trailLines, (resBreite / 8));
	    for (shz = 0; shz < trailLines; shz++) {	/* endschleife fuer fehlende hohenzeilen */
		for (sbz = 0; sbz < (resBreite / 8); sbz++) {
		    if (colorMode == 0xf0)
		        encode (c, colorKey[ccs], &stFeld[colorKey[ccs]]);
		    else
			encode (c, colorKey[3], &stFeld[colorKey[3]]);
		}
	    }
	}


	ccs++;
    }
    writePageHeader ();
    DBG(1, "###Seitenheader geschrieben\n");

    if (thisPageColorMode == 0xf0) {
	fwrite(stFeld[3].pageOut, 1, stFeld[3].indexPageOut, out_stream);
	fwrite(stFeld[2].pageOut, 1, stFeld[2].indexPageOut, out_stream);
	fwrite(stFeld[1].pageOut, 1, stFeld[1].indexPageOut, out_stream);
    }
    fwrite(stFeld[0].pageOut, 1, stFeld[0].indexPageOut, out_stream);
    DBG(1, "###Seiteninhalt geschrieben\n");

    DBG(1, "Farbverteilung der Seite:\nYellow:  %8d\nMagenta: %8d\nCyan:    %8d\nBlack:   %8d\n",
	pix[0],pix[1],pix[2],pix[3]);

    pix[0]=0;
    pix[1]=0;
    pix[2]=0;
    pix[3]=0;

}


int
main (int argc, char *argv[])
{
    extern char *optarg;
    char *inFile = NULL;
    char *outFile = NULL;
    int option;
    int encBufferSize;
    int blockBufferSize;
    int pageOutSize;
    int lineBufferSize;
    char *prog_name;

    prog_name = basename(argv[0]);
    if (!strcmp(prog_name, "m2300w")) {
        model = M2300W;
        form = form_2300;
        encode = doEncode;
    } else if (!strcmp(prog_name, "m2400w")) {
        model = M2400W;
        form = form_2400;
        encode = prepDoEncode;
        res_x = RES_MULT2;
    } else if (!strcmp(prog_name, "m2500w")) {
        model = M2500W;
        form = form_2400;
        encode = doEncode;
    } else {
        fprintf(stderr, "m2x00w must be executed as m2300w, m2400w or m2500w\n");
        return 1;
    }

/* 1. parameter lesen */

    while ((option = getopt (argc, argv, "v:hi:o:c:m:p:r:s")) >= 0)
	switch (option) {
	case 'h':
	case '?':
	    Help ();
	    return 0;
	case 'i':
	    inFile = optarg;
	    break;
	case 'o':
	    outFile = optarg;
	    if (strcmp (outFile, "-") == 0)
		verb = 0;
	    break;
	case 'c':
	    if (optarg[0] == '1') {
		blocksPerPage = 0x08;
		colorMode = (model == M2300W) ? 0x00 : 0x80;
	    }
	    else if (optarg[0] == '2') {
		blocksPerPage = 0x20;
		colorMode = 0xf0;
	    }
	    else {
		DBG(0, "Wrong color Mode %s\n", optarg);
		exit (1);
	    }
	    break;
	case 'm':
	    MediaCode = atoi (optarg);
	    if (MediaCode > 8 || MediaCode == 7) {
		DBG(0, "Wrong Media Code %d\n", MediaCode);
		exit (1);
	    }
	    break;
	case 'p':
	    PaperCode = atoi (optarg);
	    if (PaperCode > 41 || form[PaperCode].resX == 0) {
		DBG(0, "Wrong Paper Code %d\n", PaperCode);
		exit (1);
	    }
	    break;
	case 'r':
	    ResXmul = atoi (optarg);
	    if (ResXmul == 1) {
		DBG(1, "Aufloesung 600dpi\n");
		res_y = RES_600DPI;
		res_x = RES_MULT1;
	    }
	    else if (ResXmul == 2) {
		DBG(1, "Aufloesung 1200dpi\n");
		res_y = (model == M2300W) ? RES_1200DPI : RES_600DPI;
		res_x = RES_MULT2;///// BUG?
	    }
	    else if (model == M2400W && ResXmul == 3) {
		DBG(1, "Aufloesung 2400dpi\n");
		res_y = RES_600DPI;
		res_x = RES_MULT4;
	    }
	    else {
		DBG(0, "Wrong Resolutin Mode !\n");
		exit (1);
	    }
	    break;

	case 'v':
	    verb = atoi (optarg);
	    /* commended out, since this disables debugging
	       -gfuer
	    if (outFile != NULL && strcmp (outFile, "-") == 0)
		verb = 0;
	    */
	    break;
	case 's':
	    saveToner = 1;
	    break;
	}
    if (inFile == NULL || outFile == NULL) {
	Help ();
	return 1;
    }
/* 2. dateien oeffnen */

    if (strcmp (inFile, "-") == 0) {
	in_stream = stdin;
    }
    else {
	if ((in_stream = fopen (inFile, "r")) == NULL) {
	    DBG(0, "Fehler beim oeffnen der Eingabedatei\n");
	    perror ("");
	    exit (1);
	}
    }
    if (strcmp (outFile, "-") == 0) {
	out_stream = stdout;
    }
    else {
	if ((out_stream = fopen (outFile, "w")) == NULL) {
	    DBG(0, "Fehler beim oeffnen der Zieldatei\n");
	    perror ("");
	    exit (1);
	}
    }



    /* werte vorbereiten */
    resBreite = form[PaperCode].resX * ResXmul;
    resHoehe = form[PaperCode].resY;
    linesPerBlock = (form[PaperCode].resY / 8);



    /* speicher reservieren */
    if (model == M2300W) {
        encBufferSize = (resBreite / 8) + 150;	/* enthaelt im allgemeinen nur eine zeile (kleine zugabe die gleichzeitig die header abdeckt ;-) */
        blockBufferSize = encBufferSize * linesPerBlock;	/* einhaelt die zeilen mal anzahl zeilen pro block */
    } else {
        encBufferSize = (2 * (resBreite / 8)) + 150;	/* enthaelt im allgemeinen nur eine zeile (kleine zugabe die gleichzeitig die header abdeckt ;-) */
        blockBufferSize = ((resBreite / 8) + 150) * linesPerBlock;	/* einhaelt die zeilen mal anzahl zeilen pro block */
    }
    pageOutSize = blockBufferSize * 8;	/* enthaelt 8 bloecke */
    lineBufferSize = encBufferSize;

    stFeld[0].encBuffer = malloc (encBufferSize);
    stFeld[0].blockBuffer = malloc (blockBufferSize);
    stFeld[0].pageOut = malloc (pageOutSize);
    stFeld[0].lineBuffer = malloc (lineBufferSize);

    if (colorMode == 0xf0) {

	stFeld[1].encBuffer = malloc (encBufferSize);
	stFeld[1].blockBuffer = malloc (blockBufferSize);
	stFeld[1].pageOut = malloc (pageOutSize);
        stFeld[1].lineBuffer = malloc (lineBufferSize);

	stFeld[2].encBuffer = malloc (encBufferSize);
	stFeld[2].blockBuffer = malloc (blockBufferSize);
	stFeld[2].pageOut = malloc (pageOutSize);
        stFeld[2].lineBuffer = malloc (lineBufferSize);

	stFeld[3].encBuffer = malloc (encBufferSize);
	stFeld[3].blockBuffer = malloc (blockBufferSize);
	stFeld[3].pageOut = malloc (pageOutSize);
        stFeld[3].lineBuffer = malloc (lineBufferSize);
    }





    readPkmraw ();
    /* readBit(); */

    if (jobHeaderWritten == 1) {
        struct block_endpart endpart = { };
	unsigned char zero = 0;

	write_block(M2X00W_BLOCK_ENDPART, &endpart, sizeof(endpart), out_stream, NULL);
	write_block(M2X00W_BLOCK_END, &zero, sizeof(zero), out_stream, NULL);
	DBG(1, "JobFooter written.\n");
    }

    if (colorMode == 0xf0) {
	free (stFeld[3].encBuffer);
	free (stFeld[3].blockBuffer);
	free (stFeld[3].pageOut);
        free (stFeld[3].lineBuffer);

	free (stFeld[1].encBuffer);
	free (stFeld[1].blockBuffer);
	free (stFeld[1].pageOut);
        free (stFeld[1].lineBuffer);

	free (stFeld[2].encBuffer);
	free (stFeld[2].blockBuffer);
	free (stFeld[2].pageOut);
        free (stFeld[2].lineBuffer);
    }
    free (stFeld[0].encBuffer);
    free (stFeld[0].blockBuffer);
    free (stFeld[0].pageOut);
    free (stFeld[0].lineBuffer);

    fclose (in_stream);
    fclose (out_stream);
    return 0;
}

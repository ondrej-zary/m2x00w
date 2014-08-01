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
#include <math.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <libgen.h>

FILE *in_stream, *out_stream;

int verb = 0;			/* verbose level */

#define DBG(level, fmt, args ...)	if (verb > level) fprintf(stderr, fmt, ##args);

enum m2x00w_model { M2300W, M2400W };
enum m2x00w_model model;
int MediaCode = 0;
int PaperCode = 4;
int ResXmul = 1;
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
int reservedHeaderCountSH =0;   /* reservierter HeaderCount fuer den Seitenheader da dieser erst spaeter verwendet wird */

long pix[4] = { 0, 0, 0, 0 };	/* pixel counter (C,M,Y,K) */

struct format
{
    char desc[32];
    int resX;
    int resY;
};

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

struct media med[7] = {
/* 0*/ {"Normal (HQ)"},
/* 1*/ {"Karton"},
/* 2*/ {"Folie"},
/* 3*/ {"Kuvert"},
/* 4*/ {"Briefkopf"},
/* 5*/ {"Postkarte"},
/* 6*/ {"Etikette"},
};

unsigned char fileHeader[] = { 0x1B, 0x40, 0x00, 0x02, 0x00, 0xBF, 0x82, 0x10, 0xAE };

struct
{
    unsigned char jobHeaderT1[6];
    unsigned char res1;
    unsigned char res2;
    unsigned char jobHeaderT2[6];
    unsigned char prSum;
}
jobHeader =
{
    { 0x1B, 0x50, 0x01, 0x08, 0x00, 0xAF},
    0x01, 0x00,
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    0x00
};

struct
{
    unsigned char seitenHeaderT1[2];
    unsigned char headerCount;
    unsigned char seitenHeaderT2[3];
    unsigned char colorMode;
    unsigned char seitenHeaderT2b[3];
    unsigned char breite1;
    unsigned char breite2;
    unsigned char seitenHeaderT3[2];
    unsigned char hoehe1;
    unsigned char hoehe2;
    unsigned char blocksPerPage1;
    unsigned char seitenHeaderT4[1];
    unsigned char blocksPerPage2;
    unsigned char seitenHeaderT5[2];
    unsigned char paperFormat;
    unsigned char seitenHeaderT6[6];
    unsigned char paperQuality;
    unsigned char seitenHeaderT7[5];
    unsigned char prSum;
}
page_header =
{
    { 0x1B, 0x51} ,
    0x02,
    { 0x1C, 0x00, 0xAE},
    0x00,
    { 0x01, 0x00, 0x00},
    0x00, 0x00,
    { 0x00, 0x00},
    0x00, 0x00, 0x08,
    { 0x00},
    0x08,
    { 0x00, 0x00},
    0x04,
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    0x00,
    { 0x00, 0x01, 0x00, 0x00, 0x00},
    0x00
};

struct
{
    unsigned char jobFooterT1[2];
    unsigned char headerCount;
    unsigned char jobFooterT2[4];
    unsigned char prSum;
}
jobFooter =
{
    { 0x1B, 0x55},
    0x02,
    { 0x01, 0x00, 0xAA, 0x00},
    0x00
};

struct {
    unsigned char fileFooterT1[2];
    unsigned char headerCount;
    unsigned char fileFooterT2[4];
    unsigned char prSum;
}
fileFooter =
{
    { 0x1B, 0x41},
    0x00,
    { 0x01, 0x00, 0xBE, 0x00},
    0x00
};

struct
{
    unsigned char blockHeaderT1[2];
    unsigned char headerCount;
    /* headerzaehler 0x?? */
    unsigned char blockHeaderT2[3];
    unsigned char blockLength1;
    unsigned char blockLength2;
    unsigned char blockLength3;
    unsigned char blockLength4;
    /* laenge des blocks in byte 0x??,0x??,0x??,0x?? */
    unsigned char tonerColor;
    unsigned char blockCount;
    /* bblockzaehler 0x?? */
    unsigned char linesPerBlock1;
    unsigned char linesPerBlock2;
    unsigned char prSum;
    /* pruefsumme 0x?? */
}
blockHeader =
{
    { 0x1B, 0x52},
    0x03,
    { 0x08, 0x00, 0xAD},
    0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x53, 0x03, 0x00
};

struct steuerFelder
{
    unsigned int bytesIn;
    unsigned int linesOut;
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
    {0, 0, 0, NULL, 0, 0, 0, NULL, 0, NULL, 0, NULL, 0},
    {0, 0, 0, NULL, 0, 0, 0, NULL, 0, NULL, 0, NULL, 0},
    {0, 0, 0, NULL, 0, 0, 0, NULL, 0, NULL, 0, NULL, 0},
    {0, 0, 0, NULL, 0, 0, 0, NULL, 0, NULL, 0, NULL, 0}
};

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
    for (ho = 0; ho < 6; ho++) {
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

    exit (1);


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

void hex_dump(char *title, void *p, int length)
{
    int i;
    unsigned char *data = p;

    fprintf (stderr, "%s", title);
    for (i = 0; i < length; i++) {
        fprintf (stderr, " %.2hhx", *data);
        data++;
    }
    fprintf (stderr, "\n");
}

void
encodeToBlockBuffer (int colorID)
{
    int z;
    long rohBytes = stFeld[colorID].indexEncBuffer - stFeld[colorID].rleCount;
    int rohByteCount = 0;
    unsigned char rleOut[2];
    int rle64, rle1;

    DBG(5, "--> Ausgabe von %.3i Bytes fuer colorID %i davon %.3i Rohbytes und %.3i mal %.2x RLE encoded am Ende.\n",
	(int) stFeld[colorID].indexEncBuffer, (int) colorID, (int) rohBytes, (int) stFeld[colorID].rleCount, stFeld[colorID].lastByte);
    if (verb > 5)
	hex_dump("Daten fuer Encoder:\n", stFeld[colorID].encBuffer, stFeld[colorID].indexEncBuffer);
    /* rohbytes verarbeiten */
    /* 64er stufe */
    while ((rohBytes - (rohByteCount * 64)) >= 64) {
	unsigned char rBOut[65];
	DBG(5, "Segment mit 64 Bytes ausgeben ...");
	/* header belegen (standardwert) */
	rBOut[0] = (char) (64 - 1);
	memcpy (&rBOut[1], &stFeld[colorID].encBuffer[(rohByteCount * 64)],
		64);
	memcpy (&stFeld[colorID].
		blockBuffer[stFeld[colorID].indexBlockBuffer], &rBOut[0], 65);
	stFeld[colorID].indexBlockBuffer += 65;
	rohByteCount++;
	DBG(5, "OK\n");

	if (verb > 5)
	    hex_dump("Rohbytes:\n", rBOut, 65);
    }
    if ((rohBytes - (rohByteCount * 64)) > 0) {
	/* XXX */
	/* unsigned char rBOut[rohBytes - (64 * rohByteCount) + 1]; */
	unsigned char *rBOut = malloc(rohBytes - (64 * rohByteCount) + 1);

	/* einzelstufe */
	DBG(5, "Segment mit %i Bytes ausgeben ...", (int) (rohBytes - (64 * rohByteCount)));

	/* header belegen */
	rBOut[0] = (char) ((rohBytes - (64 * rohByteCount)) - 1);
	memcpy (&rBOut[1], &stFeld[colorID].encBuffer[(rohByteCount * 64)],
		(rohBytes - (64 * rohByteCount)));
	memcpy (&stFeld[colorID].
		blockBuffer[stFeld[colorID].indexBlockBuffer], &rBOut[0],
		(rohBytes + 1 - (64 * rohByteCount)));
	stFeld[colorID].indexBlockBuffer +=
	    (rohBytes - (64 * rohByteCount) + 1);
	DBG(5, "OK\n");

	if (verb > 5)
	    hex_dump("Rohbytes:\n", rBOut, rohBytes - (64 * rohByteCount));

	/* XXX */
	free(rBOut);
    }

    /* rle verarbeiten */

    rle64 = floor ((double) (stFeld[colorID].rleCount) / 64);
    rle1 = (stFeld[colorID].rleCount) - (rle64 * 64);

    if (model == M2400W && rle64 > 63) {
        rleOut[0] = 224;
	rleOut[1] = stFeld[colorID].lastByte;
	for (z = 0; z < 2; z++) {
	    memcpy (&stFeld[colorID].
		    blockBuffer[stFeld[colorID].indexBlockBuffer], &rleOut[0], 2);
	    stFeld[colorID].indexBlockBuffer += 2;
	    DBG(5, "---->64er RLE Encoding: 2048 mal %.2x - codiert als: %.2x%.2x\n", rleOut[1], rleOut[0], rleOut[1]);
	    if (verb > 5)
		hex_dump("64er RLE::\n", rleOut, 2);
	}
    	rle64 -= 64;
    }

    if (rle64 > 0) {
	rleOut[0] = 192 + rle64;
	rleOut[1] = stFeld[colorID].lastByte;
	memcpy (&stFeld[colorID].
		blockBuffer[stFeld[colorID].indexBlockBuffer], &rleOut[0], 2);
	stFeld[colorID].indexBlockBuffer += 2;

	DBG(5, "---->64er RLE Encoding: %i mal %.2x - codiert als: %.2x%.2x\n", rle64, rleOut[1], rleOut[0], rleOut[1]);
	if (verb > 5)
	    hex_dump("64er RLE::\n", rleOut, 2);
    }
    if (rle1 > 0) {
	rleOut[0] = 128 + rle1;
	rleOut[1] = stFeld[colorID].lastByte;
	memcpy (&stFeld[colorID].
		blockBuffer[stFeld[colorID].indexBlockBuffer], &rleOut[0], 2);
	stFeld[colorID].indexBlockBuffer += 2;
	DBG(5, "---->1er RLE Encoding: %i mal %.2x - codiert als: %.2x%.2x\n", rle1, rleOut[1], rleOut[0], rleOut[1]);
	if (verb > 5)
	    hex_dump(" 1er RLE::\n", rleOut, 2);
    }


    DBG(5, "--->RLE Encode for %i done.\n", colorID);
    stFeld[colorID].indexEncBuffer = 0;
}

void
doEncode (int inByte, int colorID)
{
    if (stFeld[colorID].bytesIn == 0) {
	unsigned char dummyTable[1] = { 0x80 };
	stFeld[colorID].linesOut++;
	/* jede Zeile beginnt mit einer Tabelle */
	/* die tabellenkompression wurde vorerst weggelassen deshalb eine leere tabelle */
	DBG(5, "Dummy Tabelle fuer neue Zeile %3i ausgeben .\n", stFeld[colorID].linesOut);
	memcpy (&stFeld[colorID].
		blockBuffer[stFeld[colorID].indexBlockBuffer], &dummyTable[0],
		1);
	stFeld[colorID].indexBlockBuffer += 1;

    }
    if (saveToner > 0) {
	/* spar Toner indem es jedes 2te bit loescht. */
	/* die loeschung erfolgt pro zeile versetzt (schachbrettmuster) */
	if ((model != M2400W && (stFeld[colorID].linesOut % 2 > 0)) ||
	   (model == M2400W && ((stFeld[colorID].bytesIn + 1) > (resBreite / 8)))) {
	    inByte = inByte & 0xaa;
	}
	else {
	    inByte = inByte & 0x55;
	}
    }

    if (inByte == stFeld[colorID].lastByte || stFeld[colorID].rleCount < 1) {
	stFeld[colorID].rleCount++;	/* zaehlen wie oft ein byte wiederholt wird */
    }
    else {
	if (stFeld[colorID].rleCount > 3) {	/* rle ausgabe lohnt sich, deshalb ausgeben */
	    encodeToBlockBuffer (colorID);
	    /* wenn sich das ausgeben nicht lohnt, werden die aufgelaufenen rles verworfen und als normale bytes gewertet */
	}
	stFeld[colorID].rleCount = 0;
    }
    stFeld[colorID].lastByte = inByte;

    stFeld[colorID].bytesIn++;	/* EingabeByte hochzaehlen und byyte auf den buffer legen */
    stFeld[colorID].encBuffer[stFeld[colorID].indexEncBuffer] = inByte;
    stFeld[colorID].indexEncBuffer++;


    if ((stFeld[colorID].bytesIn + 1) > ((model == M2400W ? 2 : 1) * (resBreite / 8))) {
	/* eine zeile ist voll. den rest im buffer codieren */

	encodeToBlockBuffer (colorID);
	stFeld[colorID].bytesIn = 0;
	stFeld[colorID].rleCount = 0;
	/* wenn die anzahl der zeilen f|r einen block erreicht ist muss der blockheader generiert werden */
	/* dies kann erst jetzt geschehen, da die anzahl der im block befindlichen bytes im header steht */
	if (stFeld[colorID].linesOut >= (linesPerBlock / (model == M2400W ? 2 : 1))) {
	    stFeld[colorID].blocksOut++;
	    DBG(4, "Blockheader fuer Block %i mit %5i Bytes generieren und Block raus kopieren...\n",
			 (int) stFeld[colorID].blocksOut, (int) stFeld[colorID].indexBlockBuffer);
	    blockHeader.blockLength1 =
		(char) stFeld[colorID].indexBlockBuffer;
	    blockHeader.blockLength2 =
		(char) (stFeld[colorID].indexBlockBuffer >> 8);
	    blockHeader.blockLength3 =
		(char) (stFeld[colorID].indexBlockBuffer >> 16);
	    blockHeader.blockLength4 =
		(char) (stFeld[colorID].indexBlockBuffer >> 24);
	    blockHeader.linesPerBlock1 = (char) (linesPerBlock);
	    blockHeader.linesPerBlock2 = (char) (linesPerBlock >> 8);
	    /* die berechnung der headerCount muss sicherstellen das die reichenfolge stimmt */
	    if (thisPageColorMode == 0xf0) {
		blockHeader.headerCount =
		    siteInitHeaderCount + stFeld[colorID].blocksOut - 1 +
		    ((3 - colorID) * 8);
	    }
	    else {
		blockHeader.headerCount =
		    siteInitHeaderCount + stFeld[colorID].blocksOut - 1;
	    }
	    headerCount++;
	    DBG(4, "BlockHeader.headerCount fuer colorID %i ist %i\n", colorID, blockHeader.headerCount);
	    blockHeader.blockCount = stFeld[colorID].blocksOut;
	    blockHeader.tonerColor = colorID;

	    blockHeader.prSum = checksum(&blockHeader, sizeof(blockHeader) - 1);

	    memcpy (&stFeld[colorID].pageOut[stFeld[colorID].indexPageOut],
		    &blockHeader, sizeof(blockHeader));
	    stFeld[colorID].indexPageOut += sizeof(blockHeader);

	    if (verb > 4)
		hex_dump("Blockheader: ", &blockHeader, sizeof(blockHeader));

	    memcpy (&stFeld[colorID].pageOut[stFeld[colorID].indexPageOut],
		    &stFeld[colorID].blockBuffer[0],
		    stFeld[colorID].indexBlockBuffer);
	    stFeld[colorID].indexPageOut += stFeld[colorID].indexBlockBuffer;

	    stFeld[colorID].linesOut = 0;
	    stFeld[colorID].indexBlockBuffer = 0;
	}

    }


}

void
prepDoEncode(int inByte, int colorID)
{
    stFeld[colorID].lineBuffer[stFeld[colorID].indexLineBuffer] =  inByte;
    stFeld[colorID].indexLineBuffer += 1;

    if (stFeld[colorID].indexLineBuffer == (resBreite / 4))  {
        int z;
        for (z = 0; z < (resBreite / 8); z++)  {
            doEncode (stFeld[colorID].lineBuffer[z], colorID);
            doEncode (stFeld[colorID].lineBuffer[z + (resBreite / 8)], colorID);
        }
        stFeld[colorID].indexLineBuffer = 0;
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
    fwrite(fileHeader, 1, sizeof(fileHeader), out_stream);
    headerCount++;

    jobHeader.prSum = checksum(&jobHeader, sizeof(jobHeader) - 1);

    fwrite(&jobHeader, 1, sizeof(jobHeader), out_stream);
    headerCount++;
    DBG(1, "JobHeader written.\n");
}

void
writePageHeader (void)
{
    /* seitenHeader ausgeben */
    page_header.headerCount = reservedHeaderCountSH;
    page_header.breite1 = (unsigned char) resBreite;
    page_header.breite2 = (unsigned char) (resBreite >> 8);
    page_header.hoehe1 = (unsigned char) resHoehe;
    page_header.hoehe2 = (unsigned char) (resHoehe >> 8);
    page_header.colorMode = thisPageColorMode;
    page_header.blocksPerPage1 = thisPageBlocksPerPage;
    page_header.blocksPerPage2 = thisPageBlocksPerPage;

    page_header.paperFormat = PaperCode;
    page_header.paperQuality = MediaCode;

    page_header.prSum = checksum(&page_header, sizeof(page_header) - 1);
    fwrite(&page_header, 1, sizeof(page_header), out_stream);

    if (verb > 4)
	hex_dump("Seitenheader: ", &page_header, sizeof(page_header));
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
	    reservedHeaderCountSH = headerCount++;
	    siteInitHeaderCount = headerCount;
	    thisPageColorMode=colorMode;
	    thisPageBlocksPerPage=blocksPerPage;
	    DBG(1, "Reservierter SeitenHeaderCount ist %2d\n", reservedHeaderCountSH );

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
		    inpXBytes = (int) ceil ((double) inpX / 8);
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
	    leadPixBytes =
		(int) ceil (((double) inpXBytes - ((double) resBreite / 8)) /
			    2);
	    trailPixBytes =
		(int) floor (((double) inpXBytes - ((double) resBreite / 8)) /
			     2);
	    DBG(5, "Eingabeseite zu breit - clippe auf %i mit %i am Anfang und %i am Ende.\n",
		realPixBytes, leadPixBytes, trailPixBytes);
	}
	else {			/* hinzufuegen */
	    realPixBytes = inpXBytes;
	    leadPixBytes =
		(int) ceil ((((double) resBreite / 8) - (double) inpXBytes) /
			    2);
	    trailPixBytes =
		(int) floor ((((double) resBreite / 8) - (double) inpXBytes) /
			     2);
	    xAdd = 1;
	    DBG(5, "Eingabeseite zu schmal - addiere auf %i -> %i am Anfang und %i am Ende.\n",
		realPixBytes, leadPixBytes, trailPixBytes);
	}

	if (resHoehe <= inpY) {	/* eingabe hoeher als druckbereich - clippen */
	    realLines = resHoehe;
	    leadLines =
		(int) ceil ((((double) inpY - (double) resHoehe) / 2));
	    trailLines =
		(int) floor ((((double) inpY - (double) resHoehe) / 2));
	    DBG(5, "Eingabeseite zu lang - clippe auf %i mit %i am Anfang und %i am Ende.\n",
		realLines, leadLines, trailLines);
	}
	else {			/* hinzufuegen */
	    realLines = inpY;
	    leadLines =
		(int) ceil ((((double) resHoehe - (double) inpY) / 2));
	    trailLines =
		(int) floor ((((double) resHoehe - (double) inpY) / 2));
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
		    if (colorMode == 0xf0) {
		        if (model == M2300W)
		            doEncode (0x00, colorKey[ccs]);
		        else
			    prepDoEncode (0x00, colorKey[ccs]);
		    }
		    else {
		        if (model == M2300W)
		            doEncode (0x00, colorKey[3]);
		        else
			    prepDoEncode (0x00, colorKey[3]);
		    }
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
		    if (colorMode == 0xf0) {
		        if (model == M2300W)
		            doEncode (0x00, colorKey[ccs]);
		        else
			    prepDoEncode (0x00, colorKey[ccs]);
		    }
		    else {
		        if (model == M2300W)
		            doEncode (0x00, colorKey[3]);
		        else
			    prepDoEncode (0x00, colorKey[3]);
		    }
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
                        if (model == M2300W)
                            doEncode (c, colorKey[ccs]);
                        else
			    prepDoEncode (c, colorKey[ccs]);
		    }
		    else {
		        if (model == M2300W)
		            doEncode (c, colorKey[3]);
		        else
			    prepDoEncode (c, colorKey[3]);
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
		    if (colorMode == 0xf0) {
		        if (model == M2300W)
		            doEncode (0x00, colorKey[ccs]);
		        else
			    prepDoEncode (0x00, colorKey[ccs]);
		    }
		    else {
		        if (model == M2300W)
		            doEncode (0x00, colorKey[3]);
		        else
			    prepDoEncode (0x00, colorKey[3]);
		    }
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
		    if (colorMode == 0xf0) {
		        if (model == M2300W)
		            doEncode (c, colorKey[ccs]);
		        else
			    prepDoEncode (c, colorKey[ccs]);
		    }
		    else {
		        if (model == M2300W)
		            doEncode (c, colorKey[3]);
		        else
			    prepDoEncode (c, colorKey[3]);
		    }
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
    } else if (!strcmp(prog_name, "m2400w")) {
        model = M2400W;
        form = form_2400;
        fileHeader[6] = 0x85;
        fileHeader[8] = 0xB1;
        jobHeader.res2 = 0x01;
        page_header.colorMode = 0x80;
        page_header.seitenHeaderT7[1] = 0x00;
        blockHeader.linesPerBlock1 = 0x50;
    }

/* 1. parameter lesen */

    while ((option = getopt (argc, argv, "v:hi:o:c:m:p:r:s")) >= 0)
	switch (option) {
	case 'h':
	    Help ();
	    return (0);
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
	    if (MediaCode > 6) {
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
		jobHeader.res1 = 0x01;
		jobHeader.res2 = 0x00;
	    }
	    else if (ResXmul == 2) {
		DBG(1, "Aufloesung 1200dpi\n");
		jobHeader.res1 = (model == M2300W) ? 0x02 : 0x01;
		jobHeader.res2 = 0x01;
	    }
	    else if (model == M2400W && ResXmul == 3) {
		DBG(1, "Aufloesung 2400dpi\n");
		jobHeader.res1 = 0x01;
		jobHeader.res2 = 0x02;
		ResXmul = 4;
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
	case '?':
	    Help ();
	    return (1);
	}
    if (inFile == NULL || outFile == NULL) {
	Help ();
	return (1);
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
	/* footer ausgeben */
	jobFooter.headerCount = headerCount++;
	jobFooter.prSum = checksum(&jobFooter, sizeof(jobFooter) - 1);
	fwrite(&jobFooter, 1, sizeof(jobFooter), out_stream);

	fileFooter.headerCount = headerCount++;
	fileFooter.prSum = checksum(&fileFooter, sizeof(fileFooter) - 1);
	fwrite(&fileFooter, 1, sizeof(fileFooter), out_stream);
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
    return (0);

}

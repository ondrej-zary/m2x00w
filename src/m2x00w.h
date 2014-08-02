#define DIV_ROUND_UP(n, d) (((n) + (d) - 1) / (d))
#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

#define cpu_to_le16(x) (x)
#define cpu_to_le32(x) (x)

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

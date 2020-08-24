#pragma once
// Mostly functions provided by UDF standard like CRC checksum calculation and unicode compression

/***********************************************************************
 * OSTA compliant Unicode compression, uncompression routines.
 * Copyright 1995 Micro Design International, Inc.
 * Written by Jason M. Rinn.
 * Micro Design International gives permission for the free use of the
 * following source code.
 */
#include <stddef.h>
 /***********************************************************************
  * The following two typedef's are to remove compiler dependancies.
 * byte needs to be unsigned 8-bit, and unicode_t needs to be
  * unsigned 16-bit.
  */
typedef unsigned short unicode_t;
typedef unsigned char byte;
/***********************************************************************
 * Takes an OSTA CS0 compressed unicode name, and converts
 * it to Unicode.
 * The Unicode output will be in the byte order
 * that the local compiler uses for 16-bit values.
 * NOTE: This routine only performs error checking on the compID.
 * It is up to the user to ensure that the unicode buffer is large
 * enough, and that the compressed unicode name is correct.
 *
 * RETURN VALUE
 *
 * The number of unicode characters which were uncompressed.
 * A -1 is returned if the compression ID is invalid.
 */
int UncompressUnicode(
	int numberOfBytes, /* (Input) number of bytes read from media. */
	byte* UDFCompressed, /* (Input) bytes read from media. */
	unicode_t* unicode) /* (Output) uncompressed unicode characters. */
{
	unsigned int compID;
	int returnValue, unicodeIndex, byteIndex;
	/* Use UDFCompressed to store current byte being read. */
	compID = UDFCompressed[0];
	/* First check for valid compID. */
	if (compID != 8 && compID != 16)
	{
		returnValue = -1;
	}
	else
	{
		unicodeIndex = 0;
		byteIndex = 1;
		/* Loop through all the bytes. */
		while (byteIndex < numberOfBytes)
		{
			if (compID == 16)
			{
				/*Move the first byte to the high bits of the unicode char. */
				unicode[unicodeIndex] = UDFCompressed[byteIndex++] << 8;
			}
			else
			{
				unicode[unicodeIndex] = 0;
			}
			if (byteIndex < numberOfBytes)
			{
				/*Then the next byte to the low bits. */
				unicode[unicodeIndex] |= UDFCompressed[byteIndex++];
			}
			unicodeIndex++;
		}
		returnValue = unicodeIndex;
	}
	return(returnValue);
}
/***********************************************************************
 * DESCRIPTION:
 * Takes a string of unicode wide characters and returns an OSTA CS0
 * compressed unicode string. The unicode MUST be in the byte order of
 * the compiler in order to obtain correct results. Returns an error
 * if the compression ID is invalid.
 *
 * NOTE: This routine assumes the implementation already knows, by
 * the local environment, how many bits are appropriate and
 * therefore does no checking to test if the input characters fit
 * into that number of bits or not.
 *
 * RETURN VALUE
 *
 * The total number of bytes in the compressed OSTA CS0 string,
 * including the compression ID.
 * A -1 is returned if the compression ID is invalid.
 */
int CompressUnicode(
	int numberOfChars, /* (Input) number of unicode characters. */
	int compID, /* (Input) compression ID to be used. */
	unicode_t* unicode, /* (Input) unicode characters to compress. */
	byte* UDFCompressed) /* (Output) compressed string, as bytes. */
{
	int byteIndex, unicodeIndex;
	if (compID != 8 && compID != 16)
	{
		byteIndex = -1; /* Unsupported compression ID ! */
	}
	else
	{
		/* Place compression code in first byte. */
		UDFCompressed[0] = compID;
		byteIndex = 1;
		unicodeIndex = 0;
		while (unicodeIndex < numberOfChars)
		{
			if (compID == 16)
			{
				/* First, place the high bits of the char
				* into the byte stream.
				*/
				UDFCompressed[byteIndex++] =
					(unicode[unicodeIndex] & 0xFF00) >> 8;
			}
			/*Then place the low bits into the stream. */
			UDFCompressed[byteIndex++] = unicode[unicodeIndex] & 0x00FF;
			unicodeIndex++;
		}
	}
	return(byteIndex);
}

/*
 * CRC 010041
 */
static unsigned short crc_table[256] = {
 0x0000, 0x1021, 0x2042, 0x3063, 0x4084, 0x50A5, 0x60C6, 0x70E7,
 0x8108, 0x9129, 0xA14A, 0xB16B, 0xC18C, 0xD1AD, 0xE1CE, 0xF1EF,
 0x1231, 0x0210, 0x3273, 0x2252, 0x52B5, 0x4294, 0x72F7, 0x62D6,
 0x9339, 0x8318, 0xB37B, 0xA35A, 0xD3BD, 0xC39C, 0xF3FF, 0xE3DE,
 0x2462, 0x3443, 0x0420, 0x1401, 0x64E6, 0x74C7, 0x44A4, 0x5485,
 0xA56A, 0xB54B, 0x8528, 0x9509, 0xE5EE, 0xF5CF, 0xC5AC, 0xD58D,
 0x3653, 0x2672, 0x1611, 0x0630, 0x76D7, 0x66F6, 0x5695, 0x46B4,
 0xB75B, 0xA77A, 0x9719, 0x8738, 0xF7DF, 0xE7FE, 0xD79D, 0xC7BC,
 0x48C4, 0x58E5, 0x6886, 0x78A7, 0x0840, 0x1861, 0x2802, 0x3823,
 0xC9CC, 0xD9ED, 0xE98E, 0xF9AF, 0x8948, 0x9969, 0xA90A, 0xB92B,
 0x5AF5, 0x4AD4, 0x7AB7, 0x6A96, 0x1A71, 0x0A50, 0x3A33, 0x2A12,
 0xDBFD, 0xCBDC, 0xFBBF, 0xEB9E, 0x9B79, 0x8B58, 0xBB3B, 0xAB1A,
 0x6CA6, 0x7C87, 0x4CE4, 0x5CC5, 0x2C22, 0x3C03, 0x0C60, 0x1C41,
 0xEDAE, 0xFD8F, 0xCDEC, 0xDDCD, 0xAD2A, 0xBD0B, 0x8D68, 0x9D49,
 0x7E97, 0x6EB6, 0x5ED5, 0x4EF4, 0x3E13, 0x2E32, 0x1E51, 0x0E70,
 0xFF9F, 0xEFBE, 0xDFDD, 0xCFFC, 0xBF1B, 0xAF3A, 0x9F59, 0x8F78,
 0x9188, 0x81A9, 0xB1CA, 0xA1EB, 0xD10C, 0xC12D, 0xF14E, 0xE16F,
 0x1080, 0x00A1, 0x30C2, 0x20E3, 0x5004, 0x4025, 0x7046, 0x6067,
 0x83B9, 0x9398, 0xA3FB, 0xB3DA, 0xC33D, 0xD31C, 0xE37F, 0xF35E,
 0x02B1, 0x1290, 0x22F3, 0x32D2, 0x4235, 0x5214, 0x6277, 0x7256,
 0xB5EA, 0xA5CB, 0x95A8, 0x8589, 0xF56E, 0xE54F, 0xD52C, 0xC50D,
 0x34E2, 0x24C3, 0x14A0, 0x0481, 0x7466, 0x6447, 0x5424, 0x4405,
 0xA7DB, 0xB7FA, 0x8799, 0x97B8, 0xE75F, 0xF77E, 0xC71D, 0xD73C,
 0x26D3, 0x36F2, 0x0691, 0x16B0, 0x6657, 0x7676, 0x4615, 0x5634,
 0xD94C, 0xC96D, 0xF90E, 0xE92F, 0x99C8, 0x89E9, 0xB98A, 0xA9AB,
 0x5844, 0x4865, 0x7806, 0x6827, 0x18C0, 0x08E1, 0x3882, 0x28A3,
 0xCB7D, 0xDB5C, 0xEB3F, 0xFB1E, 0x8BF9, 0x9BD8, 0xABBB, 0xBB9A,
 0x4A75, 0x5A54, 0x6A37, 0x7A16, 0x0AF1, 0x1AD0, 0x2AB3, 0x3A92,
 0xFD2E, 0xED0F, 0xDD6C, 0xCD4D, 0xBDAA, 0xAD8B, 0x9DE8, 0x8DC9,
 0x7C26, 0x6C07, 0x5C64, 0x4C45, 0x3CA2, 0x2C83, 0x1CE0, 0x0CC1,
 0xEF1F, 0xFF3E, 0xCF5D, 0xDF7C, 0xAF9B, 0xBFBA, 0x8FD9, 0x9FF8,
 0x6E17, 0x7E36, 0x4E55, 0x5E74, 0x2E93, 0x3EB2, 0x0ED1, 0x1EF0
};

unsigned short cksum(unsigned char* s, int n)
{
	register unsigned short crc = 0;
	while (n-- > 0)
		crc = crc_table[(crc >> 8 ^ *s++) & 0xff] ^ (crc << 8);
	return crc;
}
byte cksum_tag(unsigned char* s, int n) {
	register unsigned char checksum = 0;
	for (int i = 0; i < n; ++i) {
		if (i == 4) continue;
		checksum += s[i];
	}
	return checksum;
}
/* UNICODE Checksum */
unsigned short unicode_cksum(unsigned short* s, int n)
{
	unsigned short crc = 0;
	while (n-- > 0) {
		/* Take high order byte first--corresponds to a big endian byte stream. */
		crc = crc_table[(crc >> 8 ^ (*s >> 8)) & 0xff] ^ (crc << 8);
		crc = crc_table[(crc >> 8 ^ (*s++ & 0xff)) & 0xff] ^ (crc << 8);
	}
	return crc;
}


/*
 * Calculates a 16-bit checksum of the Implementation Use
 * Extended Attribute header or Application Use Extended Attribute
 * header. The fields AttributeType through ImplementationIdentifier
 * (or ApplicationIdentifier) inclusively represent the
 * data covered by the checksum (48 bytes).
 *
 */
unsigned short ComputeEAChecksum(byte* data)
{
	unsigned short checksum = 0;
	unsigned int count;
	for (count = 0; count < 48; count++)
	{
		checksum += *data++;
	}
	return(checksum);
}

unsigned short changeEndianness16(unsigned short val)
{
	return (val << 8) |          // left-shift always fills with zeros
		((val >> 8) & 0x00ff); // right-shift sign-extends, so force to zero
}

unsigned int changeEndianness32(unsigned int val)
{
	return (val << 24) |
		((val << 8) & 0x00ff0000) |
		((val >> 8) & 0x0000ff00) |
		((val >> 24) & 0x000000ff);
}


// ----------------------------------------------------------------------------
// id.cxx  --  id transmit class
//
// Copyright (C) 2006
//		Dave Freese, W1HKJ
//
// fldigi is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// fldigi is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with fldigi; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
// ----------------------------------------------------------------------------

#include <stdlib.h>
#include <iostream>

#include "id.h"
#include "ascii.h"
#include "fl_digi.h"

#undef  CLAMP
#define CLAMP(x,low,high)       (((x)>(high))?(high):(((x)<(low))?(low):(x)))

using namespace std;

id::~id()
{
//	if (txpulse) delete [] txpulse;
//	if (outbuf) delete[] outbuf;
}

id::id(modem *md)
{
	mode = md;
	make_pulse();
}

//=====================================================================
// transmit processing
//=====================================================================

void id::make_pulse()
{
	int risetime = mode->get_samplerate() / 50;
	for (int i = 0; i < IDSYMLEN; i++)
		txpulse[i] = 1.0;
	for (int i = 0; i < risetime; i++)
		txpulse[i] = txpulse[IDSYMLEN - 1 - i] =
			0.5 * (1 - cos(M_PI * i / risetime));
}

void id::make_tones()
{
	double f;
	double frequency = mode->get_txfreq_woffset();
	double sr = mode->get_samplerate();
	for (int j = 0; j < NUMCHARS; j++)
		for (int i = 0; i < NUMTONES; i++) {
			f = frequency + TONESPACING *(NUMTONES - i - j*(NUMTONES + 1));
			w[i + NUMTONES * j] = 2 * M_PI * f / sr;
		}
}

// symbol is the binary representation of the entire multitone array
// a 1 indicates a signal output and a 0 indicates no signal at that freq bin
// mask 1 = 180 phase reversal of tone, 0 = no phase reversal

void id::send(long int symbol)
{
	int i, j;
	int sym;
	int msk;
	double maximum = 0.0;
	for (i = 0; i < IDSYMLEN; i++) {
		outbuf[i] = 0.0;
		sym = symbol;
		msk = mask[symbol];
		for (j = 0; j < NUMTONES * NUMCHARS; j++) {
			if (sym & 1 == 1)
				outbuf[i] += (msk & 1 == 1 ? -1 : 1 ) * sin(w[j] * i);
			sym = sym >> 1;
			msk = msk >> 1;
		}
		if (fabs(outbuf[i]) > maximum)
			maximum = fabs(outbuf[i]);
	}
	if (maximum > 0.0)
		for (i = 0; i < IDSYMLEN; i++)
			outbuf[i] = outbuf[i] * txpulse[i] / maximum;
		
	mode->ModulateXmtr(outbuf, IDSYMLEN);
}

void id::sendchars(string s)
{
	long int symbol;
	int  len = s.length();
	int  n[NUMCHARS];
	int  c;
	while (len++ < NUMCHARS) s.insert(0," ");

	for (int i = 0; i < NUMCHARS; i++) {
		c = s[i];
		if (c > 'z' || c < ' ') c = ' ';
		c = toupper(c) - ' ';
		n[i] = c;
	}
// send rows from bottom to top so they appear to scroll down the waterfall correctly
// each character formed from 5 rows of 5 columns - uses a modifed 7x7 feldhell font
// after the symbol is or'd and shifted it contains the bit values associated with
// each frequency bin:
// For example, the bottom rows of S K are 0xF0 and 0X88
// for this row and characters the symbol is:
// 0111110001
	for (int row = 0; row < NUMROWS; row++) {
		symbol = 0;
		for (int i = 0; i < NUMCHARS; i++) {
			symbol |= (idch[n[i]].byte[NUMROWS - 1 -row] >> 3);
			if (i != (NUMCHARS - 1) )
				symbol = symbol << 5;
		}
		send (symbol);
	}
	send (0); // row of no tones
	send (0);
}

void id::transmit(trx_mode m)
{
	int numlines = 0;
	int len;
	string s;
	string tosend;

	s = mode_names[m];
	len = s.length();

	make_tones();
	
	put_status("Sending ID");
	while (numlines < len) numlines += NUMCHARS;
	numlines -= NUMCHARS;
	while (numlines >= 0) {
		tosend = s.substr(numlines, NUMCHARS);
		sendchars(tosend);
		numlines -= NUMCHARS;
	}
	put_status("");
}


double	id::txpulse[IDSYMLEN];
double	id::outbuf[IDSYMLEN];
double  id::w[NUMTONES * NUMCHARS];

idfntchr id::idch[] = {
{' ', { 0x00, 0x00, 0x00, 0x00, 0x00 }, },
{'!', { 0x80, 0x80, 0x80, 0x00, 0x80 }, },
{'"', { 0xA0, 0x00, 0x00, 0x00, 0x00 }, },
{'#', { 0x50, 0xF8, 0x50, 0xF8, 0x50 }, },
{'$', { 0x78, 0xA0, 0x70, 0x28, 0xF0 }, },
{'%', { 0xC8, 0x10, 0x20, 0x40, 0x98 }, },
{'&', { 0x40, 0xE0, 0x68, 0x90, 0x78 }, },
{ 39, { 0xC0, 0x40, 0x80, 0x00, 0x00 }, },
{'(', { 0x60, 0x80, 0x80, 0x80, 0x60 }, },
{')', { 0xC0, 0x20, 0x20, 0x20, 0xC0 }, },
{'*', { 0xA8, 0x70, 0xF8, 0x70, 0xA8 }, },
{'+', { 0x00, 0x20, 0xF8, 0x20, 0x00 }, },
{',', { 0x00, 0x00, 0x00, 0xC0, 0x40 }, },
{'-', { 0x00, 0x00, 0xF8, 0x00, 0x00 }, },
{'.', { 0x00, 0x00, 0x00, 0x00, 0xC0 }, },
{'/', { 0x08, 0x10, 0x20, 0x40, 0x80 }, },
{'0', { 0x70, 0x98, 0xA8, 0xC8, 0x70 }, },
{'1', { 0x60, 0xA0, 0x20, 0x20, 0x20 }, },
{'2', { 0xE0, 0x10, 0x20, 0x40, 0xF8 }, },
{'3', { 0xF0, 0x08, 0x30, 0x08, 0xF0 }, },
{'4', { 0x90, 0x90, 0xF8, 0x10, 0x10 }, },
{'5', { 0xF8, 0x80, 0xF0, 0x08, 0xF0 }, },
{'6', { 0x70, 0x80, 0xF0, 0x88, 0x70 }, },
{'7', { 0xF8, 0x08, 0x10, 0x20, 0x40 }, },
{'8', { 0x70, 0x88, 0x70, 0x88, 0x70 }, },
{'9', { 0x70, 0x88, 0x78, 0x08, 0x70 }, },
{':', { 0x00, 0xC0, 0x00, 0xC0, 0x00 }, },
{';', { 0xC0, 0x00, 0xC0, 0x40, 0x80 }, },
{'<', { 0x08, 0x30, 0xC0, 0x30, 0x08 }, },
{'=', { 0x00, 0xF8, 0x00, 0xF8, 0x00 }, },
{'>', { 0x80, 0x60, 0x18, 0x60, 0x80 }, },
{'?', { 0xE0, 0x10, 0x20, 0x00, 0x20 }, },
{'@', { 0x70, 0x88, 0xB0, 0x80, 0x78 }, },
{'A', { 0x70, 0x88, 0xF8, 0x88, 0x88 }, },
{'B', { 0xF0, 0x48, 0x70, 0x48, 0xF0 }, },
{'C', { 0x78, 0x80, 0x80, 0x80, 0x78 }, },
{'D', { 0xF0, 0x88, 0x88, 0x88, 0xF0 }, },
{'E', { 0xF8, 0x80, 0xE0, 0x80, 0xF8 }, },
{'F', { 0xF8, 0x80, 0xE0, 0x80, 0x80 }, },
{'G', { 0x78, 0x80, 0x98, 0x88, 0x78 }, },
{'H', { 0x88, 0x88, 0xF8, 0x88, 0x88 }, },
{'I', { 0xE0, 0x40, 0x40, 0x40, 0xE0 }, },
{'J', { 0x08, 0x08, 0x08, 0x88, 0x70 }, },
{'K', { 0x88, 0x90, 0xE0, 0x90, 0x88 }, },
{'L', { 0x80, 0x80, 0x80, 0x80, 0xF8 }, },
{'M', { 0x88, 0xD8, 0xA8, 0x88, 0x88 }, },
{'N', { 0x88, 0xC8, 0xA8, 0x98, 0x88 }, },
{'O', { 0x70, 0x88, 0x88, 0x88, 0x70 }, },
{'P', { 0xF0, 0x88, 0xF0, 0x80, 0x80 }, },
{'Q', { 0x70, 0x88, 0x88, 0xA8, 0x70 }, },
{'R', { 0xF0, 0x88, 0xF0, 0x90, 0x88 }, },
{'S', { 0x78, 0x80, 0x70, 0x08, 0xF0 }, },
{'T', { 0xF8, 0x20, 0x20, 0x20, 0x20 }, },
{'U', { 0x88, 0x88, 0x88, 0x88, 0x70 }, },
{'V', { 0x88, 0x90, 0xA0, 0xC0, 0x80 }, },
{'W', { 0x88, 0x88, 0xA8, 0xA8, 0x50 }, },
{'X', { 0x88, 0x50, 0x20, 0x50, 0x88 }, },
{'Y', { 0x88, 0x50, 0x20, 0x20, 0x20 }, },
{'Z', { 0xF8, 0x10, 0x20, 0x40, 0xF8 }, }
};

// MASK for optimizing power in Waterfall ID signal

int  id::mask[1024] = {
0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x02, 0x03, 0x00, 0x01, 0x02, 0x02, 0x04, 0x04, 0x06, 0x04,
0x00, 0x01, 0x02, 0x01, 0x00, 0x01, 0x06, 0x03, 0x00, 0x01, 0x02, 0x01, 0x04, 0x05, 0x0e, 0x02, 
0x00, 0x00, 0x02, 0x03, 0x04, 0x04, 0x04, 0x04, 0x00, 0x09, 0x02, 0x03, 0x04, 0x05, 0x0c, 0x01, 
0x00, 0x10, 0x12, 0x13, 0x04, 0x04, 0x14, 0x04, 0x18, 0x01, 0x1a, 0x02, 0x0c, 0x05, 0x08, 0x1d, 
0x00, 0x01, 0x02, 0x03, 0x04, 0x04, 0x06, 0x04, 0x00, 0x08, 0x0a, 0x03, 0x08, 0x0d, 0x08, 0x02, 
0x10, 0x01, 0x12, 0x02, 0x04, 0x05, 0x06, 0x16, 0x18, 0x10, 0x1a, 0x01, 0x18, 0x05, 0x02, 0x1b, 
0x20, 0x20, 0x02, 0x02, 0x04, 0x04, 0x04, 0x05, 0x28, 0x20, 0x02, 0x20, 0x08, 0x09, 0x04, 0x0d, 
0x10, 0x11, 0x22, 0x02, 0x04, 0x34, 0x02, 0x27, 0x38, 0x39, 0x20, 0x21, 0x14, 0x09, 0x2e, 0x28, 
0x00, 0x01, 0x02, 0x01, 0x00, 0x01, 0x04, 0x01, 0x08, 0x01, 0x08, 0x08, 0x0c, 0x04, 0x02, 0x0d, 
0x10, 0x11, 0x12, 0x01, 0x10, 0x01, 0x04, 0x01, 0x10, 0x11, 0x1a, 0x01, 0x10, 0x05, 0x0e, 0x1d, 
0x00, 0x21, 0x20, 0x22, 0x20, 0x25, 0x22, 0x01, 0x20, 0x09, 0x2a, 0x2a, 0x24, 0x20, 0x08, 0x09, 
0x10, 0x11, 0x10, 0x33, 0x04, 0x04, 0x34, 0x13, 0x38, 0x11, 0x20, 0x19, 0x18, 0x19, 0x06, 0x0c, 
0x40, 0x01, 0x40, 0x43, 0x04, 0x04, 0x40, 0x05, 0x40, 0x41, 0x42, 0x4b, 0x40, 0x4d, 0x0a, 0x41, 
0x50, 0x11, 0x02, 0x12, 0x44, 0x41, 0x40, 0x41, 0x08, 0x10, 0x12, 0x53, 0x40, 0x5c, 0x16, 0x4f, 
0x20, 0x61, 0x20, 0x01, 0x60, 0x25, 0x46, 0x05, 0x28, 0x28, 0x6a, 0x43, 0x40, 0x09, 0x42, 0x4c, 
0x70, 0x31, 0x70, 0x33, 0x60, 0x25, 0x12, 0x50, 0x58, 0x39, 0x2a, 0x09, 0x5c, 0x60, 0x0c, 0x0d, 
0x00, 0x01, 0x02, 0x02, 0x04, 0x01, 0x02, 0x06, 0x00, 0x09, 0x02, 0x08, 0x08, 0x09, 0x02, 0x0e, 
0x10, 0x10, 0x12, 0x02, 0x04, 0x01, 0x04, 0x14, 0x08, 0x19, 0x08, 0x13, 0x10, 0x10, 0x0e, 0x1d, 
0x20, 0x01, 0x02, 0x22, 0x20, 0x20, 0x22, 0x02, 0x28, 0x08, 0x2a, 0x29, 0x08, 0x0c, 0x0c, 0x0e, 
0x10, 0x30, 0x32, 0x10, 0x14, 0x34, 0x06, 0x36, 0x28, 0x01, 0x02, 0x1a, 0x0c, 0x2d, 0x1c, 0x1d, 
0x00, 0x41, 0x42, 0x41, 0x44, 0x04, 0x44, 0x06, 0x08, 0x09, 0x02, 0x41, 0x44, 0x48, 0x02, 0x06, 
0x40, 0x01, 0x12, 0x12, 0x10, 0x11, 0x54, 0x13, 0x58, 0x01, 0x50, 0x5a, 0x04, 0x19, 0x12, 0x57, 
0x60, 0x41, 0x40, 0x02, 0x20, 0x24, 0x02, 0x47, 0x08, 0x41, 0x08, 0x6a, 0x4c, 0x68, 0x6a, 0x47, 
0x10, 0x71, 0x42, 0x43, 0x10, 0x55, 0x26, 0x50, 0x10, 0x58, 0x72, 0x28, 0x2c, 0x39, 0x1a, 0x4f, 
0x80, 0x80, 0x82, 0x02, 0x04, 0x84, 0x02, 0x02, 0x88, 0x88, 0x08, 0x01, 0x08, 0x05, 0x04, 0x0d, 
0x10, 0x01, 0x12, 0x90, 0x84, 0x94, 0x02, 0x90, 0x10, 0x98, 0x9a, 0x98, 0x14, 0x9c, 0x16, 0x0b, 
0x20, 0x20, 0x80, 0xa3, 0x80, 0xa1, 0xa6, 0x87, 0x28, 0x29, 0xa8, 0xa8, 0x08, 0x09, 0x2a, 0x87, 
0x10, 0x80, 0x82, 0x11, 0xb0, 0xa5, 0x32, 0x32, 0xb8, 0xa0, 0x2a, 0x11, 0x9c, 0x81, 0x8e, 0xa4, 
0x40, 0xc0, 0xc2, 0x01, 0x40, 0xc5, 0x80, 0x05, 0xc0, 0x80, 0x4a, 0x09, 0x8c, 0x09, 0x0a, 0x09, 
0x50, 0x41, 0xc0, 0x41, 0x40, 0xc0, 0x12, 0x87, 0x80, 0xc0, 0x82, 0x83, 0x9c, 0xd8, 0x98, 0x16, 
0x20, 0x41, 0x82, 0x83, 0x80, 0xc0, 0x46, 0xc7, 0x48, 0xe0, 0x6a, 0x29, 0x4c, 0x29, 0xc0, 0x8e, 
0x40, 0xc0, 0x50, 0x21, 0x74, 0xf4, 0x32, 0x25, 0x28, 0x98, 0x9a, 0x32, 0x74, 0x58, 0x8e, 0x83, 
0x00, 0x00, 0x02, 0x01, 0x04, 0x05, 0x04, 0x06, 0x00, 0x09, 0x02, 0x02, 0x0c, 0x01, 0x06, 0x04, 
0x00, 0x10, 0x12, 0x13, 0x14, 0x14, 0x12, 0x16, 0x10, 0x19, 0x12, 0x01, 0x04, 0x18, 0x16, 0x17, 
0x20, 0x20, 0x22, 0x03, 0x04, 0x24, 0x24, 0x26, 0x20, 0x20, 0x20, 0x21, 0x0c, 0x2c, 0x2c, 0x24, 
0x20, 0x10, 0x30, 0x20, 0x04, 0x10, 0x04, 0x31, 0x10, 0x28, 0x08, 0x33, 0x0c, 0x05, 0x0c, 0x3d, 
0x40, 0x01, 0x40, 0x02, 0x04, 0x01, 0x44, 0x03, 0x40, 0x08, 0x0a, 0x08, 0x0c, 0x44, 0x4e, 0x44, 
0x10, 0x50, 0x40, 0x11, 0x54, 0x14, 0x06, 0x56, 0x58, 0x49, 0x52, 0x49, 0x18, 0x4d, 0x56, 0x49, 
0x20, 0x41, 0x02, 0x01, 0x44, 0x65, 0x64, 0x05, 0x20, 0x69, 0x6a, 0x23, 0x68, 0x0c, 0x0c, 0x27, 
0x70, 0x50, 0x72, 0x71, 0x54, 0x54, 0x14, 0x56, 0x40, 0x21, 0x22, 0x33, 0x44, 0x79, 0x2e, 0x57, 
0x00, 0x01, 0x80, 0x81, 0x84, 0x85, 0x84, 0x01, 0x88, 0x81, 0x8a, 0x8b, 0x80, 0x80, 0x02, 0x06, 
0x10, 0x90, 0x12, 0x80, 0x94, 0x80, 0x80, 0x11, 0x88, 0x09, 0x90, 0x19, 0x0c, 0x11, 0x06, 0x13, 
0xa0, 0x21, 0xa2, 0x21, 0xa4, 0x04, 0xa6, 0x24, 0x20, 0xa8, 0x8a, 0xa8, 0xa4, 0x85, 0x06, 0x06, 
0x10, 0x20, 0xb2, 0xa0, 0x10, 0xb4, 0x12, 0xb6, 0xb8, 0xa0, 0x18, 0x81, 0x24, 0x85, 0xb6, 0x0c, 
0xc0, 0xc1, 0xc0, 0x01, 0xc0, 0x44, 0x04, 0xc5, 0x08, 0xc0, 0x02, 0x0a, 0x4c, 0xc4, 0x8e, 0x03, 
0x10, 0xd0, 0x82, 0x90, 0x50, 0x11, 0xd4, 0x11, 0x98, 0x88, 0x12, 0x53, 0xd4, 0x81, 0xc4, 0xd4, 
0x20, 0x20, 0x62, 0xe2, 0x24, 0x85, 0xa6, 0xe5, 0x68, 0x69, 0xc0, 0xe9, 0xa0, 0xe9, 0x6c, 0x43, 
0x70, 0x71, 0x60, 0xf1, 0x94, 0x31, 0xf2, 0xc5, 0xf0, 0xc9, 0x8a, 0x79, 0x18, 0x25, 0x68, 0xf6, 
0x100, 0x01, 0x02, 0x100, 0x04, 0x01, 0x02, 0x05, 0x100, 0x08, 0x08, 0x100, 0x10c, 0x05, 0x04, 0x107, 
0x110, 0x11, 0x110, 0x12, 0x10, 0x105, 0x100, 0x12, 0x100, 0x119, 0x0a, 0x119, 0x11c, 0x14, 0x1a, 0x10f, 
0x20, 0x01, 0x02, 0x22, 0x24, 0x04, 0x24, 0x05, 0x100, 0x01, 0x120, 0x128, 0x28, 0x12c, 0x126, 0x2d, 
0x110, 0x10, 0x122, 0x130, 0x104, 0x14, 0x14, 0x117, 0x100, 0x09, 0x138, 0x39, 0x0c, 0x09, 0x12e, 0x13c, 
0x100, 0x40, 0x40, 0x02, 0x40, 0x141, 0x04, 0x107, 0x48, 0x108, 0x08, 0x140, 0x04, 0x140, 0x10e, 0x42, 
0x50, 0x141, 0x102, 0x52, 0x150, 0x114, 0x54, 0x45, 0x10, 0x101, 0x12, 0x153, 0x54, 0x101, 0x44, 0x5a, 
0x160, 0x161, 0x42, 0x101, 0x164, 0x105, 0x64, 0x65, 0x20, 0x121, 0x168, 0x10b, 0x10c, 0x10d, 0x120, 0x6a, 
0x100, 0x101, 0x22, 0x103, 0x54, 0x31, 0x14, 0x33, 0x158, 0x101, 0x50, 0x72, 0x74, 0x105, 0x7a, 0x150, 
0x80, 0x180, 0x82, 0x103, 0x184, 0x85, 0x100, 0x107, 0x80, 0x89, 0x8a, 0x09, 0x04, 0x101, 0x10e, 0x103, 
0x180, 0x90, 0x100, 0x11, 0x194, 0x190, 0x12, 0x107, 0x118, 0x11, 0x12, 0x09, 0x14, 0x09, 0x118, 0x11c, 
0x1a0, 0xa0, 0x1a2, 0x120, 0x1a4, 0x185, 0x126, 0x127, 0x1a0, 0x1a8, 0x8a, 0x123, 0x1a4, 0x18d, 0x2a, 0x22, 
0x80, 0x81, 0x1b0, 0x11, 0x94, 0x1b4, 0x32, 0x25, 0x120, 0xa0, 0x28, 0x8b, 0x120, 0x39, 0x12e, 0x13c, 
0x1c0, 0x1c0, 0x82, 0x41, 0x44, 0x180, 0x106, 0xc5, 0x100, 0x88, 0x180, 0x42, 0x1c4, 0xcd, 0xc4, 0x1c4, 
0x90, 0x90, 0x1c0, 0x103, 0xd4, 0x180, 0x110, 0x52, 0x50, 0x49, 0x52, 0x158, 0x180, 0xd5, 0x1c4, 0x4d, 
0x40, 0x101, 0x102, 0x21, 0x44, 0xc5, 0x1e4, 0xe2, 0x88, 0x1a1, 0x42, 0x22, 0x24, 0xac, 0x1a0, 0x127, 
0x50, 0x41, 0x112, 0xf2, 0x180, 0xc0, 0x1d6, 0x1b0, 0x50, 0x51, 0x32, 0x160, 0x74, 0x1bc, 0x1b0, 0x16f };

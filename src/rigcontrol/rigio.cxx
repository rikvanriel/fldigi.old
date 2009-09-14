// ====================================================================
//
// rigio.cxx  -- thread for reading/writing to serial port
//			   to which the transceiver is connected
//
// ====================================================================

#include <config.h>

#include <ctime>
#include <sys/time.h>
#include <iostream>
#include <list>
#include <vector>
#include <string>

#ifdef RIGCATTEST
	#include "rigCAT.h"
#else
	#include "fl_digi.h"
	#include "misc.h"
	#include "configuration.h"
#endif

#include "rigsupport.h"
#include "rigdialog.h"
#include "rigxml.h"
#include "trx.h"
#include "serial.h"
#include "rigio.h"
#include "debug.h"
#include "threads.h"
#include "qrunner.h"
#include "confdialog.h"

LOG_FILE_SOURCE(debug::LOG_RIGCONTROL);

using namespace std;

Cserial rigio;
static pthread_mutex_t  rigCAT_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_t		*rigCAT_thread = 0;

static bool			 rigCAT_exit = false;
static bool			 rigCAT_open = false;
static bool			 rigCAT_bypass = false;
static unsigned char	replybuff[200];
static unsigned char	sendbuff[200];

static string		   sRigWidth = "";
static string		   sRigMode = "";
static long long		llFreq = 0;

static bool nonCATrig = false;
static bool noXMLfile = false;
long long  noCATfreq = 3580000L;
string noCATmode = "USB";
string noCATwidth = "";

static void *rigCAT_loop(void *args);

int  readtimeout, readwait;

bool hexout(const string& s, int retnbr)
{
	unsigned char retbuf[2];
	int numread = 0;
	int numwrite = (int)s.size();

	for (int n = 0; n < progdefaults.RigCatRetries; n++) {
		memset(sendbuff,0, 200);
		for (unsigned int i = 0; i < s.length(); i++)
			sendbuff[i] = s[i];

// write the command string
		rigio.WriteBuffer(sendbuff, s.size());

// read the echo if required
		numread = 0;
		if (progdefaults.RigCatECHO == true) {
			memset(replybuff,0,200);
			while (numread < numwrite) {
				readwait = progdefaults.RigCatWait + 1;
				memset(retbuf, 0, 2);
				while (readwait && rigio.ReadBuffer(retbuf, 1) == 0) {
					MilliSleep(1);
					readwait--;
				}
				if (!readwait) break;
				replybuff[numread] = retbuf[0];
				numread++;
			}

			if (readwait == 0) {
				return false; // could not read the echo
			}
		}

// read the response if required
		if (retnbr > 0) {
			memset(replybuff,0,200);
			numread = 0;
			while (numread < retnbr) {
				readwait = progdefaults.RigCatWait + 1;
				memset(retbuf, 0, 2);
				while (readwait && rigio.ReadBuffer(retbuf, 1) == 0) {
					MilliSleep(1);
					readwait--;
				}
				if (!readwait) break;
				replybuff[numread] = retbuf[0];
				numread++;
			}
			if (numread == retnbr) {
				return true;
			} else
				LOG_ERROR("read fail, %d bytes, retry # %d", numread, n);
		}
		else // no response need
			return true; // just return true

	  if ((readtimeout = progdefaults.RigCatTimeout) > 0)
		  while (readtimeout--)
			  MilliSleep(1);
	}

// failed to read in RigCatRetries
	return false; // timed out
}

string to_bcd_be(long long freq, int len)
{
	string bcd = "";
	unsigned char a;
	int numchars = len / 2;
	if (len & 1) numchars ++;
	for (int i = 0; i < numchars; i++) {
		a = 0;
		a |= freq%10;
		freq /= 10;
		a |= (freq%10)<<4;
		freq /= 10;
		bcd += a;
	}
	return bcd;
}

string to_bcd(long long freq, int len)
{
	string bcd = "";
	string bcd_be = to_bcd_be(freq, len);
	int bcdlen = bcd_be.size();
	for (int i = bcdlen - 1; i >= 0; i--)
		bcd += bcd_be[i];
	return bcd;
}

long long fm_bcd (size_t p, int len)
{
	int i;
	long long f = 0;
	int numchars = len/2;
	if (len & 1) numchars ++;
	for (i = 0; i < numchars; i++) {
		f *=10;
		f += replybuff[p + i] >> 4;
		f *= 10;
		f += replybuff[p + i] & 0x0F;
	}
	return f;
}


long long fm_bcd_be(size_t p, int len)
{
	unsigned char temp;
	int numchars = len/2;
	if (len & 1) numchars++;
	for (int i = 0; i < numchars / 2; i++) {
		temp = replybuff[p + i];
		replybuff[p + i] = replybuff[p + numchars -1 - i];
		replybuff[p + numchars -1 - i] = temp;
	}
	return fm_bcd(p, len);
}

string to_binary_be(long long freq, int len)
{
	string bin = "";
	for (int i = 0; i < len; i++) {
		bin += freq & 0xFF;
		freq >>= 8;
	}
	return bin;
}

string to_binary(long long freq, int len)
{
	string bin = "";
	string bin_be = to_binary_be(freq, len);
	int binlen = bin_be.size();
	for (int i = binlen - 1; i >= 0; i--)
		bin += bin_be[i];
	return bin;
}

long long fm_binary(size_t p, int len)
{
	int i;
	long long f = 0;
	for (i = 0; i < len; i++) {
		f *= 256;
		f += replybuff[p + i];
	}
	return f;
}

long long fm_binary_be(size_t p, int len)
{
	unsigned char temp;
	int numchars = len/2;
	if (len & 1) numchars++;
	for (int i = 0; i < numchars / 2; i++) {
		temp = replybuff[p + i];
		replybuff[p + i] = replybuff[p + numchars -1 - i];
		replybuff[p + numchars -1 - i] = temp;
	}
	return fm_binary(p, len);
}

string to_decimal_be(long long d, int len)
{
	string sdec_be = "";
	for (int i = 0; i < len; i++) {
		sdec_be += (char)((d % 10) + '0');
		d /= 10;
	}
	return sdec_be;
}

string to_decimal(long long d, int len)
{
	string sdec = "";
	string sdec_be = to_decimal_be(d, len);
	int bcdlen = sdec_be.size();
	for (int i = bcdlen - 1; i >= 0; i--)
		sdec += sdec_be[i];
	return sdec;
}

long long fm_decimal(size_t p, int len)
{
	long long d = 0;
	for (int i = 0; i < len; i++) {
		d *= 10;
		d += replybuff[p + i] - '0';
	}
	return d;
}

long long fm_decimal_be(size_t p, int len)
{
	unsigned char temp;
	int numchars = len/2;
	if (len & 1) numchars++;
	for (int i = 0; i < numchars / 2; i++) {
		temp = replybuff[p + i];
		replybuff[p + i] = replybuff[p + numchars -1 - i];
		replybuff[p + numchars -1 - i] = temp;
	}
	return fm_decimal(p, len);
}

string to_freqdata(DATA d, long long f)
{
	int num, den;
	num = 100;
	den = (int)(d.resolution * 100);
	if (d.size == 0) return "";
	if (d.dtype == "BCD") {
		if (d.reverse == true)
			return to_bcd_be((long long int)(f * num / den), d.size);
		else
			return to_bcd((long long int)(f * num / den), d.size);
	} else if (d.dtype == "BINARY") {
		if (d.reverse == true)
			return to_binary_be((long long int)(f * num / den), d.size);
		else
			return to_binary((long long int)(f * num / den), d.size);
	} else if (d.dtype == "DECIMAL") {
		if (d.reverse == true)
			return to_decimal_be((long long int)(f * num / den), d.size);
		else
			return to_decimal((long long int)(f * num / den), d.size);
	}
	return "";
}

long long fm_freqdata(DATA d, size_t p)
{
	int num, den;
	num = (int)(d.resolution * 100);
	den = 100;
	if (d.dtype == "BCD") {
		if (d.reverse == true) {
			// LOG_INFO("BCD reverse");
			return (long long int)(fm_bcd_be(p, d.size) * num / den);
		} else {
			// LOG_INFO("BCD normal");
			return (long long int)(fm_bcd(p, d.size)  * num / den);
		}
	} else if (d.dtype == "BINARY") {
		if (d.reverse == true)
			return (long long int)(fm_binary_be(p, d.size)  * num / den);
		else
			return (long long int)(fm_binary(p, d.size)  * num / den);
	} else if (d.dtype == "DECIMAL") {
		if (d.reverse == true)
			return (long long int)(fm_decimal_be(p, d.size)  * num / den);
		else
			return (long long int)(fm_decimal(p, d.size)  * num / den);
	}
	return 0;
}

long long rigCAT_getfreq()
{
	XMLIOS modeCmd;
	list<XMLIOS>::iterator itrCmd;
	string strCmd;

	if (nonCATrig == true || noXMLfile) {
		return noCATfreq;
	}

//	LOG_DEBUG("get frequency");

	itrCmd = commands.begin();
	while (itrCmd != commands.end()) {
		if ((*itrCmd).SYMBOL == "GETFREQ")
			break;
		++itrCmd;
	}

	if (itrCmd == commands.end()) {
		LOG_WARN("GET_FREQ not defined");
		return -2; // get_freq command is not defined!
	}

	modeCmd = *itrCmd;

	if ( modeCmd.str1.empty() == false)
		strCmd.append(modeCmd.str1);

	if (modeCmd.str2.empty() == false)
		strCmd.append(modeCmd.str2);

	if (modeCmd.info.size() == 0)
		return 0;

	for (list<XMLIOS>::iterator preply = reply.begin(); preply != reply.end(); ++preply) {
		if (preply->SYMBOL != modeCmd.info)
			continue;

		XMLIOS  rTemp = *preply;
		size_t p = 0, pData = 0;
		size_t len = rTemp.str1.size();

		// send the command
		if (hexout(strCmd, rTemp.size ) == false) {
			LOG_ERROR("Hexout failed");
			return -1;
		}

		// check the pre data string
		if (len) {
			for (size_t i = 0; i < len; i++) {
				if ((char)rTemp.str1[i] != (char)replybuff[i]) {
					LOG_WARN("failed pre data string test @ %" PRIuSZ, i);
					return 0;
				}
			}
			p = len;
		}
		if (rTemp.fill1) p += rTemp.fill1;
		pData = p;
		if (rTemp.data.dtype == "BCD") {
			p += rTemp.data.size / 2;
			if (rTemp.data.size & 1) p++;
		} else
			p += rTemp.data.size;
		// check the post data string
		len = rTemp.str2.size();
		if (rTemp.fill2) p += rTemp.fill2;
		if (len) {
			for (size_t i = 0; i < len; i++)
				if ((char)rTemp.str2[i] != (char)replybuff[p + i]) {
					LOG_WARN("failed post data string test @ %" PRIuSZ, i);
					return 0;
				}
		}
		// convert the data field
		long long f = fm_freqdata(rTemp.data, pData);
		if ( f >= rTemp.data.min && f <= rTemp.data.max) {
			return f;
		}
		else {
			LOG_WARN("freq value failed %lld", f);
			return 0;
		}
	}

	return 0;
}

void rigCAT_setfreq(long long f)
{
	XMLIOS modeCmd;
	list<XMLIOS>::iterator itrCmd;
	string strCmd;

	if (noXMLfile || nonCATrig) {
		noCATfreq = f;
		return;
	}

//	LOG_DEBUG("set frequency %lld", f);

	itrCmd = commands.begin();
	while (itrCmd != commands.end()) {
		if ((*itrCmd).SYMBOL == "SETFREQ")
			break;
		++itrCmd;
	}
	if (itrCmd == commands.end()) {
		LOG_WARN("SET_FREQ not defined");
		noCATfreq = f;
		return;
	}

	modeCmd = *itrCmd;

	if ( modeCmd.str1.empty() == false)
		strCmd.append(modeCmd.str1);

	strCmd.append( to_freqdata(modeCmd.data, f) );

	if (modeCmd.str2.empty() == false)
		strCmd.append(modeCmd.str2);

	if (modeCmd.ok.size()) {
		list<XMLIOS>::iterator preply = reply.begin();
		while (preply != reply.end()) {
			if (preply->SYMBOL == modeCmd.ok) {
				XMLIOS  rTemp = *preply;
// send the command
				pthread_mutex_lock(&rigCAT_mutex);
					hexout(strCmd, rTemp.size);
				pthread_mutex_unlock(&rigCAT_mutex);
				return;
			}
			preply++;
		}
	} else {
		pthread_mutex_lock(&rigCAT_mutex);
			hexout(strCmd, 0);
		pthread_mutex_unlock(&rigCAT_mutex);
	}
}

string rigCAT_getmode()
{
	XMLIOS modeCmd;
	list<XMLIOS>::iterator itrCmd;
	string strCmd;

	if (nonCATrig == true || noXMLfile)
		return noCATmode;

//	LOG_DEBUG("get mode");

	itrCmd = commands.begin();
	while (itrCmd != commands.end()) {
		if ((*itrCmd).SYMBOL == "GETMODE")
			break;
		++itrCmd;
	}
	if (itrCmd == commands.end())
		return "";
	modeCmd = *itrCmd;

	if ( modeCmd.str1.empty() == false)
		strCmd.append(modeCmd.str1);

	if (modeCmd.str2.empty() == false)
		strCmd.append(modeCmd.str2);

	if (modeCmd.info.size()) {
		list<XMLIOS>::iterator preply = reply.begin();
		while (preply != reply.end()) {
			if (preply->SYMBOL == modeCmd.info) {
				XMLIOS  rTemp = *preply;
				size_t p = 0, pData = 0;
// send the command
				if (hexout(strCmd, rTemp.size) == false) {
					return "";
				}

// check the pre data string
				size_t len = rTemp.str1.size();
				if (len) {
					for (size_t i = 0; i < len; i++)
						if ((char)rTemp.str1[i] != (char)replybuff[i])
							return "";
					p = len;
				}
				if (rTemp.fill1) p += rTemp.fill1;
				pData = p;
// check the post data string
				p += rTemp.data.size;
				len = rTemp.str2.size();
				if (rTemp.fill2) p += rTemp.fill2;
				if (len) {
					for (size_t i = 0; i < len; i++)
						if ((char)rTemp.str2[i] != (char)replybuff[p + i])
							return "";
				}
// convert the data field
				string mData = "";
				for (int i = 0; i < rTemp.data.size; i++)
					mData += (char)replybuff[pData + i];
// new for FT100 and the ilk that use bit fields
				if (rTemp.data.size == 1) {
					unsigned char d = mData[0];
					if (rTemp.data.shiftbits)
						d >>= rTemp.data.shiftbits;
					d &= rTemp.data.andmask;
					mData[0] = d;
				}
				list<MODE>::iterator mode;
				list<MODE> *pmode;
				if (lmodes.empty() == false)
					pmode = &lmodes;
				else if (lmodeREPLY.empty() == false)
					pmode = &lmodeREPLY;
				else
					return "";
				mode = pmode->begin();
				while (mode != pmode->end()) {
					if ((*mode).BYTES == mData)
						break;
					mode++;
				}
				if (mode != pmode->end())
					return ((*mode).SYMBOL);
				else
					return "";
			}
			preply++;
		}
	}

	return "";
}

void rigCAT_setmode(const string& md)
{
	XMLIOS modeCmd;
	list<XMLIOS>::iterator itrCmd;
	string strCmd;

	if (nonCATrig == true || noXMLfile) {
		noCATmode = md;
		return;
	}

//	LOG_DEBUG("set mode %s", md.c_str());

	itrCmd = commands.begin();
	while (itrCmd != commands.end()) {
		if ((*itrCmd).SYMBOL == "SETMODE")
			break;
		++itrCmd;
	}
	modeCmd = *itrCmd;

	if ( modeCmd.str1.empty() == false)
		strCmd.append(modeCmd.str1);

	if ( modeCmd.data.size > 0 ) {
		list<MODE>::iterator mode;
		list<MODE> *pmode;
		if (lmodes.empty() == false)
			pmode = &lmodes;
		else if (lmodeCMD.empty() == false)
			pmode = &lmodeCMD;
		else
			return;
		mode = pmode->begin();
		while (mode != pmode->end()) {
			if ((*mode).SYMBOL == md)
				break;
			mode++;
		}
		if (mode != pmode->end())
			strCmd.append( (*mode).BYTES );
	}
	if (modeCmd.str2.empty() == false)
		strCmd.append(modeCmd.str2);

	if (modeCmd.ok.size()) {
		list<XMLIOS>::iterator preply = reply.begin();
		while (preply != reply.end()) {
			if (preply->SYMBOL == modeCmd.ok) {
				XMLIOS  rTemp = *preply;
// send the command
				pthread_mutex_lock(&rigCAT_mutex);
					hexout(strCmd, rTemp.size);
				pthread_mutex_unlock(&rigCAT_mutex);
				return;
			}
			preply++;
		}
	} else {
		pthread_mutex_lock(&rigCAT_mutex);
			hexout(strCmd, 0);
		pthread_mutex_unlock(&rigCAT_mutex);
	}
}

string rigCAT_getwidth()
{
	XMLIOS modeCmd;
	list<XMLIOS>::iterator itrCmd;
	string strCmd;

	if (nonCATrig == true || noXMLfile)
		return noCATwidth;

//	LOG_DEBUG("get width");

	itrCmd = commands.begin();
	while (itrCmd != commands.end()) {
		if ((*itrCmd).SYMBOL == "GETBW")
			break;
		++itrCmd;
	}
	if (itrCmd == commands.end())
		return "";
	modeCmd = *itrCmd;

	if ( modeCmd.str1.empty() == false)
		strCmd.append(modeCmd.str1);

	if (modeCmd.str2.empty() == false)
		strCmd.append(modeCmd.str2);

	if (modeCmd.info.size()) {
		list<XMLIOS>::iterator preply = reply.begin();
		while (preply != reply.end()) {
			if (preply->SYMBOL == modeCmd.info) {
				XMLIOS  rTemp = *preply;
				size_t p = 0, pData = 0;
// send the command
				if (hexout(strCmd, rTemp.size) == false) {
					return "";
				}


// check the pre data string
				size_t len = rTemp.str1.size();
				if (len) {
					for (size_t i = 0; i < len; i++)
						if ((char)rTemp.str1[i] != (char)replybuff[i])
							return "";
					p = pData = len;
				}
				if (rTemp.fill1) p += rTemp.fill1;
				pData = p;
				p += rTemp.data.size;
// check the post data string
				if (rTemp.fill2) p += rTemp.fill2;
				len = rTemp.str2.size();
				if (len) {
					for (size_t i = 0; i < len; i++)
						if ((char)rTemp.str2[i] != (char)replybuff[p + i])
							return "";
				}
// convert the data field
				string mData = "";
				for (int i = 0; i < rTemp.data.size; i++)
					mData += (char)replybuff[pData + i];
// new for FT100 and the ilk that use bit fields
				if (rTemp.data.size == 1) {
					unsigned char d = mData[0];
					if (rTemp.data.shiftbits)
						d >>= rTemp.data.shiftbits;
					d &= rTemp.data.andmask;
					mData[0] = d;
				}
				list<BW>::iterator bw;
				list<BW> *pbw;
				if (lbws.empty() == false)
					pbw = &lbws;
				else if (lbwREPLY.empty() == false)
					pbw = &lbwREPLY;
				else
					return "";
				bw = pbw->begin();
				while (bw != pbw->end()) {
					if ((*bw).BYTES == mData)
						break;
					bw++;
				}
				if (bw != pbw->end())
					return ((*bw).SYMBOL);
				else
					return "";
			}
			preply++;
		}
	}
	return "";
}

void rigCAT_setwidth(const string& w)
{
	XMLIOS modeCmd;
	list<XMLIOS>::iterator itrCmd;
	string strCmd;

//	LOG_DEBUG("set width");

	if (noXMLfile) {
		noCATwidth = w;
		return;
	}

	itrCmd = commands.begin();
	while (itrCmd != commands.end()) {
		if ((*itrCmd).SYMBOL == "SETBW")
			break;
		++itrCmd;
	}
	if (itrCmd == commands.end()) {
		noCATwidth = w;
		return;
	}
	modeCmd = *itrCmd;

	if ( modeCmd.str1.empty() == false)
		strCmd.append(modeCmd.str1);

	if ( modeCmd.data.size > 0 ) {

		list<BW>::iterator bw;
		list<BW> *pbw;
		if (lbws.empty() == false)
			pbw = &lbws;
		else if (lbwCMD.empty() == false)
			pbw = &lbwCMD;
		else
			return;
		bw = pbw->begin();
		while (bw != pbw->end()) {
			if ((*bw).SYMBOL == w)
				break;
			bw++;
		}
		if (bw != pbw->end())
			strCmd.append( (*bw).BYTES );
	}
	if (modeCmd.str2.empty() == false)
		strCmd.append(modeCmd.str2);

	if (modeCmd.ok.size()) {
		list<XMLIOS>::iterator preply = reply.begin();
		while (preply != reply.end()) {
			if (preply->SYMBOL == modeCmd.ok) {
				XMLIOS  rTemp = *preply;
// send the command
				pthread_mutex_lock(&rigCAT_mutex);
					hexout(strCmd, rTemp.size);
				pthread_mutex_unlock(&rigCAT_mutex);
				return;
			}
			preply++;
		}
	} else {
		pthread_mutex_lock(&rigCAT_mutex);
			hexout(strCmd, 0);
		pthread_mutex_unlock(&rigCAT_mutex);
	}
}

void rigCAT_pttON()
{
	XMLIOS modeCmd;
	list<XMLIOS>::iterator itrCmd;
	string strCmd;

	LOG_INFO("ptt ON");

	rigio.SetPTT(1); // always execute the h/w ptt if enabled

	itrCmd = commands.begin();
	while (itrCmd != commands.end()) {
		if ((*itrCmd).SYMBOL == "PTTON")
			break;
		++itrCmd;
	}
	if (itrCmd == commands.end())
		return;
	modeCmd = *itrCmd;

	if ( modeCmd.str1.empty() == false)
		strCmd.append(modeCmd.str1);
	if (modeCmd.str2.empty() == false)
		strCmd.append(modeCmd.str2);

	if (modeCmd.ok.size()) {
		list<XMLIOS>::iterator preply = reply.begin();
		while (preply != reply.end()) {
			if (preply->SYMBOL == modeCmd.ok) {
				XMLIOS  rTemp = *preply;
// send the command
				hexout(strCmd, rTemp.size);
				return;
			}
			preply++;
		}
	} else {
		hexout(strCmd, 0);
	}

}

void rigCAT_pttOFF()
{
	XMLIOS modeCmd;
	list<XMLIOS>::iterator itrCmd;
	string strCmd;

	LOG_INFO("ptt OFF");

	rigio.SetPTT(0); // always execute the h/w ptt if enabled

	itrCmd = commands.begin();
	while (itrCmd != commands.end()) {
		if ((*itrCmd).SYMBOL == "PTTOFF")
			break;
		++itrCmd;
	}
	if (itrCmd == commands.end())
		return;
	modeCmd = *itrCmd;

	if ( modeCmd.str1.empty() == false)
		strCmd.append(modeCmd.str1);
	if (modeCmd.str2.empty() == false)
		strCmd.append(modeCmd.str2);

	if (modeCmd.ok.size()) {
		list<XMLIOS>::iterator preply = reply.begin();
		while (preply != reply.end()) {
			if (preply->SYMBOL == modeCmd.ok) {
				XMLIOS  rTemp = *preply;
// send the command
				hexout(strCmd, rTemp.size);
				return;
			}
			preply++;
		}
	} else {
		hexout(strCmd, 0);
	}
}

void rigCAT_sendINIT()
{
	XMLIOS modeCmd;
	list<XMLIOS>::iterator itrCmd;
	string strCmd;

	if (noXMLfile)
		return;

//	LOG_DEBUG("INIT rig");

	itrCmd = commands.begin();
	while (itrCmd != commands.end()) {
		if ((*itrCmd).SYMBOL == "INIT")
			break;
		++itrCmd;
	}
	if (itrCmd == commands.end())
		return;
	modeCmd = *itrCmd;

	if ( modeCmd.str1.empty() == false)
		strCmd.append(modeCmd.str1);
	if (modeCmd.str2.empty() == false)
		strCmd.append(modeCmd.str2);

	if (modeCmd.ok.size()) {
		list<XMLIOS>::iterator preply = reply.begin();
		while (preply != reply.end()) {
			if (preply->SYMBOL == modeCmd.ok) {
				XMLIOS  rTemp = *preply;
// send the command
				pthread_mutex_lock(&rigCAT_mutex);
					hexout(strCmd, rTemp.size);
				pthread_mutex_unlock(&rigCAT_mutex);
				return;
			}
			preply++;
		}
	} else {
		pthread_mutex_lock(&rigCAT_mutex);
			hexout(strCmd, 0);
		pthread_mutex_unlock(&rigCAT_mutex);
	}
}

void rigCAT_defaults()
{
	progdefaults.XmlRigBaudrate = xmlrig.baud;
	mnuXmlRigBaudrate->value(xmlrig.baud);

	progdefaults.RigCatRTSplus = xmlrig.rts;
	btnRigCatRTSplus->value(xmlrig.rts);

	progdefaults.RigCatDTRplus = xmlrig.dtr;
	btnRigCatDTRplus->value(xmlrig.dtr);

	progdefaults.RigCatRTSptt = xmlrig.rtsptt;
	btnRigCatRTSptt->value(xmlrig.rtsptt);

	progdefaults.RigCatDTRptt = xmlrig.dtrptt;
	btnRigCatDTRptt->value(xmlrig.dtrptt);

	progdefaults.RigCatRTSCTSflow = xmlrig.rtscts;
	chkRigCatRTSCTSflow->value(xmlrig.rtscts);

	progdefaults.RigCatRetries = xmlrig.retries;
	cntRigCatRetries->value(xmlrig.retries);

	progdefaults.RigCatTimeout = xmlrig.timeout;
	cntRigCatTimeout->value(xmlrig.timeout);

	progdefaults.RigCatWait = xmlrig.write_delay;
	cntRigCatWait->value(xmlrig.write_delay);

	progdefaults.RigCatECHO = xmlrig.echo;
	btnRigCatEcho->value(xmlrig.echo);

	progdefaults.RigCatCMDptt = xmlrig.cmdptt;
	btnRigCatCMDptt->value(xmlrig.cmdptt);

}

bool rigCAT_init(bool useXML)
{

	if (rigCAT_open == true) {
		LOG_ERROR("RigCAT already open");
		return false;
	}

	noXMLfile = true;
	sRigMode = "";
	sRigWidth = "";
	nonCATrig = true;

	if (useXML == true) {
		if (readRigXML() == false)
			LOG_ERROR("No rig.xml file present");
		else {
			noXMLfile = false;
			rigio.Device(progdefaults.XmlRigDevice);
			rigio.Baud(progdefaults.BaudRate(progdefaults.XmlRigBaudrate));
			rigio.RTS(progdefaults.RigCatRTSplus);
			rigio.DTR(progdefaults.RigCatDTRplus);
			rigio.RTSptt(progdefaults.RigCatRTSptt);
			rigio.DTRptt(progdefaults.RigCatDTRptt);
			rigio.RTSCTS(progdefaults.RigCatRTSCTSflow);
			rigio.Stopbits(progdefaults.RigCatStopbits);

			LOG_INFO("\n\
Serial port parameters:\n\
device     : %s\n\
baudrate   : %d\n\
stopbits   : %d\n\
retries    : %d\n\
timeout    : %d\n\
wait       : %d\n\
initial rts: %+d\n\
use rts ptt: %c\n\
initial dtr: %+d\n\
use dtr ptt: %c\n\
flowcontrol: %c\n\
echo       : %c\n",
				rigio.Device().c_str(),
				rigio.Baud(),
				rigio.Stopbits(),
				progdefaults.RigCatRetries,
				progdefaults.RigCatTimeout,
				progdefaults.RigCatWait,
				(rigio.RTS() ? +12 : -12), (rigio.RTSptt() ? 'T' : 'F'),
				(rigio.DTR() ? +12 : -12), (rigio.DTRptt() ? 'T' : 'F'),
				(rigio.RTSCTS() ? 'T' : 'F'),
				progdefaults.RigCatECHO ? 'T' : 'F');

			if (rigio.OpenPort() == false) {
				LOG_ERROR("Cannot open serial port %s", rigio.Device().c_str());
				return false;
			}
			sRigMode = "";
			sRigWidth = "";

			nonCATrig = false;

			if (rigCAT_getfreq() == -1) { // will set nonCATrig to true if getfreq not spec'd
				LOG_ERROR("Xcvr Freq request not answered");
				rigio.ClosePort();
				return false;
			} else {
				rigCAT_sendINIT();
				init_Xml_RigDialog();
			}
		}
	} else
		init_NoRig_RigDialog();

	llFreq = 0;
	rigCAT_bypass = false;
	rigCAT_exit = false;

	rigCAT_thread = new pthread_t;

	if (pthread_create(rigCAT_thread, NULL, rigCAT_loop, NULL) < 0) {
		LOG_ERROR("pthread_create failed");
		rigio.ClosePort();
		return false;
	}

	rigCAT_open = true;
	return true;
}

void rigCAT_close(void)
{
	int count = 200;
	if (rigCAT_open == false)
		return;
	rigCAT_exit = true;

	while (rigCAT_open == true) {
		MilliSleep(50);
		count--;
		if (!count) {
			LOG_ERROR("RigCAT stuck");
			pthread_mutex_lock(&rigCAT_mutex);
			rigio.ClosePort();
			pthread_mutex_unlock(&rigCAT_mutex);
			exit(0);
		}
	}
	delete rigCAT_thread;
	rigCAT_thread = 0;
	LOG_DEBUG("RigCAT closed, count = %d", count);
}

bool rigCAT_active(void)
{
	return (rigCAT_open);
}

void rigCAT_set_ptt(int ptt)
{
	if (rigCAT_open == false)
		return;
	pthread_mutex_lock(&rigCAT_mutex);
		if (ptt) {
			rigCAT_pttON();
			rigCAT_bypass = true;
		} else{
			rigCAT_pttOFF();
			rigCAT_bypass = false;
		}
	pthread_mutex_unlock(&rigCAT_mutex);
}

#ifndef RIGCATTEST
void rigCAT_set_qsy(long long f, long long fmid)
{
	if (rigCAT_open == false)
		return;

	// send new freq to rig
	rigCAT_setfreq(f);

	if (active_modem->freqlocked() == true) {
		active_modem->set_freqlock(false);
		active_modem->set_freq((int)fmid);
		active_modem->set_freqlock(true);
	} else
		active_modem->set_freq((int)fmid);
	wf->rfcarrier(f);
	wf->movetocenter();
}
#endif

bool ModeIsLSB(const string& s)
{
	if (noXMLfile) {
		if (s == "LSB")
			return true;
		return false;
	}
	list<string>::iterator pM = LSBmodes.begin();
	while (pM != LSBmodes.end() ) {
		if (*pM == s)
			return true;
		pM++;
	}
	return false;
}

static void *rigCAT_loop(void *args)
{
	SET_THREAD_ID(RIGCTL_TID);

	long long freq = 0L;
	string sWidth, sMode;

	for (;;) {
		MilliSleep(200);

		if (rigCAT_bypass == true)
			continue;
		if (rigCAT_exit == true)
			break;

		pthread_mutex_lock(&rigCAT_mutex);
			freq = rigCAT_getfreq();
		pthread_mutex_unlock(&rigCAT_mutex);

		pthread_mutex_lock(&rigCAT_mutex);
			sWidth = rigCAT_getwidth();
		pthread_mutex_unlock(&rigCAT_mutex);

		pthread_mutex_lock(&rigCAT_mutex);
			sMode = rigCAT_getmode();
		pthread_mutex_unlock(&rigCAT_mutex);

		if ((freq > 0) && (freq != llFreq)) {
			llFreq = freq;
			show_frequency(freq);
			wf->rfcarrier(freq);
		}

		if (sWidth.size() && sWidth != sRigWidth) {
			sRigWidth = sWidth;
			show_bw(sWidth);
		}

		if (sMode.size() && sMode != sRigMode) {
			sRigMode = sMode;
			if (ModeIsLSB(sMode))
				wf->USB(false);
			else
				wf->USB(true);
			show_mode(sMode);
		}
	}

	if (rigcontrol)
		rigcontrol->hide();
	wf->USB(true);
//  wf->setQSY(0);

	pthread_mutex_lock(&rigCAT_mutex);
		LOG_DEBUG("Exiting rigCAT loop, closing serial port");
		rigio.ClosePort();
	pthread_mutex_unlock(&rigCAT_mutex);

	rigCAT_open = false;
	rigCAT_exit = false;
	rigCAT_bypass = false;

	return NULL;
}


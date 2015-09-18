// ---------------------------------------------------------------------------
#include <vcl.h>
#include <windows.h>
#include "ff.h"
#include "vdskapi.h"
#include "disk.h"
#include "netstorage.h"
#include "cache.h"
#include "extern.h"
#include "ftp.h"
#include "FtpControl.h"

#pragma hdrstop
#pragma argsused

#pragma link "madExcept"
#pragma link "madLinkDisAsm"

using namespace std;
bool breakpasvread = false;
vector<EVENT>events;
extern DWORD heapsize;
int ftplockline = -2;
extern TCriticalSection *filescs;
DWORD pasvsockconntimeout = 2000;
unsigned long lastdelay = 0;
unsigned long timeoutst = 0;
unsigned long netfatfilesize;
char loadfile[128] = " ";
extern bool processing_disk_event;
char ftpcurdir[128] = "<ssss>";
unsigned long ftpgetfileoffset = 0, ftpgetfilelen = 0;
// ---------------------------------------------------------------------------
char temp8[1024];
bool unmounting = false;
TCriticalSection *ccs = 0;
unsigned int nf = 0;
// FtpMount *fm;
unsigned long precacheminbytes = 0;
long sockets_buffers = 0;
double avg = 1, avg2 = 2;
SOCKET tempsock;
bool sockets_nodelay = false;
double formatp = 0.0;
int dirlev = 0;
double delayperc = 0;

double dp;
DWORD tmsleft = 0;
double gt = 0, bt = 0;
LPVOID rootdirptr;
// int ftpport = 0;
unsigned long maxfilesizemb = 50000;
char ftphost[128] = "";
extern unsigned char fat32[];
LARGE_INTEGER sizee;
TCriticalSection *ecs = 0;
int totreads = 0, totwrites = 0;
// formatthread *ft = NULL;
LONGLONG curdisksize = 0;
LONGLONG fatsize;
DWORD fs;
TCriticalSection *cchs;
char ip[128];
int ftplport;
TEvent *cev;
LONGLONG totcachsize = 0;
DWORD fat1, fat2;
int ftpstartport = 21;
LPVOID pData;
// TForm1 *Form1;
extern unsigned char fb[];

unsigned long rntrys = 0;
LONGLONG rlastoffset = 0;
unsigned long wntrys = 0;
LONGLONG wlastoffset = 0;
HANDLE hdisk = NULL;
LPVOID memory = NULL;
SOCKET ConnectSocket = 0, FtpSocket = 0;
LONGLONG clstsize = 4096;
bool precache = false;
DWORD ftpcontroltime = 0, ftpdatatime = 0;
char ftpuser[128];
char ftppass[128];
int ftpstate = -5;
unsigned long lastpasvread = 0;
bool showformathint;
int numpkt = 1;
bool loggedin = false;
DWORD fspace_ptr;
unsigned long start_cluster = 3;
unsigned long stc;
unsigned long totclusters;
unsigned long dataoffset;
char ftpcmdhist[128], ftprlhist[128];
char lastftpcmd[1024] = "";
char lastftprep[1024];
LPVOID dirptr;
int nfiles = 0;
LONGLONG maxdiskuse = 0;
unsigned long rootdirectory = 0;
int err;
bool nconn(void);
int ftpport = 21;
bool ramdisk = false;
char savefile[128];
LONGLONG disksize = 32 * 1024 * 1024 * 1024;
// statthread *st = NULL;
char ftpgetfilename[128];
// CacheThread *ct = NULL;
TCriticalSection *ftpcs = 0;
char drive_letter = 'y';
double ibytesin = 0, ibytes_sttime = 0, ibytes_st = 0, ibps = 0, ilastlen;
double obytesin = 0, obytes_sttime = 0, obytes_st = 0, obps = 0, olastlen;
double maxquad = 0;
double totalmb = 0;
bool sockets_oobinline = false;
unsigned long pfat1, pfat2;
char serv[] = "192.168.1.107"; // "192.168.253.129"; // "69.197.152.44";
unsigned int *diskcolor = NULL;
PKT pkt;
char delaystr[128];
int pids[0xffff];
vector<FATFILE>files;
vector<CACHE>cache;
unsigned int *fat;
char fatfiles[32768] = "";
FAT32BOOT fat32boot;

FtpControl *fc = 0;

int WINAPI DllEntryPoint(HINSTANCE hinst, DWORD reason, void* lpReserved)
{
	if (reason == DLL_PROCESS_ATTACH)
	{
		fc = new FtpControl(false);
		// deb("fatftp.dll attached [hinst %x, FtpControl %x].", hinst, fc);
	}
	else if (reason == DLL_THREAD_ATTACH)
	{
		// events.clear();
		// deb("fatftp.dll thread attached %x", GetCurrentThread());
	}
	return 1;
}

void _call_events(char *func, int line, int type, LPVOID arg, LPVOID arg2, LPVOID arg3,
	LPVOID arg4, LPVOID arg5, LPVOID arg6, LPVOID arg7, LPVOID arg8)
{
	static unsigned long numevent = 0;

	static bool calling_events = false;
	bool waited = false;
	LARGE_INTEGER arrivetime;
	if (calling_events)
	{
		QueryPerformanceCounter(&arrivetime);
	}

	// while (calling_events)
	// {
	// // fdeb("THREAD %x : call_events( %d, %x, %x ... ", GetCurrentThreadId(), type, arg, arg2);
	// // fdeb(" waiting %5d ms ...", GetTickCount() - stms);
	// Sleep(1);
	// i waited = true;
	// }

	DWORD stms = GetTickCount();
	if (!calling_events)
		QueryPerformanceCounter(&arrivetime);
	calling_events = true;

	for (vector<EVENT>::iterator it = events.begin(); it != events.end(); it++)
	{
		call_func cf;
		cf = (call_func)(*it).addr;

		try
		{
			// fdeb("%4x calling %8p type %2d arg %8x", GetCurrentThreadId(), cf, type, arg);
			cf(type, arrivetime, arg, arg2, arg3, arg4, arg5, arg6, arg7, arg8);
		}
		catch(...)
		{
			fdeb("thread=%x, exception inside event func=0x%p ( %2d, %x, %x ", GetCurrentThreadId(),
				cf, type, arg, arg2);
			fdeb("exception called from %s():%d", func, line);
		}
	}

	if (waited)
		deb(" call_events done in %d ms ", GetTickCount() - stms);
	calling_events = false;
	numevent++;
}

void ftpblocking(bool block)
{
	unsigned long iMode;
	iMode = !block;

	if (FtpSocket <= 0)
	{
		// deb("ftpblocking zero socket!");
		return;
	}
	ioctlsocket(FtpSocket, FIONBIO, (unsigned long*) & iMode);
	// ioctlsocket(FtpSocket, FIONREAD, (unsigned long*)&iMode);

}

void ftpdisconnect(void)
{
	closesocket(FtpSocket);
	closesocket(tempsock);

	tempsock = 0;
	FtpSocket = 0;
	ftpstate = -1;

	call_events(E_FTPDISCONNECT, (LPVOID)__FILE__, (LPVOID)__LINE__, (LPVOID)ftpstate);
}

int _ftprl(char *func, int line, char *buf, DWORD maxms = 0, char *sign = 0)
{
	char *bp = buf;
	int nr = 0;
	int ss = 0;
	memset(buf, 0, 2056);
	buf[0] = 1;
	int totr = 0;
	DWORD ftime = GetTickCount();

	try
	{
		if (unmounting)
		{
			ExitThread(0);
		}
		sprintf(ftprlhist, "%s():%5d -> ftprl(maxms=%lu,sign=%s)", func, line, (unsigned long)maxms,
			(int)sign > 0 ? sign : "");
		ftpstate = 0;
		char blaaa[1111];
		ftpblocking(false);
		DWORD tms = GetTickCount();
		int flags = 0;

		call_events(E_FTPREADINGLINE, (LPVOID)func, (LPVOID)line);
		// ftplock();
		while (true)
		{
			if (ss > 0)
				bp++;
			// ftpblocking(false);
			ss = recv(FtpSocket, bp, 1, flags);
			// ftpblocking(true);
			if (ss > 0 && !*bp && !sign)
				break;

			if (ss > 0)
			{
				ibytesin += ss;
				if (ss > 1)
					ilastlen = (double)ss;
				totr += ss;
			}

			if (maxms > 0 && (int)sign == 0 && GetTickCount() - tms >= maxms / 4 && ss <= 0 && strstr
				(buf, "\r\n"))
				break;

			if (GetTickCount() - tms >= 2400)
			{
				deb("%s max rl timeout", ftprlhist);
				call_events(E_TIMEOUT, (LPVOID)func, (LPVOID)line, (LPVOID)ftprlhist);
				break;
			}
			if (maxms == (DWORD) - 1 && GetTickCount() - tms >=
				(totr ? lastdelay ? lastdelay * 3 : 100 : timeoutst))
			{
				call_events(E_TIMEOUT, (LPVOID)__FILE__, (LPVOID)__LINE__);
				break;
			}
			// if (maxms > 0 && GetTickCount() - tms >= maxms && !sign)
			// break;
			if (((int)sign > 0 && ss <= 0 && strstr(buf, sign) && strstr(buf, "\r\n")))
			{
				break;
			}
			// if ((int)sign <= 0 && strstr(buf, "\r\n"))
			// break;
			if (!ss)
				break;
		}
		// ftpunlock();
		if (ss < 0)
		{
			int err = WSAGetLastError();
			// deb("rln: ss recv : %d err:%d", ss, err);
		}
		// deb("r: %c (%x)",*bp,*bp);
		tms = GetTickCount() - tms;
		lastdelay = tms;
		// if (maxms)
		// ftpblocking(true);
		deb(ftprlhist);
		deb("rln(sign=%s,maxms=%lu) totr:%d in %u ms, *bp=%x, ss=%d, err:%d", sign,
			(unsigned long)maxms, totr, tms, *bp, ss, err);
		if (totr && tms && tms > timeoutst)
		{
			timeoutst = (DWORD)(floor((double)tms * 1.9));
			// ftpdeb(" %%+ packet delay %lu ms", timeoutst);
			call_events(E_TIMEOUTCHANGE, (LPVOID)timeoutst);
		}
		else if (totr && tms && tms < timeoutst)
		{
			timeoutst = (DWORD)(floor((double)tms * 1.9));
			// ftpdeb(" %%- packet delay %lu ms", timeoutst);
			call_events(E_TIMEOUTCHANGE, (LPVOID)timeoutst);
		}
		if (tms <= 0)
		{
			timeoutst = 1000;
			call_events(E_TIMEOUTCHANGE, (LPVOID)timeoutst);
		}
		char *p;
		if (totr && // buf[0] == '2' && buf[1] == '2' && buf[2] == '7' //&&
			(p = strstr(buf, "227 Entering Passive")))
		{
			deb("227 string ");
			char portaddr[255];
			sscanf(p, "%*s %*s %*s %*s %s", portaddr);
			deb("portaddr '%s'", portaddr);
			unsigned long port1, port2;
			int ip1, ip2, ip3, ip4;
			sscanf(portaddr + 1, "%d,%d,%d,%d,%d,%d", &ip1, &ip2, &ip3, &ip4, &port1, &port2);
			sprintf(ftphost, "%d.%d.%d.%d", ip1, ip2, ip3, ip4);
			unsigned long port3 = port1 * 256 + port2;
			ftpport = (int)port3;
			deb("port addr %s:%d", ftphost, ftpport);
		}
		strncpy(lastftprep, buf, sizeof(lastftprep));
		ftime = GetTickCount() - ftime;
		ftpcontroltime += ftime;
		if (totr)
		{

			deb("> %s", buf);
			call_events(E_FTPREPLY, buf, (LPVOID)ftime, (LPVOID)func, (LPVOID)line);
		}
		call_events(E_FTPREADLINE, (LPVOID)buf, (LPVOID)ftime);
		// deb("ftprl() ext");
		// Form1->ftpent->Text = "";
		ftpstate = -1;
		// if (buf[0] == '5' && buf[1] == '3' && buf[2] == '0')
		// ftpauth();
		// Application->ProcessMessages();
	}
	catch(...)
	{
		deb("portscan 227 except");
		ds;
	}
	return totr;
}

int _ftpcmd(char *func, int line, char *cmd, char *obuf = 0, int len = 0)
{
	char buf[2048];
	ftpstate = 1;
	DWORD ftime = GetTickCount();
	// Application->ProcessMessages();
	sprintf(lastftpcmd, "%s", cmd);
	sprintf(ftpcmdhist, "%15s->ftpcmd(%s):%5d", func, cmd, line);
	// if (Form1->debcheck->Checked)
	// ftplock();
	// ftpdeb(" # %s", cmd);
	if (!FtpSocket)
		if (!ftpconn(true))
		{
			ftpstate = -1;
			return -1;
		}

	ftpstate = 1;

	while (ftphasdata())
		ftprl(buf, timeoutst, 0);

	ftpblocking(true);
	int canread;

	if (!strstr(lastftpcmd, "\r"))
	{
		strcat(lastftpcmd, "\r");
	}
	if (!strstr(lastftpcmd, "\n"))
	{
		strcat(lastftpcmd, "\n");
	}
	int ss;
	int flags = 0;
	int sd = send(FtpSocket, (char*)lastftpcmd, strlen(lastftpcmd), flags);
	int err = WSAGetLastError();
	// ftpblocking(false);
	// ftpunlock();
	char blaaa[1111];
	// if (!Form1->ftpent->Focused())
	// {
	// sprintf(blaaa, "computer: %s", cmd);
	// Form1->ftpent->Text = blaaa;
	// }
	ftpstate = -1;
	ftime = GetTickCount() - ftime;
	deb("# %s", cmd);
	call_events(E_FTPCMD, lastftpcmd, (LPVOID)ftime, (LPVOID)func, (LPVOID)line);
	if (sd > 0)
	{
		olastlen = sd;
		obytesin += sd;

	}
	else
	{
		deb("sd: %d: %s", sd, fmterr(err));
		shutdown(FtpSocket, SD_BOTH);
		closesocket(FtpSocket);
		FtpSocket = 0;
		ftpconn(false);
		return sd;
	}
	// deb("lv");
	ftpcontroltime += ftime;
	return sd;
}

int count_recs(void *dirptr)
{
	char *p;
	int nr = 0;
	p = (char*)dirptr;
	while (*p)
	{
		nr++;
		p += sizeof(DIR);
	}
	return nr;
}

unsigned long getbytecluster(unsigned long byte)
{
	unsigned long clb;
	byte -= rootdirectory - 2 * clstsize;
	byte /= clstsize;
	return byte;
}

unsigned long getclusterbyte(unsigned long ncl)
{
	unsigned long clb;
	if (!ncl)
		clb = rootdirectory;
	else
		clb = ((unsigned long)rootdirectory + (unsigned long)((unsigned long)ncl * (unsigned long)
			clstsize)) - 2 * clstsize;
	// deb("cluster %lu %lu", ncl, clb);
	return clb;
}

void ftppsvblocking(bool block)
{
	unsigned long iMode;

	iMode = !block;
	int ret = ioctlsocket(tempsock, FIONBIO, (unsigned long*) & iMode);
	if (ret == SOCKET_ERROR)
	{
		deb("ioctlsocket tempsock nonbblock(%d): %s", block, fmterr(WSAGetLastError()));
		ds;
	}
	// ioctlsocket(tempsock, FIONREAD, (unsigned long*)&iMode);
}

unsigned char lfn_checksum(const unsigned char *pFcbName)
{
	int i;
	unsigned char sum = 0;
	for (i = 11; i; i--)
		sum = ((sum & 1) << 7) + (sum >> 1) + *pFcbName++;
	return sum;
}

unsigned long getfreecluster(void)
{
	static unsigned long nextfree = 0;
	unsigned long i;
	static unsigned long lasti = 0;
	unsigned long fs;
	if (nextfree)
	{
		if (!fat[nextfree])
		{
			i = nextfree;
			nextfree = 0;
			return i;
		}
		nextfree = 0;
	}
	fs = fatsize / 4;
	if (lasti >= fs)
		lasti = 0;
	for (i = lasti; i < fs; i++)
		if (!fat[i])
			break;
	if (!fat[i + 1])
		nextfree = i + 1;
	lasti = i;
	return i;
}

void markclused(unsigned long cl)
{
	fat[cl] = 0x0fffffff;
}

int fataddfiles(char *dirname, LPVOID memory, unsigned long stclst, char *buf)
{
	deb("fataddfiles(%s,%p,%lu,%p)", dirname, memory, stclst, buf);
	char line[1024];
	char temp2[32768], temp3[32768];
	char tempbuf[32768];
	char filename[1024];
	char fn[4096];
	unsigned long allocatedbytes;
	unsigned long startclust;
	unsigned long startoffset;
	unsigned int offset;
	unsigned int clst;
	unsigned int ssize;
	unsigned int nclust;
	unsigned int entrcls;
	unsigned long cluster;
	int attr = 0;
	char fnm[1024];
	char *p;
	unsigned long freeclusters;
	char *nl;
	static int curdirlev = 0;
	unsigned int nb = 0;
	char xyu0[128];
	char *next_token3 = NULL, *next_token4 = NULL;
	LARGE_INTEGER off;
	unsigned int i = 1024;
	int nfiles;
	DIR dir;
	DWORD tms2;
	LPVOID dirptr, ndirptr;
	int nr = 0;
	unsigned long nf = 0;
	nl = strtok_s(buf, &i, "\r\n", &next_token3);
	static int numit = 0;
	char ftpdir[255] = "";
	nfiles = 0;
	call_events(E_FATSCANDIR, (LPVOID)dirname);
	// if (stclst>2)
	// stclst-=2;
	// deb("scanning:%d fat(%s)", curdirlev, dirname);
	int added = 0;
	strncpy(ftpdir, dirname, sizeof(ftpdir));
	unsigned long clstbyte;
	clstbyte = getclusterbyte(stclst);
	// ndirptr = (LPVOID)((unsigned long)memory+ ((unsigned long)getclusterbyte(stclst)));
	ndirptr = (LPVOID)((unsigned long)memory);
	char spcrs[1024];
	// memset(spcrs, 0, 1024);
	// for (int i = 0;i<curdirlev*4;i++)
	// spcrs[i] = 0x20;
	deb("@%d:%s=%p (%lu kb in %lu kb) cluster %lu", curdirlev, dirname, ndirptr,
		(unsigned long)((unsigned long)ndirptr - (unsigned long)memory) / 1024,
		(unsigned long)curdisksize / 1024, stclst);
	dirptr = ndirptr;
	nfiles = 0;
	nf = 0;
	fat[stclst] = 0x0fffffff;
	stc = stclst;
	numit++;
	UnicodeString st;
	if ((LONGLONG)totalmb >= maxdiskuse)
	{
		deb("reached maxdiskuse (%lu MB)", (unsigned long)maxdiskuse / 1024 / 1024);
		return 0;
	}
	char sss[128];
	LONGLONG maxssize;
	// char txt2[11];
	// deunicode(Form1->ftpmaxfilesize->Text.c_str(), txt2, sizeof(txt2));
	maxssize = (LONGLONG)maxfilesizemb * (LONGLONG)1024 * (LONGLONG)1024;
	// deb("max file size %I64d b", maxssize);
	// ds;
	if (!nl)
		deb("no file records!");
	while (nl)
	{
		deb("nl %s", nl);
		if (ftphasdata())
		{
			char str[1111];
			ftprl(str, timeoutst, 0);
			// deb("fataddfiles skip ftprl: %s", str);
		}
		// Application->ProcessMessages();
		// Sleep(1);
		if ((LONGLONG)curdisksize > (LONGLONG)maxdiskuse)
		{
			deb("stop fat fill, totalmb %lu reach", (unsigned long)maxdiskuse);
			return added;
		}
		char snf[128];
		// sprintf(snf, "%lu", (unsigned long)curdisksize/1024/1024);
		st = "";
		// Form1->knotts->Series[1]->Add((double)floor(heapsize/1024/1024), st, clRed);
		// sprintf(snf, "%lu", (unsigned long)curdisksize/1024/1024);
		// st = "";
		// Form1->knotts->Series[2]->Add((double)floor(totalmb/1024/1024), st, clGray);
		dirptr = (LPVOID)((unsigned long)memory + ((unsigned long)(nr ? sizeof(dir) : 0) *
				((unsigned long)nr ? nr : 0)));
		if (IsBadWritePtr(dirptr, sizeof(dir)))
		{
			deb("bad write ptr dir %p (mem %p + %lu KB) [%lu kb in %lu kb]", dirptr, memory,
				(unsigned long)((unsigned long)dirptr - (unsigned long)memory) / 1024,
				(unsigned long)((unsigned long)dirptr - (unsigned long)memory) / 1024,
				HeapSize(GetProcessHeap(), 0, memory) / 1024);
		}
		// if(nfiles++)
		attr = 0x20;
		tms2 = GetTickCount();
		nfiles++;
		DWORD size = 0;
		memset(&dir, 0, sizeof(dir));
		memset(filename, 0, sizeof(filename));
		memset(fn, 0, sizeof(fn));
		memset(fnm, 0, sizeof(fnm));
		int ret;
		strncpy(line, nl, sizeof(line));
		// memset(line, 0, sizeof(line));
		// int s;
		// s = 0;
		// int d;
		// d = 0;
		// for (int i = 0; i < strlen(nl); i++)
		// {
		// if (nl[s])
		// {
		// if (nl[s] == 0x20 && line[d - 1] == 0x20)
		// {
		// s++;
		// continue;
		// }
		// line[d] = nl[s];
		// d++;
		// }
		// s++;
		// }
		unsigned long fsize = 0;
		// deb("line #%d:[%s]", dirlev, line);
		unsigned long d1;
		char attrs[1110];
		memset(attrs, 0, 11);
		char crtime[1211];
		char month[1211];
		char day[1211];
		int hour, min;
		int iday;
		int imonth;
		// deb("scanning line ...");
		fsize = 0;
		ret = sscanf(line, "%10s %*lu %*s %*s %lu %s %s %s %[!a-zA-Z0-9~ \-�-��-�._]", &attrs,
			&fsize, &month, &day, &crtime, &fn);
		if (ret != 6 || strstr(line, ":"))
		{
			// deb("another scanf %d", ret);
			ret = sscanf(line, "%10s %*lu %*s %*s %lu %s %s %s %*s %[!a-zA-Z0-9~ \-�-��-�._]", &attrs,
				&fsize, &month, &day, &crtime, &fn);

			if (ret != 5)
			{
				i = 1024;
				nl = strtok_s(NULL, &i, "\r\n", &next_token3);
				// if (nr)
				deb("skip (%d) !=5 %s", ret, nl);
				// ds;
				continue;
			}
		}

		if (!strlen(fn))
		{
			nl = strtok_s(NULL, &i, "\r\n", &next_token3);
			// if (nr)
			deb("skip fn %s", nl);
			// ds;
			continue;
		}
		// ds;
		hour = atoi(crtime);
		min = atoi(crtime + 3);
		iday = atoi(day);
		imonth = atoi(month);
		// Many FAT file systems do not support Date/Time other than DIR_WrtTime and DIR_WrtDate. For
		// this reason, DIR_CrtTimeMil, DIR_CrtTime, DIR_CrtDate, and DIR_LstAccDate are actually
		// optional fields. DIR_WrtTime and DIR_WrtDate must be supported, however. If the other date and
		// time fields are not supported, they should be set to 0 on file create and ignored on other file
		// operations.
		// Date Format. A FAT directory entry date stamp is a 16-bit field that is basically a date relative to the
		// MS-DOS epoch of 01/01/1980. Here is the format (bit 0 is the LSB of the 16-bit word, bit 15 is the
		// MSB of the 16-bit word):
		// Bits 0�4: Day of month, valid value range 1-31 inclusive.
		// Bits 5�8: Month of year, 1 = January, valid value range 1�12 inclusive.
		// Bits 9�15: Count of years from 1980, valid value range 0�127 inclusive (1980�2107).
		// Time Format. A FAT directory entry time stamp is a 16-bit field that has a granularity of 2 seconds.
		// Here is the format (bit 0 is the LSB of the 16-bit word, bit 15 is the MSB of the 16-bit word).
		// Bits 0�4: 2-second count, valid value range 0�29 inclusive (0 � 58 seconds).
		// Bits 5�10: Minutes, valid value range 0�59 inclusive.
		// Bits 11�15: Hours, valid value range 0�23 inclusive.
		// deb("ret: %d, month %s, crtime %s, day %s", ret, month, crtime, day);
		dir.crdate = (unsigned short)time(NULL);
		dir.crtime = (unsigned short)time(NULL);
		// dir.crtimesecs = 0x11;
		// dir.laccessdate = (unsigned short)time(NULL);
		// dir.lmoddate = (unsigned short)time(NULL);
		// dir.lmodtime = (unsigned short)time(NULL);
		AnsiString as(fn);
		as = as.Trim();
		strncpy(fn, as.c_str(), sizeof(fn));
		// if ((fn[0] == '.' && strlen(fn)==1) || (fn[1] == '.' && strlen(fn)==2))
		if (fn[0] == '.')
		{
			i = 1024;
			nl = strtok_s(NULL, &i, "\r\n", &next_token3);
			deb("skip '.' %s", fn);
			// ds;
			continue;
		}
		strncpy(filename, fn, sizeof(filename));
		// for(int o=0;o<strlen(filename);o++)
		// if(filename[o]==0x20)filename[o]=0;
		if (attrs[0] == 'd' || attrs[0] == 'l')
		{
			if (attrs[0] == 'l')
			{
				sscanf(line, "%10s %lu %*s %*s %lu %s %*s %*s %*s -> %*s", &attrs, &d1, &fsize, &temp8,
					&fn);
				strcpy(filename, fn);
			}
			attr = 0x10;
		}
		i = 128;
		p = strtok_s(fn, &i, ".", &next_token4);
		if (p)
		{
			strncpy(fnm, p, sizeof(fnm));
			strncpy(dir.fname, strupr(fnm), 8);
			p = strtok_s(NULL, &i, ".", &next_token4);
			// deb("fn %s ext %s", fnm, p);
			if (p)
				strncpy(dir.ext, strupr(p), 3);
		}
		else
		{
			strncpy(dir.fname, fn, sizeof(dir.fname));
		}
		for (int u = 0; u < 11; u++)
			if (!dir.fname[u])
				dir.fname[u] = 0x20;
		char fullname[255];
		strncpy(fullname, filename, sizeof(fullname));
		call_events(E_FATWRITEFILE, (LPVOID)fullname, (LPVOID)fsize);

		char na[] = "1234567890";
		static unsigned int nra = 0;
		if (strlen(filename) > 11)
		{
			dir.fname[6] = '~';
			dir.fname[7] = na[nra];
			if (++nra >= sizeof(na) - 1)
				nra = 0;
		}
		FATFILE f;
		// Form1->currentadd->Caption = dir.fname;
		stc = getfreecluster();
		dir.st_clust_l = LOWORD(stc);
		dir.st_clust_h = HIWORD(stc);
		int charsleft;
		charsleft = strlen(fullname);
		// deb("%s %d chars", fullname, charsleft);
		int numseq;
		numseq = 1;
		wchar_t xyu2[255];
		memset(xyu2, 0xFF, sizeof(xyu2));
		unicode(fullname, xyu2, 255);
		// deb("unicode[%S]", xyu2);
		int copyed = 0;
		copyed = 0;
		bool write_first = false;
		vector<LFNDIR>lfns;
		lfns.clear();
		LFNDIR lfn;
		while (charsleft > 0)
		{
			charsleft = charsleft - 5 - 6 - 2;
			if (charsleft < 0)
				charsleft = 0;
			// deb("writing lfn (copyed %d, charsleft %d) ", copyed, charsleft);
			memset(&lfn, 0xFF, sizeof(lfn));
			lfn.attr = 0x0f;
			lfn.seq = charsleft ? numseq : 0x42;
			if (strlen(fullname) <= 13)
				lfn.seq = 0x41;
			lfn.entry_type = 0x00;
			lfn.zero = 0x00;
			memcpy(lfn.fn5, xyu2 + copyed, 10);
			memcpy(lfn.fn6, xyu2 + 5 + copyed, 12);
			memcpy(lfn.fn7, xyu2 + 5 + 6 + copyed, 4);
			lfn.cksum = lfn_checksum((unsigned char*) & dir);
			copyed += 6 + 5 + 2;
			numseq++;
			lfns.push_back(lfn);
			write_first = true;
			// Sleep(1000);
		}
		// deb("dos name: [%s] checksum %x", dir.fname, lfn.cksum);
		// deb("%s - %d lfns", fullname, lfns.size());
		for (int i = lfns.size(); i; i--)
		{
			// deb("lfn %d (%d)", i, lfns[i - 1].seq);
			memcpy((LPVOID)(dirptr), &lfns[i - 1], sizeof(lfn));
			nr++;
			dirptr = (LPVOID)((unsigned long)dirptr + (unsigned long)sizeof(LFNDIR));
		}
		strcpy(f.dosfn, dir.fname);
		if (attr == 0x10)
		{
			//
			markclused(stc);
			dir.attr = 0x10;
			// deb("memcpy (%p)",dirptr);
			memcpy((LPVOID)dirptr, &dir, sizeof(dir));
			nr++;
			char cmd[128];
			char obuf[2048];
			char snf[128];
			sprintf(snf, "#%d %s", nf, dir.fname);
			st = "";
			// Form1->knotts->Series[0]->Add((double)floor(dir.size/1024/1024), st, clYellow);
			f.memory = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, 32768);
			f.memsize = 32768;
			// memset(f.memory, 0, 32768);
			strcpy(f.fn, filename);
			f.start_offset = getclusterbyte(stc);
			f.end_offset = getclusterbyte(stc) + clstsize;
			f.firstcluster = stc;
			f.clusters = 1;
			f.accesses = 0;
			f.dir = true;
			strcpy(f.ftppath, ftpdir);
			f.size = 0;
			f.fetched = true;
			call_events(E_FATADD, (LPVOID)ftpdir, (LPVOID)filename, (LPVOID)fsize);
			// deb(obuf);
			deb("  <dir> %s @ %p, cluster %lu, byte %lu", dir.fname, dirptr, stc, getclusterbyte(stc));
			char *tbuf;
			char fd[111];
			char curd[111];
			if (curdirlev < dirlev)
			{
				char ffn[1024];
				sprintf(ffn, "y:\\%s\\%s", dirname + 1, filename);
				sprintf(sss, "%-25s (psv=%s:%-5d,timeoutst %5lu,lastdelay %5lu)", ffn, ip, ftpport,
					timeoutst, lastdelay);
				// Form1->knotts->Title->Text->Text = sss;
				strcpy(curd, ftppwd());
				// deb("recursive:%d ftp reading [%s] ...", dirlev, filename);
				// snprintf(cmd, sizeof(cmd), "CWD %s", filename);
				int code = ftpcd(filename);
				// ftprl(obuf, timeoutst, "250");
				// ftpcmd(cmd, obuf, 0);
				bool dirok;
				dirok = false;
				if (code == 250)
				{
					dirok = true;
				}
				else
				{
					deb("skip dir %s, code=%d", filename, code);
					i = 1024;
					nl = strtok_s(NULL, &i, "\r\n", &next_token3);
					deb(E_FTPCONTROL, "skip '%s' %d", filename, code);
					continue;
					// ds;
					// ExitThread(0);
				}
				if (dirok)
				{
					curdirlev++;
					// sprintf(cmd, "PWD");
					// ftpcmd(cmd, obuf, 0);
					// ftprl(obuf, timeoutst, 0);
					if (ftphasdata())
					{
						ftprl(obuf, timeoutst, 0);
						deb("ftpprepasv: %s", obuf);
					}
					ftpcmd("PASV", obuf, 0);
					ftprl(obuf, timeoutst, "227 ");
					// sprintf(cmd, "LIST -l");
					ftpcmd("LIST -l", obuf, 0);
					tbuf = (char*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, 2 * 1024 * 1024);
					// new char[2 * 1024 * 1024];
					// memset(tbuf, 0, sizeof(tbuf));
					int nr = ftppasvread(ftphost, ftpport, tbuf, 0, 2 * 1024 * 1024, 0, 0, 0);
					if (strlen(tbuf) < 100)
						deb("[%s]", tbuf);
					// ndirptr = (char*)(unsigned long)memory+(unsigned long)getclusterbyte(stc);
					// deb("ndirptr %p", ndirptr);
					if (strlen(dirname) > 2)
						snprintf(fd, 111, "%s\\%s", dirname, filename);
					else
						snprintf(fd, 111, "%s", filename);
					// added +=
					fataddfiles(fd, f.memory, stc, tbuf);
					// deb("new dir %s crc:%x",fd,checksum((unsigned short*)f.memory,4096));
					// delete[]tbuf;
					HeapFree(GetProcessHeap(), 0, tbuf);
					// dirptr = (LPVOID)((unsigned long)dirptr+  (unsigned long)
					// sizeof(dir));
					memset(obuf, 0, sizeof(obuf));
					curdirlev--;
				}
				else
				{
					deb(" skip dir %s - %s in %s", filename, obuf, curd);
					nl = strtok_s(NULL, &i, "\r\n", &next_token3);
					dirptr = (LPVOID)((unsigned long)dirptr + (unsigned long)sizeof(LFNDIR));
					continue;
					// return added;
				}
				// deb(E_FTPCONTROL, "chang dir %s", curd);
				code = ftpcd(curd);

				// ftprl(ffn, timeoutst, "250");
				if (code == 250)
					dirok = true;
				// while (ftprl((char*)ffn, dirok?lastdelay?lastdelay:100:timeoutst, 0)>0)
				// {
				// if (strstr((char*)ffn, "250"))
				// {
				// dirok = true;
				// // break;
				// }
				// continue;
				// }
				f.fetched = true;
			}
			else
			{
				// ftpdeb("max dir reaxched");
				f.fetched = false;
			}
			f.numrecs = count_recs(f.memory);
			// deb("%s %d recs", dir.fname, f.numrecs);
			f.recptr = dirptr;
			files.push_back(f);
			dirptr = (LPVOID)((unsigned long)dirptr + (unsigned long)sizeof(LFNDIR));
			nf++;
			i = 1024;
			nl = strtok_s(NULL, &i, "\r\n", &next_token3);
			continue;
		}
		nf++;
		// size = 512;
		dir.size = (unsigned int)fsize;
		// sprintf(snf, "%s", nf, dir.fname);
		st = "";
		// Form1->knotts->Series[0]->Add((double)floor(dir.size/1024.0/1024.0), st,
		// checksum((unsigned short*)dirname, strlen(dirname)));
		if (totalmb > maxquad)
			maxquad = totalmb;
		// if (Form1->fatdeb->Checked)
		deb(" #%-3d %25s %8u ���� (%.2f MB) ", nfiles, filename, fsize, (double)fsize / 1024 / 1024);
		// stc = cluster;
		// fat[stc] = 0x0fffffff;
		// stc = getfreecluster();
		// fat[stc] = 0x0fffffff;
		ssize = fsize;
		if ((LONGLONG)ssize > maxssize)
		{
			deb("skip %s too big %u > %I64d [maxfilesizemb:%lu]", filename, ssize, maxssize,
				maxfilesizemb);
			nl = strtok_s(NULL, &i, "\r\n", &next_token3);
			ds;
			continue;
		}
		if ((LONGLONG)totalmb + (LONGLONG)fsize > (LONGLONG)maxdiskuse)
		{
			deb("max diskuse reached %lu MB", (unsigned long)maxdiskuse / 1024 / 1024);
			return added;
		}
		totalmb += (double)fsize;
		markclused(stc);
		// stc = getfreecluster();
		clst = stc;
		nclust = 0;
		int clustersused;
		clustersused = 1;
		startoffset = 0;
		startclust = stc;
		offset = (clstsize * stc);
		// if (Form1->fatdeb->Checked)
		// deb(" offset +%lu (0x%08x) cluster %d ",
		// (unsigned long)dataoffset + offset,
		// (unsigned long)dataoffset + offset, stc);
		entrcls = (unsigned int)dir.size / clstsize;
		allocatedbytes = 0;
		// sprintf(sss, "%s\%s, %.2f ��", dirname + 1, filename, (double)((double)fsize / 1024.0));
		// Form1->knotts->Title->Text->Text = sss;
		while (ssize > 0)
		{
			unsigned long nclst;
			nclst = getfreecluster();
			fat[stc] = nclst;
			clustersused++;
			stc = nclst;
			markclused(stc);
			char str2[255];
			double pr;
			if (entrcls)
			{
				char sss[111];
				if ((GetTickCount() - tms2) >= 700)
				{
					pr = ((((double)allocatedbytes ? (double)allocatedbytes : (double)1)) / (double)
						dir.size) * (double)100;
					sprintf(sss, "������ FAT %.2f %%[%s\%s]", pr, dirname, filename);
					// Form1->current->Text = sss;
					// Form1->knotts->Title->Text->Text = sss;
					deb(sss);
					tms2 = GetTickCount();
					// Form1->headptr->Left = (int)avg;
				}
			}
			ssize = (unsigned int)ssize - ((unsigned int)clstsize < (unsigned int)ssize ?
				(unsigned int)clstsize : (unsigned int)ssize);
			if (!startoffset)
				startoffset = getclusterbyte(startclust); // (unsigned long)offset+(unsigned long)dataoffset-(unsigned long)32768;
			offset = (unsigned long)((unsigned long)clstsize * (unsigned long)stc);
			off.QuadPart = (LONGLONG)(unsigned long)offset + (unsigned long)dataoffset;
			// paintdisk(9, off, clMedGray, true, 0);
			allocatedbytes += clstsize;
		}
		// char ffn[1024];
		// sprintf(ffn, "y:\\%s\\%s", dirname + 1, filename);
		// sprintf(sss, "%-50s (%.2f M�, %6lu clstrs from %5lu)", ffn,
		// (double)((double)fsize / 1024.0 / 1024.0), clustersused,
		// startclust);
		// Form1->knotts->Title->Text->Text = sss;
		off.QuadPart = startoffset;
		// paintdisk(9, off, clDkGray, true, 0);
		fat[stc] = 0x0fffffff;
		// stc++;
		freeclusters -= nclust;
		// if (Form1->fatdeb->Checked)
		// deb("   %u clusters (%d free clusters)", nclust, freeclusters);
		// deb("startoffset %lu",startoffset);
		strcpy(f.fn, filename);
		f.start_offset = startoffset;
		f.end_offset = getclusterbyte(stc) + clstsize; // startclust+nclust)+clstsize; // startoffset+fsize;
		f.firstcluster = startclust;
		strcpy(f.ftppath, ftpdir);
		f.accesses = 0;
		f.size = fsize;
		f.clusters = clustersused;
		f.dir = false;
		f.fetched = false;
		f.precachesize = clstsize * 2;
		f.precacheclstrs = 32;
		f.lastoffset = 0;
		f.lastlen = 0;

		call_events(E_FATADD, (LPVOID)ftpdir, (LPVOID)filename, (LPVOID)fsize);

		netfatfilesize += fsize;
		dir.attr = 0x20;
		memcpy((LPVOID)dirptr, &dir, sizeof(dir));
		nr++;
		// deb("write %s @ %p", dir.fname, dirptr);
		f.recptr = dirptr;
		files.push_back(f);
		// deb("      %11s %8lu offset:%8lu cls:%lu clustersused:%lu", dir.fname,
		// (unsigned long)dir.size, startoffset, clst, clustersused);
		// stc+=clustersused;
		fspace_ptr += size;
		i = 1024;
		nl = strtok_s(NULL, &i, "\r\n", &next_token3);
		// dirptr = (LPVOID)((unsigned long)dirptr +(unsigned long)sizeof(LFNDIR));
	}
	added += nf;
	memset(&dir, 0, sizeof(dir));
	dirptr = (LPVOID)((unsigned long)dirptr + (unsigned long)sizeof(LFNDIR));
	memcpy(dirptr, &dir, sizeof(dir));
	deb("fataddfiles(%s): crc(%p): %x", dirname, memory, checksum((unsigned short*)memory, 4096));
	// deb("     %s %d files", spcrs, added);
	numit--;
	return added;
}

bool nconn(void)
{
	unsigned long iMode = 0;
	static int checkdone = true;
	// if (ramdisk)
	// {
	// deb("nconn ram used");
	// return false;
	// }
	numpkt = 0;
	shutdown(ConnectSocket, SD_BOTH);
	closesocket(ConnectSocket);
	deb("���������� � �������� ...");
	// Form1->Canvas->Font->Color=clRed;
	// Form1->Canvas->Font->Size=12;
	// Form1->deblog->Canvas->TextOutA(Form1->Left+Form1->deblog->Width/2,Form1->deblog->Height/2,"���������� ...");
	DWORD ms = GetTickCount();
	// Application->ProcessMessages();
	ConnectSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (ConnectSocket == INVALID_SOCKET)
	{
		deb("Client: Error at socket(): %ld\n", WSAGetLastError());
		return false;
	}
	sockaddr_in clientService;
	clientService.sin_family = AF_INET;
	clientService.sin_addr.s_addr = inet_addr(serv);
	clientService.sin_port = htons(11000);
	// iMode=!iMode;
	ioctlsocket(ConnectSocket, FIONBIO, (unsigned long*) & iMode);
	// iMode=99999999;
	ioctlsocket(ConnectSocket, FIONREAD, (unsigned long*) & iMode);
	iMode = true;
	// setsockopt(ConnectSocket, SOL_SOCKET, TCP_NODELAY, (unsigned char*)&iMode, 1);
	setsockopt(ConnectSocket, SOL_SOCKET, SO_DEBUG, (unsigned char*) & iMode, 1);
	setsockopt(ConnectSocket, SOL_SOCKET, SO_KEEPALIVE, (unsigned char*) & iMode, 1);
	iMode = false;
	setsockopt(ConnectSocket, SOL_SOCKET, SO_OOBINLINE, (unsigned char*) & iMode, 1);
	iMode = 999999999999;
	setsockopt(ConnectSocket, SOL_SOCKET, SO_RCVBUF, (unsigned char*) & iMode, 4);
	setsockopt(ConnectSocket, SOL_SOCKET, SO_SNDBUF, (unsigned char*) & iMode, 4);
	deb("tcp.socket max recv() size = %u", (unsigned long)iMode);
	iMode = true;
	if (connect(ConnectSocket, (SOCKADDR*) & clientService, sizeof(clientService)) == SOCKET_ERROR)
	{
		deb("Client: ���������� ��������� � ��������\n");
		return false;
	}
	ms = GetTickCount() - ms;
	deb("    �� %d ����\n", ms);
	// Application->ProcessMessages();
	if (!checkdone)
	{
		deb("performing check ...");
		unsigned int sizo = 112000;
		char *testbuf = new char[sizo];
		int a = 1;
		for (int i = 0; i < sizo; i++)
		{
			testbuf[i] = i;
		}
		USHORT *us = (USHORT*)testbuf;
		*us = checksum((USHORT*)testbuf, sizeof(testbuf));
		unsigned long dwt = 0;
		unsigned long sz;
		LARGE_INTEGER ofs;
		ofs.QuadPart = 0;
		sz = sizo;
		dump(testbuf, sizo, "initial");
		OnWrite(NULL, NULL, testbuf, sz, ofs, 0, &dwt);
		// dump(testbuf, sizeof(testbuf), "awrite");
		memset(testbuf, 0, sizo);
		OnRead(NULL, NULL, testbuf, sz, ofs, 0, &dwt);
		dump(testbuf, sizo, "readed");
		deb("check done");
		checkdone = true;
		delete[](char*)testbuf;
	}
	static int numconn = 0;
	memset(&pkt, 0, sizeof(pkt));
	pkt.cmd = 3;
	pkt.offset = numconn++;
	// pkt.size = size;
	pkt.sign = 'A';
	pkt.size = disksize;
	pkt.paged = 0;
	pkt.preverr = err;
	err = 0;
	DWORD dwSiz = 64;
	GetComputerNameEx(ComputerNameDnsHostname, pkt.compid, &dwSiz);
	// dwSiz = 128-strlen(pkt.compid);
	// pkt.compid[strlen(pkt.compid)] = '-';
	// GetComputerNameEx(ComputerNameNetBIOS, pkt.compid+strlen(pkt.compid), &dwSiz);
	// pkt.crc = (unsigned long)checksum((unsigned short*)buf, size);
	int ss = send(ConnectSocket, (char*) & pkt, sizeof(pkt), 0);
	if (ss != sizeof(pkt))
	{
		err = WSAGetLastError();
		deb("nconn:failed to send PACKET! sent only %d bytes err %d", ss, err);
		closesocket(ConnectSocket);
		ConnectSocket = 0;
	}
	else
	{
		deb("sent helo packet %d", ss);
	}
	// Application->ProcessMessages();
	return true;
}

int ftpauth(void)
{
	int ss;

	OutputDebugString("ftpauth() enter");
	if (!strlen(ftpuser) && !strlen(ftppass))
	{
		strcpy(ftpuser, "anonymous");
		char comp[255];
		unsigned long zize = sizeof(comp);
		GetComputerName(comp, &zize);
		snprintf(ftppass, sizeof(ftppass), "%s@%s", strlwr(comp), strlwr(ftphost));
		deb("set ftppass = '%s'", ftppass);
	}
	else
	{
		deb("ftpauth using user %s pass %s", ftpuser, ftppass);
		// deunicode(Form1->ftpuser->Text.c_str(), user, sizeof(user));
		// deunicode(Form1->ftppass->Text.c_str(), pass, sizeof(pass));
	}
	// Form1->knotts->Title->Text->Text = "����� ...";

	call_events(E_FTPAUTHSTART, (LPVOID)ftpuser, (LPVOID)ftppass);

	char *buf = 0;
	// [931072];
	buf = (char*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, 131072);
	sprintf(buf, "USER %s", ftpuser);
	ftpcmd(buf, 0, 0);
	ftprl(buf, timeoutst, (char*)0);
	sprintf(buf, "PASS %s", ftppass);
	ftpcmd(buf, 0, 0);
	Sleep(timeoutst);
	ftprl(buf, timeoutst, (char*)0);
	if (!strstr(buf, "230") && !strstr(buf, "331"))
	{
		// deb("������ ��������������: %s", buf);
		// ftpcmd("STAT\r\n", 0, 0);
		// ftprl(buf, timeoutst, 0);
		call_events(E_FTPAUTHERR, (LPVOID)buf);
		closesocket(FtpSocket);
		call_events(E_FTPDISCONNECT, (LPVOID)buf);
		FtpSocket = 0;
		HeapFree(GetProcessHeap(), 0, buf);
		return false;
	}
	else
	{
		loggedin = true;
		call_events(E_FTPAUTHOK, (LPVOID)ftpuser, (LPVOID)ftppass, (LPVOID)buf);
	}
	// loggedin=true;
	// if (!quick)

	char *ftpcmds[] =
	{
		"SYST", "FEAT", "CLNT fatftpapp-1.0", "OPTS UTF8 OFF", // "TYPE A",
		"PASV", NULL // ,
	};
	char *p;
	for (int i = 0; ftpcmds[i]; i++)
	{
		deb("exec %s", ftpcmds[i]);
		sprintf(buf, "%s", ftpcmds[i]);
		ftpcmd(buf, 0, 0);
		ftprl(buf, timeoutst, 0);
		Sleep(111);
		while (ftphasdata())
		{
			ftprl(buf, timeoutst, 0);
			Sleep(111);
			continue;
		}
		// deb("  > %s", buf);
		// }
	}

	ftpcmd("LIST -l", 0, 0);
	// ftprl(buf, timeoutst, 0);
	ftppasvread(ftphost, ftpport, buf, 0, 131072, 0, 0, 0);
	strcpy(fatfiles, buf);

	int charset = GetTextCharset(GetDC(0));
	bool isunic = false;
	int ires = 0;

	isunic = IsTextUnicode(buf, strlen(buf), &ires);

	deb("user charset: %d, list unicoded: %d", charset, isunic);
	deb("ftp list: \r\n%s", fatfiles);

	// strcpy(ftpcurdir, ftppwd());

	 ftpcmd("TYPE I", 0, 0);
	 ftprl(buf, timeoutst, 0);

	 while (ftphasdata())
	 {
	 ftprl(buf, timeoutst, 0);
	 Sleep(200);
	 }
	HeapFree(GetProcessHeap(), 0, buf);

	return true;
}

bool ftpconn(bool quick = false)
{
	ftpstate = -2;
	loggedin = false;
	unsigned long iMode = 1;
	static int checkdone = true;
	timeoutst = 5000;
	shutdown(FtpSocket, SD_BOTH);
	closesocket(FtpSocket);
	FtpSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (FtpSocket == INVALID_SOCKET)
	{
		deb("Client: socket(): %ld\n", WSAGetLastError());
		return false;
	} // deb("tcp.socket max recv() size = %u", (unsigned long)iMode);
	sockaddr_in clientService;
	clientService.sin_family = AF_INET;
	DWORD ftime = GetTickCount();
	clientService.sin_addr.s_addr = resolve(ftphost);
	// inet_addr(host);
	ftime = GetTickCount() - ftime;
	ftpdatatime += ftime;
	call_events(E_TIMEOUTCHANGE, (LPVOID)ftime);
	clientService.sin_port = htons(ftpstartport);
	if (clientService.sin_addr.s_addr == 0)
	{
		deb("failed to resolve[%s]: %s", ftphost, fmterr(WSAGetLastError()));
		call_events(E_RESOLVEERR, (LPVOID)ftphost, (LPVOID)fmterr(WSAGetLastError()));
		return false;
	}
	// ioctlsocket(FtpSocket, FIONBIO, (unsigned long*) & iMode);
	//
	int lport;
	struct sockaddr_in sa;
	int nl = sizeof(sa);
	if (unmounting)
		ExitThread(0);
	deb("connecting %s:%d (%s) ...", ftphost, ftpstartport, ip);
	ftpblocking(false);

	call_events(E_FTPCONNECTING, (LPVOID)ftphost, (LPVOID)ftpport);
	DWORD ms = GetTickCount();
	while (int ret = connect(FtpSocket, (SOCKADDR*) & clientService, sizeof(clientService)) == -1)
	{
		int err = WSAGetLastError();
		if (err == 10056)
		{
			deb("FtpSocket already connected [%s:%d]", ftphost, ftpstartport);
			break;
		}
		else
		{
			// Sleep(rand() % 10);
			// deb("ftpconn err:%d [%s]", err, fmterr(err));
		}
		call_events(E_TIMEOUTCHANGE, (LPVOID)(GetTickCount() - ms));
		if (GetTickCount() - ms >= timeoutst)
		{
			deb("ftpconn() failed connect(%s,%d) =\r\n  %s", ftphost, ftpstartport, fmterr(err));
			FtpSocket = 0;
			call_events(E_FTPCONNECTERR, (LPVOID)fmterr(err));
			return false;
		}
		ftpblocking(false);
	}
	ms = GetTickCount() - ms;
	call_events(E_TIMEOUTCHANGE, (LPVOID)(ms));
	call_events(E_FTPCONNECTED, (LPVOID)ftphost, (LPVOID)ftpport);
	bool nodelay = sockets_nodelay;
	//
	int ret = setsockopt(FtpSocket, SOL_SOCKET, TCP_NODELAY, (char*) & nodelay, sizeof(bool));
	if (SOCKET_ERROR == ret)
		deb("sockets_nodelay(%d): %s", sockets_nodelay, fmterr(WSAGetLastError()));
	bool oob = sockets_oobinline;
	//
	ret = setsockopt(FtpSocket, SOL_SOCKET, SO_OOBINLINE, (char*) & oob, sizeof(bool));
	if (SOCKET_ERROR == ret)
		deb("sockets_OOB(%d): %s", oob, fmterr(WSAGetLastError()));
	// setsockopt(FtpSocket, SOL_SOCKET, SO_DEBUG, (unsigned char*)&iMode, 1);
	iMode = true;
	setsockopt(FtpSocket, SOL_SOCKET, SO_KEEPALIVE, (char*) & iMode, 1);
	ff_SocketBuffers(sockets_buffers);
	getsockname(FtpSocket, (struct sockaddr*) & sa, &nl);
	strcpy(ip, inet_ntoa(sa.sin_addr));
	timeoutst = ms ? ms * 5 : 5000;
	call_events(E_TIMEOUTCHANGE, (LPVOID)timeoutst);
	char buf[65535];
	lport = ntohs(sa.sin_port);
	ftplport = lport;
	deb("%6lu ms, %s:%d <=> %s:%d, err=%d, timeoutst=%d  ", ms, ftphost, ftpport, ip, lport, err,
		timeoutst);
	ftpstate = -1;

	ms = GetTickCount();
	ftprl(buf, timeoutst, 0);
	if (strlen(buf) < 2)
	{
		deb("no server banner or 220 code!");
		closesocket(FtpSocket);
		FtpSocket = 0;
		call_events(E_FTPDISCONNECT, (LPVOID)"��� ���� ����������� 220!");
		return false;
	}

	if (strlen(buf))
		call_events(E_SERVERBANNER, buf);

	ms = GetTickCount() - ms;
	deb(" read %d bytes in %lu ms", strlen(buf), ms);
	if (!quick)
		ftpauth();
	return true;
}

//
// void paintdisk(int what, LARGE_INTEGER off, ULONG len, bool paging = false, unsigned short crc = 0)
// {
// // if (!Form1->debcheck->Checked)
// // return;
// static DWORD tms = 0;
// static double si;
// static double si2;
//
// // Form1->imgptr->Left = Form1->ptr->Left;
//
// try
// {
// if ((double)off.QuadPart >= (double)maxquad)
// {
// maxquad = (double)(off.QuadPart);
// // Form1->OnPaint(NULL);
// }
//
// static double temp, temp2;
// temp = (double)off.QuadPart;
// temp2 = (double)Form1->pb1->Width;
//
// // if(temp > ((double)maxquad*0.7))
// // {
// // maxquad = temp*1.4;
// //
// // }
// if (!diskcolor)
// {
//
// diskcolor = new unsigned int[Form1->pb1->Width];
//
// for (UINT i = 0;i<Form1->pb1->Width;i++)
// diskcolor[i] = clWhite;
//
// }
// if (temp > 0 && maxquad > 0 & temp2 > 0)
// si = (temp/maxquad) * (temp2);
//
// if (what == 9&&diskcolor)
// {
//
// diskcolor[(unsigned int)si] = len;
// }
//
// tms = GetTickCount();
//
// // if (ramdisk)
// // return;
// char ss[128];
//
// if ((double)temp > ((double)maxquad))
// {
// maxquad = (double)temp;
// // Form1->Repaint();
//
// }
// si = (double)((double)temp/(double)(maxquad)) * ((double)temp2);
//
// avg = (double)si;
//
// char avgstr[111];
// sprintf(avgstr, "%-3.2f MB - %.2f", temp/1024/1024, avg);
// // Form1->secnum->Text = avgstr;
// // deb("avg %.2f", avg);
// if (Form1->debcheck->Checked)
// {
// Form1->headptr->Visible = false;
// Form1->headptr->Top = Form1->pb1->Top-13;
// if (avg>1.0&&avg<Form1->pb1->Width)
// Form1->headptr->Left = (int)avg;
// // deb("set avg to %.0f", avg);
// //
// Form1->headptr->Visible = true;
// }
// // Application->ProcessMessages();
// char *swhat = "<��������>";
// switch(what)
// {
// case 1:
// swhat = "������";
// Form1->ptr->Font->Color = clBlack; // clTeal;
// sprintf(ss, "%6s %5lu @ %I64d ", swhat, len, off.QuadPart);
// if (Form1->debcheck->Checked)
// Form1->ptr->Text = ss;
// break;
// case 0:
// swhat = "������";
// Form1->ptr->Font->Color = clBlack;
// sprintf(ss, "%6s %5lu @ %I64d ", swhat, len, off.QuadPart);
// if (Form1->debcheck->Checked)
// Form1->ptr->Text = ss;
// break;
// case 5:
// Form1->ptr->Font->Color = clGreen;
// swhat = "������ ����";
// sprintf(ss, "%6s %5lu @ %I64d ", swhat, len, off.QuadPart);
// if (Form1->debcheck->Checked)
// Form1->ptr->Text = ss;
// break;
//
// default:
// Form1->ptr->Font->Color = clLtGray;
// // ss[0] = 0;
// // sprintf(ss, "%5s", swhat);
// break;
// }
//
// // Form1->ptr->Font->Size = 9;
//
// if (paging)
// Form1->imgpaging->Visible = true; // Form1->ptr->Font->Color = clRed;
// else
// Form1->imgpaging->Visible = false; // Form1->ptr->Font->Color = clBlue;
//
// // Form1->ptr->Width = Form1->Canvas->TextWidth(ss)*2;
//
// switch(what)
// {
// case 0:
//
// for (int i = (int)avg;i<(int)avg+1;i++)
// {
//
// unsigned char r, g, b;
// unsigned int fourBytes;
// fourBytes = diskcolor[i];
// // caches 4 bytes at a time
// r = (fourBytes>>16);
// g = (fourBytes>>8);
// b = fourBytes;
//
// r-=85;
// b-=85;
// g-=85;
//
// // for (int u = 1;u<11;u++)
// diskcolor[i] = RGB(r, g, b);
// // Form1->pb1->Canvas->Pixels[i][1]-50-len;
// // Form1->pb1->Canvas->Pixels[i][u] = Form1->pb1->Canvas->Pixels[i][u]+50+len; // clRed;
// }
// break;
// case 5:
// case 1:
//
// for (int i = (int)avg;i<(int)avg+1;i++)
// {
// unsigned char r, g, b;
// unsigned int fourBytes;
// fourBytes = diskcolor[i];
// // caches 4 bytes at a time
// r = (fourBytes>>16);
// g = (fourBytes>>8);
// b = fourBytes;
// // r+=45;
// // b+=45;
// // g+=45;
// diskcolor[i] = RGB(r, g, b);
// }
// break;
//
// case 4:
// Form1->diskptr->Left = 0;
//
// // sprintf(ss, "<��������>");
// // Form1->ptr->Text = ss;
// Form1->ptr->Font->Color = clLtGray;
// // Form1->imgptr->Visible = false;
// break;
//
// case 9:
// // deb("avg %.2f avg2 %.2f ofs %I64d", avg, avg2,off.QuadPart);
// // for (double i = avg;i<avg+1;i++)
// // {
//
// diskcolor[(int)avg] = len;
//
// // }
// break;
// }
//
// }
// catch(...)
// {
// deb("exception while paintdisk");
// }
// if (!paging)
// {
// // Form1->diskptr->Repaint();
// // Form1->ptr->Repaint();
// }
//
// // Form1->Repaint();
//
// }

bool ftphasdata(bool wait)
{
	int canread;
	if (!FtpSocket)
		return false;
	ftpblocking(false);
	char c;
	int err = 10035;
	canread = recv(FtpSocket, &c, 1, MSG_PEEK);
	if (canread > 0)
		return true;
	err = WSAGetLastError();
	ftpblocking(true);
	if (err != 10035)
		deb("canread: %d [err:%d] FtpSock %d [%s]", canread, err, FtpSocket, fmterr(err));
	return false;
}

unsigned long ftpgetfile(char*fname, unsigned long offset, char*buf, unsigned long len,
	char *dosfn, unsigned long cacheblocks, unsigned long startoffset)
{
	char tb[5120];
	int rr = 0;
	ftpcontroltime = 0;
	ftpdatatime = 0;
	if (!FtpSocket)
		ftpconn(true);
	strncpy(ftpgetfilename, fname, sizeof(ftpgetfilename));
	ftpgetfileoffset = offset;
	ftpgetfilelen = len;

	// Sleep(timeoutst < 300 ? timeoutst * 2 : 20);
	while (ftphasdata())
	{
		// deb("reading OOB %d bytes", canread);
		ftprl(buf, timeoutst, 0);
		if (strstr(buf, "550 "))
		{
			deb("failed RETR: %s", buf);
			return -1;
		}

	}
	deb("fname %s", fname);
	DWORD tmepsv = GetTickCount();
	int pasv_run = true;
	while (pasv_run)
	{
		// sprintf(tb, "PASV");
		ftpcmd("PASV", 0, 0);
		// memset(tb, 0, sizeof(tb));
		ftprl(tb, timeoutst, 0);

		if (strstr(tb, "227 En")) // tb[0] == '2' && tb[1] == '2' && tb[2] == '7')
			pasv_run = false;

		Sleep(timeoutst < 30 ? timeoutst : 50);
		// }
	}

	deb("ws fname %s", fname);
	if (offset)
	{
		sprintf(tb, "REST %lu", offset);
		// rr = send(FtpSocket, tb, strlen(tb), 0);
		ftpcmd(tb, 0, 0);
		tb[0] = 0;
		memset(tb, 0, sizeof(tb));
		ftprl(tb, timeoutst, "350");
		// {
		// continue;
		// }
	}
	DWORD tms = GetTickCount();
	sprintf(tb, "RETR %s", fname);

	ftpcmd(tb, 0, 0);

	// Sleep(100);
	if (ftphasdata())
	{
		ftprl(tb, timeoutst, 0);
		if (atoi(tb) == 550)
		{
			deb("file not found");
			// ds;
			return 0;
		}
	}
	char *nbuf = 0;

	// nbuf = (char*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, len);

	unsigned long rrr = ftppasvread(ftphost, ftpport, buf, len, len, dosfn, cacheblocks,
		startoffset);

	return rrr;
}

bool netfat_hasfile(char *dosfn)
{
	return false;
}

void readfatdir(unsigned int clst, LPVOID dirptr, char*dn)
{
	DIR dir;
	int nf = 0;
	static int numit = 0;
	LPVOID memory;
	memory = dirptr;
	if (!dirptr)
		return;
	wchar_t wfn[44];
	// unsigned int stcl = 0;
	deb("readfatdir(%u, %p, %s) crc:%x", clst, dirptr, dn, checksum((unsigned short*)dirptr, 4096));
	if (clst > 2)
		clst -= 2;
	dump((char*)dirptr, 16384, (char*)dn);
	// dirptr = (LPVOID)(unsigned long)((unsigned long)memory+(unsigned long)dataoffset+
	// ((unsigned long)clstsize*clst));
	memcpy(&dir, dirptr, sizeof(dir));
	numit++;
	char spcrs[1024];
	memset(spcrs, 0, 1024);
	for (int i = 0; i < numit * 3; i++)
		spcrs[i] = 0x20;
	// deb("%sreading dir %s cluster %lu", spcrs, dn, clst);
	int nr = 0;
	char lfn[128];
	LFNDIR lfndir;
	for (int i = 0; dir.fname[0]; i++)
	{
		char ff[13];
		char type[111];
		char strclusters[1024] = "";
		// stcl = ((dir.st_clust_h<<16) | (dir.st_clust_l&0xff));
		// stcl+=fat32boot.reserved_sectors-2+2*fat32boot.sectors_per_fat*2;
		unsigned long stcl = 0;
		if (ff[0] == 0xe5)
		{
			// memcpy(&dir, ((char*)memory)+rootdirectory+((i+1)*sizeof(dir)), sizeof(dir));
			dirptr = (LPVOID)((unsigned long)dirptr + (unsigned long)sizeof(dir));
			memcpy(&dir, dirptr, sizeof(dir));
			nr++;
			continue;
		}
		if (dir.attr == 0x10)
			strcpy(type, "D");
		else if (dir.attr == 0x08)
			strcpy(type, "V");
		else
			strcpy(type, "F");
		if (dir.attr == 0x0f) // long file name
		{
			// deb("sizeof dir=%d lfndir=%d",sizeof(dir),sizeof(lfndir));
			memcpy(&lfndir, &dir, sizeof(lfndir));
			if (lfndir.zero || lfndir.attr != 0x0f)
				deb("LFNDIR ZERO NOT ZERO lfndir.attr!=0x0f");
			memset(lfn, 0, 128);
			wchar_t wfn[1024];
			memset(wfn, 0, sizeof(wfn));
			memcpy(wfn, &lfndir.fn5, 10);
			memcpy(wfn + 10, &lfndir.fn7, 4);
			memcpy(wfn + 10 + 4, &lfndir.fn6, 12);
			// wfn[25] = 0x0;
			deunicode(wfn, lfn, sizeof(lfn));
			// sprintf(lfn, "%S", wfn);
		}
		else if (dir.attr)
		{
			snprintf(ff, 13, "%s%c%s", dir.fname, strlen(dir.ext) > 1 ? '.' : ' ',
				dir.ext[0] ? dir.ext : "");
			// strncpy(ff, dir.fname, 11);
			if (dir.attr != 0x20 && dir.attr != 0x0f && dir.attr != 8 && dir.attr != 0x10)
				deb(" attr=%x ", dir.attr);
			int clusters;
			if (dir.size > fat32boot.bytes_per_sector)
				clusters = dir.size / (fat32boot.sectors_per_cluster * fat32boot.bytes_per_sector);
			else
				clusters = 1;
			clusters = dir.size / (fat32boot.bytes_per_sector * fat32boot.sectors_per_cluster);
			// ? dir.size/ (fat32boot.bytes_per_sector*fat32boot.sectors_per_cluster):1;
			// if(dir.attr
			stcl = make_dword(dir.st_clust_l, dir.st_clust_h);
			unsigned long ncl;
			ncl = stcl;
			unsigned long curcl;
			unsigned long offset;
			curcl = 0;
			LARGE_INTEGER off;
			offset = dataoffset + (clstsize * ncl) - 32768;
			if (dir.size)
			{
				strclusters[0] = 0;
				off.QuadPart = offset;
				// paintdisk(9, off, clGray, 0, 0);
				try
				{
					while (curcl++ <= 5)
					{
						if (!fat[ncl] || fat[ncl] == 0xffffff0f || fat[ncl] == 0xffffffff || fat[ncl]
							== 0xfffffff0 || fat[ncl] == 0x0fffffff)
							break;
						char str2[128];
						// (int)pData+offset);
						sprintf(str2, " %u ", ncl);
						strcat(strclusters, str2);
						ncl = fat[ncl];
					}
				}
				catch(...)
				{
					deb("exception while traversing fat");
				}
				ff[12] = 0;
				if (!clusters)
					strclusters[0] = 0;
			}
			nf++;
			unsigned long byte;
			byte = getclusterbyte(make_dword(dir.st_clust_l, dir.st_clust_h));
			deb("%s %s %10s %-8lu clst:%-8lu (byte %lu) attr=%x res=%x sth=%x stl=%x siz=%x (%u KB)",
				spcrs, type, ff, (unsigned long)dir.size, make_dword(dir.st_clust_l, dir.st_clust_h),
				byte, dir.attr, dir.reserved, dir.st_clust_h, dir.st_clust_l, dir.size,
				dir.size / 1024);
			if (lfn[0])
			{
				// deb("         %-20s (seq %x entry_type: %x cksum: %x zero:%x)", lfn,
				// lfndir.seq, lfndir.entry_type, lfndir.cksum, lfndir.zero);
				lfn[0] = 0;
			}
			int c = 0;
			char *l;
			l = (char*)pData;
			if (dir.attr == 0x10)
			{
				unsigned long dclst = make_dword(dir.st_clust_l, dir.st_clust_h);
				if (dclst == clst)
				{
					deb("self-lock: directory to self [%s]", dn);
					dirptr = (LPVOID)((unsigned long)dirptr + (unsigned long)sizeof(dir));
					memcpy(&dir, dirptr, sizeof(dir));
					nr++;
					continue;
				}
				LPVOID mem = 0;
				bool foundnetfat;
				foundnetfat = false;
				for (vector<FATFILE>::iterator it = files.begin(); it != files.end(); it++)
				{
					if ((*it).firstcluster == dclst)
					{
						mem = (*it).memory;
						readfatdir(dclst, mem, dir.fname);
						foundnetfat = true;
					}
				}
				if (!foundnetfat)
					deb("no files record for %s", dir.fname);
			}
		}
		else
		{
			deb("volume record");
		}
		dirptr = (LPVOID)((unsigned long)dirptr + (unsigned long)sizeof(dir));
		memcpy(&dir, dirptr, sizeof(dir));
		nr++;
	}
	numit--;
	char s[128];
	sprintf(s, "%s %d", spcrs, nf);

}

void showfat(void)
{
	FILE *x;
	LPVOID disk = memory;
	// debcheck2->Checked = true;
	try
	{
		if (!memory || IsBadReadPtr(memory, curdisksize))
		{
			deb("NO MEMORY || Bad read ptr %x - %x", memory, (long)memory + (long)disksize);
			return;
		}
	}
	catch(...)
	{
		deb("exception while cpying memory to disk [read fat]");
	}
	deb("memory @ 0x%08p, disk: ������������ %lu MB �� %I64d (%I64d GB)", memory,
		(unsigned long)curdisksize / 1024 / 1024, disksize, disksize / 1024 / 1024 / 1024);
	////////////////////////////////////////////////////////////////////////////////////
	PART part;
	FB fb;
	char *p;
	memcpy(&fb, memory, sizeof(fb));
	p = (char*) & fb.sign;
	// deb("sign(55AA): 0x%2x (%x %x)", fb.sign, (unsigned char)*(p), (unsigned char)*(p+1));
	if (fb.sign != 0xaa55)
	{
		deb("��������� FAT �����������, copy %d", 4096);
		return;
	}
	memcpy(&fat32boot, memory, sizeof(fat32boot));
	if (fat32boot.sign_0x29 != 0x29)
	{
		deb("   --- FAIL\r\n   no sign - sign0x29 = %02x\r\n", fat32boot.sign_0x29);
		return;
	}
	deb("oem: %s bytes_per_sector=%d sectors_per_cluster=%d", fat32boot.oem,
		fat32boot.bytes_per_sector, fat32boot.sectors_per_cluster);
	deb("reserved_sectors=%d (%d bytes) fat_copys=%d max_root_ent=%d", fat32boot.reserved_sectors,
		fat32boot.reserved_sectors * fat32boot.bytes_per_sector, fat32boot.fat_copys,
		fat32boot.max_root_ent);
	deb("media=%x sectors_per_track=%u heads=%u hidden_sectors=%u", fat32boot.media,
		fat32boot.sectors_per_track, fat32boot.heads, fat32boot.hidden_sectors);
	deb("sectors=%u \r\nsectors_per_fat=%lu\r\n"
		"%lu clusters, %lu sectors, size %.2f GB flags=%x version=%x", (unsigned)fat32boot.sectors,
		(unsigned long)fat32boot.sectors_per_fat, (unsigned long)fatsize / 4,
		(unsigned long)((disksize / (LONGLONG)fat32boot.bytes_per_sector)),
		(double)((LONGLONG)((fatsize / 4) * (LONGLONG)clstsize) / 1024 / 1024 / 1024),
		fat32boot.flags, fat32boot.version);
	deb("start_root_cluster=%d file_info_sector=%d backup_boot_sector=%d",
		fat32boot.start_root_cluster, fat32boot.file_info_sector, fat32boot.backup_boot_sector);
	deb("logical_drive_num=%d sign_0x29=%x serial=%x", fat32boot.logical_drive_num,
		fat32boot.sign_0x29, fat32boot.serial_num);
	deb("volume_name=%s fat32_name=%s", fat32boot.volume_name, fat32boot.fat32_name);
	int ptr = 0;
	// deb("fat32boot @ %d", 0);
	ptr += sizeof(FB);
	deb("fsinfo @ %d", ptr);
	ptr += sizeof(FSINFO) + fat32boot.reserved_sectors * fat32boot.bytes_per_sector;
	fatsize = (fat32boot.sectors_per_fat) * fat32boot.bytes_per_sector;
	deb("������ FAT = %d (%d MB)", fatsize, fatsize / 1024 / 1024);
	int fat_correct = 1024;
	fat1 = ptr - fat_correct;
	deb("fat1 @ %d", fat1);
	ptr += fat32boot.sectors_per_fat * fat32boot.bytes_per_sector;
	deb("fat2 @ %d", ptr - fat_correct);
	fat2 = ptr;
	DIR dir;
	// FirstRootDirSecNum = BPB_ResvdSecCnt + (BPB_NumFATs * BPB_FATSz16);
	ptr = fat32boot.reserved_sectors * fat32boot.bytes_per_sector +
		 (fat32boot.fat_copys * fat32boot.sectors_per_fat * fat32boot.bytes_per_sector);
	// deb("root dir @ %d", ptr);
	rootdirectory = ptr;
	fs = ptr;
	clstsize = fat32boot.sectors_per_cluster * fat32boot.bytes_per_sector;
	deb("clustersize: %lu", clstsize);
	deb("root @ %d (0x%x) in %.2f%%of disk", fs, fs, (float)(fs / disksize) * 100.0);
	// deb("need for fs %u MB", ptr/1024/1024);
	// cdump((char*)disk+fs, fat32boot.sectors_per_fat*fat32boot.bytes_per_sector*fat32boot.fat_copys,
	// "fat1.2");
	LARGE_INTEGER off;
	off.QuadPart = fat1;
	// paintdisk(9, off, clRed, 0, 0);
	off.QuadPart = fat2;
	// paintdisk(9, off, clRed, 0, 0);
	off.QuadPart = rootdirectory;
	// paintdisk(9, off, clGreen, 0, 0);
	fatsize = fat32boot.sectors_per_fat * fat32boot.bytes_per_sector;
	// dataoffset = (fatsize*2) + (fat32boot.reserved_sectors*fat32boot.bytes_per_sector) -
	// (16 * fat32boot.bytes_per_sector) +16384;
	// FirstDataSector = BPB_ResvdSecCnt + (BPB_NumFATs * FATSz)
	dataoffset = fat32boot.reserved_sectors * fat32boot.bytes_per_sector +
		 (fat32boot.fat_copys * fat32boot.sectors_per_fat * fat32boot.bytes_per_sector);
	pData = (LPVOID)((unsigned int)memory + dataoffset);
	deb("data @ 0x%08p (byte %lu) memory: 0x%08p - 0x%08p (%lu MB) pData 0x%08p", dataoffset,
		dataoffset, memory, HeapSize(GetProcessHeap(), 0, memory) + (SIZE_T)memory,
		HeapSize(GetProcessHeap(), 0, memory) / 1024 / 1024, pData);
	// if (maxquad < (dataoffset+1*1024*1024))
	// maxquad = dataoffset+(1*1024*1024);
	int nf = 0;
	LFNDIR lfndir;
	char lfn[128];
	wchar_t wfn[44];
	unsigned int stcl = 0;
	memset(lfn, 0, sizeof(lfn));
	memset(wfn, 0, sizeof(wfn));
	if (fat)
		HeapFree(GetProcessHeap(), 0, fat);
	fat = (unsigned int*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, fatsize);
	deb(E_FTPCONTROL, "fat @ %p (%d bytes)", fat, fatsize);
	// memset(fat, 0, fatsize);
	// memcpy((LPVOID)fat, (LPVOID)((DWORD)memory+fat1), fatsize);
	deb("load fat from %u", (fat1));
	memset(&lfndir, 0, sizeof(lfndir));
	if (sizeof(lfndir) != sizeof(dir))
	{
		deb("lfndir != dir");
		return;
	}
	readfatdir(0, rootdirptr, "root");
	// Form1->OnPaint(NULL);
	// Application->ProcessMessages();
	deb("done fat ");
}

void makefatdisk(void)
{
	char temp2[16374], temp3[16374];
	char tempbuf[16374];
	maxquad = 0;
	LPVOID dirptr;
	files.clear();
	if (!disksize)
	{
		disksize = (LONGLONG)32 * 1024 * 1024 * 1024;
	}
	if (!curdisksize || !memory)
	{
		curdisksize = 18 * 1024 * 1024;
		if (memory)
			HeapFree(GetProcessHeap(), 0, memory);
		memory = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, curdisksize);
		deb("disksize = %.2f GB (%lu), memsize = %lu (%.2f MB) memory @ %p",
			(double)((double)disksize / 1024.0 / 1024.0 / 1024.0), (unsigned long)disksize,
			(unsigned long)curdisksize, (double)((double)curdisksize / 1024.0 / 1024.0), memory);
	}
	char slev[128];
	memcpy(memory, fb, 512);
	// memset((LPVOID)((unsigned)memory +sizeof(FAT32BOOT)), (unsigned char)rand()%255,
	// 512-sizeof(FAT32BOOT)-2);
	memcpy(&fat32boot, memory, sizeof(fat32boot));
	fat32boot.bytes_per_sector = 512;
	fat32boot.sectors_per_cluster = 32;
	fat32boot.reserved_sectors = 38;
	fat32boot.sectors = 67108863;
	fat32boot.sectors_per_fat = 16377;
	fat32boot.fat_copys = 2;
	memcpy(memory, &fat32boot, sizeof(fat32boot));
	char *nl;
	unsigned int nb = 0, i = 0;
	char xyu0[128];
	char *next_token1 = NULL, *next_token2 = NULL;
	nl = temp3;
	if (!strlen(fatfiles))
	{
		strcpy(fatfiles,
			"345354 temp1.txt\r\n823673 text.doc\r\n345345 file.c\r\n345345 project.zip");
	}
	else
	{
		strncpy(temp3, fatfiles, sizeof(temp3)); // trlen(fatfiles));
	}
	// while (nl[i++])
	// if (nl[i] == 0xa)
	// nb++;
	// deb(" FAT:��������� %d �������: \r\n%s", nb, temp3);
	DWORD pfat = 0; // 0xab020;
	fatsize = fat32boot.sectors_per_fat * fat32boot.bytes_per_sector;
	// deb("������ FAT = %lu (%lu MB � %d ��������)", fatsize, fatsize/1024/1024,
	// fat32boot.sectors_per_fat);
	// pfat = ((DWORD)fs);
	deb("bytes_per_sector=%d sectors_per_cluster=%d\r\n", fat32boot.bytes_per_sector,
		fat32boot.sectors_per_cluster);
	deb("reserved_sectors=%d (%d bytes) fat_copys=%d ", fat32boot.reserved_sectors,
		fat32boot.reserved_sectors * fat32boot.bytes_per_sector, fat32boot.fat_copys);
	deb("sectors_per_fat=%d (FAT: %d MB)", fat32boot.sectors_per_fat, fatsize / 1024 / 1024);
	if (!fat)
		fat = (unsigned int*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, fatsize);
	// new unsigned int[fatsize];
	else if (fat)
		HeapFree(GetProcessHeap(), 0, fat);
	deb(E_FTPCONTROL, "fat @ %p (%d bytes)", fat, fatsize);
	if (fatsize > curdisksize)
	{
		unsigned long ns;
		ns = fatsize * 4;
		memory = HeapReAlloc(GetProcessHeap(), 0, memory, ns);
		deb("���  %d �� @ %p", ns / 1024 / 1024, memory);
		curdisksize = (LONGLONG)ns;
		maxquad = (double)curdisksize;
	}
	// memset(fat, 0x00, fatsize);
	nf = 0;
	pfat = (unsigned long)(fat32boot.reserved_sectors * fat32boot.bytes_per_sector) +
		 (fat32boot.fat_copys * fat32boot.sectors_per_fat * fat32boot.bytes_per_sector);
	// deb("fat end offset %lu", pfat);
	// deb("FAT ��������� �������� %lu ���� (%.2f MB)", (unsigned long)pfat,
	// (double)((double)pfat/1024.0/1024.0));
	rootdirectory = pfat;
	DIR dir;
	fspace_ptr = (DWORD)memory + (DWORD)pfat;
	pfat1 = (fat32boot.reserved_sectors * fat32boot.bytes_per_sector)+sizeof(FSINFO)+sizeof(FB);
	pfat2 = (fat32boot.reserved_sectors * fat32boot.bytes_per_sector) +
		 ((fat32boot.sectors_per_fat * fat32boot.bytes_per_sector))+sizeof(FSINFO)+sizeof(FB);
	pfat1 -= 1024;
	pfat2 -= 1024;
	LARGE_INTEGER off;
	off.QuadPart = pfat1;
	// paintdisk(9, off, clRed, true, 0);
	off.QuadPart = pfat2;
	// paintdisk(9, off, clRed, true, 0);
	clstsize = fat32boot.sectors_per_cluster * fat32boot.bytes_per_sector;
	totclusters = ((LONGLONG)disksize) / (LONGLONG)clstsize;
	deb("������� = %lu ����, ����� ���� = %lu (%.2f GB)\r\��������� = %lu", clstsize,
		(unsigned long)disksize, (double)((double)disksize / 1024.0 / 1024.0 / 1024.0), totclusters);
	LPVOID pData;
	dataoffset = (fatsize * 2) + (fat32boot.reserved_sectors * fat32boot.bytes_per_sector);
	off.QuadPart = dataoffset;
	// paintdisk(9, off, clGreen, true, 0);
	pData = (LPVOID)((unsigned int)memory + dataoffset);
	deb("1 ������� @ %p (dataoffset %d, 0x%4X)", pData, dataoffset, dataoffset);
	dirptr = (LPVOID)((unsigned long)memory + (unsigned long)rootdirectory);
	deb("rootdirectory @ memory:0x%08p (offset %lu - 0x%08X) ... ", dirptr, pfat, pfat);
	deb("pfat2 size need %d MB", ((unsigned long)pfat2 + fatsize) / 1024 / 1024);
	// if (pfat2+fatsize>curdisksize)
	// {
	// unsigned long ns;
	// ns = pfat2+(2*1024*1024);
	// deb("������ ��� ��� 2 ������� %d ��", ns/1024/1024);
	// memory = HeapReAlloc(GetProcessHeap(), 0, memory, ns);
	// curdisksize = ns;
	//
	// }
	char filename[255];
	char fn[255];
	unsigned long allocatedbytes;
	unsigned long startclust;
	unsigned long startoffset;
	unsigned int offset;
	unsigned int clst;
	unsigned int ssize;
	unsigned int nclust;
	unsigned int entrcls;
	unsigned long cluster;
	unsigned long fsize = 0;
	int attr = 0;
	char fnm[128];
	char *p;
	unsigned long freeclusters;
	freeclusters = totclusters;
	totalmb = dataoffset;
	deb("total B = %.0f (%.1f MB )", totalmb, totalmb / 1024 / 1024);
	maxquad = (double)curdisksize;
	deb("maxquad=%.2f MB, disksize=%.2f MB", (double)maxquad / 1024.0 / 1024.0,
		(double)((double)disksize / 1024.0 / 1024.0));
	DWORD tms2;
	// Form1->Repaint();
	netfatfilesize = 0;
	cluster = start_cluster;
	stc = start_cluster;
	fat[0] = 0x0ffffff8;
	fat[1] = 0xffffffff;
	fat[2] = 0x0fffffff;
	fat[3] = 0x0fffffff;
	fat[4] = 0x0fffffff;
	// for(unsigned long u=0;u<14000;u++)
	// if(!fat[u])
	// fat[u]=0x0fffffff;
	LPVOID ndirptr;
	ndirptr = (LPVOID)dirptr;
	deb("fat write rootdir begin @ %p (%lu kb)", dirptr,
		((unsigned long)dirptr - (unsigned long)memory) / 1024);
	char db[1024];
	strcpy(db, "");
	FATFILE f;
	memset(&f, 0, sizeof(f));
	f.memory = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, 32768);
	// memset(f.memory, 0, 32768);
	f.memsize = 32768;
	f.start_offset = rootdirectory;
	f.end_offset = rootdirectory + 32768;
	f.fetched = true;
	f.dir = true;
	strcpy(f.fn, "root");
	rootdirptr = (LPVOID)f.memory;
	deb("  ========== FAT ========================================================");
	int added = fataddfiles(db, f.memory, 5, temp3);
	f.numrecs = count_recs(f.memory);
	files.push_back(f);
	deb("  ========== FAT ========================================================");
	deb("fat write rootdir done");
	char *cp = (char*)rootdirptr;
	while (*cp)
	{
		cp += sizeof(DIR);
	}
	memset(&dir, 0, sizeof(dir));
	dir.attr = 8;
	strcpy(dir.fname, "FtpDisk");
	deb("    write volume dir label '%s' @ %p", dir.fname, dirptr);
	memcpy((LPVOID)cp, &dir, sizeof(dir));
	if (!files.size())
	{
		deb("no netfat, ftpfiles: %s", fatfiles);
	}
	// deb("  ------------ totalMB %.0f (%.1f MB) [%.1f MB FAT32]", totalmb, totalmb/1024/1024,
	// (double)dataoffset/1024/1024);
	deb("crc fat32boot %d @ %p = %x", sizeof(fat32boot), memory,
		(USHORT)checksum((unsigned short*)memory, sizeof(fat32boot)));
	// deb("pfat1 %d pfat2 %d", pfat1, pfat2);
	deb("crc fat[]            %x (%d)", checksum((unsigned short*)fat, fatsize), fatsize);
	memcpy((LPVOID)((DWORD)memory + pfat1), fat, fatsize);
	deb("crc fat 1 @ %p %x (%d)", ((unsigned long)memory + pfat1),
		checksum((unsigned short*)((unsigned long)memory + pfat1), fatsize), fatsize);
	memcpy((LPVOID)((DWORD)memory + pfat2), fat, fatsize);
	deb("crc fat 2 @ %p %x (%d)", ((unsigned long)memory + pfat2),
		checksum((unsigned short*)((unsigned long)memory + pfat2), fatsize), fatsize);
	deb("NetFAT: %d, vol: 1, FAT: %d. rootdirsize: %lu ����", files.size(), added,
		(unsigned long)(added * 2 + 1)*sizeof(DIR));
	// deb("�������� fat1 = > %d fat2 = > %d (%d bytes) memory @ %p", pfat1, pfat2,
	// fat32boot.sectors_per_fat*fat32boot.bytes_per_sector, memory);
	off.QuadPart = pfat;
	// paintdisk(9, off, clLime, true, 0);
	// delete[]fat;
	deb("done FAT");
	try
	{
		// delete[]temp2;
		// delete[]temp3;
		// }
		// Form1->Button4->Enabled = true;
		// Form1->Button3->Enabled = true;
		// Form1->Button5->Enabled = true;
		// Form1->Button6->Enabled = true;
		// Form1->Button2->Enabled = true;
		LARGE_INTEGER sizee;
		sizee.QuadPart = disksize;
		// maxquad = totalmb*1.7;
		// Form1->current->Text = "FAT ���� ������";
		// Form1->Repaint();
	}
	catch(...)
	{
		deb("exception while makefatdisk");
	}
	// Application->ProcessMessages();
	// VdskSetDisk(hdisk, sizee, 2, 32, 512, false, true, true);
	// Button9Click(NULL);
	return;
}

int mountdisk(void)
{
	LARGE_INTEGER size;
	unmounting = false;
	memset(&size, 0, sizeof(size));
	size.QuadPart = (LONGLONG)32 * 1024 * 1024 * 1024;
	disksize = (LONGLONG)size.QuadPart;
	// maxquad = (double)curdisksize;
	deb("creating disk %I64d/%lu..", size.QuadPart, (unsigned long)curdisksize);
	hdisk = VdskCreateVirtualDisk(size, 2, 32, 0, VDSK_DISK_REMOVABLE | VDSK_DISK_MEDIA_IN_DEVICE,
		11300, OnRead, OnWrite, NULL, NULL, OnEvent, NULL);
	if (hdisk == INVALID_HANDLE_VALUE)
	{
		deb("VdskCreateVirtualDisk: %s", fmterr());
		deb("\r\n.\r\n.\r\n.\r\n             -- STOP -- Critical error.");
		closesocket(ConnectSocket);
		return -1;
	}
	if (VdskStartDisk(hdisk, true))
	{
		// deb("started drive");
	}
	// if(!VdskSetDisk(hdisk,size, 2, 32, 8192, 1, 1, 1)) {
	// deb("failed VdskSetDisk(): %s",fmterr());
	// }
	bool ret = VdskMountDisk(hdisk, 'y', true);
	if (!ret)
	{
		deb("VdskMountDisk: %s", fmterr());
		// delete[]memory;
		HeapFree(GetProcessHeap(), 0, memory);
		memory = NULL;
		// Form1->Button1->Enabled = true;
		// Form1->Button2->Enabled = false;
		return -1;
	}
	else
	{
		extern char ftphost[128];
		// deunicode(Form1->ftpip->Text.c_str(), ftphost, 128);
		deb("���� Y:\\ (%lu GB)  ��������� � %s.",
			(unsigned long)(size.QuadPart / 1024 / 1024 / 1024), ftphost);
	}
	// Form1->ptr->Text = "���������";
	// Application->ProcessMessages();
}

void cdump(char*b, int size, char*tag = NULL)
{
	FILE *x;
	char fn[128];
	sprintf(fn, "dump-%s.c", tag ? tag : "unnamed");
	x = fopen(fn, "w");
	fprintf(x, "// %d bytes\r", size);
	int nl = 0;
	for (int i = 0; i < size; i++)
	{
		if (nl++ > 6)
		{
			fprintf(x, "\r");
			nl = 0;
		}
		char str[128];
		sprintf(str, "0x%02x, ", (unsigned char)b[i]);
		fwrite(str, 1, strlen(str), x);
	}
	fclose(x);
}

void ftpabor(void)
{
	char tb1[10240];
	char abor1[12120];
	char buff1[300];
	buff1[0] = 0xff;
	buff1[1] = 0xf4;
	// buff1[2] = 0xff;
	buff1[2] = 0xf2;
	ftpblocking(false);
	// ftplock();
	send(FtpSocket, buff1, 3, MSG_OOB);
	// ftpunlock();
	// sprintf(abor1, "ABOR\r\n");
	ftpcmd("ABOR\r\n", 0, 0);
	memset(tb1, 0, sizeof(tb1));
	if (ftphasdata())
	{
		ftprl(tb1, timeoutst, 0);
		// Sleep(timeoutst);
	}
	// ftprl(tb1, timeoutst, 0);
	// deb("ftpabor() exiting: %s", tb1);
	// }
	// catch(...)
	// {
	// deb("inside eshelmeks function abor");
	// call_events(E_EXCEPTION, (LPVOID)"eshelmeks functon ftpabor()");
	// }
}

bool sortcachebyoffset(CACHE c1, CACHE c2)
{
	return c1.offset < c2.offset;
}

int ftppasvconnect(char *host, int port)
{
	shutdown(tempsock, SD_BOTH);
	closesocket(tempsock);
	deb("pasv connect %s:%d", host, port);

	DWORD ms = GetTickCount();
	int ret = 0;

	ftpstate = 4;
	int err;
	sockaddr_in clientService;
	tempsock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (tempsock == INVALID_SOCKET)
	{
		deb("Client: Error at socket(): %ld\n", WSAGetLastError());
		ftpstate = -1;
		return false;
	}
	if (clientService.sin_addr.s_addr == -1)
	{
		deb("resolve %s failed", host);
		closesocket(tempsock);
		tempsock = 0;
		call_events(E_RESOLVEERR, (LPVOID)host);
		return false;
	}
	// deb("dns: IP %s", inet_ntoa(clientService.sin_addr));
	memset(&clientService, 0, sizeof(clientService));
	clientService.sin_family = AF_INET;
	clientService.sin_addr.s_addr = resolve(ftphost);
	clientService.sin_port = htons(ftpport);

	ftppsvblocking(false);

	ff_SocketBuffers(sockets_buffers);

	// if (pasvsockconntimeout < 1000)
	pasvsockconntimeout = 4000;

	unsigned long ntry = 0;

	call_events(E_PASVCONNECTING, (LPVOID)host, (LPVOID)ftpport);

	if (tempsock <= 0)
		deb("tempsock %d", tempsock);
	while ((ret = connect(tempsock, (SOCKADDR*) & clientService, sizeof(clientService))) == -1)
	{
		err = WSAGetLastError();
		if (err == 10035)
			continue;

		if (err == 10056)
			break;

		// Sleep(10);

		call_events(E_TIMEOUTCHANGE, (LPVOID)(GetTickCount() - ms));
		// deb("connect err %s", fmterr(err));
		if (GetTickCount() - ms >= pasvsockconntimeout)
		{
			char s[1024];
			// if(err==10037)
			// continue;

			sprintf(s, "ftppasvread failed connect %s:%d in %lu ms [err %d: %s]", host, port,
				pasvsockconntimeout, err, fmterr(err));
			call_events(E_TIMEOUT, (LPVOID)__FILE__, (LPVOID)__LINE__, (LPVOID)s);
			// ftpstate = -1;
			// // deb(" ! too many connect fails");
			// deb(s);
			call_events(E_FTPPASVERR, (LPVOID)s);
			break;
		}
	}
	ms = GetTickCount() - ms;

	if (ms >= 400 && ms <= 2000)
		pasvsockconntimeout = ms * 5;

	if (!err)
		err = WSAGetLastError();
	if (err && err != 10056)
	{
		char s[1024];
		sprintf(s, "ftppasvconnect failed  %s:%d\r\n[err %d: %s]", host, port, err, fmterr(err));
		deb(s);
		call_events(E_FTPPASVERR, s);
		return false;
	}

	int lport;
	struct sockaddr_in sa;
	int nl = sizeof(sa);
	getsockname(tempsock, (struct sockaddr*) & sa, &nl);
	lport = ntohs(sa.sin_port);
	clientService.sin_addr.s_addr = resolve(host);
	// ftpdeb(" dns: A %s", inet_ntoa(clientService.sin_addr));
	deb("�������� � %s:%d - %u ����\n", inet_ntoa(clientService.sin_addr), ftpport, ms);
	call_events(E_PASVCONNECTED, (LPVOID)host, (LPVOID)ftpport, (LPVOID)ms);
	return true;
}

unsigned long ftppasvread(char *host, int port, char *buf, int bufsiz, unsigned long len,
	char *dosfn, unsigned long cacheblocks, unsigned long startoffset)
{
	char tb[22200];
	bool gotopen = false;
	DWORD ftime = GetTickCount();
	breakpasvread = false;
	deb("ftppasvread(%s, %d, 0x%p, %d, %lu)", host, port, buf, bufsiz, len);

	if (HeapSize(GetProcessHeap(), 0, buf) < (unsigned)len)
	{
		deb("buf too small for %lu bytes,only %u", len, HeapSize(GetProcessHeap(), 0, buf));
		return 0;
	}
	else
	{
		deb("buf %p OK = %u bytes", buf, HeapSize(GetProcessHeap(), 0, buf));
	}

	// memset(buf, 0, 4096);
	DWORD ms = GetTickCount();

	if (ftphasdata())
	{
		ftprl(tb, timeoutst, 0);
		gotopen = true;
	}

	if (!ftppasvconnect(host, port))
		return -1;

	// if (!gotopen)
	// {
	// ftprl(tb, timeoutst, 0);
	// gotopen = true;
	// }

	ms = GetTickCount() - ms;

	ftpstate = 2;

	// if (ms)
	// {

	int ss = 0;
	char *ptr = buf;
	bool cont = true;
	int readleft = len ? len : bufsiz;
	if (!len && !bufsiz)
	{
		readleft = 16384;
		cont = true;
	}
	bool exact = false;
	if (len == (unsigned long)bufsiz && len)
		exact = true;
	unsigned long readok = 0;
	DWORD tms = GetTickCount();
	int numr = 0;
	char tempbuf[8192];
	bool tempreading = false;

	DWORD ltms = 0;
	// ftppsvblocking(false);
	DWORD lastinms = timeoutst;
	// deb("passive start recv @ %p", ptr);
	ftppsvblocking(false);
	int lastreadok = 0;
	unsigned long numrecvs = 0;
	unsigned long ptrsiz;
	ptrsiz = (unsigned long)HeapSize(GetProcessHeap(), 0, ptr);
	// deb("passive read block size %lu (%p), blocksize=%lu", ptrsiz, ptr, cacheblocks);
	unsigned long sleeps = 0;
	err = 0;

	DWORD lastetime = 0, lasterrtime = 0;
	lastetime = GetTickCount();

	deb("passive reading start");
		DWORD lastsstime = GetTickCount();
	while (true)
	{
		if (cacheblocks && breakpasvread && readok >= 256000)
		{
			deb(E_FTPCONTROL, "breaking due to breakpasvread: readok: %lu", readok);
			break;
		}

		ftpstate = 4;
		DWORD tms2 = GetTickCount();

		if (unmounting)
			return readok;

		try
		{
			unsigned long readsize;
			// readsize = cacheblocks ? cacheblocks : 131072; // (cacheblocks > readleft ? cacheblocks : readleft) :
			readsize = len > sockets_buffers ? sockets_buffers : len;
			if (!sockets_buffers)
				readsize = 65535;
			// (readleft > 131072 ? 131072 : readleft);
			// deb(E_FTPCONTROL, "execing recv(%d, %p, %lu, 0)", tempsock, ptr, readleft);
			if (IsBadWritePtr(ptr, readleft))
			{
				deb(E_FTPCONTROL, "bad write ptr pasv recv @ %p", ptr);
				deb("%p bufsize = %u, len = %lu", buf, HeapSize(GetProcessHeap(), 0, buf), len);
				ds;
			}
			ss = recv(tempsock, (char*)ptr, readleft, 0);

			lasterrtime = GetTickCount();
			err = WSAGetLastError();

			 if (ss == -1)
			 	Sleep(1);


		}
		catch(Exception & e)
		{
			char sss[1111];
			deunicode(e.Message.c_str(), sss, sizeof(sss));
			fdeb("recv except %s", sss);
			exp;
		}
		// ftppsvblocking(false);
		numrecvs++;

		if (ss == 0)
		{

			deb("break pasv read (ss=0) @ %lu", readok);
			break;
		}
	
		if (ss < 0)
		{
			// err = WSAGetLastError();
			if (err == 10035)
			{
				// Sleep(100);
				// sleeps += 100;
			}
			else if (err == 10053 && exact && readok < len)
			{
				call_events(E_PASVDISCONNECT, (LPVOID)host, (LPVOID)ftpport,
					(LPVOID)(GetTickCount() - ftime), (LPVOID)readok);
				deb(E_FTPCONTROL,
					"recv()=%s\r\nlost pasv connection @ byte %lu\r\nreconnecting %s:%d [max %lu ms]..."
					, fmterr(err), readok, host, port, pasvsockconntimeout);
				if (ftphasdata())
					ftprl(tb, timeoutst, 0);

				// if (!ftppasvconnect(host, port))
				{
					deb("unsuccessful reconect");
					break;
				}
			}
			else
			{
				if (err)
					deb("recv ss: %d, exact %d readok %lu recv: %d %s", ss, exact, readok, err,
					fmterr(err));
				// Sleep(timeoutst);
				// deb("ss %d err %d", ss, err);
				// if (err)
				break;
			}
		}

		if (GetTickCount() - tms2 > 0)
			lastdelay = GetTickCount() - tms2;

		int done;
		if (ss > 0)
		{
			if (!gotopen && ss > 0)
			{

				// call_events(E_TIMEOUTCHANGE, (LPVOID)ms);
			}

			ltms = GetTickCount();
			lastpasvread = ss;

			// if (readok - lastreadok >= clstsize / 2)
			if ((GetTickCount() - lastetime >= 150 || cacheblocks) && ss > 0)
			{
				call_events(E_PASVREAD, (LPVOID)readok, (LPVOID)len, (LPVOID)bufsiz);
				// call_events(E_TIMEOUTCHANGE, (LPVOID)(GetTickCount() - lastetime));
				lastetime = GetTickCount();
				lastreadok = readok;

				// deb("  recv() = %d readok:%lu -- %lu", ss, readok, GetTickCount() - tms);
			}

			if (cacheblocks)
			{
				// deb("saving '%s' @ %lu, ss %d, ptr %p..", dosfn, startoffset + readok, ss, ptr);
				DWORD xyutm;
				xyutm = GetTickCount();
				cache_save("", dosfn, startoffset + readok, ss, ptr);
				if (GetTickCount() - xyutm >= 200)
					deb(E_FTPCONTROL, "saved %lu in %d ms", ss, GetTickCount() - xyutm);
			}

			readok += ss;
			numr = 0;

			ptr = ptr + ss;

			if (ss > 1)
			{
				ilastlen = (double)ss;
				ibytesin += (double)ss;
			}
		}

		if ((readleft - ss) >= 0 && ss > 0)
			readleft -= ss;

		if (exact && readok >= len)
		{
			deb("  recved exact %lu bytes", readok);
			break;
		}


		if (ss <= 0 && GetTickCount() - lastsstime >= 2000)
		{
			call_events(E_TIMEOUT, (LPVOID)__FILE__, (LPVOID)__LINE__, (LPVOID)"maxpasv reach 2 sec");
			err = WSAGetLastError();
			deb("no psv data err: %d ss %d %s", err, ss, fmterr(err));
			// ftpabor();
			break;
		}
		else
		{
			if (ss > 0)
				lastsstime = GetTickCount();
		}
		if (ptrsiz && readok > ptrsiz)
		{
			deb("readok pasv read buf overflow: readed %lu, buffer only %lu", readok, ptrsiz);
			ds;
		}
	}

	// deb("done data read = %d,ss=%d", readok, ss);
	// if (ss <= 0)
	// {
	// if (!err)
	// err = WSAGetLastError();
	// if (err)
	// deb(E_FTPCONTROL, "recv ret:%d err:%d [%s]", ss, err, fmterr(err));
	// }
	// else
	if (ss > 0)
	{
		lastreadok = readok;
	}

	shutdown(tempsock, SD_BOTH);
	closesocket(tempsock);

	tempsock = 0;

	call_events(E_PASVREAD, (LPVOID)readok, (LPVOID)len, (LPVOID)bufsiz);
	call_events(E_PASVDISCONNECT, (LPVOID)host, (LPVOID)ftpport, (LPVOID)(GetTickCount() - ftime),
		(LPVOID)readok);

	if (!gotopen || (!strstr(tb, "226 ") && !strstr(tb, "426 ")))
	{
		if (ftphasdata() || !gotopen)
			ftprl(tb, timeoutst, 0);

		ftpabor();
	}
	// }
	tms = GetTickCount() - tms;
	ftpstate = -1;
	ftime = GetTickCount() - ftime;
	ftpdatatime += ftime;

	if (ftphasdata())
		ftprl(tb, timeoutst, 0);

	deb("ftppasvread() = read %d, numr: %lu ss %d, %lu ��, numrecvs: %lu,sleeps:%lu", readok, numr,
		ss, ftime, numrecvs, sleeps);

	return readok;
}

extern "C" int __stdcall ff_ConnectFTP(char*host, int port)
{
	deb("inside ff_ConnectFTP(%s, %d)", host, port);
	processing_disk_event = true;
	WORD wVersionRequested;
	WSADATA wsaData;
	fatreaded = false;
	int err;
	wVersionRequested = MAKEWORD(2, 2);
	err = WSAStartup(wVersionRequested, &wsaData);

	deb("wsastart: %d", err);
	if (err != 0)
	{
		deb("wsastartup failed: %s", fmterr(WSAGetLastError()));
		processing_disk_event = false;
		return err;
	}
	unmounting = false;

	if (precacheminbytes)
		deb("precache minimum read-ahead bytes: %lu", precacheminbytes);

	deb("vdsk init: %d", VdskInitializeLibrary());

	ecs = new TCriticalSection();
	ccs = new TCriticalSection();
	filescs = new TCriticalSection();
	// if (!ftpcs)
	ftpcs = new TCriticalSection();
	cchs = new TCriticalSection();
	cev = new TEvent(false);

	deb("critical created");

	strcpy(ftphost, host);
	ftpport = port;

	ftpstartport = port;
	int yop = ftpconn(true);

	deb("ff_connectftp(%s,%d) = %d", host, port, yop);

	processing_disk_event = false;
	deb("returning %d", yop);
	return yop;
}

extern "C" int __stdcall ff_AuthFTP(char*login, char*pass)
{
	deb("inside ff_AuthFTP(%s,%s) ", login, pass);
	strcpy(ftpuser, login);
	strcpy(ftppass, pass);

	deb("credentials copy OK");

	processing_disk_event = true;

	deb("executing ftpauth() @ 0x%08p", ftpauth);
	int ret = ftpauth();

	processing_disk_event = false;
	deb("ff_AuthFTP(%s,%s) ret ftpauth() = %d", login, pass, ret);
	return ret;
}

extern "C" int __stdcall ff_ScanFTP(int leveldir, unsigned long maxfilesizemb,
	unsigned long maxdiskusemb)
{
	deb("ff_ScanFTP(%d,%lu,%lu)", leveldir, maxfilesizemb, maxdiskusemb);
	dirlev = leveldir;
	maxdiskuse = (LONGLONG)maxdiskusemb * 1024L * 1024L;
	deb("dirlev %d", dirlev);
	processing_disk_event = true;
	makefatdisk();
	processing_disk_event = false;
	deb("ff_ScanFTP done");
}

extern "C" int __stdcall ff_MountDisk(char let)
{
	drive_letter = let;
	deb("ff_MountDisk(%c)", drive_letter);
	mountdisk();
}

extern "C" int __stdcall ff_Version(void)
{
	int ver = 0;
	ver = checksum((unsigned short*)__DATE__, sizeof(__DATE__));
	ver += checksum((unsigned short*)__TIME__, sizeof(__TIME__));
	return ver;
}

extern "C" DWORD __stdcall ff_AddEventHandler(LPVOID func)
{
	EVENT e;
	e.addr = func;
	events.push_back(e);
	deb("func %p added to event list", func);
	return rand();
}

extern "C" int __stdcall ff_Clear(void)
{
	unmounting = true;

	cache_clear();

	files.clear();

	ftpdisconnect();
	VdskDeleteDisk(hdisk);
	VdskFreeLibrary();
	WSACleanup();
	deb("ff_Clear done");
}

extern "C" int __stdcall ff_SocketNoDelay(bool on)
{
	sockets_nodelay = on;
	bool nodelay = sockets_nodelay;
	deb(" socket nodelay %d ", nodelay);
	//
	int ret;
	if (FtpSocket)
	{
		deb(" FtpSocket %d ", FtpSocket);
		ret = setsockopt(FtpSocket, SOL_SOCKET, TCP_NODELAY, (char*) & nodelay, sizeof(bool));
		if (SOCKET_ERROR == ret)
			deb(" ff_SocketNoDelay FtpSocket : %s ", fmterr(WSAGetLastError()));
	}
	if (tempsock)
	{
		deb(" tempsock %d ", tempsock);
		ret = setsockopt(tempsock, SOL_SOCKET, TCP_NODELAY, (char*) & nodelay, sizeof(bool));
		if (SOCKET_ERROR == ret)
			deb(" ff_SocketNoDelay tempsock : %s ", fmterr(WSAGetLastError()));
	}
	return on;
}

extern "C" int __stdcall ff_SocketOOBInline(bool on)
{
	sockets_oobinline = on;
	bool iMode = sockets_oobinline;
	int ret;
	deb(" socket oobinline %d ", iMode);
	if (FtpSocket)
	{
		ret = setsockopt(FtpSocket, SOL_SOCKET, SO_OOBINLINE, (char*) & iMode, 1);
		if (SOCKET_ERROR == ret)
			deb(" ff_SocketOOBInline FtpSocket %d : %s ", FtpSocket, fmterr(WSAGetLastError()));
	}
	if (tempsock)
	{
		ret = setsockopt(tempsock, SOL_SOCKET, SO_OOBINLINE, (char*) & iMode, 1);
		if (SOCKET_ERROR == ret)
			deb(" ff_SocketOOBInline tempsock : %s ", fmterr(WSAGetLastError()));
	}
	return on;
}

extern "C" int __stdcall ff_NetSpeed(double*spd)
{
	*spd = (double)ibytesin;
	return 0;
}

extern "C" int __stdcall ff_ReadAheadMinCache(unsigned long minaheadbytes)
{
	precacheminbytes = minaheadbytes;
	if (precacheminbytes)
		deb(E_FTPCONTROL, " precache minimum read - ahead MB : %.2f ",
		(double)((double)precacheminbytes / 1024.0 / 1024.0));
	else
		deb(E_FTPCONTROL, " precacheminbytes = cluster size first ");
}

extern "C" int __stdcall ff_SocketBuffers(long bufsize)
{
	sockets_buffers = bufsize;
	int sb = (int)sockets_buffers;
	if (!sockets_buffers)
		return 0;
	// if (FtpSocket)
	// {
	// setsockopt(FtpSocket, SOL_SOCKET, SO_RCVBUF, (unsigned char*) & bufsize, 4);
	// setsockopt(FtpSocket, SOL_SOCKET, SO_SNDBUF, (unsigned char*) & bufsize, 4);
	// }
	if (tempsock)
	{
		int ret = setsockopt(tempsock, SOL_SOCKET, SO_RCVBUF, (unsigned char*) & sb, sizeof(int));
		if (ret == SOCKET_ERROR)
		{
			deb(" ff_SocketBuffers setsockopt : %s ", fmterr());
		}
		ret = setsockopt(tempsock, SOL_SOCKET, SO_SNDBUF, (unsigned char*) & sb, sizeof(int));
		if (ret == SOCKET_ERROR)
		{
			deb(" ff_SocketBuffers setsockopt : %s ", fmterr());
		}
	}
	// deb(" set SO_RCVBUF / SO_SNDBUF on socket %d = %d ", tempsock, sb);
	sb = -1;
	int ilen = sizeof(sb);
	getsockopt(tempsock, SOL_SOCKET, SO_RCVBUF, (char*) & sb, &ilen);
	// deb(" so_rcvbuf %d ", sb);

	getsockopt(tempsock, SOL_SOCKET, SO_SNDBUF, (char*) & sb, &ilen);
	// deb(" SO_SNDBUF %d ", sb);
}

extern "C" int __stdcall ff_Init(void)
{
	// memcpy((void*)0,(void*)111110,1111111);
	if (!VdskStartDriver() || !VdskInitializeLibrary())
	{
		deb(" Error #[Vdsk Driver : %s]", fmterr());
		exp;
	}
}

extern "C" int __stdcall ff_Debug(int type)
{
	// if (type == 1)
	// {
	// cache_list();
	// return 0;
	// }
	// deb(" ftpcurdir : %s pwd : %s ", ftpcurdir, ftppwd());
	// for (vector<FATFILE>::iterator it = files.begin(); it != files.end(); it++)
	// {
	// if (!(*it).dir || !(*it).memory)
	// continue;
	// char fn[255];
	// sprintf(fn, " %s.txt ", (*it).fn);
	// dump((char*)(*it).memory, 32768, fn);
	// deb(" saved %s ", fn);
	// }
	readfatdir(0, rootdirptr, " root ");
	deb(" NETFAT == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == = ");
	for (vector<FATFILE>::iterator it = files.begin(); it != files.end(); it++)
	{
		deb(" %s / %-20s [ %4s]size : %lu startcl : %lu stoff : %lu enofs : %lu ", (*it).ftppath,
			(*it).fn, (*it).dir ? " dir " : " ", (*it).size, (*it).firstcluster, (*it).start_offset,
			(*it).end_offset);
	}
}

// ---------------------------------------------------------------------------

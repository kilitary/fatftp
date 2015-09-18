#include "ff.h"
#include "ftp.h"
#include "vdskapi.h"
#include "disk.h"
#include "functions.h"
#include "extern.h"
#include <vector>
#pragma hdrstop

char ftpcs_lockedat[1024] = "unknown!";
int ftpcs_entered = 0;
char ftpcs_enteringat[111];

bool _ftptrylock(char *f, int line)
{
	bool xui = false;

	deb("_ftptrylock(%s, %d)", f, line);
	if (!ftpcs)
	{
		deb("ftpcs = 0!");
		return false;
	}
	if (ftplockline != -1)
		deb("ftplockline %d", ftplockline);
	if (ftpcs->TryEnter())
	{
		ftpcs_entered++;
		xui = true;
		ftplockline = line;

		deb("_ftptrylock locked %s:%d", f, line);
		call_events(E_LOCKED, (LPVOID)"_ftptrylock", (LPVOID)ftplockline, (LPVOID)ftpcs_entered);
	}
	else
	{
		deb("_ftptrylock can't lock %x, locked @ %d", ftpcs, ftplockline);
	}
	// deb("xyu %d",xui);

	return xui;
}

void _ftplock(char *func, int line)
{
	if (!ftpcs)
		return;

	sprintf(ftpcs_enteringat, "entering in %s():%3d", func, line);
	if (ftplockline)
		deb("ftplockline %d", ftplockline);

	// if (ftpcs_entered < 0 || ftpcs_entered > 3)
	// deb("_ftplock(%s,%d) ftpstate: %d ftpcs_entered: %d", func, line, ftpstate, ftpcs_entered);
	int ss = ftpstate;
	ftpstate = 33;
	call_events(E_LOCKING, (LPVOID)func, (LPVOID)line, (LPVOID)ftpcs_entered);
	ftpcs->Enter();
	ftplockline = line;
	if (line)
		sprintf(ftpcs_lockedat, "%s:%d", func, line);
	ftpcs_entered++;
	call_events(E_LOCKED, (LPVOID)func, (LPVOID)line, (LPVOID)ftpcs_entered);
	ftpstate = ss;
	ftpcs_enteringat[0] = 0;
}

void _ftpunlock(char *func, int line)
{
	if (!ftpcs)
		return;
	// if (ftpcs_entered < 0 || ftpcs_entered > 3)
	// deb("_ftpunlock(%s,%d) ftpstate: %d ftpcs_entered: %d", func, line, ftpstate, ftpcs_entered);
	int ss = ftpstate;
	ftpstate = 34;
	call_events(E_UNLOCKING, (LPVOID)func, (LPVOID)line, (LPVOID)ftpcs_entered);
	ftpcs->Leave();
  //	ftplockline = -1;
	ftpcs_entered--;
	call_events(E_UNLOCKED, (LPVOID)func, (LPVOID)line, (LPVOID)ftpcs_entered);
	ftpstate = ss;

}

char *ftppwd(void)
{
	static char b[1024];
	int code = 0;
	bool done = false;
	char *p = NULL;

	while (ftphasdata())
	{
		ftprl(b, timeoutst, 0);
		Sleep(timeoutst);
	}
	ftpcmd("PWD", 0, 0);
	ftprl(b, timeoutst, 0);

	Sleep(50);
	while (ftphasdata())
	{
		ftprl(b, timeoutst, 0);
		Sleep(timeoutst);
	}
	// while (!done)
	// {
	// ftpcmd("PWD", 0, 0);
	// ftprl(b, timeoutst, 0);
	//
	// if (!(p = strstr(b, "257 "))) // b[0] != '2' || b[1] != '5' || b[2] != '7')
	//
	// {
	// // if (!code)
	// // code = atoi(p);
	// while (ftphasdata())
	// {
	// ftprl(b, timeoutst, 0);
	// if (strstr(b, "257 "))
	// {
	// done = true;
	// break;
	// }
	// }
	//
	// }
	// else
	// {
	// done = true;
	// }
	//
	// }
	static char bb[128];
	char *c;
	c = strstr(b, "257");
	if (c)
	{
		int i = sscanf(c, "257 \"%[^\"]\"", bb);
		if (!i)
			return "<unscanned>";
	}
	return bb;
}

int ftpcd(char *dir)
{
	char b[2222] = "";
	char *p = NULL;
	char sdir[222];

	if (dir[0] == 0x20)
		return -1;

	memset(sdir, 0, sizeof(sdir));

	if (dir[0] == '!')
		sdir[0] = '/';

	strcat(sdir, dir);

	int code = -1;
	bool done = false;
	// while (!done)
	// {
	// sprintf(b, "CWD %s", sdir);
	// ftpcmd(b, 0, 0);
	// ftprl(b, timeoutst, 0);
	//
	// // if (char*p = strstr(b, "250 ")) // b[0] != '2' || b[1] != '5' || b[2] != '7')
	// code = atoi(b);
	//
	// if (!(p = strstr(b, "250"))) // b[0] != '2' || b[1] != '5' || b[2] != '7')
	//
	// {
	// // if (!code)
	// // code = atoi(p);
	// while (ftphasdata())
	// {
	// ftprl(b, timeoutst, 0);
	// if (strstr(b, "250"))
	// {
	// done = true;
	// break;
	// }
	// }
	//
	// }
	// else
	// {
	// done = true;
	// }
	//
	// }
  //	Sleep(timeoutst);
	while (ftphasdata())
	{
		ftprl(b, timeoutst, 0);
		Sleep(timeoutst);
	}

	sprintf(b, "CWD %s", sdir);
	ftpcmd(b, 0, 0);
	ftprl(b, timeoutst, 0);

	code = atoi(b);

	return code;
}

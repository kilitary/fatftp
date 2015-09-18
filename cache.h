#pragma once

bool in_cache(char*fn, unsigned long offset, ULONG len);
long cache_read(char*fn, unsigned long offset, LPVOID buf, ULONG len, bool paging = false);
int cache_save(char*ftpdir, char*fn, unsigned long offset, unsigned long size, char*buffer);
bool cache_write(long offset, LPVOID buf, ULONG len, int*hits, bool unsaved);
unsigned long highcacheoffset(char*fn);
unsigned long seqcacheoffset(char*fn);
void cache_clear(void);
int cache_seq_reads(void);
void cache_list(void);
unsigned long nextcacheoffset(char*fn, unsigned long offset);
typedef struct
{
	unsigned long offset;
	unsigned long len;
	LPVOID buf;
	unsigned long changed;
	DWORD lastaccessed;
	bool unsaved;
	DWORD opid;
	int updates;
	int hits;
	char fn[255];
	char ftppath[255];
	unsigned long size;
	unsigned long bufsize;
	unsigned long fetchedsize;
	unsigned long cacheleft;
	bool nowcaching;
	unsigned long nowcachingoffset;
	unsigned long nowcachinglen;
}CACHE;

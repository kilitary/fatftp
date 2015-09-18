unsigned long nread(unsigned long buf, unsigned long offset, unsigned long size, bool paged);
unsigned long nwrite(unsigned long buf, unsigned long offset, unsigned long size, bool paged, bool cachesave = false,
	int flags = 0, char*addon = NULL);

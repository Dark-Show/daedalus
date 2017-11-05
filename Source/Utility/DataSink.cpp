#include "stdafx.h"
#include "DataSink.h"
#include "Base/Assert.h"

DataSink::~DataSink() {}

FileSink::FileSink() : Handle(NULL) {}

FileSink::~FileSink()
{
	if (Handle) fclose(Handle);
}

bool FileSink::Open(const char* filename, const char* mode)
{
	DAEDALUS_ASSERT(Handle == NULL, "Alreadh have an open file");
	Handle = fopen(filename, mode);
	return Handle != NULL;
}

size_t FileSink::Write(const void* p, size_t len)
{
	if (Handle) return fwrite(p, 1, len, Handle);

	return 0;
}

void FileSink::Flush()
{
	if (Handle) fflush(Handle);
}

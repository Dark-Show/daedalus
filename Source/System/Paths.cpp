#include "Base/Daedalus.h"
#include "Paths.h"

#include "absl/strings/str_cat.h"

#include "System/IO.h"

static std::string gDaedalusExePath;

constexpr char kRunfilesDir[] = "/daedalus.runfiles/daedalus/";

void SetExeFilename(const std::string& filename)
{
	std::string path = filename;
	IO::Path::RemoveFileSpec(&path);
	gDaedalusExePath = path;
}

std::string GetRunfilePath(absl::string_view filename)
{
	return IO::Path::Join(gDaedalusExePath, kRunfilesDir, filename);
}

bool LoadRunfile(absl::string_view filename, std::string* out)
{
	std::string fullpath = GetRunfilePath(filename);
	FILE * fh = fopen(fullpath.c_str(), "r");
	if (!fh)
	{
		return false;
	}

	fseek(fh, 0, SEEK_END);
	size_t len = ftell(fh);
	fseek(fh, 0, SEEK_SET);
	char * p = (char *)malloc(len+1);
	fread(p, len, 1, fh);
	p[len] = 0;
	fclose(fh);
	*out = p;
	free(p);
	return true;
}

std::string GetDataFilename(absl::string_view filename)
{
	return IO::Path::Join(gDaedalusExePath, filename);
}

std::string GetOutputFilename(absl::string_view filename)
{
	return IO::Path::Join(gDaedalusExePath, filename);
}

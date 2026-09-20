
#include "editor.h"
#include <Path.h>
#include <Entry.h>
#include "AutoSaver.fdh"

static int LastAutoSave = 0;
static int Timer = 0;
static bool UpToDate = true;

/*
	Debugging feature to help protect against possible editor crashes
	while working on editor using the editor.

	The scheme goes like this:

	If the document has been changed since the last autosave,
	and we have not autosaved for at least 30 seconds,
	and user stops typing for at least 1 second,

	come up with a filename made up of the current filename's filespec
	plus a number which makes it a unique filename.

	then save the document to /tmp/ under that file name, but don't change
	the name of the current document.
*/

void AutoSaver_StartTimer()
{
	Timer = 0;
	UpToDate = false;
}

void AutoSaver_Tick()
{
	if (!editor.settings.EnableAutoSaver)
	{
		Timer = 0;
		return;
	}

	// if document modified since last autosave...
	if (UpToDate) return;

	++Timer;

	// haven't auto-saved for at least 60 seconds...
	if (LastAutoSave < 600)
	{
		LastAutoSave++;
	}
	else
	{
		// user has stopped typing for at least 1 second...
		if (Timer >= 10)
		{
			// autosave!
			AutoSaver_Fire();
		}
	}

	//stat("%d : %d", LastAutoSave, Timer);
}

// <dir>name_<number>.ext
static void AutoSaveName(BString *path, const BString &dir, const char *name, \
						const char *ext, int number)
{
	*path = dir;
	*path << name << "_" << (int32)number;
	if (ext)
		*path << "." << ext;
}

static void AutoSaver_Fire()
{
char *filespec, *ext;
const char *autosv_filename;

	if (!editor.curev) return;	// just in case

	// get name of current document, minus the path
	filespec = smal_strdup(GetFileSpec(editor.curev->filename));

	// seperate extension from filename
	ext = strrchr(filespec, '.');
	if (ext)
	{
		*ext = 0;
		ext++;
	}

	// get path where autosaved files are stored
	char dir[MAXPATHLEN];
	find_directory(B_SYSTEM_TEMP_DIRECTORY, 0, true, dir, sizeof(dir) - 20);
	AddSuffixIfMissing(dir, '/');
	strcat(dir, "Sisong/");

	BString basepath(dir);
	BString path;
	mkdir(basepath.String(), 0755);

	// One of ten names per document: the first that is free, else the one
	// written longest ago. (It took a new name every time and deleted
	// nothing: a full copy of the document for every minute of editing,
	// and every earlier name opened to see if it existed.)
	const int kCopies = 10;
	int chosen = 0;
	time_t oldest = 0;

	for(int number=0;number<kCopies;number++)
	{
		AutoSaveName(&path, basepath, filespec, ext, number);

		BEntry entry(path.String());
		if (!entry.Exists())
		{
			chosen = number;
			break;
		}

		time_t modified = 0;
		entry.GetModificationTime(&modified);
		if (number == 0 || modified < oldest)
		{
			oldest = modified;
			chosen = number;
		}
	}

	AutoSaveName(&path, basepath, filespec, ext, chosen);
	autosv_filename = path.String();

	stat("autosave: %s", autosv_filename);
	editor.curev->Save(autosv_filename);

	frees(filespec);

	LastAutoSave = 0;
	Timer = 0;
	UpToDate = true;
}



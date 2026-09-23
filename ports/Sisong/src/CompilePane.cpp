
#include "editor.h"
#include <unistd.h>
#include <sys/param.h>
#include <sys/wait.h>
#include <signal.h>
#include "CompilePane.lh"
#include "CompilePane.fdh"

// priority of 8 is higher than B_LOW_PRIORITY (5), but won't compete with
// apps running at B_NORMAL_PRIORITY (10).
#define COMPILE_THREAD_PRIORITY		8

// The pane's ground is the editor's, and its inks are chosen for that ground:
// one set for a light ground, one for a dark one. (It was light inks on black
// whatever the editor looked like.) Every ink is at least 4.5:1 against the
// port's Paper and Midnight Blue grounds.
static const rgb_color inks_on_light[CompilePane::NUM_INKS] =
{
	{ 0x3a, 0x39, 0x34, 255 },	// INK_TEXT
	{ 0xb3, 0x26, 0x1e, 255 },	// INK_ERROR
	{ 0x8a, 0x56, 0x00, 255 },	// INK_WARNING
	{ 0x1f, 0x6b, 0x2a, 255 },	// INK_EXEC
	{ 0x0b, 0x6e, 0x66, 255 },	// INK_SCRIPTNAME
	{ 0x8c, 0x3b, 0x2e, 255 },	// INK_HEADER
	{ 0x1b, 0x4f, 0x9c, 255 }	// INK_TITLE
};

static const rgb_color inks_on_dark[CompilePane::NUM_INKS] =
{
	{ 0xc8, 0xd0, 0xda, 255 },	// INK_TEXT
	{ 0xff, 0x7b, 0x72, 255 },	// INK_ERROR
	{ 0xff, 0xc8, 0x5a, 255 },	// INK_WARNING
	{ 0xbe, 0xff, 0xbe, 255 },	// INK_EXEC
	{ 0x64, 0xff, 0x64, 255 },	// INK_SCRIPTNAME
	{ 0xff, 0x9e, 0x80, 255 },	// INK_HEADER
	{ 0xff, 0xd7, 0x5e, 255 }	// INK_TITLE
};

rgb_color CompilePane::Ink(int which)
{
	if (which < 0 || which >= NUM_INKS) which = INK_TEXT;
	return GetEditBGColor(COLOR_TEXT).IsDark() ? inks_on_dark[which] : inks_on_light[which];
}

#define color_bg			(GetEditBGColor(COLOR_TEXT))
#define color_selection		(GetEditColor(COLOR_SELECTION))
#define color_text			(CompilePane::Ink(CompilePane::INK_TEXT))
#define color_error			(CompilePane::Ink(CompilePane::INK_ERROR))
#define color_warning		(CompilePane::Ink(CompilePane::INK_WARNING))
#define color_exec			(CompilePane::Ink(CompilePane::INK_EXEC))
#define color_scriptname	(CompilePane::Ink(CompilePane::INK_SCRIPTNAME))
#define color_header		(CompilePane::Ink(CompilePane::INK_HEADER))

// The editor's colours changed: what the pane already shows changes with
// them. A line keeps its meaning -- an ink of one set becomes the same ink
// of the other -- and takes the new ground and selection colour.
void CompilePane::ColorsChanged()
{
	if (!ListView) return;
	bool locked = LockLooper();

	rgb_color ground = color_bg;
	rgb_color selection = color_selection;

	ListView->SetViewColor(ground);
	ListView->SetLowColor(ground);

	for(int32 i=0;i<ListView->CountItems();i++)
	{
		ColoredStringItem *item = (ColoredStringItem *)ListView->ItemAt(i);
		if (!item) continue;

		rgb_color fg = item->Color();
		for(int ink=0;ink<NUM_INKS;ink++)
		{
			if (fg == inks_on_light[ink] || fg == inks_on_dark[ink])
			{
				item->SetColor(Ink(ink));
				break;
			}
		}

		// a line that cannot be clicked has its ground as its selection colour
		bool selectable = (item->SelectionColor() != item->BackgroundColor());
		item->SetBackgroundColor(ground);
		item->SetSelectionColor(selectable ? selection : ground);
	}

	ListView->Invalidate();
	if (locked) UnlockLooper();
}


CompilePane::CompilePane()
{
	// create list
	BRect lvrect(Bounds());
	lvrect.InsetBy(2, 2);
	lvrect.right -= B_V_SCROLL_BAR_WIDTH;

	ListView = new BListView(lvrect, "compilelist", B_SINGLE_SELECTION_LIST, B_FOLLOW_ALL);
	ListView->SetViewColor(color_bg);

	BFont font;
	GetFixedFont(&font);
	font.SetSize(editor.settings.font_size);
	ListView->SetFont(&font);

	ListView->SetSelectionMessage(new BMessage(M_COMPILEPANE_INVOKE));

	// create scrollview
	ScrollView = new BScrollView("ScrollView", ListView, \
							B_FOLLOW_ALL, 0, false, true);
	AddChild(ScrollView);

	ListSemaphore = create_sem(1, "list_sem");
	CompileThread = -1;

	fScriptName[0] = 0;

	// get temp file used when running scripts (see RunScriptLine)
	find_directory(B_SYSTEM_TEMP_DIRECTORY, 0, true, fTempScriptFile, 
sizeof(fTempScriptFile) - 32);
	AddSuffixIfMissing(fTempScriptFile, '/');

	maxcpy(fSingleFileScript, fTempScriptFile, sizeof(fSingleFileScript));
	strcat(fTempScriptFile, "sicompile");
	strcat(fSingleFileScript, "sisinglefile");

	fPendingScript[0] = 0;
	fScriptLabel[0] = 0;
	fSingleFile = false;
}

CompilePane::~CompilePane()
{
	Clear();
}

/*
void c------------------------------() {}
*/

void CompilePane::PopupClosing()
{
	// ensure compile thread isn't running
	AbortThread();
}

void CompilePane::ChangeFontSize(int new_point_size)
{
	BFont font;
	GetFixedFont(&font);
	font.SetSize(new_point_size);
	ListView->SetFont(&font);
	ListView->Invalidate();
}

/*
void c------------------------------() {}
*/

void CompilePane::AddLine(const char *text, rgb_color lineColor, bool selectable)
{
BScrollBar *sbar;
float smax, oldsmax, oldval;

	if (acquire_sem(ListSemaphore) != B_NO_ERROR)
		return;

	bool locked = LockLooper();

	// get scrollbar range and value before the add; this prevents jerking
	// scroll back down if the user is trying to look at the history.
	sbar = ScrollView->ScrollBar(B_VERTICAL);
	sbar->GetRange(NULL, &oldsmax);
	oldval = sbar->Value();

	// if there is a spacer item already, replace it with the new line,
	// else add this line as the first one.
	ColoredStringItem *item = (ColoredStringItem *)ListView->LastItem();
	if (item)
	{
		item->SetText(text);
		item->SetColor(lineColor);
		if (selectable) item->SetSelectionColor(color_selection);
		ListView->InvalidateItem(ListView->CountItems() - 1);
	}
	else
	{
		ListView->AddItem(new ColoredStringItem(text, lineColor, color_bg, color_selection));
	}

	// add a spacer item so that it looks nicer when scrolling (leave a blank line)
	// would prefer to use AddList to add both items at once, but the implementation
	// in BListView is lazy and causes flicker
	ListView->AddItem(new ColoredStringItem("", color_text, color_bg, color_bg));

	// move the scroll bar so that the latest line is always visible
	if (oldval == oldsmax)
	{
		sbar->GetRange(NULL, &smax);
		if (smax) sbar->SetValue(smax);
	}

	if (locked)
	{
		Sync();
		UnlockLooper();
	}

	fLineCount++;
	release_sem_etc(ListSemaphore, 1, B_DO_NOT_RESCHEDULE);
}

void CompilePane::Clear()
{
ColoredStringItem *item;

	int i = 0;
	while((item = (ColoredStringItem *)ListView->ItemAt(i++)))
		delete item;

	ListView->MakeEmpty();

	fAutoScrollLine = -1;
	fAutoScrollLineType = -1;
	fAutoJumpLine = -1;
	fAutoJumpLineType = -1;

	fHasErrors = false;
	fShowingProblems = false;
	fLineCount = 0;
}

/*
void c------------------------------() {}
*/

void CompilePane::SetScriptName(const char *fname)
{
	if (fname)
		maxcpy(fScriptName, fname, sizeof(fScriptName));
	else
		fScriptName[0] = 0;
}

// starts the compile thread running the given script.
// if run_result is false, lines beginning with '%' are skipped.
void CompilePane::RunScript(bool runWhenDone)
{
	if (!fScriptName[0])
	{
		if (CompileThread != -1)
			AbortThread();

		Clear();
		MainWindow->popup.pane->Open(runWhenDone ? "Build" : "Build (no run)", this);

		AddLine("unable to build:", color_warning, false);
		AddLine("  No build script is set...please open a project first", color_error, false);
		return;
	}

	fSingleFile = false;
	StartScript(fScriptName, fScriptName, \
				runWhenDone ? "Build" : "Build (no run)", runWhenDone);
}

// Runs a script in the compile thread: the project's, or the one CompileFile()
// writes for a single source file. "label" is what the pane calls it.
void CompilePane::StartScript(const char *scriptPath, const char *label, \
							const char *title, bool runWhenDone)
{
	if (CompileThread != -1)
		AbortThread();

	Clear();
	MainWindow->popup.pane->Open(title, this);
	fRunResult = runWhenDone;

	maxcpy(fPendingScript, scriptPath, sizeof(fPendingScript));
	maxcpy(fScriptLabel, label, sizeof(fScriptLabel));

	thread.quit_ack = create_sem(0, "quit_ack");
	thread.please_quit = false;

	CompileThread = spawn_thread(ScriptRunnerThread, "Compile Thread", COMPILE_THREAD_PRIORITY, this);
	resume_thread(CompileThread);
}

#define kDefaultBuildCommand	"%c -g -Wall -o %e %f -lbe"

// what this build command said before Prose had a compiler of its own: a
// setting still holding it was never chosen, it was only the default of the
// day, and is replaced by the one above
#define kOldBuildCommand		"g++ -g -Wall -o %e %f -lbe"

// The makefile in a folder, if there is one. A file that comes with one is
// built by it: the single-file command below knows nothing of what a source
// needs beyond libbe, so an example whose makefile says "-lgame" failed to
// link with "undefined symbol: BGamePane::..." while the library sat on the
// machine all along. The makefile is the file's own answer to that question.
static bool FolderMakefile(const char *folder)
{
	static const char *names[] = { "Makefile", "makefile", "GNUmakefile", NULL };

	for(int i=0;names[i];i++)
	{
		BPath candidate(folder, names[i]);
		if (candidate.InitCheck() == B_OK && access(candidate.Path(), R_OK) == 0)
			return true;
	}

	return false;
}

// the compiler for a file, by its name: C++ unless it is plainly C
static const char *CompilerFor(const char *path)
{
	const char *dot = strrchr(path, '.');

	if (dot && (!strcmp(dot, ".c") || !strcmp(dot, ".h")))
		return "clang";

	return "clang++";
}

// "prog" as the shell would find it: an absolute or relative name as it is,
// a plain name through PATH
static bool CommandExists(const char *prog)
{
	if (!prog || !prog[0]) return false;

	if (strchr(prog, '/'))
		return (access(prog, X_OK) == 0);

	const char *path = getenv("PATH");
	if (!path || !path[0])
		path = "/boot/system/bin:/boot/system/non-packaged/bin";

	BString dirs(path);
	int32 start = 0;

	while(start <= dirs.Length())
	{
		int32 colon = dirs.FindFirst(':', start);
		if (colon < 0) colon = dirs.Length();

		BString candidate;
		dirs.CopyInto(candidate, start, colon - start);

		if (candidate.Length() > 0)
		{
			candidate << "/" << prog;
			if (access(candidate.String(), X_OK) == 0)
				return true;
		}

		start = colon + 1;
	}

	return false;
}

// the command's first word
static BString FirstWord(const char *command)
{
BString word;

	while(*command == ' ' || *command == '\t') command++;
	while(*command && *command != ' ' && *command != '\t')
		word << *(command++);

	return word;
}

// The build command, with the file's names in it:
//   %f  the source file        %e  the executable to make
//   %d  the folder it is in    %c  the compiler for this file
//   %%  a percent sign
// Each path is quoted, so a folder with spaces in its name works.
static BString ExpandBuildCommand(const char *tmpl, const char *source, \
								const char *exe, const char *dir)
{
BString out;

	for(const char *p = tmpl; *p; p++)
	{
		if (*p != '%')
		{
			out << *p;
			continue;
		}

		switch(*(++p))
		{
			case 'f': out << '"' << source << '"'; break;
			case 'e': out << '"' << exe << '"'; break;
			case 'd': out << '"' << dir << '"'; break;
			case 'c': out << CompilerFor(source); break;
			case '%': out << '%'; break;
			case 0: return out;		// a trailing % is nothing
			default: out << '%' << *p; break;
		}
	}

	return out;
}

// Compile and link one source file, with no project around it: the file's own
// folder, an executable beside it named after it, and the command from the
// setting "SingleFileBuildCommand". With runAfter, the executable is run when
// it has been made (the script's "%" prefix, as for a project's build script).
void CompilePane::CompileFile(const char *sourcePath, bool runAfter)
{
BPath source(sourcePath);
BPath folder;

	if (source.InitCheck() != B_OK || source.GetParent(&folder) != B_OK)
	{
		StartScript("", sourcePath, "Compile", false);
		return;
	}

	// the executable: the file's name without its last extension, beside it
	BString stem(source.Leaf());
	int32 dot = stem.FindLast('.');
	if (dot > 0) stem.Truncate(dot);

	if (stem.Length() == 0 || stem == source.Leaf())
		stem << ".out";		// a file with no extension: don't overwrite it

	BString exe(folder.Path());
	exe << "/" << stem;

	// the command, kept in the settings file so it can be changed there
	const char *stored = settings->GetString("SingleFileBuildCommand", NULL);
	if (!stored || !strcmp(stored, kOldBuildCommand))
		settings->SetString("SingleFileBuildCommand", kDefaultBuildCommand);

	const char *tmpl = settings->GetString("SingleFileBuildCommand", kDefaultBuildCommand);

	// A folder with a makefile in it is built by the makefile: it is the only
	// thing that knows which libraries this source wants. The executable is
	// still guessed from the file's name, which is the convention the examples
	// follow; a makefile that makes something else will build, and the run
	// step will say it cannot find what it was told to run.
	const bool useMake = FolderMakefile(folder.Path());
	BString command;

	if (useMake)
		command = "make";
	else
		command = ExpandBuildCommand(tmpl, source.Path(), exe.String(), folder.Path());

	const char *title = runAfter ? "Compile and Run" : "Compile";

	// a machine with no compiler on it: say so, rather than let the shell
	// answer "command not found" for something the user never typed
	BString prog = FirstWord(command.String());
	if (!CommandExists(prog.String()))
	{
		if (CompileThread != -1)
			AbortThread();

		Clear();
		MainWindow->popup.pane->Open(title, this);

		BString str;
		str << "cannot compile: there is no \"" << prog << "\" on this machine.";
		AddLine(str.String(), color_error, false);
		AddLine("", color_text, false);

		// make is what we chose, not what the settings asked for, so the
		// advice below about compilers and the build command does not apply
		if (useMake)
		{
			AddLine("  This folder has a makefile, so it is built with \"make\".", color_text, false);
			AddLine("  \"pkgman install make\" puts it on the machine.", color_text, false);
			return;
		}
		// Prose ships clang and lld, so this is nearly always a command from
		// the settings file naming something else: say what is on the machine
		// rather than guess at why.
		static const char *known[] = { "clang", "clang++", "gcc", "g++", "cc", NULL };
		BString available;

		for(int i=0;known[i];i++)
		{
			if (!CommandExists(known[i])) continue;
			if (available.Length() > 0) available << ", ";
			available << known[i];
		}

		if (available.Length() > 0)
		{
			str = "  compilers on this machine: ";
			str << available;
			AddLine(str.String(), color_text, false);
			AddLine("  %c in the command stands for the right one: clang++ for C++, clang for C.", color_text, false);
		}
		else
		{
			AddLine("  no compiler was found at all, which is not how Prose ships:", color_text, false);
			AddLine("  \"pkgman install clang\" puts one back.", color_text, false);
		}

		AddLine("", color_text, false);
		AddLine("  The command is \"SingleFileBuildCommand\" in the settings file", color_text, false);
		AddLine("  ~/config/settings/Sisong/settings, where %f is the source file,", color_text, false);
		AddLine("  %e the executable to make, %d the folder it is in, %c the compiler.", color_text, false);
		AddLine("", color_text, false);

		str = "  now: ";
		str << tmpl;
		AddLine(str.String(), color_warning, false);
		return;
	}

	// the script: the file's folder, the command, and the executable itself
	// on a line the runner skips unless it was asked to run it
	FILE *fp = fopen(fSingleFileScript, "wt");
	if (!fp)
	{
		if (CompileThread != -1)
			AbortThread();

		Clear();
		MainWindow->popup.pane->Open(title, this);

		BString str("cannot compile: ");
		str << fSingleFileScript << " could not be written.";
		AddLine(str.String(), color_error, false);
		return;
	}

	// "cd" is the script runner's own, not the shell's: it takes the rest of
	// the line as the path, quotes and all, so the path goes in unquoted
	// (which also means a folder whose name has spaces in it works)
	fprintf(fp, "cd %s\n", folder.Path());
	fprintf(fp, "%s\n", command.String());
	fprintf(fp, "%%\"%s\"\n", exe.String());
	fclose(fp);

	fSingleFile = true;
	StartScript(fSingleFileScript, source.Path(), title, runAfter);
}

// The thread's id is kept after it ends, until AbortThread() reaps it; what
// says it is running is quit_ack, which it releases as the last thing it does.
bool CompilePane::IsBusy()
{
	if (CompileThread == -1) return false;

	int32 count = 0;
	if (get_sem_count(thread.quit_ack, &count) != B_OK) return false;
	return count == 0;
}

// if the compile thread is currently running, sees to it that it is stopped
void CompilePane::AbortThread()
{
status_t ok;
int lockcount = 0;

	if (CompileThread == -1)
		return;

	// if we are holding a lock on the looper, temporarily release it, so
	// the thread can use AddLine to report the termination.
	if (Window()->LockingThread() == find_thread(NULL))
	{
		// this is safe because we just proved it's us that holds the lock
		lockcount = Window()->CountLocks();
		for(int i=0;i<lockcount;i++)
			UnlockLooper();
	}

	// try to exit gracefully.
	//
	// The thread releases quit_ack once, as the last thing it does, whether
	// it was asked to stop or ran to its end; only this side deletes the
	// semaphore and forgets the thread. (It used to be the thread that did
	// both when it finished by itself. If it looked at please_quit just before
	// we set it, it deleted the semaphore under our wait, the wait failed at
	// once, and we killed a thread that was still running -- inside malloc,
	// with luck not. The script command "hide" made the two meet.)
	stat("aborting compile thread");
	thread.please_quit = true;
	ok = acquire_sem_etc(thread.quit_ack, 1, B_RELATIVE_TIMEOUT, 2000 * 1000);

	if (ok == B_NO_ERROR)
	{	// it has said it is done: let it get out of its function
		status_t result;
		wait_for_thread(CompileThread, &result);
	}
	else
	{	// time to get the mallet: it is stuck somewhere
		stat("compile thread still hasn't quit, getting the mallet");
		kill_thread(CompileThread);
	}

	// lock looper back the way we found it
	for(int i=0;i<lockcount;i++)
		LockLooper();

	delete_sem(thread.quit_ack);
	CompileThread = -1;
	thread.quit_ack = -1;
}

status_t ScriptRunnerThread(void *data)
{
CompilePane *pane = (CompilePane *)data;
char fname[MAXPATHLEN];

	maxcpy(fname, pane->fPendingScript, sizeof(fname) - 1);
	pane->RunScriptInternal(fname);
	return B_OK;
}

/*
void c------------------------------() {}
*/

// this is called from the compile thread, don't call it directly
int CompilePane::RunScriptInternal(char *scriptname)
{
FILE *fp;
char *line = NULL;
size_t linesize = 0;
ssize_t len;
char olddir[MAXPATHLEN];
int exitcode = 0;
bool scriptEmpty = true;

	BString fn(fScriptLabel[0] ? fScriptLabel : scriptname);
	fn.Prepend("-> ");
	AddLine(fn.String(), color_scriptname, false);

	fp = fopen(scriptname, "rb");
	if (!fp)
	{
		BString str = "unable to open script ";
		str.Append(scriptname);

		AddLine(str.String(), color_error, false);

		release_sem(thread.quit_ack);	// see AbortThread()
		return -1;
	}

	// save current working directory in case script changes it
	getcwd(olddir, sizeof(olddir));

	// Whole lines, however long: read into MAXPATHLEN + 10 bytes, a longer
	// line -- the link line of a project of many files -- came in as two, and
	// its tail was run as a command of its own.
	while((len = getline(&line, &linesize, fp)) >= 0)
	{
		while(len > 0 && (line[len - 1] == 13 || line[len - 1] == 10))
			line[--len] = 0;

		if (!*line) continue;
		if (line[0] == '#') continue;
		if (line[0] == '/' && line[1] == '/') continue;

		scriptEmpty = false;

		if (line[0] == '!')	// "ignore exit value"
		{
			RunScriptLine(&line[1]);
		}
		else if (line[0] == '%')	// "skip line in 'Build but Don't Run' mode
		{
			if (fRunResult)
			{
				exitcode = RunScriptLine(&line[1]);
				if (exitcode != 0) break;
			}
		}
		else
		{
			exitcode = RunScriptLine(line);
			if (exitcode != 0) break;
		}
	}

	chdir(olddir);
	fclose(fp);
	free(line);

	if (scriptEmpty)
	{
		AddLine("  build script is empty!", color_warning, false);
		AddLine("  you can configure it from the Projects menu.", color_warning, false);
	}

	//stat("C thread going bye bye, please_quit=%d", thread.please_quit);
	if (!thread.please_quit)
	{
		if (fHasErrors)
		{
			if (editor.settings.build.JumpToErrors && \
				MainWindow->popup.pane->IsOpen())
			{
				// scroll pane up so first error is visible
				int y = (fAutoScrollLine - 1);
				if (y < 0) y = 0;

				if (LockLooper())
				{
					BListItem *item = ListView->ItemAt(0);
					y *= (int)item->Height();

					ScrollView->ScrollBar(B_VERTICAL)->SetValue(y);
					UnlockLooper();
				}

				// now simulate a click on the first clickable line--take them right to it
				if (fAutoJumpLine != -1)
					ListView->Select(fAutoJumpLine);
			}
		}
		else if (exitcode == 0 && !scriptEmpty && !fSingleFile)
		{
			if (!MainWindow->top.menubar->ShowConsoleItem->IsMarked())
				MainWindow->PostMessage(M_POPUPPANE_CLOSE);
		}
	}

	// done, asked to or not: the last thing this thread does (AbortThread())
	release_sem(thread.quit_ack);
	return exitcode;
}

// interpret and execute a single line from a script, and return the exit/status code.
// nonzero return status causes the script to abort.
int CompilePane::RunScriptLine(const char *line)
{
	AddLine(line, color_exec, false);

	if (strbegin(line, "cd "))
	{
		if (chdir(line + 3))
		{
			BString error = "cd failed to directory ";
			error.Append(line + 3);
			AddLine(error.String(), color_error, false);
			return 1;
		}

		return 0;
	}
	else if (!strcmp(line, "hide"))
	{
		if (!fHasErrors)
			if (!MainWindow->top.menubar->ShowConsoleItem->IsMarked())
				MainWindow->PostMessage(M_POPUPPANE_CLOSE);

		return 1;
	}
	else if (!strcmp(line, "show"))
	{
		return 1;
	}

	// else, no commands known, try to exec

	// to support things such as "rm *.o", we can create a 1-liner script,
	// then execute it with "bash".
	//
	// this is real messy
	FILE *fp = fopen(fTempScriptFile, "wt");
	if (!fp)
	{
		BString str = "cannot write ";
		str.Append(fTempScriptFile);
		AddLine(str.String(), color_error, false);
		return -1;
	}

	fprintf(fp, "%s\n", line);
	fprintf(fp, "exit $?\n");
	fclose(fp);

	char *array[5];
	char *program;

	array[0] = "bash";
	array[1] = fTempScriptFile;
	array[2] = NULL;

	char *ptr = strchr(line, ' ');
	if (!ptr)
	{
		program = smal_strdup(line);
	}
	else
	{
		int len = (ptr - line);
		program = (char *)smal(len + 1);
		memcpy(program, line, len);
		program[len] = 0;
	}

	int exitcode = this->Exec(program, array);

	frees(program);
	remove(fTempScriptFile);

	return exitcode;
}

/*
void c------------------------------() {}
*/


// executes a program and pipes it's output to the compile pane.
// args is an array of command-line arguments and should be null terminated.
int CompilePane::Exec(const char *program, char *args[])
{
rfContext rStdout;
rfContext rStderr;
int pfd[2];
int epfd[2];
int status;
pid_t child;
int exitstat;

#define EXEC_FAILED_CODE	179	// just a magick

	// create pipes for stdout and stderr
	pipe(pfd);
	pipe(epfd);

	// increase priority for a sec--we don't want child to inherit our low priority
	set_thread_priority(find_thread(0), B_NORMAL_PRIORITY);

	// fork(), not vfork(): here vfork() is the bare system call, without the
	// heap's fork hooks, and this program has other threads. If one of them
	// held a malloc lock at that instant, the child -- whose execv() allocates
	// -- waited for it for ever, holding the pipes open.
	child = fork();
	if (child < 0)
	{
		close(pfd[0]); close(pfd[1]);
		close(epfd[0]); close(epfd[1]);
		set_thread_priority(find_thread(0), COMPILE_THREAD_PRIORITY);
		AddLine("** cannot start a process (fork failed)", color_error, false);
		return -1;
	}

	if (child == 0)
	{	// this is the child
		// redirect stdout/stderr
		close(STDOUT_FILENO);
		dup(pfd[1]);
		close(pfd[0]);

		close(STDERR_FILENO);
		dup(epfd[1]);
		close(epfd[0]);

		// this makes spawned child a session group leader so if we change
		// our mind, we can kill it and all of it's children at once.
		setpgid(0, 0);

		// execute the program
		execv("/bin/bash", args);

		// guess exec must have failed, cause we're still here
		if (!file_exists("/bin/bash"))
			fprintf(stderr, "** cannot find /bin/bash");

		// this message is received by the code below, as if we were the child app.
		fprintf(stderr, "** failed exec of '%s'\n", program);
		fflush(stderr);
		// _exit: we are a fork of a program with many threads and live
		// windows; exit() would run its atexit handlers and destructors here
		_exit(EXEC_FAILED_CODE);
	}

	set_thread_priority(find_thread(0), COMPILE_THREAD_PRIORITY);
	close(pfd[1]);
	close(epfd[1]);

	InitRFContext(&rStdout, pfd[0]);
	InitRFContext(&rStderr, epfd[0]);

	int counter = 0;
	while(!thread.please_quit)
	{
		status = RunRFContext(this, &rStdout, false);
		status += RunRFContext(this, &rStderr, true);
		if (status == 2) break;	// both pipes are empty & closed

		if (++counter >= 1500)
		{
			snooze(10 * 1000);
			counter = 0;
		}
	}

	CloseRFContext(&rStdout);
	CloseRFContext(&rStderr);

	//stat("outta here, thread.please_quit = %d", thread.please_quit);

	if (thread.please_quit)
	{	// compile thread is aborting, slaughter our child process
		char str[80];
		sprintf(str, "compile thread aborting: killing child PID %d", (int)child);
		AddLine(str, color_warning, false);

		kill(-child, SIGKILL);		// its process group, and
		kill(child, SIGKILL);		// itself, should it not have made the group yet
		waitpid(child, &exitstat, 0);	// it was left a zombie
		exitstat = -1;
	}
	else
	{
		// obtain exit code
		waitpid(child, &exitstat, 0);
		exitstat = WEXITSTATUS(exitstat);

		if (exitstat != EXEC_FAILED_CODE)
		{
			char str[MAXPATHLEN + 80];
			sprintf(str, "<< %s: exit code %d", program, exitstat);
			AddLine(str, color_exec, false);
		}
	}

	return exitstat;
}


static void InitRFContext(rfContext *rf, int fd)
{
	fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
	rf->fd = fd;

	rf->max_line_len = CP_MAX_LINE_LEN;
	rf->line = (char *)smal(rf->max_line_len + 1);
	rf->line_len = 0;
	rf->line[0] = 0;
}

static void CloseRFContext(rfContext *rf)
{
	close(rf->fd);
	frees(rf->line);
}

// parses a line, and attempts to detect if it is a compiler error message,
// and if so, extracts information on the error into "info".
void ParseLineInfo(const char *line, stLineInfo *info)
{
char *ptr;
int length;

	//stat("-- line: '%s'", line);

	memset(info, 0, sizeof(stLineInfo));
	info->errorType = ET_NONE;
	info->lineNo = -1;

	// check if it is a compiler error message
	if (line[0] != '/') return;
	ptr = strchr(line, ':');
	if (!ptr) return;

	// get filename
	length = (ptr - line);
	if (length >= CP_MAX_LINE_LEN-1) length = CP_MAX_LINE_LEN-2;
	memcpy(info->filename, line, length);
	info->filename[length] = 0;

	// try to get line no
	ptr++;
	if (*ptr >= '1' && *ptr <= '9')
	{
		info->lineNo = atoi(ptr) - 1;
		info->errorType = ET_ERROR;
	}
	else
	{
		info->errorType = ET_HEADER;
	}

	// check for special stuff
	if (strstr(ptr, "warning: "))
		info->errorType = ET_WARNING;
	else
	{
		if (strstr(ptr, "(Each undeclared identifier is reported only once") || \
			strstr(ptr, "for each function it appears in.)"))
		{
			info->errorType = ET_INFO;
		}
	}
}

static int RunRFContext(CompilePane *pane, rfContext *rf, bool from_stderr)
{
char ch;

	int status = read(rf->fd, &ch, 1);

	if (status < 0) return 0;	// no data avail
	if (status == 0) return 1;	// EOF

	if (ch == '\n')
	{
		rf->line[rf->line_len] = 0;
		rf->line_len = 0;

		if (from_stderr)
		{
			stLineInfo info;
			ParseLineInfo(rf->line, &info);

			// handle auto-jumping in case of errors
			if (info.errorType != ET_NONE)
			{
				pane->fHasErrors = true;

				bool error_precedence = (editor.settings.build.NoJumpToWarning && \
										 pane->fAutoScrollLineType != ET_ERROR && \
										 info.errorType == ET_ERROR);

				// will scroll the pane up to the first line which is not "normal".
				// errors take precedence over warnings in NoJumpToWarning mode.
				if (pane->fAutoScrollLine == -1 || error_precedence)
				{
					pane->fAutoScrollLine = pane->fLineCount;
					pane->fAutoScrollLineType = info.errorType;
				}

				// will jump to the first non-normal line for which a line number is available.
				// again, errors take precedence over warnings in NoJumpToWarning mode.
				if (info.lineNo != -1)
				{
					bool error_precedence = (editor.settings.build.NoJumpToWarning && \
											 pane->fAutoJumpLineType != ET_ERROR && \
											 info.errorType == ET_ERROR);

					if (pane->fAutoJumpLine == -1 || error_precedence)
						pane->fAutoJumpLine = pane->fLineCount;

					if (pane->fAutoJumpLine == pane->fLineCount - 1)
					{
						if (strstr(rf->line, "at this point in file") ||
							strstr(rf->line, "within this context"))
						{
							pane->fAutoJumpLine = pane->fLineCount;
							pane->fAutoJumpLineType = info.errorType;
						}
					}
				}
			}

			switch(info.errorType)
			{
				case ET_INFO:
				break;

				case ET_ERROR:
					pane->AddLine(rf->line, color_error, (info.lineNo != -1));
				break;

				case ET_WARNING:
					pane->AddLine(rf->line, color_warning, (info.lineNo != -1));
				break;

				case ET_HEADER:
					pane->AddLine(rf->line, color_header, false);
				break;

				default:
					pane->AddLine(rf->line, color_error, false);
				break;
			}
		}
		else
		{
			pane->AddLine(rf->line, color_text, false);
		}
	}
	else if (ch == TAB)
	{
		for(int i=0;i<4;i++)
		{
			if (rf->line_len >= rf->max_line_len)
				break;

			rf->line[rf->line_len++] = ' ';
		}
	}
	else if (ch != '\r')
	{
		if (rf->line_len < rf->max_line_len)
			rf->line[rf->line_len++] = ch;
	}

	return 0;
}

void CompilePane::ItemClicked(int index)
{
ColoredStringItem *item = (ColoredStringItem *)ListView->ItemAt(index);
if (!item) return;
const char *line = item->Text();
if (!line) return;

	stLineInfo info;
	ParseLineInfo(line, &info);

	if (info.lineNo != -1 && file_exists(info.filename))
	{
		DoFileOpenAtLine(info.filename, info.lineNo, -1, -1);
	}
	else
	{
		ListView->DeselectAll();
	}
}




#include "editor.h"
#include "lsp_client.h"
#include "lsp_protocol.h"

#include <Messenger.h>
#include <Roster.h>
#include <String.h>

// the whole state of the conversation: who to talk to, the token the
// server answered with, and which documents it has been told about
static BMessenger *lsp_server = NULL;
static int32 lsp_token = 0;
static bool lsp_session_asked = false;	// a session is asked for, not yet answered
static BString lsp_waiting_doc;		// the document whose question waits for it
static bool lsp_waiting_is_sync = false;	// ...and whether it is only its text
static bool lsp_dead = false;			// the server was not there, stop asking
static bigtime_t lsp_last_try = 0;		// ...but look again every minute

// the document being edited since the server last had its text, and when:
// a second's pause in the editing sends it again (lsp_tick)
static BString lsp_edited_doc;
static bigtime_t lsp_edited_at = 0;
#define LSP_EDIT_PAUSE		1000000

// the document whose errors the Build pane lists, if it lists any
static BString lsp_problems_doc;

// What is known of each C or C++ document the server has been told about:
// the text it has (as a hash and a length -- the same text is not sent
// twice) and what clangd last said of it. clangd counts lines and columns
// from 0, columns in bytes; severities are 1 error, 2 warning,
// 3 information and 4 hint.
struct lsp_doc
{
	char *name;
	int32 sent_len;			// -1: never sent, the next send is its didOpen
	uint32 sent_hash;
	bool awaiting;			// text sent, clangd's word on it not back yet
	bool show_problems;		// saved: list its errors when they are known

	int32 count;
	int32 *lines;
	int32 *cols;
	int32 *sevs;
	BString *texts;
};
static BList lsp_docs;

static lsp_doc *lsp_find_doc(const char *name, bool create)
{
	for(int32 i=0;i<lsp_docs.CountItems();i++)
	{
		lsp_doc *doc = (lsp_doc *)lsp_docs.ItemAt(i);
		if (!strcmp(doc->name, name)) return doc;
	}

	if (!create) return NULL;

	lsp_doc *doc = new lsp_doc;
	doc->name = strdup(name);
	doc->sent_len = -1;
	doc->sent_hash = 0;
	doc->awaiting = false;
	doc->show_problems = false;
	doc->count = 0;
	doc->lines = doc->cols = doc->sevs = NULL;
	doc->texts = NULL;
	lsp_docs.AddItem(doc);
	return doc;
}

static void lsp_free_diagnostics(lsp_doc *doc)
{
	delete[] doc->lines;
	delete[] doc->cols;
	delete[] doc->sevs;
	delete[] doc->texts;
	doc->lines = doc->cols = doc->sevs = NULL;
	doc->texts = NULL;
	doc->count = 0;
}

// a server that went away took its session and every document with it:
// the next question opens a new session, and each document goes again as
// a didOpen. What it said of them stands until the new one speaks.
static void lsp_forget_session()
{
	lsp_token = 0;
	lsp_session_asked = false;

	for(int32 i=0;i<lsp_docs.CountItems();i++)
	{
		lsp_doc *doc = (lsp_doc *)lsp_docs.ItemAt(i);
		doc->sent_len = -1;
		doc->awaiting = false;
	}
}

// the file names the language server is asked about: C and C++ only
static bool lsp_is_cpp(const char *name)
{
	const char *dot = strrchr(name, '.');

	if (!dot) return false;
	return !strcasecmp(dot, ".c") || !strcasecmp(dot, ".cc")
		|| !strcasecmp(dot, ".cpp") || !strcasecmp(dot, ".cxx")
		|| !strcasecmp(dot, ".h") || !strcasecmp(dot, ".hh")
		|| !strcasecmp(dot, ".hpp");
}

static BMessenger *lsp_get_server()
{
	if (lsp_server && lsp_server->IsValid()) return lsp_server;

	// the server there was has gone (quit, or crashed): nothing it was told
	// holds for the one that answers next
	if (lsp_server)
	{
		delete lsp_server;
		lsp_server = NULL;
		lsp_forget_session();
	}

	// asking costs a roster round trip; a server that is not there is not
	// asked about again for a while
	bigtime_t now = system_time();
	if (lsp_dead && now - lsp_last_try < 60000000) return NULL;

	status_t err = B_ERROR;
	BMessenger *m = new BMessenger(CLANGD_SERVER_SIGNATURE, -1, &err);
	if (err != B_OK || !m->IsValid())
	{
		// Not running: a messenger addressed by signature reaches a running
		// application and never starts one, so it is started here, the way
		// the roster starts any application by its signature. It takes a
		// moment to open its port: half a second is waited for it, and if it
		// is not there yet this question goes to the index alone and the
		// next one finds the server. Only a server that cannot be started
		// at all is given up on for a while.
		delete m;
		m = NULL;

		status_t launched = be_roster->Launch(CLANGD_SERVER_SIGNATURE);
		if (launched != B_OK && launched != B_ALREADY_RUNNING)
		{
			lsp_dead = true;
			lsp_last_try = now;
			return NULL;
		}

		for(int i = 0; i < 10 && !m; i++)
		{
			snooze(50000);
			BMessenger *t = new BMessenger(CLANGD_SERVER_SIGNATURE, -1, &err);
			if (err == B_OK && t->IsValid()) m = t;
			else delete t;
		}

		if (!m)
		{
			lsp_dead = false;
			return NULL;
		}
	}

	lsp_server = m;
	lsp_dead = false;
	return lsp_server;
}

// ask for a session, once: its answer is the token every question goes with
static void lsp_ask_session(BMessenger *server)
{
	if (lsp_session_asked) return;

	BMessage start(LSP_SESSION);
	start.AddMessenger("notify", BMessenger(MainWindow));
	// the answer is a message to the main window: name it as the reply
	// handler, or the one-way send carries no address for it
	server->SendMessage(&start, MainWindow);
	lsp_session_asked = true;
}

// the document as one string, lines joined with newlines
static BString *lsp_document_text(EditView *ev)
{
	BString *out = new BString();

	for(clLine *line = ev->firstline; line; line = line->next)
	{
		BString *s = line->GetLineAsString();

		*out += s->String();
		if (line->next) *out += "\n";
		delete s;
	}

	return out;
}

static uint32 lsp_hash(const char *s, int32 len)
{
	uint32 h = 2166136261u;		// FNV-1a

	for(int32 i=0;i<len;i++)
	{
		h ^= (uint8)s[i];
		h *= 16777619u;
	}

	return h;
}

// The document's text to the server: didOpen the first time, didChange
// after. Text the server already has is not sent again, and false says so.
static bool lsp_send_document(BMessenger *server, EditView *ev)
{
	BString *text = lsp_document_text(ev);
	lsp_doc *doc = lsp_find_doc(ev->filename, true);
	uint32 hash = lsp_hash(text->String(), text->Length());

	if (lsp_edited_doc == ev->filename) lsp_edited_doc = "";

	if (doc->sent_len == text->Length() && doc->sent_hash == hash)
	{
		delete text;
		return false;
	}

	BMessage msg(doc->sent_len < 0 ? LSP_OPEN : LSP_CHANGE);
	msg.AddInt32("session", lsp_token);
	msg.AddString("name", ev->filename);
	msg.AddString("text", text->String());
	server->SendMessage(&msg);

	doc->sent_len = text->Length();
	doc->sent_hash = hash;
	doc->awaiting = true;
	delete text;
	return true;
}

bool lsp_complete(EditView *ev)
{
	if (ev->IsUntitled || !lsp_is_cpp(ev->filename)) return false;

	BMessenger *server = lsp_get_server();
	if (!server) return false;

	// the first question has to open the session: its answer is the token
	// every question goes with. The question is kept and asked the moment the
	// token arrives (lsp_session_opened), so the first press is answered like
	// any other rather than only opening the way for the next one.
	if (lsp_token == 0)
	{
		lsp_ask_session(server);
		lsp_waiting_doc = ev->filename;
		lsp_waiting_is_sync = false;
		return true;
	}

	lsp_send_document(server, ev);

	BMessage req(LSP_COMPLETE);
	req.AddInt32("session", lsp_token);
	req.AddString("name", ev->filename);
	req.AddInt32("line", ev->cursor.y);
	req.AddInt32("col", ev->cursor.x);
	req.AddInt32("reqid", 1);
	server->SendMessage(&req);

	return true;
}

// Keep the server's copy of a C or C++ document current: called when the
// document is opened or brought to the front, after it is saved, and when
// its editing pauses. clangd answers every text it is given with its
// diagnostics (lsp_diagnostics_arrived) -- errors appear without anything
// asked. True if the text went, or goes as soon as the session opens.
bool lsp_sync_document(EditView *ev)
{
	if (!lsp_is_cpp_document(ev)) return false;

	BMessenger *server = lsp_get_server();
	if (!server) return false;

	if (lsp_token == 0)
	{
		lsp_ask_session(server);

		// a completion that is waiting outranks a document that is
		if (lsp_waiting_doc.Length() == 0)
		{
			lsp_waiting_doc = ev->filename;
			lsp_waiting_is_sync = true;
		}
		return (lsp_waiting_doc == ev->filename);
	}

	return lsp_send_document(server, ev);
}

bool lsp_is_cpp_document(EditView *ev)
{
	return ev && !ev->IsUntitled && lsp_is_cpp(ev->filename);
}

void lsp_document_edited(EditView *ev)
{
	if (!lsp_is_cpp_document(ev)) return;

	lsp_edited_doc = ev->filename;
	lsp_edited_at = system_time();
}

void lsp_tick()
{
	if (lsp_edited_doc.Length() == 0) return;
	if (system_time() - lsp_edited_at < LSP_EDIT_PAUSE) return;

	// only the document in front is edited; one left behind is sent when
	// it comes to the front again
	EditView *ev = editor.curev;
	BString edited = lsp_edited_doc;
	lsp_edited_doc = "";

	if (lsp_is_cpp_document(ev) && edited == ev->filename)
		lsp_sync_document(ev);
}

/*
void c------------------------------() {}
*/

// is the Build pane showing this document's errors right now?
static bool lsp_problems_showing(lsp_doc *doc)
{
	CompilePane *pane = MainWindow->popup.compile;

	return pane && pane->fShowingProblems
		&& MainWindow->popup.pane->IsOpen()
		&& MainWindow->popup.pane->Contents() == pane
		&& lsp_problems_doc == doc->name;
}

// A document's errors in the Build pane, one line each in the compiler's
// own format -- path:line:column: error: text -- so a click takes the
// editor to the line, as it does for a build's errors. Warnings go with
// them. A document with no errors closes a list of its own that is showing;
// a pane busy with a build, or a program it runs, is left alone.
static void lsp_show_problems(lsp_doc *doc)
{
	CompilePane *pane = MainWindow->popup.compile;
	if (!pane || pane->IsBusy()) return;

	int errors = 0, warnings = 0;
	for(int32 i=0;i<doc->count;i++)
	{
		if (doc->sevs[i] == 1) errors++;
		else if (doc->sevs[i] == 2) warnings++;
	}

	if (errors == 0)
	{
		if (lsp_problems_showing(doc))
			MainWindow->popup.pane->Close();
		return;
	}

	// in the order they come in the document
	int32 *order = new int32[doc->count];
	for(int32 i=0;i<doc->count;i++)
	{
		int32 j = i;
		while(j > 0 && (doc->lines[order[j-1]] > doc->lines[i]
			|| (doc->lines[order[j-1]] == doc->lines[i]
				&& doc->cols[order[j-1]] > doc->cols[i])))
		{
			order[j] = order[j-1];
			j--;
		}
		order[j] = i;
	}

	pane->AbortThread();	// only reaps a build that has finished
	pane->Clear();
	MainWindow->popup.pane->Open("Problems", pane);

	const char *base = strrchr(doc->name, '/');
	base = base ? base + 1 : doc->name;

	BString head;
	head << base << ": " << errors << (errors == 1 ? " error" : " errors");
	if (warnings)
		head << ", " << warnings << (warnings == 1 ? " warning" : " warnings");
	head << ", from clangd";
	pane->AddLine(head.String(), CompilePane::Ink(CompilePane::INK_HEADER), false);

	for(int32 k=0;k<doc->count;k++)
	{
		int32 i = order[k];
		int sev = doc->sevs[i];
		if (sev != 1 && sev != 2) continue;

		// clangd appends its notes to the message on lines of their own:
		// the first line is the error, the rest are shown under it
		BString text = doc->texts[i];
		int32 nl = text.FindFirst('\n');
		BString first, rest;
		if (nl >= 0)
		{
			text.CopyInto(first, 0, nl);
			text.CopyInto(rest, nl + 1, text.Length() - nl - 1);
		}
		else
			first = text;

		BString line;
		line << doc->name << ":" << doc->lines[i] + 1 << ":"
			<< doc->cols[i] + 1 << ": "
			<< (sev == 1 ? "error: " : "warning: ") << first;
		pane->AddLine(line.String(), CompilePane::Ink(sev == 1
			? CompilePane::INK_ERROR : CompilePane::INK_WARNING), true);

		while(rest.Length() > 0)
		{
			nl = rest.FindFirst('\n');
			BString note;
			if (nl >= 0)
			{
				rest.CopyInto(note, 0, nl);
				rest.Remove(0, nl + 1);
			}
			else
			{
				note = rest;
				rest = "";
			}

			if (note.Length() > 0)
			{
				note.Prepend("    ");
				pane->AddLine(note.String(),
					CompilePane::Ink(CompilePane::INK_TEXT), false);
			}
		}
	}

	delete[] order;
	pane->fShowingProblems = true;
	lsp_problems_doc = doc->name;
}

void lsp_diagnostics_arrived(BMessage *message)
{
	const char *name = NULL;
	if (message->FindString("name", &name) != B_OK || !name) return;

	lsp_doc *doc = lsp_find_doc(name, false);
	if (!doc) return;

	int32 count = 0;
	message->FindInt32("count", &count);
	if (count < 0) count = 0;

	lsp_free_diagnostics(doc);
	if (count > 0)
	{
		doc->lines = new int32[count];
		doc->cols = new int32[count];
		doc->sevs = new int32[count];
		doc->texts = new BString[count];

		for(int32 i=0;i<count;i++)
		{
			const char *text = NULL;

			doc->lines[i] = doc->cols[i] = 0;
			doc->sevs[i] = 1;
			message->FindInt32("line", i, &doc->lines[i]);
			message->FindInt32("col", i, &doc->cols[i]);
			message->FindInt32("sev", i, &doc->sevs[i]);
			if (message->FindString("text", i, &text) == B_OK && text)
				doc->texts[i] = text;
		}
	}
	doc->count = count;
	doc->awaiting = false;

	// the line numbers of the document in front take the news at once
	if (editor.curev && !strcmp(editor.curev->filename, name) && ln_panel)
		ln_panel->MarksChanged();

	// a saved document's errors are listed; a list of them already showing
	// is kept up to date, and goes when they are gone
	if (doc->show_problems)
	{
		doc->show_problems = false;
		lsp_show_problems(doc);
	}
	else if (lsp_problems_showing(doc))
		lsp_show_problems(doc);
}

void lsp_document_saved(EditView *ev)
{
	if (!lsp_is_cpp_document(ev)) return;

	lsp_doc *doc = lsp_find_doc(ev->filename, true);

	// the answer to the text now sent, or to one already on its way, lists
	// them; if the server has this text and has spoken, what it said stands
	doc->show_problems = true;
	if (lsp_sync_document(ev) || doc->awaiting) return;

	doc->show_problems = false;
	lsp_show_problems(doc);
}

void lsp_show_problems_of(EditView *ev)
{
	if (!lsp_is_cpp_document(ev)) return;

	lsp_doc *doc = lsp_find_doc(ev->filename, false);
	if (doc) lsp_show_problems(doc);
}

int lsp_line_severity(EditView *ev, int line)
{
	if (!ev || lsp_docs.CountItems() == 0) return 0;

	lsp_doc *doc = lsp_find_doc(ev->filename, false);
	if (!doc) return 0;

	int worst = 0;
	for(int32 i=0;i<doc->count;i++)
	{
		if (doc->lines[i] != line) continue;
		if (doc->sevs[i] == 1) return 1;
		if (doc->sevs[i] == 2) worst = 2;
	}

	return worst;
}

bool lsp_line_messages(EditView *ev, int line, BString *out)
{
	*out = "";
	if (!ev || lsp_docs.CountItems() == 0) return false;

	lsp_doc *doc = lsp_find_doc(ev->filename, false);
	if (!doc) return false;

	for(int32 i=0;i<doc->count;i++)
	{
		if (doc->lines[i] != line) continue;
		if (doc->sevs[i] != 1 && doc->sevs[i] != 2) continue;

		// the first line of each: the notes clangd appends are in the list
		BString text = doc->texts[i];
		int32 nl = text.FindFirst('\n');
		if (nl >= 0) text.Truncate(nl);

		if (out->Length() > 0) *out << "\n";
		*out << (doc->sevs[i] == 1 ? "error: " : "warning: ") << text;
	}

	return out->Length() > 0;
}

/*
void c------------------------------() {}
*/

void lsp_session_opened(BMessage *message)
{
	message->FindInt32("session", &lsp_token);
	lsp_session_asked = false;

	// the question the session was opened for, asked now -- of the document
	// being edited, if it is still the one it was asked about, and from where
	// its caret is now
	BString waiting = lsp_waiting_doc;
	lsp_waiting_doc = "";
	bool sync = lsp_waiting_is_sync;
	lsp_waiting_is_sync = false;
	if (lsp_token != 0 && waiting.Length() > 0 && editor.curev
		&& !editor.curev->IsUntitled && waiting == editor.curev->filename)
	{
		if (sync)
		{
			BMessenger *server = lsp_get_server();
			if (server) lsp_send_document(server, editor.curev);
		}
		else
			lsp_complete(editor.curev);
	}
}

void lsp_end_session()
{
	if (lsp_server && lsp_token != 0)
	{
		BMessage end(LSP_END);
		end.AddInt32("session", lsp_token);
		lsp_server->SendMessage(&end);
	}
	lsp_token = 0;
	lsp_session_asked = false;
	lsp_waiting_doc = "";
	lsp_waiting_is_sync = false;
}

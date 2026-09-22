
#include "editor.h"
#include "lsp_client.h"
#include "lsp_protocol.h"

#include <Messenger.h>
#include <Roster.h>

// the whole state of the conversation: who to talk to, the token the
// server answered with, and which documents it has been told about
static BMessenger *lsp_server = NULL;
static int32 lsp_token = 0;
static bool lsp_dead = false;			// the server was not there, stop asking
static bigtime_t lsp_last_try = 0;		// ...but look again every minute
static BList lsp_opened_docs;			// char* file names sent with didOpen

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

	delete lsp_server;
	lsp_server = m;
	lsp_dead = false;
	return lsp_server;
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

bool lsp_complete(EditView *ev)
{
	if (ev->IsUntitled || !lsp_is_cpp(ev->filename)) return false;

	BMessenger *server = lsp_get_server();
	if (!server) return false;

	// the first question can only open the session; its answer is the
	// token the next one goes with
	if (lsp_token == 0)
	{
		BMessage start(LSP_SESSION);
		start.AddMessenger("notify", BMessenger(MainWindow));
		// the answer is a message to the main window: name it as the reply
		// handler, or the one-way send carries no address for it
		server->SendMessage(&start, MainWindow);
		return false;
	}

	BString *text = lsp_document_text(ev);
	bool opened = false;

	for(int32 i=0;i<lsp_opened_docs.CountItems();i++)
		if (!strcmp((const char *)lsp_opened_docs.ItemAt(i), ev->filename))
			opened = true;

	BMessage doc(opened ? LSP_CHANGE : LSP_OPEN);
	doc.AddInt32("session", lsp_token);
	doc.AddString("name", ev->filename);
	doc.AddString("text", text->String());
	server->SendMessage(&doc);

	if (!opened) lsp_opened_docs.AddItem(strdup(ev->filename));

	BMessage req(LSP_COMPLETE);
	req.AddInt32("session", lsp_token);
	req.AddString("name", ev->filename);
	req.AddInt32("line", ev->cursor.y);
	req.AddInt32("col", ev->cursor.x);
	req.AddInt32("reqid", 1);
	server->SendMessage(&req);

	delete text;
	return true;
}

void lsp_session_opened(BMessage *message)
{
	message->FindInt32("session", &lsp_token);
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
}

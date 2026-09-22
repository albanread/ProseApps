
#include "editor.h"
#include "api_complete.h"
#include "lsp_client.h"
#include "lsp_protocol.h"

#include <Message.h>

#include <ListView.h>
#include <ListItem.h>
#include <Screen.h>
#include <FindDirectory.h>
#include <File.h>
#include <Path.h>
#include <ctype.h>
#include <string.h>
#include <strings.h>

// the undo group closers are prototyped in each file's .fdh (makegen's old
// output); undo.h has only the opener
UndoGroupRecord *EndUndoGroup(EditView *ev);

// the index: one symbol per line, "name TAB kind TAB signature TAB header",
// sorted case-insensitively by name (it is written by Prose's api-db and
// shipped as data/sisong/api-index; a loose copy in the user's non-packaged
// data directory is read first, which is how a build in progress is tried)
struct ApiSymbol
{
	const char *name;
	const char *kind;
	const char *signature;
	const char *header;
};

static char *api_buffer = NULL;
static ApiSymbol *api_symbols = NULL;
static int api_symbol_count = 0;
static bool api_load_attempted = false;

// the open completion. The list holds what the index matched first and
// what the language server answered after, so the first part keeps its
// places while the second arrives.
#define API_PREFIX_MAX			128
#define API_MAX_MATCHES			400
#define API_MAX_ROWS			12

struct CompMatch
{
	char name[96];					// what is inserted
	char detail[144];				// what shows beside it
};

static struct
{
	EditView *ev;					// document the list belongs to
	char prefix[API_PREFIX_MAX];
	int prefix_len;
	int word_start_x;				// column the word being completed starts at
	CompMatch matches[API_MAX_MATCHES];
	int match_count;
	bool clangd_pending;			// a question is out to the language server
} comp;

/* --------------------------------------------------------------- the index */

static char api_is_word_char(char ch)
{
	unsigned char u = ch;
	return (u < 128) && (isalnum(u) || u == '_' || u == '~');
}

static bool api_load_index()
{
	const char *paths[2];
	BPath path;
	int i;

	if (api_load_attempted) return (api_symbols != NULL);
	api_load_attempted = true;

	find_directory(B_USER_NONPACKAGED_DATA_DIRECTORY, &path);
	path.Append("sisong/api-index");
	paths[0] = strdup(path.Path());

	find_directory(B_SYSTEM_DATA_DIRECTORY, &path);
	path.Append("sisong/api-index");
	paths[1] = strdup(path.Path());

	for(i=0;i<2;i++)
	{
		BFile file(paths[i], B_READ_ONLY);
		off_t size;
		char *p, *end, *line_end;
		int count;

		if (file.InitCheck() != B_OK || file.GetSize(&size) != B_OK || size <= 0)
			continue;

		api_buffer = (char *)malloc(size + 1);
		if (!api_buffer) break;
		if (file.Read(api_buffer, size) != size)
		{
			free(api_buffer);
			api_buffer = NULL;
			continue;
		}
		api_buffer[size] = 0;

		// the fields stay in the buffer; the symbols point into it
		count = 0;
		for(p=api_buffer; p && *p; p = strchr(p, '\n'))
		{
			count++;
			p++;	// past the newline of this line (strchr may give NULL)
		}

		api_symbols = (ApiSymbol *)malloc(sizeof(ApiSymbol) * count);
		if (!api_symbols)
		{
			free(api_buffer);
			api_buffer = NULL;
			continue;
		}

		api_symbol_count = 0;
		p = api_buffer;
		end = api_buffer + size;
		while(p < end)
		{
			ApiSymbol sym;
			char *fields[4];
			int f, nf = 0;

			line_end = (char *)memchr(p, '\n', end - p);
			if (!line_end) line_end = end;
			*line_end = 0;

			fields[nf++] = p;
			for(f=0; p[f]; f++)
			{
				if (p[f] == '\t')
				{
					p[f] = 0;
					if (nf < 4) fields[nf++] = p + f + 1;
				}
			}
			while(nf < 4) fields[nf++] = p + f;

			// a line needs at least a name and a kind to be a symbol
			if (fields[0][0] && fields[1][0])
			{
				sym.name = fields[0];
				sym.kind = fields[1];
				sym.signature = fields[2];
				sym.header = fields[3];
				api_symbols[api_symbol_count++] = sym;
			}

			p = line_end + 1;
		}

		stat("api_complete: %d symbols from %s", api_symbol_count, paths[i]);
		break;
	}

	free((void *)paths[0]);
	free((void *)paths[1]);
	return (api_symbols != NULL);
}

/* ------------------------------------------------------------ the popup list */

class CApiCompleteWindow : public BWindow
{
	public:
		CApiCompleteWindow();

		virtual void MessageReceived(BMessage *message);

		BListView *list;
};

class CApiCompleteItem : public BListItem
{
	public:
		CApiCompleteItem(const char *name, const char *detail);

		virtual void DrawItem(BView *owner, BRect frame, bool complete=false);
		virtual void Update(BView *owner, const BFont *font);

		const char *Detail() { return detail; }

	private:
		char name[96];
		char detail[144];
		float baseline;
};

static CApiCompleteWindow *comp_win = NULL;

static rgb_color api_blend(rgb_color a, rgb_color b, float amount)
{
	rgb_color out;

	out.red = (uint8)(a.red + (b.red - a.red) * amount);
	out.green = (uint8)(a.green + (b.green - a.green) * amount);
	out.blue = (uint8)(a.blue + (b.blue - a.blue) * amount);
	out.alpha = 255;
	return out;
}

CApiCompleteItem::CApiCompleteItem(const char *name, const char *detail)
{
	strlcpy(this->name, name, sizeof(this->name));
	strlcpy(this->detail, detail ? detail : "", sizeof(this->detail));
}

void CApiCompleteItem::Update(BView *owner, const BFont *font)
{
	font_height fh;

	font->GetHeight(&fh);
	baseline = fh.ascent + 2;
	SetHeight(ceilf(fh.ascent + fh.descent) + 4);
}

void CApiCompleteItem::DrawItem(BView *owner, BRect frame, bool complete)
{
	rgb_color bg, text;

	if (IsSelected())
	{
		bg = ui_color(B_LIST_SELECTED_BACKGROUND_COLOR);
		text = ui_color(B_LIST_SELECTED_ITEM_TEXT_COLOR);
	}
	else
	{
		bg = ui_color(B_LIST_BACKGROUND_COLOR);
		text = ui_color(B_LIST_ITEM_TEXT_COLOR);
	}

	owner->SetHighColor(bg);
	owner->FillRect(frame);

	owner->SetHighColor(text);
	owner->MovePenTo(frame.left + 5, frame.top + baseline);
	owner->DrawString(name);

	owner->SetHighColor(api_blend(text, bg, 0.45));
	owner->MovePenTo(frame.left + 5 + owner->StringWidth(name) + 10,
					frame.top + baseline);
	owner->DrawString(detail);
}

CApiCompleteWindow::CApiCompleteWindow()
	: BWindow(BRect(0, 0, 399, 199), "api completion",
			B_BORDERED_WINDOW_LOOK, B_FLOATING_APP_WINDOW_FEEL,
			B_NOT_MOVABLE | B_NOT_RESIZABLE | B_NOT_ZOOMABLE | B_AVOID_FOCUS)
{
	BRect rc(Bounds());

	list = new BListView(rc.InsetByCopy(1, 1), "apilist", B_SINGLE_SELECTION_LIST,
						B_FOLLOW_ALL);
	list->SetInvocationMessage(new BMessage(M_API_COMPLETE_ACCEPT));
	AddChild(list);
}

// a double-click on the list: the main window takes the completion, on the
// thread the document belongs to
void CApiCompleteWindow::MessageReceived(BMessage *message)
{
	switch(message->what)
	{
		case M_API_COMPLETE_ACCEPT:
			MainWindow->PostMessage(M_API_COMPLETE_ACCEPT);
		break;

		default:
			BWindow::MessageReceived(message);
		break;
	}
}

/* --------------------------------------------------------- list housekeeping */

static void api_select(int index)
{
	if (index < 0) index = comp.match_count - 1;
	if (index >= comp.match_count) index = 0;

	if (comp_win && comp_win->Lock())
	{
		comp_win->list->Select(index);
		comp_win->list->ScrollToSelection();
		comp_win->Unlock();
	}
}

void api_complete_cancel()
{
	comp.ev = NULL;

	if (comp_win && comp_win->Lock())
	{
		if (!comp_win->IsHidden()) comp_win->Hide();
		comp_win->Unlock();
	}
}

bool api_complete_active()
{
	return (comp.ev != NULL);
}

/* ----------------------------------------------------------------- opening */

// the word ending at the caret, if it is a plain word; returns its length
static int api_word_before_caret(EditView *ev, char *out, int maxlen)
{
	BString *str = ev->curline->GetLineAsString();
	const char *line = str->String();
	int len = str->Length();
	int end = ev->cursor.x;
	int start, i, outlen = 0, result;

	if (end > len) end = len;
	start = end;
	while(start > 0 && api_is_word_char(line[start - 1]))
		start--;

	result = 0;
	for(i=start; i<end && outlen < maxlen-1; i++)
	{
		if (line[i] & 0x80) goto done;	// not a word the index can know
		out[outlen++] = line[i];
	}
	out[outlen] = 0;
	result = outlen;

done:
	delete str;
	return result;
}

// place the list beside the word it completes, flipping above the line if
// the screen ends first
static BRect api_popup_frame(EditView *ev, float width, float height)
{
	BPoint below(GET_CURSOR_PX(comp.word_start_x),
				GET_CURSOR_PY(ev->cursor.y) + editor.font_height);
	BPoint caret_top(GET_CURSOR_PX(comp.word_start_x), GET_CURSOR_PY(ev->cursor.y));
	BScreen screen(B_MAIN_SCREEN_ID);
	BRect sf(screen.Frame());

	MainView->ConvertToScreen(&below);
	MainView->ConvertToScreen(&caret_top);

	if (below.y + height > sf.bottom)
		below.y = caret_top.y - height;

	if (below.x + width > sf.right) below.x = sf.right - width;
	if (below.x < sf.left) below.x = sf.left;

	return BRect(below.x, below.y, below.x + width - 1, below.y + height - 1);
}

static void api_insert_match(EditView *ev, const char *name)
{
	int fx, fy;

	if (!strncmp(name, comp.prefix, comp.prefix_len)
		&& (int)strlen(name) > comp.prefix_len)
	{
		// what is typed is the name's own beginning, case and all: add the rest
		BeginUndoGroup(ev);
		ev->action_insert_string(ev->cursor.x, ev->cursor.y,
								name + comp.prefix_len, &fx, &fy);
		ev->cursor.move(fx, fy);
		EndUndoGroup(ev);
	}
	else if (strcasecmp(name, comp.prefix))
	{
		// typed with other letters than the name: the word gives way to it
		BeginUndoGroup(ev);
		ev->action_delete_right(comp.word_start_x, ev->cursor.y, comp.prefix_len);
		ev->action_insert_string(comp.word_start_x, ev->cursor.y, name, &fx, &fy);
		ev->cursor.move(fx, fy);
		EndUndoGroup(ev);
	}
	else
	{
		return;		// the word is already the name
	}

	ev->cursor.set_mode(CM_FREE);
	ev->MakeCursorVisible();
	ev->RedrawView();
}

static void api_show_popup(EditView *ev)
{
	BFont font;
	float width, itemheight;
	int i;

	if (!comp_win) comp_win = new CApiCompleteWindow();
	if (!comp_win->Lock()) return;

	comp_win->list->MakeEmpty();
	comp_win->list->GetFont(&font);

	width = 0;
	for(i=0;i<comp.match_count;i++)
	{
		CApiCompleteItem *item = new CApiCompleteItem(comp.matches[i].name,
													comp.matches[i].detail);
		float w = font.StringWidth(comp.matches[i].name)
				+ font.StringWidth(item->Detail()) + 40;

		if (w > width) width = w;
		comp_win->list->AddItem(item);
	}

	if (comp_win->list->CountItems() > 0)
	{
		CApiCompleteItem *first = (CApiCompleteItem *)comp_win->list->ItemAt(0);
		first->Update(comp_win->list, &font);
		itemheight = first->Height();
	}
	else itemheight = editor.font_height;

	if (width < 220) width = 220;
	if (width > 520) width = 520;

	int rows = min(comp.match_count, API_MAX_ROWS);
	comp_win->ResizeTo(width, rows * itemheight + 2);
	comp_win->MoveTo(api_popup_frame(ev, width, rows * itemheight + 2).LeftTop());

	comp_win->list->Select(0);
	comp_win->list->ScrollToSelection();

	if (comp_win->IsHidden()) comp_win->Show();
	comp_win->Unlock();
}

void api_complete_word(EditView *ev)
{
	int i;

	if (api_complete_active()) api_complete_cancel();

	comp.prefix_len = api_word_before_caret(ev, comp.prefix, API_PREFIX_MAX);

	// Straight after "p.", "p->" or "Class::" there is no word yet, and what
	// is wanted is every member there is: a question only the language server
	// can answer. Anywhere else an empty word asks nothing.
	bool member_access = false;
	if (comp.prefix_len == 0)
	{
		BString *str = ev->curline->GetLineAsString();
		const char *line = str->String();
		int x = ev->cursor.x;
		if (x > str->Length()) x = str->Length();
		member_access = (x >= 1 && line[x - 1] == '.')
			|| (x >= 2 && line[x - 2] == '-' && line[x - 1] == '>')
			|| (x >= 2 && line[x - 2] == ':' && line[x - 1] == ':');
		delete str;
		if (!member_access) return;
	}

	// the question to the language server goes out first: its answer
	// arrives as a message, and joins the list when it does. The first
	// question only opens the session -- the token it answers with is
	// needed before anything can be asked.
	comp.clangd_pending = lsp_complete(ev);

	// the index is optional: a machine without it still has the language
	// server, and a document the server cannot read still has the index
	comp.match_count = 0;
	if (!member_access && api_load_index() && api_symbol_count)
	{
		const char *prev = "";

		for(i=0;i<api_symbol_count && comp.match_count < API_MAX_MATCHES;i++)
		{
			const ApiSymbol *s = &api_symbols[i];

			if (strncasecmp(s->name, comp.prefix, comp.prefix_len)) continue;
			if (!strcasecmp(s->name, prev)) continue;

			CompMatch *m = &comp.matches[comp.match_count++];
			strlcpy(m->name, s->name, sizeof(m->name));
			if (strlen(s->kind) + strlen(s->header) + 2 >= sizeof(m->detail))
				strlcpy(m->detail, s->kind, sizeof(m->detail));
			else
				snprintf(m->detail, sizeof(m->detail), "%s  %s", s->kind,
						s->header);
			prev = s->name;
		}
	}

	if (comp.match_count == 0)
	{
		// nothing here yet; the server's answer may still open the list
		if (comp.clangd_pending)
		{
			comp.ev = ev;
			comp.word_start_x = ev->cursor.x - comp.prefix_len;
		}
		return;
	}

	if (comp.match_count == 1)
	{
		// only one thing in the API it can be: it goes in without a list, and
		// whatever the language server says after that is not waited for
		comp.ev = ev;
		api_insert_match(ev, comp.matches[0].name);
		comp.ev = NULL;
		return;
	}

	comp.ev = ev;
	comp.word_start_x = ev->cursor.x - comp.prefix_len;
	api_show_popup(ev);
}

// the language server answered (LSP_COMPLETE_REPLY on the main window):
// its completions join the list, after the index's and without repeating
// them, and the list is drawn again with them
void api_complete_clangd_reply(BMessage *message)
{
	int32 count = 0;

	if (!comp.ev || !comp.clangd_pending) return;
	message->FindInt32("count", &count);

	for(int32 i=0;i<count && comp.match_count < API_MAX_MATCHES;i++)
	{
		const char *label = NULL, *detail = NULL;

		if (message->FindString("label", i, &label) != B_OK || !label
			|| !label[0]) continue;
		message->FindString("detail", i, &detail);

		// clangd's answers are about the text as it was sent; anything the
		// typing has moved past since is not offered
		if (strncasecmp(label, comp.prefix, comp.prefix_len)) continue;

		bool have = false;
		for(int k=0;k<comp.match_count;k++)
			if (!strcasecmp(comp.matches[k].name, label)) { have = true; break; }
		if (have) continue;

		CompMatch *m = &comp.matches[comp.match_count++];
		strlcpy(m->name, label, sizeof(m->name));
		if (detail && detail[0])
			strlcpy(m->detail, detail, sizeof(m->detail));
		else
			strlcpy(m->detail, "language server", sizeof(m->detail));
	}

	api_show_popup(comp.ev);
}

/* ------------------------------------------------------------------ taking */

void api_complete_take()
{
	EditView *ev = comp.ev;
	int32 index = -1;

	if (!ev || !editor.curev || ev->DocID != editor.curev->DocID)
	{
		api_complete_cancel();
		return;
	}

	if (comp_win && comp_win->Lock())
	{
		index = comp_win->list->CurrentSelection();
		comp_win->Unlock();
	}
	if (index < 0) index = 0;
	if (index >= comp.match_count) index = comp.match_count - 1;

	api_complete_cancel();
	api_insert_match(ev, comp.matches[index].name);
}

/* -------------------------------------------------------------- the keys it takes */

bool api_complete_key(EditView *ev, int ch)
{
	if (!comp.ev) return false;

	// the document changed under the list (a tab switch): the list is gone
	if (comp.ev->DocID != ev->DocID)
	{
		api_complete_cancel();
		return false;
	}

	// the wheel scrolls the document; the list can stay where it is
	if (ch == KEY_MOUSEWHEEL_DOWN || ch == KEY_MOUSEWHEEL_UP) return false;

	int32 index = -1;
	if (comp_win && comp_win->Lock())
	{
		index = comp_win->list->CurrentSelection();
		comp_win->Unlock();
	}
	if (index < 0) index = 0;

	switch(ch)
	{
		case B_UP_ARROW:	api_select(index - 1); return true;
		case B_DOWN_ARROW:	api_select(index + 1); return true;
		case B_PAGE_UP:		api_select(index - API_MAX_ROWS); return true;
		case B_PAGE_DOWN:	api_select(index + API_MAX_ROWS); return true;
		case B_HOME:		api_select(0); return true;
		case B_END:			api_select(comp.match_count - 1); return true;

		case B_ENTER:
		case B_TAB:
			api_complete_take();
			return true;

		case B_ESCAPE:
			api_complete_cancel();
			return true;

		default:
			// any other key goes to the editor; the text moves on without
			// the list
			api_complete_cancel();
			return false;
	}
}

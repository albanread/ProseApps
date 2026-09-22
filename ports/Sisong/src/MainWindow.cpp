
#include "editor.h"
#include "MainWindow.fdh"
#include "api_complete.h"
#include "lsp_client.h"
#include "lsp_protocol.h"
#include <ctype.h>


CMainWindow::CMainWindow(BRect frame)
	: BWindow(frame, "Sisong", B_TITLED_WINDOW, 0)
{
BRect bo(Bounds());
BRect rc;

	MainWindow = this;
	fClosing = false;
	fDoingInstantQuit = false;
	fWindowIsForeground = true;

	/* create top area (menu bar and tabs) */
	rc.Set(0, 0, bo.right, TOPVIEW_HEIGHT-1);
	top.topview = new BView(rc, "topview", B_FOLLOW_TOP | B_FOLLOW_LEFT_RIGHT, 0);
	top.topview->SetViewColor(B_TRANSPARENT_COLOR);
	AddChild(top.topview);
	{
		// menu bar
		rc.Set(0, 0, bo.right, MENU_HEIGHT-1);
		top.menubar = new MainMenuBar(rc, B_FOLLOW_TOP | B_FOLLOW_LEFT_RIGHT);
		top.topview->AddChild(top.menubar);

		// tab bar
		rc = top.topview->Bounds();
		rc.top = MENU_HEIGHT;
		rc.left++;
		top.tabbar = new CTabBar(rc, B_FOLLOW_BOTTOM | B_FOLLOW_LEFT_RIGHT);
		top.topview->AddChild(top.tabbar);

		// the 1-pixel gap at left of tabbar
		rc.left = rc.right = 0;
		BView *gapview = new BView(rc, "tbgap", B_FOLLOW_LEFT | B_FOLLOW_TOP, 0);
		gapview->SetViewColor(top.tabbar->fBackgroundColor);
		AddChild(gapview);
	}

	// main editing area
	rc = bo;
	rc.top = TOPVIEW_HEIGHT;
	main.editarea = new CEditArea(rc, B_FOLLOW_ALL);
	AddChild(main.editarea);

	// init color scheme, before the panes that are made in its colours.
	// The scheme last used is kept by its index, which means nothing once
	// the built-in schemes have been replaced by a newer set: then, and for
	// a new user, it is the default (Paper, or Midnight Blue on a dark desktop).
	popup.pane = NULL;
	popup.searchresults = NULL;
	popup.compile = NULL;
	popup.buildhelp = NULL;
	{
		bool fresh = ColorScheme::InitDefaultsIfNeeded();
		int scheme = ColorScheme::DefaultSchemeIndex();
		if (!fresh)
			scheme = settings->GetInt("SelectedColorScheme", scheme);
		if (!ColorScheme::SchemeExists(scheme))
			scheme = ColorScheme::DefaultSchemeIndex();

		CurrentColorScheme.LoadScheme(scheme);
	}

	// popup panes
	popup.pane = new PopupPane();
	popup.searchresults = new SearchResultsPane();
	popup.compile = new CompilePane();
	popup.buildhelp = new BuildHelpPane();

	// this is a joke that nobody will get, a reference to an easter egg in
	// the firmware of something called "Mirack".
	popup.pane->SetContents("Build", popup.compile);
	popup.compile->AddLine("Bunnies, enchiladas, and tin.", CompilePane::Ink(CompilePane::INK_SCRIPTNAME), false);
	popup.compile->AddLine(" ;-)", CompilePane::Ink(CompilePane::INK_SCRIPTNAME), false);

	// cursor-flashing pulsar thread
	cursor_timer = new CViewTimer(this, M_CURSOR_TIMER, 100);

	main.editarea->editpane->MakeFocus();

	Show();
	//testprefs();
}

CMainWindow::~CMainWindow()
{
	settings->SetInt("SelectedColorScheme", CurrentColorScheme.GetLoadedSchemeIndex());

	// this needs to be done here, because once we leave here, our tab bar will
	// be auto-destroyed, and we don't want to try closing documents with an
	// invalid tab bar pointer.
	stat("~CMainWindow: Close_All()");
	EditView::Close_All();

	// close popup panes
	popup.pane->Close();
	popup.pane->RemoveContents();
	delete popup.pane;
	delete popup.searchresults;

	delete cursor_timer;
	cursor_timer = NULL;

	// save window position
	settings->SetInt("window_left", (int)Frame().left);
	settings->SetInt("window_right", (int)Frame().right);
	settings->SetInt("window_top", (int)Frame().top);
	settings->SetInt("window_bottom", (int)Frame().bottom);
}

// The editor's colours changed (another scheme, or one colour edited in the
// preferences): the panes under the text, and the strip beside it, show them
// too. They used to keep the colours they were made with.
void CMainWindow::EditorColorsChanged()
{
	if (popup.compile) popup.compile->ColorsChanged();
	if (popup.buildhelp) popup.buildhelp->ColorsChanged();
	if (popup.searchresults) popup.searchresults->ColorsChanged();

	if (main.editarea && LockLooper())
	{
		main.editarea->Invalidate();
		for(int32 i=0;i<main.editarea->CountChildren();i++)
			main.editarea->ChildAt(i)->Invalidate();
		UnlockLooper();
	}
}

void CMainWindow::UpdateWindowTitle()
{
	if (editor.curev)
	{
		BString app_title(editor.curev->filename);

		const char *prjName = ProjectManager.GetCurrentProjectName();
		if (prjName[0])
		{
			app_title.Append(" - ");
			app_title.Append(prjName);
		}

		app_title.Append(" - ");
		app_title.Append(APPLICATION_NAME);

		SetTitle(app_title.String());
	}
	else
	{
		SetTitle(APPLICATION_NAME);
	}
}

/*
void c------------------------------() {}
*/

// the menu item with this shortcut, in this menu or below it
static BMenuItem *FindShortcutItem(BMenu *menu, char key, uint32 modifiers)
{
	for(int32 i=0;i<menu->CountItems();i++)
	{
		BMenuItem *item = menu->ItemAt(i);
		uint32 item_modifiers = 0;
		char item_key = item->Shortcut(&item_modifiers);

		if (item_key && toupper(item_key) == toupper(key) && item_modifiers == modifiers)
			return item;

		if (item->Submenu())
		{
			BMenuItem *found = FindShortcutItem(item->Submenu(), key, modifiers);
			if (found) return found;
		}
	}

	return NULL;
}

// The menu's Command+Control shortcuts (Save All, Close All, Find in files,
// Find Previous, Build but Don't Run, Abort Compile, Save Layout), found by
// the key's own character.
//
// The Interface Kit finds a shortcut by the first byte of the key's text,
// and with Control held the keymap makes that text a control character
// (0x06 for F) or, for the digits, nothing at all (the key then arrives as
// B_UNMAPPED_KEY_DOWN). So none of these shortcuts fired, and the key went
// on to start one of the editor's own Control+key command sequences.
static bool HandleCommandControlShortcut(BWindow *window, BMenu *menubar, BMessage *message)
{
	int32 modifiers = 0;
	if (message->FindInt32("modifiers", &modifiers) != B_OK) return false;
	if (!(modifiers & B_COMMAND_KEY) || !(modifiers & B_CONTROL_KEY)) return false;

	// the character the key has with no modifier held; an unmapped key has
	// none, and the digit row is known by its key codes
	int32 ch = 0;
	if (message->FindInt32("raw_char", &ch) != B_OK || ch <= 0)
	{
		int32 key = 0;
		message->FindInt32("key", &key);
		if (key < 0x12 || key > 0x1b) return false;
		ch = "1234567890"[key - 0x12];
	}

	uint32 want = modifiers & (B_COMMAND_KEY | B_CONTROL_KEY | B_SHIFT_KEY | B_OPTION_KEY);
	BMenuItem *item = FindShortcutItem(menubar, (char)ch, want);
	if (!item || !item->IsEnabled() || !item->Message()) return false;

	BMessage copy(*item->Message());
	window->PostMessage(&copy);
	return true;
}

void CMainWindow::DispatchMessage(BMessage *message, BHandler *handler)
{
	// The window is shown by its constructor, which EApp's constructor calls
	// before it sets MainView and opens the first document: a key or wheel
	// event in that moment would go through both, unset. Until the
	// application says it is running, events are the Interface Kit's alone.
	if (!app_running || !MainView || !editor.curev)
	{
		BWindow::DispatchMessage(message, handler);
		return;
	}

	switch(message->what)
	{
		case B_UNMAPPED_KEY_DOWN:
			if (!HandleCommandControlShortcut(this, top.menubar->bar, message))
				BWindow::DispatchMessage(message, handler);
		break;

		// have to hook keyboard input here so we can catch modifier keys
		// that would otherwise be gobbled up by the menu manager
		case B_KEY_DOWN:
		{
			if (HandleCommandControlShortcut(this, top.menubar->bar, message))
				break;

			const char *bytes = NULL;
			int32 ch;
			if (message->FindString("bytes", &bytes) != B_OK || !bytes)
			{
				// a key message without its text (a script can send one):
				// nothing for the editor in it, and nothing to look at below
				BWindow::DispatchMessage(message, handler);
				break;
			}

			{
				if (!MainView->IsFocus())
					MainView->MakeFocus();

				ch = *bytes;
				if (ch == B_FUNCTION_KEY)
				{
					int32 fkey;
					message->FindInt32("key", &fkey);
					fkey -= B_F1_KEY;

					if (fkey >= 0 && fkey < NUM_F_KEYS)
					{
						int what = editor.settings.fkey_mapping[fkey];
						if (what) ProcessMenuCommand(what);
					}
				}
				else if (ch == B_ESCAPE && !editor.settings.esc_quits_immediately)
				{
					if (api_complete_active())
					{
						api_complete_cancel();
					}
					else if (editor.curev->IsCommandSeqActive())
					{
						editor.curev->CancelCommandSeq();
					}
					else
					{
						!popup.pane->IsOpen() ?
								popup.pane->Open() :
								popup.pane->Close();
					}
				}
				else
				{
					editor.curev->HandleKey(ch);
				}
			}

			// eat tabs, we don't want focus changing on us.
			switch(*bytes)
			{
				case '\t':
					if (!IsAltDown() && !IsCtrlDown() && !IsShiftDown())
					{
						break;
					}
					// fall thru
				default:
					BWindow::DispatchMessage(message, handler);
			}
		}
		break;

		case B_MOUSE_WHEEL_CHANGED:
		{
			float fDelta = 0;

			if (MainView->IsFocus())
			{
				// by the sign of the delta, and not at all for none: it was
				// cut to an int first, so a delta under one line (a trackpad's)
				// and a purely horizontal event both scrolled up
				if (message->FindFloat("be:wheel_delta_y", &fDelta) == B_OK && fDelta != 0)
				{
					int key = (fDelta > 0) ? KEY_MOUSEWHEEL_DOWN : KEY_MOUSEWHEEL_UP;
					editor.curev->HandleKey(key);
				}
			}
			else
			{
				BWindow::DispatchMessage(message, handler);
			}
		}
		break;

		default:
		{
			BWindow::DispatchMessage(message, handler);
		}
		break;
	}
}

void CMainWindow::MessageReceived(BMessage *message)
{
	switch(message->what)
	{
		case M_API_COMPLETE_ACCEPT:
		{
			api_complete_take();
		}
		break;

		// the language server's session token, and its answers
		case LSP_SESSION_REPLY:
		{
			lsp_session_opened(message);
		}
		break;

		case LSP_COMPLETE_REPLY:
		{
			api_complete_clangd_reply(message);
		}
		break;

		case LSP_DIAGNOSTICS:
		{
			// what clangd thinks of a document: the numbers of the lines it
			// faults turn red for an error and amber for a warning, and a
			// saved document's errors are listed in the Build pane
			lsp_diagnostics_arrived(message);
		}
		break;

		case M_CURSOR_TIMER:
		{
			if (MainView) MainView->cursor.tick();
			if (FunctionList) FunctionList->TimerTick();
			AutoSaver_Tick();
			lsp_tick();

			Stats_Tick(fWindowIsForeground || IsPrefsWindowOpenAndActive());
		}
		break;

		case M_TAB_CHANGED:
		{
			EditView *newev;

			if (message->FindPointer("ev", (void **)&newev) == B_OK)
			{
				if (editor.curev != newev)
				{
					editor.curev = newev;
					editor.curev->FullRedrawView();

					// a C or C++ document brought to the front is sent to the
					// language server, whose diagnostics come back unasked
					lsp_sync_document(newev);
				}
			}
		}
		break;

		// a dumb hack; see comments in FindBox for why I do this...
		case M_FINDBOX_PINGPONG:
		{
			int32 old_what;

			if (message->FindInt32("old_what", &old_what) == B_OK)
			{
				message->what = old_what;
				CFindBox *FindBox = GetCurrentFindBox();

				if (FindBox)
				{
					FindBox->LockLooper();
					FindBox->HandleSearchRequest(message);
					FindBox->UnlockLooper();
				}
			}
		}
		break;

		case M_FUNCTIONLIST_INVOKE:
		{
			int32 index;

			if (message->FindInt32("index", &index) == B_OK)
			{
				FunctionList->JumpToIndex(index);
			}
		}
		break;

		case M_SEARCHRESULTS_INVOKE:
		{
			int32 index;

			if (message->FindInt32("index", &index) == B_OK)
			{
				if (popup.searchresults)
					popup.searchresults->ItemClicked(index);
			}
		}
		break;

		case M_COMPILEPANE_INVOKE:
		{
			int32 index;

			if (message->FindInt32("index", &index) == B_OK)
			{
				if (popup.compile)
					popup.compile->ItemClicked(index);
			}
		}
		break;

		case M_POPUPPANE_CLOSE:
		{
			if (MainWindow->popup.pane)
				MainWindow->popup.pane->Close();
		}
		break;

		case B_SAVE_REQUESTED:
			FinishFileSaveAs(message);
		break;

		case B_REFS_RECEIVED:
		case 'DATA':	// drag n' drop
			ProcessRefs(message);
		break;

		case B_CANCEL:
			DismissFilePanel();
		break;

		default:
			if (IsMenuCommand(message->what))
			{
				ProcessMenuCommand(message->what, message);
			}
			else
			{
				BWindow::MessageReceived(message);
			}
		break;
	}
}

void CMainWindow::ProcessRefs(BMessage *message)
{
int i;
uint32 type;
int32 count;
entry_ref ref;
EditView *ev = NULL;

	if (message->GetInfo("refs", &type, &count) != B_OK) return;
	if (type != B_REF_TYPE) return;

	LockLooper();

	// The empty "new 1" the editor starts with has served its purpose once a
	// real file is open beside it: a file opened from Tracker or the command
	// line should not come with a blank tab. Only if nothing was done to it.
	EditView *blank = NULL;
	if (editor.DocList->CountItems() == 1)
	{
		EditView *only = (EditView *)editor.DocList->ItemAt(0);
		if (only && only->IsUntitled && !only->IsDirty && \
			only->nlines == 1 && only->firstline->GetLength() == 0)
		{
			blank = only;
		}
	}

	for(i=0;i<count;i++)
	{
		if (message->FindRef("refs", i, &ref) == B_OK)
		{
			BEntry entry(&ref, true);
			BPath path;

			entry.GetPath(&path);
			const char *filename = path.Path();

			// if file is already open don't open another copy
			EditView *opened = FindEVByFilename(filename);
			if (!opened)
				opened = CreateEditView((char *)filename);

			// one that could not be opened (a directory, no permission)
			// must not become the active tab
			if (opened) ev = opened;
		}
	}

	if (ev)
	{
		top.tabbar->SetActiveTab(ev);
		lsp_sync_document(ev);
		if (blank && blank != ev)
			blank->Close(false);
	}
	DismissFilePanel();	// harmless if file panel isn't open
	UnlockLooper();
}

/*
void c------------------------------() {}
*/

void CMainWindow::WindowActivated(bool active)
{
	if (main.editarea && main.editarea->editpane)
		main.editarea->editpane->cursor.EnableFlashing(active);

	if (active)
		ProjectManager.UpdateProjectsMenu();

	fWindowIsForeground = active;
}

bool CMainWindow::QuitRequested()
{
	// if we've already been here, just return true. this prevents a bug
	// where the layout file was corrupted if SS was quit with the compile
	// pane running, because in that case we will be called twice for some reason.
	if (fClosing) return true;

	if (fDoingInstantQuit || ConfirmCloseSaveFiles(true))
	{
		fClosing = true;
		lsp_end_session();
		ProjectManager.SaveProject();
		be_app->PostMessage(B_QUIT_REQUESTED);
		return true;
	}

	return false;
}

// triggered from QuitRequested, when the user clicks Close on window, selects Exit
// from menu, or uses ESC quick-quit shortcut.
//
// loops through each unsaved document and asks if the user wants to save it.
//
// if the shutdown is aborted, returns false.
bool ConfirmCloseSaveFiles(bool quitting)
{
EditView *ev;
int i;

	for(i=0;
		ev = (EditView *)editor.DocList->ItemAt(i);
		i++)
	{
		if (!ev->IsDirty) continue;

		TabBar->SetActiveTab(ev);
		ev->FullRedrawView();

		BString prompt;
		prompt << "Save \"" << GetFileSpec(ev->filename) << "\" before "
			<< (quitting ? "quitting?" : "closing it?");

		BAlert *alert = new BAlert("", prompt.String(), \
							"Cancel", "Don't Save", "Save It",
							B_WIDTH_AS_USUAL, B_WARNING_ALERT);

		switch(alert->Go())
		{
			case 0: return false;		// Cancel
			case 1:	// Don't Save
			break;

			case 2:	// Save
			{
				if (!ev->IsUntitled)
				{
					// document already has a name, so this will save immediately,
					// and return to us.
					if (FileSave())
					{
						// Save Failed
						return false;
					}
				}
				else
				{
					// document is Untitled, so popup Save As box. Save As is NOT modal,
					// so we abort the shutdown for now, but ask Save As box to continue
					// it after user makes the selection.
					// (Not quitting: the panel just saves, and what was asked
					// for -- another project, a layout -- is asked for again.)
					FileSaveAs(false, quitting);
					return false;
				}
			}
			break;
		}
	}

	return true;
}


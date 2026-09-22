
#ifndef __LSP_CLIENT_H
#define __LSP_CLIENT_H

// the editor's half of the conversation with clangd_server: BMessages to
// the server's signature, answers arriving on the main window's looper.
// See lsp_protocol.h for the messages.

class EditView;

// ask the language server about the word at the caret: the document's text
// goes with the question, and the answer comes back as a message (never a
// wait). Returns true if a question was actually sent. A C or C++ document
// and a running server are required; anything else is the caller's to do.
bool lsp_complete(EditView *ev);

// send a C or C++ document's text to the server (opened, brought to the
// front, saved, its editing paused): clangd's diagnostics about it come
// back unasked. True if the text went, or goes when the session opens.
bool lsp_sync_document(EditView *ev);

// the document was saved: its errors, when clangd has judged the saved
// text, are listed in the Build pane
void lsp_document_saved(EditView *ev);

// the document was edited (EditView::SetDirty), and the window's 100 ms
// timer: a second after the editing pauses the text goes to the server
void lsp_document_edited(EditView *ev);
void lsp_tick();

// clangd's diagnostics for a document (LSP_DIAGNOSTICS): kept, shown in the
// line numbers, and listed in the Build pane if the document was saved
void lsp_diagnostics_arrived(class BMessage *message);

// list a document's errors in the Build pane, as a save does
void lsp_show_problems_of(EditView *ev);

// the worst problem clangd reported on a line (counted from 0): 1 an error,
// 2 a warning, 0 neither
int lsp_line_severity(EditView *ev, int line);

// the errors and warnings on a line, one to a line; false if there are none
bool lsp_line_messages(EditView *ev, int line, class BString *out);

// is this a document the language server can be asked about?
bool lsp_is_cpp_document(EditView *ev);

// remember the session the server answered with (LSP_SESSION_REPLY)
void lsp_session_opened(class BMessage *message);

// end the session, if there is one (the editor is quitting)
void lsp_end_session();

#endif

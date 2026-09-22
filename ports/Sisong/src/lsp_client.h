
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

// remember the session the server answered with (LSP_SESSION_REPLY)
void lsp_session_opened(class BMessage *message);

// end the session, if there is one (the editor is quitting)
void lsp_end_session();

#endif

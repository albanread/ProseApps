
#ifndef __API_COMPLETE_H
#define __API_COMPLETE_H

// Complete Word: completions of the word before the caret from the index of
// the Be/Haiku API (data/sisong/api-index, made by Prose's api-db). The list
// is a floating window that never takes focus: the editor keeps the keys and
// hands the ones it owns to it (api_complete_key).

class EditView;

// open the list at the caret (Edit ▸ Complete Word); the only possible
// completion is inserted without a list
void api_complete_word(EditView *ev);

// a key pressed while the list is open; returns true if the key was the
// list's (up/down/page/home/end select, enter/tab take, escape closes)
bool api_complete_key(EditView *ev, int ch);

// is the list open?
bool api_complete_active();

// close the list, inserting nothing
void api_complete_cancel();

// take the selected completion (a double-click on the list arrives as this,
// posted to the main window so it runs on its thread)
void api_complete_take();

// the language server's answer to the question Complete Word sent: its
// completions join the list (LSP_COMPLETE_REPLY on the main window)
void api_complete_clangd_reply(class BMessage *message);

#endif

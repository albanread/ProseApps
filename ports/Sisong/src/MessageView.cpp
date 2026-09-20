
#include "MessageView.h"
#include "MessageView.fdh"

/*
	a BView that creates it's own BLooper for receiving messages.
	this allows the BView to SetTarget it's controls before the BView is added
	to a window (otherwise, the SetTarget call will fail).
*/


MessageView::MessageView(BRect frame, const char *name, uint32 resizingMode, uint32 flags)
	: BView(frame, name, resizingMode, flags)
{
	_init();
}


MessageView::MessageView(BMessage *archive)
	: BView(archive)
{
	_init();
}


void MessageView::_init()
{
	fLooper = new MessageViewLooper(this);
	fLooper->Run();
	
	fMessenger = new BMessenger(NULL, fLooper);
}


MessageView::~MessageView()
{
	if (fMessenger) {
		delete fMessenger;
		fMessenger = NULL;
	}
	
	if (fLooper) {
		// We are on the window's thread, holding the window's lock, and are
		// about to wait for the looper's. The looper holds its own lock while
		// it dispatches, and waits for the window's to hand us a message: a
		// timer's message arriving now had each waiting for the other, the
		// preferences window hung half closed, and the next "Preferences..."
		// hung the main window on it. So: tell the looper we are going, which
		// makes it stop waiting (see DispatchMessage), then wait for it.
		fLooper->Detach();
		fLooper->Lock();
		fLooper->Quit();
		fLooper = NULL;
	}
}

/*
void c------------------------------() {}
*/

BLooper *MessageView::Looper()
{
	return fLooper;
}

BMessenger *MessageView::Messenger()
{
	return fMessenger;
}

/*
void c------------------------------() {}
*/

MessageViewLooper::MessageViewLooper(MessageView *assoc_view)
	: BLooper(),
	  fAssocView(assoc_view)
{ }


void MessageViewLooper::Detach()
{
	fAssocView = NULL;
}

void MessageViewLooper::DispatchMessage(BMessage *msg, BHandler *target)
{
	// wait for the window's lock in slices, giving up once the view has said
	// it is going away: see ~MessageView()
	for(;;)
	{
		MessageView *view = fAssocView;
		if (!view) break;

		status_t err = view->LockLooperWithTimeout(20 * 1000);
		if (err == B_OK)
		{
			if (fAssocView)
				view->MessageReceived(msg);

			view->UnlockLooper();
			break;
		}

		if (err != B_TIMED_OUT)
			break;		// not in a window (yet, or any more)
	}
	
	BLooper::DispatchMessage(msg, target);
}





#include <OS.h>
#include <Looper.h>
#include <Message.h>
#include <Messenger.h>
#include <MessageRunner.h>
#include "ViewTimer.h"
#include "../common/basics.h"

// A BMessageRunner now. This was a thread of its own that slept and posted
// the message, and the destructor ended it with kill_thread(): whatever the
// thread held at that instant -- the target's message queue lock, a malloc
// lock inside PostMessage() -- stayed held for good, and the next thread to
// want it hung. Timers are made and destroyed on every change of page in the
// preferences and at quit.

// post message "msg" to "target' every "interval" milliseconds
CViewTimer::CViewTimer(BLooper *target, int msg, uint interval)
{
	BMessage message(msg);
	fRunner = new BMessageRunner(BMessenger(NULL, target), &message, \
							(bigtime_t)interval * 1000);
}

CViewTimer::~CViewTimer()
{
	delete fRunner;
}


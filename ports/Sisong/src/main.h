

// the application class
class EApp : public BApplication
{
    public:
		EApp();
		~EApp();
		
		virtual void RefsReceived(BMessage *message);
		virtual void ArgvReceived(int32 argc, char **argv);
		virtual void MessageReceived(BMessage *msg);
};



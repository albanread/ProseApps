
class BLooper;
class BMessageRunner;

class CViewTimer
{
public:
	CViewTimer(BLooper *target, int msg, uint interval);
	~CViewTimer();

private:
	BMessageRunner *fRunner;
};

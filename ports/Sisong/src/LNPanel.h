
// affects only the maximum height of window, not maximum height of document
#define MAX_LN_NUMBERS		1000
class LNPanel;

// an individual line number
class LNItem
{
public:
	LNItem() : number(0), draw_x(0) { StringToDraw[0] = 0; }
	void SetNumber(LNPanel *parent, int new_no);
	void DrawItem(LNPanel *parent, int y);
	int Number() const { return number; }
	
private:
	char StringToDraw[10];
	int number;			// as shown: the document's line, counted from 1
	int draw_x;
	
};

// the line number view
class LNPanel : public BView
{
public:
	LNPanel(BRect frame, uint32 resizingMode);
	~LNPanel();
	virtual void Draw(BRect updateRect);
	friend class LNItem;
	
	void SetLineNumber(int y, int newNumber);
	void SetNumVisibleLines(int count);
	void Redraw(void);
	void RedrawIfNeeded(void);
	
	void SetFontSize(int newsize);
	void SetFontFamily(const char *family);

	// a line with clangd's errors on it: the pointer over its number shows
	// them, a click lists the document's in the Build pane
	virtual void MouseMoved(BPoint where, uint32 transit, const BMessage *drag);
	virtual void MouseDown(BPoint where);

	// clangd has spoken again: the numbers are redrawn, and a tip showing
	// what it said before goes
	void MarksChanged();

	// the colour of a line number that clangd faults: 1 error, 2 warning
	static rgb_color MarkColor(int severity);
	
private:
	int LineAt(BPoint where);	// the document's line there, from 0; -1 none

	CFontDrawer *font_drawer;
	int fTipLine;
	LNItem lineItems[MAX_LN_NUMBERS];
	int numLinesShown;
	bool redraw_needed;
	int fFace;
};


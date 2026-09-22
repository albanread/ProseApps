
#include "editor.h"
#include "lsp_client.h"
#include "LNPanel.fdh"

/***********************************************
*  THE											*
*   LINE NUMBERS								*
*     VIEW										*
************************************************/

LNPanel::LNPanel(BRect frame, uint32 resizingMode)
            : BView(frame, "LineNumberView",
			resizingMode, B_WILL_DRAW | B_FULL_UPDATE_ON_RESIZE)
{
	font_drawer = new CFontDrawer(editor.settings.font_size);
	SetFont(font_drawer->font);
	
	SetViewColor(B_TRANSPARENT_COLOR);
	
	fFace = -1;	// be sure to update it first time
	
	numLinesShown = 0;
	redraw_needed = true;
	fTipLine = -1;
}

LNPanel::~LNPanel()
{
	delete font_drawer;
}

void LNPanel::SetFontSize(int new_size)
{
	font_drawer->SetSize(new_size);
	SetFont(font_drawer->font);
}

void LNPanel::SetFontFamily(const char *family)
{
	font_drawer->SetFamily(family);
	SetFont(font_drawer->font);
}

/*
void c------------------------------() {}
*/

void LNPanel::Draw(BRect updateRect)
{
	Redraw();
}

void LNPanel::Redraw()
{
int i, y;

	if (LockLooper())
	{
		// check if Bold option has changed in prefs panel
		int face = GetColorBoldState(COLOR_LINENUM) ? B_BOLD_FACE : B_REGULAR_FACE;
		if (face != fFace)
		{
			font_drawer->SetFace(this, face);
			fFace = face;
		}
		
		y = 0;
		for(i=0;i<numLinesShown;i++)
		{
			lineItems[i].DrawItem(this, y);
			y += font_drawer->fontheight;
		}
		
		// clear unused area if lines don't cover whole height
		BRect unusedrect(Bounds());	
		unusedrect.top = y;
		
		if (unusedrect.top <= unusedrect.bottom)
		{
			SetLowColor(GetEditBGColor(COLOR_LINENUM));
			FillRect(unusedrect, B_SOLID_LOW);
		}
		
		UnlockLooper();
		redraw_needed = false;
		//stat("LNPANEL redrawn @ nls = %d, Height = %.2f, clear from %d-%d", numLinesShown, Bounds().Height(), (int)unusedrect.top, (int)unusedrect.bottom);
	}
}

void LNPanel::RedrawIfNeeded()
{
	if (redraw_needed)
		Redraw();
}

// change line number at row Y to read line number 'newNumber'.
void LNPanel::SetLineNumber(int y, int newNumber)
{
	if (y < 0 || y >= MAX_LN_NUMBERS)
		return;
	
	lineItems[y].SetNumber(this, newNumber);
	redraw_needed = true;
	// needed to fix a bug where the numbers would occasionally not be redrawn
	// after the compile panel was closed
	Invalidate();
}

void LNPanel::SetNumVisibleLines(int count)
{
	if (count >= MAX_LN_NUMBERS)
		count = MAX_LN_NUMBERS-1;
	
	if (count != numLinesShown)
	{
		numLinesShown = count;
		redraw_needed = true;
	}
}

int LNPanel::LineAt(BPoint where)
{
	if (where.y < 0 || font_drawer->fontheight <= 0) return -1;

	int i = (int)where.y / font_drawer->fontheight;
	if (i >= numLinesShown || lineItems[i].Number() <= 0) return -1;
	return lineItems[i].Number() - 1;
}

void LNPanel::MouseMoved(BPoint where, uint32 transit, const BMessage *drag)
{
	int line = (transit == B_EXITED_VIEW || transit == B_OUTSIDE_VIEW)
		? -1 : LineAt(where);
	if (line == fTipLine) return;
	fTipLine = line;

	BString messages;
	if (line >= 0 && lsp_line_messages(editor.curev, line, &messages))
		SetToolTip(messages.String());
	else
		SetToolTip((const char *)NULL);
}

void LNPanel::MarksChanged()
{
	Invalidate();
	if (fTipLine >= 0)
	{
		fTipLine = -1;
		SetToolTip((const char *)NULL);
	}
}

void LNPanel::MouseDown(BPoint where)
{
	int line = LineAt(where);

	if (line >= 0 && lsp_line_severity(editor.curev, line) != 0)
		lsp_show_problems_of(editor.curev);
}

// red and amber, lighter on a dark ground than on a light one
rgb_color LNPanel::MarkColor(int severity)
{
	static const rgb_color on_light[2] = {
		{ 0xd0, 0x1c, 0x1c, 255 }, { 0xb8, 0x6e, 0x00, 255 } };
	static const rgb_color on_dark[2] = {
		{ 0xff, 0x6b, 0x60, 255 }, { 0xff, 0xc2, 0x4a, 255 } };

	int which = (severity == 1) ? 0 : 1;
	return GetEditBGColor(COLOR_LINENUM).IsDark()
		? on_dark[which] : on_light[which];
}

/***********************************************
*  EACH	INDIVIDUAL								*
*    LINE NUMBER (PRIVATE)						*
************************************************/

void LNItem::SetNumber(LNPanel *parent, int new_no)
{
	number = new_no;

	// build string to draw
	snprintf(StringToDraw, sizeof(StringToDraw), "%d", new_no);
	
	// get X coordinate that aligns the number with right edge
	parent->LockLooper();
	
	draw_x = ((int)(parent->Bounds().right - parent->Bounds().left) -
		parent->font_drawer->GetStringWidth(StringToDraw));
	
	parent->UnlockLooper();
	
	draw_x -= 6;
}

void LNItem::DrawItem(LNPanel *parent, int y)
{
	// a line clangd faults has its number in red for an error, amber for a
	// warning, with a bar of that colour at the panel's edge
	int sev = lsp_line_severity(editor.curev, number - 1);

	// clear the area we're about to redraw
	parent->SetLowColor(GetEditBGColor(COLOR_LINENUM));
	BRect r(parent->Bounds());
	r.top = y;
	r.bottom = (y + parent->font_drawer->fontheight);
	parent->FillRect(r, B_SOLID_LOW);

	if (sev)
	{
		parent->SetHighColor(LNPanel::MarkColor(sev));
		parent->FillRect(BRect(r.left, r.top + 1, r.left + 2, r.bottom - 1));
	}
	else
		parent->SetHighColor(GetEditFGColor(COLOR_LINENUM));
	
	// draw the string
	parent->font_drawer->DrawString(parent, StringToDraw, draw_x, y);
}


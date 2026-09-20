
#include "editor.h"
#include "draw.h"
#include <string.h>

// The editor works on a grid of character cells, so it needs a font of one
// width. be_fixed_font is the system's, but it is only as fixed as the fonts
// installed: when the family the app_server wants for it is missing, the
// server falls back to any family at all, and a proportional one may come
// back. So: take be_fixed_font if it really is fixed, else the first fixed
// family the system has, the well-known ones first.
static void UseFixedFamily(BFont *font)
{
	// fonts made for reading code first, then whatever monospaced family the
	// system has
	static const char *preferred[] =
		{ "JetBrains Mono", "Fira Code", "Noto Sans Mono", "Noto Mono",
		  "DejaVu Sans Mono", "Liberation Mono", NULL };

	if (font->IsFixed())
		return;

	for(int i=0;preferred[i];i++)
	{
		font_family family;
		memset(family, 0, sizeof(family));
		strncpy(family, preferred[i], sizeof(family) - 1);

		BFont candidate(font);
		if (candidate.SetFamilyAndFace(family, B_REGULAR_FACE) == B_OK)
		{
			// SetFamilyAndFace() keeps the old family if there is no such one
			font_family got; font_style style;
			candidate.GetFamilyAndStyle(&got, &style);
			if (!strcmp(got, family) && candidate.IsFixed())
			{
				*font = candidate;
				return;
			}
		}
	}

	int32 count = count_font_families();
	for(int32 i=0;i<count;i++)
	{
		font_family family;
		uint32 flags = 0;

		if (get_font_family(i, &family, &flags) != B_OK) continue;
		if (!(flags & B_IS_FIXED)) continue;

		BFont candidate(font);
		if (candidate.SetFamilyAndFace(family, B_REGULAR_FACE) == B_OK && candidate.IsFixed())
		{
			*font = candidate;
			return;
		}
	}

	// none at all: be_fixed_font as it is. DrawString() still puts every
	// character into its cell.
}

// be_fixed_font, or a family that really is fixed: for the panes that show
// text in columns (the editor, compiler output, search results)
void GetFixedFont(BFont *font)
{
	*font = *be_fixed_font;
	UseFixedFamily(font);
}

CFontDrawer::CFontDrawer(int font_size)
{
	font = new BFont;
	GetFixedFont(font);

	_face = B_REGULAR_FACE;
	_lastview = NULL;
	
	SetSize(font_size);
}

CFontDrawer::~CFontDrawer()
{
	delete font;
}

/*
void c------------------------------() {}
*/

// sets the size of the font (but you must reapply it to the view before it will change)
void CFontDrawer::SetSize(int newsize)
{
font_height fh;

	font->SetSize(newsize);
	font->GetHeight(&fh);

	_ascent = (int)(fh.ascent + 0.5f);
	_descent = (int)(fh.descent + 0.5f);
	
	fontheight = (int)(((fh.ascent + fh.descent) + fh.leading) + 0.5);

	// the cell must hold the glyph: round an advance of 9.6 up, not down
	fontwidth = (int)ceilf(font->StringWidth("M") - 0.01f);
	if (fontwidth < 1) fontwidth = 1;
}


void CFontDrawer::SetColors(BView *view, rgb_color fg, rgb_color bg)
{
	view->SetHighColor(fg);
	view->SetLowColor(bg);
}


int CFontDrawer::GetStringWidth(char *string)
{
	return (fontwidth * strlen(string));
}


void CFontDrawer::SetFace(BView *view, int newface)
{
	if (_face != newface || view != _lastview)
	{
		font->SetFace(newface);
		view->SetFont(font);
		
		_face = newface;
		_lastview = view;
	}
}


void CFontDrawer::DrawString(BView *view, char *string, int x, int y)
{
	DrawString(view, string, x, y, strlen(string));
}


// Draws len bytes of string with every character in its own cell of the
// grid, the cell being the byte's: the editor counts columns in bytes and
// places the cursor, the selection and the next run of text by them. Left to
// itself the server advances by the font's own width, which need not be a
// whole number of pixels (hinting off, or a size the font does not hint to
// one), and a long run then drifts away from its columns.
void CFontDrawer::DrawString(BView *view, char *string, int x, int y, int len)
{
	const int kChunk = 256;
	BPoint locations[kChunk];
	float baseline = y + _ascent;
	int start = 0;

	while(start < len)
	{
		// a chunk ends at a character boundary (UTF-8: continuation bytes
		// are 10xxxxxx and belong to the character before them)
		int end = start, nglyphs = 0;
		while(end < len)
		{
			bool starts_char = ((uchar)string[end] & 0xC0) != 0x80;
			if (starts_char)
			{
				if (nglyphs == kChunk) break;
				locations[nglyphs++].Set(x + (end * fontwidth), baseline);
			}
			end++;
		}

		if (nglyphs)
			view->DrawString(string + start, end - start, locations, nglyphs);

		start = end;
	}
}





#include "editor.h"
#include <ctype.h>
#include "../common/basics.h"

#include "colors.fdh"

ColorScheme CurrentColorScheme;
const int kCurrentSchemeVersion = 3;	// 3: Paper and Midnight Blue lead the built-in schemes


static const char *color_names[] =
{
	"Text",
	"Identifier",
	"Operator",
	"System Constant",
	"Number",
	"Preprocessor",
	"Line Comment",
	"Block Comment",
	"String (double-quoted)",
	"String (single-quoted)",
	"String (broken)",
	"Brace",
	"Brace (being matched)",
	"Brace (broken)",
	"Line Numbers",
	"Selection",
	"Tab line",
	"Tab line (active)",
	"Cursor"
};

// The port's two schemes, and its defaults: paper for a light desktop, a dark
// blue for a dark one. Every ink was checked against its ground (WCAG contrast:
// text 13:1 and 12:1, no token under 4.5:1). The pairs are foreground and
// background; Selection, Tab line and Cursor use the first of their pair,
// and the search results pane draws a selected row in Selection's second.
#define PAPER		{ 0xfb, 0xf5, 0xdc }
static const rgb_color scheme_Paper[] =
{
	{ 0x2b, 0x2a, 0x26 }, PAPER,					// text
	{ 0x1b, 0x4f, 0x9c }, PAPER,					// id (keywords, types)
	{ 0x5a, 0x4a, 0x2f }, PAPER,					// operator
	{ 0x7a, 0x3e, 0x9d }, PAPER,					// system constant
	{ 0xa8, 0x48, 0x0c }, PAPER,					// number
	{ 0x0b, 0x6e, 0x66 }, PAPER,					// pp
	{ 0x6b, 0x70, 0x58 }, PAPER,					// line comment
	{ 0x6b, 0x70, 0x58 }, PAPER,					// block comment
	{ 0x2c, 0x7a, 0x30 }, PAPER,					// string
	{ 0x4b, 0x74, 0x10 }, PAPER,					// single string
	{ 0xc0, 0x26, 0x1f }, PAPER,					// broken string
	{ 0x33, 0x31, 0x2b }, PAPER,					// brace
	{ 0x1b, 0x1b, 0x1b }, { 0xff, 0xd7, 0x5e },		// matched brace
	{ 0xff, 0xff, 0xff }, { 0xd3, 0x2f, 0x2f },		// broken brace
	{ 0x85, 0x7d, 0x5e }, { 0xf1, 0xe9, 0xc8 },		// line numbers
	{ 0xbf, 0xd8, 0xfb }, { 0xbf, 0xd8, 0xfb },		// selection
	{ 0xdd, 0xd3, 0xae }, PAPER,					// tabline
	{ 0x3d, 0x7b, 0xd9 }, PAPER,					// tabline (active)
	{ 0x1b, 0x1b, 0x1b }, PAPER						// cursor
};

#define MIDNIGHT	{ 0x13, 0x23, 0x3f }
static const rgb_color scheme_Midnight[] =
{
	{ 0xdd, 0xe5, 0xee }, MIDNIGHT,					// text
	{ 0xff, 0xc6, 0x6d }, MIDNIGHT,					// id (keywords, types)
	{ 0xb7, 0xc3, 0xd0 }, MIDNIGHT,					// operator
	{ 0xd7, 0xa6, 0xff }, MIDNIGHT,					// system constant
	{ 0xff, 0x9e, 0x80 }, MIDNIGHT,					// number
	{ 0x6f, 0xd6, 0xc6 }, MIDNIGHT,					// pp
	{ 0x7e, 0x93, 0xab }, MIDNIGHT,					// line comment
	{ 0x7e, 0x93, 0xab }, MIDNIGHT,					// block comment
	{ 0xa5, 0xd6, 0xa7 }, MIDNIGHT,					// string
	{ 0xc5, 0xe1, 0xa5 }, MIDNIGHT,					// single string
	{ 0xff, 0x7b, 0x72 }, MIDNIGHT,					// broken string
	{ 0xe6, 0xec, 0xf3 }, MIDNIGHT,					// brace
	{ 0x13, 0x23, 0x3f }, { 0xff, 0xc6, 0x6d },		// matched brace
	{ 0xff, 0xff, 0xff }, { 0xd8, 0x43, 0x3b },		// broken brace
	{ 0x6f, 0x86, 0xa3 }, { 0x0e, 0x1b, 0x33 },		// line numbers
	{ 0x2e, 0x4c, 0x7e }, { 0x2e, 0x4c, 0x7e },		// selection
	{ 0x25, 0x37, 0x5a }, MIDNIGHT,					// tabline
	{ 0x5c, 0x9d, 0xff }, MIDNIGHT,					// tabline (active)
	{ 0xff, 0xff, 0xff }, MIDNIGHT					// cursor
};

static const rgb_color scheme_Eggplant[] =
{
	{ 0xe8, 0xe8, 0xe8 }, { 0x22, 0x03, 0x40 },		// text
	{ 0xc3, 0xcf, 0x5c }, { 0x22, 0x03, 0x40 },		// id
	{ 0xe8, 0xe8, 0xe8 }, { 0x22, 0x03, 0x40 },		// operator
	{ 0xd6, 0x40, 0x59 }, { 0x22, 0x03, 0x40 },		// system constant
	{ 0xa7, 0xec, 0xbb }, { 0x22, 0x03, 0x40 },		// number
	{ 0xe7, 0x8d, 0x58 }, { 0x22, 0x03, 0x40 },		// pp
	{ 0x1b, 0x8c, 0xa0 }, { 0x22, 0x03, 0x40 },		// line comment
	{ 0x1b, 0x8c, 0xa0 }, { 0x22, 0x03, 0x40 },		// block comment
	{ 0xa0, 0xa0, 0xa0 }, { 0x22, 0x03, 0x40 },		// string
	{ 0x80, 0x80, 0x80 }, { 0x22, 0x03, 0x40 },		// single string
	{ 0xff, 0x80, 0x80 }, { 0x22, 0x03, 0x40 },		// broken string
	{ 0xe8, 0xe8, 0xe8 }, { 0x22, 0x03, 0x40 },		// brace
	{ 0xff, 0xff, 0x80 }, { 0x40, 0x00, 0x80 },		// matched brace
	{ 0xff, 0x00, 0x00 }, { 0x00, 0x00, 0x00 },		// broken brace
	{ 0xff, 0xff, 0x80 }, { 0x1a, 0x40, 0x00 },		// line numbers	
	{ 0x40, 0x00, 0x80 }, { 0xff, 0xff, 0xff }, 	// selection
	{ 0xc0, 0xc0, 0xc0 }, { 0xff, 0xff, 0xff },		// tabline
	{ 0xff, 0xff, 0x80 }, { 0x40, 0x00, 0x80 },		// tabline (active)
	{ 0xff, 0xff, 0x80 }, { 0xee, 0xee, 0xee }		// cursor
};

static const rgb_color scheme_Brown[] =
{
	{ 0xff, 0xbb, 0xbb }, { 0x3d, 0x00, 0x00 },		// text
	{ 0xff, 0xff, 0x80 }, { 0x40, 0x00, 0x80 },		// id
	{ 0xff, 0xbb, 0xbb }, { 0x3d, 0x00, 0x00 },		// operator
	{ 0xd6, 0x40, 0x59 }, { 0x3d, 0x00, 0x00 },		// sys constant
	{ 0xff, 0x80, 0x80 }, { 0x3d, 0x00, 0x00 },		// number
	{ 0xe7, 0x8d, 0x58 }, { 0x3d, 0x00, 0x00 },		// pp
	{ 0x80, 0x80, 0x40 }, { 0x3d, 0x00, 0x00 },		// line comment
	{ 0x80, 0x80, 0x40 }, { 0x3d, 0x00, 0x00 },		// block comment
	{ 0xa0, 0xa0, 0xa0 }, { 0x3d, 0x00, 0x00 },		// string
	{ 0x80, 0x80, 0x80 }, { 0x3d, 0x00, 0x00 },		// single string
	{ 0xff, 0x80, 0x80 }, { 0x3d, 0x00, 0x00 },		// broken string
	{ 0xff, 0xbb, 0xbb }, { 0x3d, 0x00, 0x00 },		// brace
	{ 0xff, 0xff, 0x80 }, { 0x40, 0x00, 0x80 },		// matched brace
	{ 0xff, 0x00, 0x00 }, { 0x00, 0x00, 0x00 },		// broken brace
	{ 0xff, 0xff, 0x80 }, { 0x80, 0x40, 0x00 },		// line numbers	
	{ 0x40, 0x00, 0x80 }, { 0xff, 0xff, 0xff }, 	// selection	
	{ 0xc0, 0xc0, 0xc0 }, { 0xff, 0xff, 0xff },		// tabline
	{ 0xff, 0xff, 0x80 }, { 0x40, 0x00, 0x80 },		// tabline (active)
	{ 0xff, 0xff, 0x80 }, { 0xee, 0xee, 0xee }		// cursor
};

static const rgb_color scheme_Lightbulb[] =
{
	{ 0x0b, 0x0b, 0x0b }, { 0xff, 0xff, 0xff },		// text
	{ 0x39, 0x74, 0x79 }, { 0xff, 0xff, 0xff },		// id
	{ 0x44, 0x8a, 0x00 }, { 0xff, 0xff, 0xff },		// operator
	{ 0xfb, 0x00, 0x00 }, { 0xff, 0xff, 0xff },		// sys constant
	{ 0x8e, 0x2a, 0x2a }, { 0xff, 0xff, 0xff },		// number
	{ 0x39, 0x74, 0x79 }, { 0xff, 0xff, 0xff },		// pp
	{ 0xa1, 0x64, 0x0e }, { 0xff, 0xff, 0xff },		// line comment
	{ 0xa1, 0x64, 0x0e }, { 0xff, 0xff, 0xff },		// block comment
	{ 0xa0, 0xa0, 0xa0 }, { 0xff, 0xff, 0xff },		// string
	{ 0x80, 0x80, 0x80 }, { 0xff, 0xff, 0xff },		// single string
	{ 0xff, 0x80, 0x80 }, { 0xff, 0xff, 0xff },		// broken string
	{ 0x39, 0x74, 0x79 }, { 0xff, 0xff, 0xff },		// brace
	{ 0x39, 0x74, 0x79 }, { 0xff, 0xec, 0x7c },		// matched brace
	{ 0xff, 0x00, 0x00 }, { 0x00, 0x00, 0x00 },		// broken brace
	{ 0x00, 0x00, 0x00 }, { 0xef, 0xef, 0xef },		// line numbers	
	{ 0xff, 0xec, 0x7c }, { 0xff, 0xff, 0xff }, 	// selection	
	{ 0x40, 0x40, 0x40 }, { 0xff, 0xff, 0xff },		// tabline
	{ 0x00, 0x00, 0xff }, { 0xff, 0xff, 0xff },		// tabline (active)
	{ 0x00, 0x00, 0x00 }, { 0xff, 0xff, 0xff }		// cursor
};

static const rgb_color scheme_Console[] =
{
	{ 192, 192, 192 },    { 0, 0, 0 },		// text
	{ 128, 192, 128 }, { 0, 0, 0 },		// id
	{ 192, 192, 192 },    { 0, 0, 0 },		// operator
	{ 0xff, 0xec, 0x7c }, { 0, 0, 0 },		// sys constant
	{ 255, 255, 255 }, { 0, 0, 0 },		// number
	{ 0xff, 0xec, 0x7c }, { 0, 0, 0 },		// pp
	{ 0xc1, 0x74, 0x0e }, { 0, 0, 0 },		// line comment
	{ 0xc1, 0x74, 0x0e }, { 0, 0, 0 },		// block comment
	{ 0xa0, 0xa0, 0xa0 }, { 0, 0, 0 },		// string
	{ 0x80, 0x80, 0x80 }, { 0, 0, 0 },		// single string
	{ 0xff, 0x80, 0x80 }, { 0, 0, 0 },		// broken string
	{ 255, 255, 255 }, { 0, 0, 0 },		// brace
	{ 0xff, 0xec, 0x7c }, { 40, 40, 100  },		// matched brace
	{ 0, 0, 0 }, { 255, 0, 0 },		// broken brace
	{ 0xff, 0xff, 0xff }, { 0, 0, 0 },		// line numbers	
	{ 0x7d, 0x00, 0x00 }, { 0xff, 0xff, 0xff }, 	// selection	
	{ 0xe0, 0xe0, 0xe0 }, { 0, 0, 0 },	// tabline
	{ 0xff, 0xec, 0x7c }, { 40, 40, 100  },	// tabline (active)
	{ 255, 255, 255 }, { 0, 0, 0 },		// cursor
};

static const rgb_color scheme_Hallow[] =
{
	{ 0xe4, 0x62, 0x31 }, { 0x11, 0x00, 0x00 },		// text
	{ 0x3f, 0xff, 0x80 }, { 0x11, 0x00, 0x00 },		// id
	{ 0xdf, 0xdf, 0xdf }, { 0x11, 0x00, 0x00 },		// operator
	{ 0xd6, 0xff, 0x59 }, { 0x11, 0x00, 0x00 },		// sys constant
	{ 0xc2, 0xc2, 0xc2 }, { 0x11, 0x00, 0x00 },		// number
	{ 0xe7, 0x8d, 0x58 }, { 0x11, 0x00, 0x00 },		// pp
	{ 0xff, 0xc3, 0x2f }, { 0x11, 0x00, 0x00 },		// line comment
	{ 0xff, 0xc3, 0x2f }, { 0x39, 0x14, 0x00 },		// block comment
	{ 0xa0, 0xa0, 0xa0 }, { 0x11, 0x00, 0x00 },		// string
	{ 0x80, 0x80, 0x80 }, { 0x11, 0x00, 0x00 },		// single string
	{ 0xff, 0x80, 0x80 }, { 0x11, 0x00, 0x00 },		// broken string
	{ 0xdf, 0xdf, 0xdf }, { 0x11, 0x00, 0x00 },		// brace
	{ 0xff, 0xff, 0xff }, { 0x11, 0x6a, 0x37 },		// matched brace
	{ 0xdf, 0xdf, 0xdf }, { 0x92, 0x00, 0x00 },		// broken brace
	{ 0xe3, 0x66, 0x27 }, { 0x00, 0x05, 0x00 },		// line numbers	
	{ 0x27, 0x27, 0x27 }, { 0xff, 0xff, 0xff }, 	// selection	
	{ 0xc0, 0xc0, 0xc0 }, { 0x11, 0x00, 0x00 },		// tabline
	{ 0x5f, 0xff, 0x60 }, { 0x11, 0x00, 0x00 },		// tabline (active)
	{ 0xdf, 0xdf, 0xdf }, { 0xee, 0xee, 0xee }		// cursor
};
/*
void c------------------------------() {}
*/

// construct and initilize all entries to a failsafe monochrome scheme
ColorScheme::ColorScheme()
{
	for(int i=0;i<MAX_COLORS;i++)
	{
		_colors[i].fg.red = _colors[i].fg.green = _colors[i].fg.blue = 255;
		_colors[i].bg.red = _colors[i].bg.green = _colors[i].bg.blue = 0;
		_colors[i].isBold = false;
	}
	
	fSchemeName[0] = 0;
	fCurrentSchemeIndex = -1;
}

// construct and initilize entries from a colorset
ColorScheme::ColorScheme(int schemeNo)
{
	LoadScheme(schemeNo);
}

bool ColorScheme::operator==(const ColorScheme &other) const
{
int i;

	for(i=0;i<NUM_COLORS;i++)
	{
		if (_colors[i].fg != other._colors[i].fg) return false;
		if (_colors[i].bg != other._colors[i].bg) return false;
		if (_colors[i].isBold != other._colors[i].isBold) return false;
	}
	
	return true;
}

bool ColorScheme::operator!=(const ColorScheme &other) const
{
int i;

	for(i=0;i<NUM_COLORS;i++)
	{
		if (_colors[i].fg != other._colors[i].fg) return true;
		if (_colors[i].bg != other._colors[i].bg) return true;
		if (_colors[i].isBold != other._colors[i].isBold) return true;
	}
	
	return false;
}

/*
void c------------------------------() {}
*/

// check if this is the first time the editor is started, and if so,
// create the default color schemes from the built in settings.
void ColorScheme::_FirstTimeInit()
{
	InitDefaultsIfNeeded();
}

// The same, for whoever wants to know that it happened: the index of the
// scheme last used means nothing among a new set of schemes.
bool ColorScheme::InitDefaultsIfNeeded()
{
	if (!SchemeExists(0) || \
		settings->GetInt("ColorSchemeVersion", 0) != kCurrentSchemeVersion)
	{
		stat("! ColorScheme::_FirstTimeInit: setting up default colorschemes");
		settings->SetInt("ColorSchemeVersion", kCurrentSchemeVersion);
		ResetToDefaults();
		return true;
	}

	return false;
}

// the built-in schemes, in the order of the Settings menu
static const struct
{
	const char *name;
	const rgb_color *colors;
} default_schemes[] =
{
	{ "Paper", scheme_Paper },
	{ "Midnight Blue", scheme_Midnight },
	{ "Eggplant Sea", scheme_Eggplant },
	{ "Maroon Mountain", scheme_Brown },
	{ "Lightbulb City", scheme_Lightbulb },
	{ "Console Caves", scheme_Console },
	{ "Hallows Eve", scheme_Hallow },
	{ NULL, NULL }
};

// The scheme a new user starts with: Paper, or Midnight Blue on a desktop
// whose own document background is dark.
int ColorScheme::DefaultSchemeIndex()
{
	return ui_color(B_DOCUMENT_BACKGROUND_COLOR).IsDark() ? 1 : 0;
}

void ColorScheme::ResetToDefaults()
{
	// all of them go, the user's too (the preferences' button says so)
	while(GetNumColorSchemes() > 1)
		DeleteScheme(0);
	
	ColorScheme temp;
	for(int i=0;default_schemes[i].name;i++)
		temp._CreateDefaultScheme(default_schemes[i].name, default_schemes[i].colors, i);
	
	if (MainWindow)
		MainWindow->top.menubar->UpdateColorSchemesMenu();
}

// creates a scheme named "name" from "initdata[]" array, then writes it to scheme index "index".
void ColorScheme::_CreateDefaultScheme(const char *name, const rgb_color initdata[], int index)
{
int i, j;

	// load ourselves up with the built in defaults
	for(i=j=0;i<NUM_COLORS;i++)
	{
		_colors[i].fg = initdata[j];
		_colors[i].bg = initdata[j+1];
		_colors[i].isBold = false;
		j += 2;
	}
	
	_colors[COLOR_BRACE_MATCHED].isBold = true;
	_colors[COLOR_BRACE_UNMATCHED].isBold = true;
	
	strcpy(fSchemeName, name);
	
	// now write ourselves to the config file
	SaveScheme(index);
}

/*
void c------------------------------() {}
*/

// load a scheme by it's index.
void ColorScheme::LoadScheme(int schemeNo)
{
char cfg_key[80];
int i;

	_FirstTimeInit();

	sprintf(cfg_key, "scheme%d_name", schemeNo);
	const char *name = settings->GetString(cfg_key, fSchemeName);
	maxcpy(fSchemeName, name, sizeof(fSchemeName) - 1);

	stat("Loading scheme %d: '%s'", schemeNo, fSchemeName);
	
	for(i=0;i<NUM_COLORS;i++)
	{
		GetColorConfigKey(schemeNo, i, cfg_key, "fg");
		_colors[i].fg = ColorFromHex(settings->GetString(cfg_key, ""));
		
		GetColorConfigKey(schemeNo, i, cfg_key, "bg");
		_colors[i].bg = ColorFromHex(settings->GetString(cfg_key, ""));
		
		GetColorConfigKey(schemeNo, i, cfg_key, "bold");
		_colors[i].isBold = settings->GetInt(cfg_key, 0);
	}

	fCurrentSchemeIndex = schemeNo;
	
	// update menu checkmark when changing active color scheme
	if (this == &CurrentColorScheme)
	{
		MainWindow->top.menubar->SetMarkedColorScheme(schemeNo);
		MainWindow->EditorColorsChanged();
	}
}


static void GetColorConfigKey(int schemeNo, int colorIndex, char *buffer, const char *suffix)
{
int i, j;
const char *name;
char *ptr;

	sprintf(buffer, "scheme%d_", schemeNo);
	
	name = ColorScheme::GetColorName(colorIndex);
	ptr = strchr(buffer, '\0');
	
	for(i=j=0;name[i];i++)
	{
		if (name[i] == '(' || name[i] == ')') continue;
		
		char ch = name[i];
		if (ch == ' ') ch = '_';
		else ch = tolower(ch);
		
		*(ptr++) = ch;
	}
	
	*(ptr++) = '_';
	strcpy(ptr, suffix);
}

// converts a hex string in format "#000042" into an rgb_color.
static rgb_color ColorFromHex(const char *hcol)
{
rgb_color result;

	// alpha was left unset; rgb_color's == compares it, and the preferences'
	// Revert button goes by that comparison
	result.alpha = 255;

	if (*hcol=='#') hcol++;
	
	if (strlen(hcol) < 6)
	{
		result.red = result.blue = 255;
		result.green = 0;
		return result;
	}
	
	result.red = ReadHexByte(hcol);
	result.green = ReadHexByte(hcol + 2);
	result.blue = ReadHexByte(hcol + 4);
	
	return result;
}

// converts a hex byte in form "A7" into a char
uchar ReadHexByte(const char *hbyte)
{
uchar hi, lo;

	hi = ReadHexDigit(*hbyte);
	lo = ReadHexDigit(*(hbyte + 1));
	
	return (hi << 4) | lo;
}

// reads a single hex digit and returns a value from 0-15
uchar ReadHexDigit(const char digit)
{
	if (digit >= '0' && digit <= '9')
	{
		return (digit - '0');
	}
	else if (digit >= 'A' && digit <= 'F')
	{
		return (digit - 'A') + 10;
	}
	else if (digit >= 'a' && digit <= 'f')
	{
		return (digit - 'a') + 10;
	}
	else
	{
		return 0;
	}
}

/*
void c------------------------------() {}
*/

// save the colors in the scheme, updating the color scheme they were loaded from.
void ColorScheme::SaveScheme()
{
	if (fCurrentSchemeIndex != -1)
		SaveScheme(fCurrentSchemeIndex);
}

// save the colors in the scheme into the config file as color scheme #"schemeNo".
// the current color scheme number is not changed.
void ColorScheme::SaveScheme(int schemeNo)
{
char cfg_key[80];
char hcol[16];
int i;

	stat("saving scheme %d: '%s'", schemeNo, fSchemeName);
	
	sprintf(cfg_key, "scheme%d_name", schemeNo);
	settings->SetString(cfg_key, fSchemeName);
	
	for(i=0;i<NUM_COLORS;i++)
	{
		GetColorConfigKey(schemeNo, i, cfg_key, "fg");
		ColorToHex(_colors[i].fg, hcol);
		settings->SetString(cfg_key, hcol);
		
		GetColorConfigKey(schemeNo, i, cfg_key, "bg");
		ColorToHex(_colors[i].bg, hcol);
		settings->SetString(cfg_key, hcol);
	}
	
	for(i=0;i<NUM_COLORS;i++)
	{
		GetColorConfigKey(schemeNo, i, cfg_key, "bold");
		settings->SetInt(cfg_key, _colors[i].isBold);
	}	
}

static void ColorToHex(rgb_color color, char *buffer)
{
	sprintf(buffer, "#%02x%02x%02x", color.red, color.green, color.blue);
}

/*
void c------------------------------() {}
*/

// returns whether or not the given colorscheme index is present
bool ColorScheme::SchemeExists(int index)
{
char cfg_key[80];
const char *str;

	GetColorConfigKey(index, 0, cfg_key, "fg");
	str = settings->GetString(cfg_key, NULL);
	
	if (str)
		return true;
	else
		return false;
}


// counts the current number of color schemes currently available
int ColorScheme::GetNumColorSchemes()
{
int i;

	for(i=0;;i++)
	{
		if (!SchemeExists(i))
			return i;
	}
}


// returns the number of colors in each scheme
int ColorScheme::GetNumColors()
{
	return NUM_COLORS;
}


// returns the name of the currently loaded color scheme.
const char *ColorScheme::GetSchemeName()
{
	return fSchemeName;
}


// returns the name of the color scheme at the given index, without loading it.
const char *ColorScheme::GetSchemeName(int schemeNo)
{
	if (SchemeExists(schemeNo))
	{
		char cfg_key[80];
		sprintf(cfg_key, "scheme%d_name", schemeNo);
		
		return settings->GetString(cfg_key, "untitled colorscheme");
	}
	else
	{
		return "invalid colorscheme";
	}
}

/*
void c------------------------------() {}
*/

// change the name of the currently-loaded scheme
void ColorScheme::SetSchemeName(const char *newName)
{
	maxcpy(fSchemeName, newName, sizeof(fSchemeName) - 1);
}


/*
void c------------------------------() {}
*/

// returns the index of the scheme currently loaded into the class,
// or -1 if no scheme has ever been loaded.
int ColorScheme::GetLoadedSchemeIndex()
{
	return fCurrentSchemeIndex;
}


// returns the name of a color given it's index.
const char *ColorScheme::GetColorName(int index)
{
	if (index >= 0 && index < NUM_COLORS)
		return color_names[index];
	else
		return "invalid color index";
}

/*
void c------------------------------() {}
*/

// delete a color scheme from the settings file
void ColorScheme::DeleteScheme(int schemeNo)
{
ColorScheme tempscheme;
int i, lastScheme, numSchemes;

	numSchemes = GetNumColorSchemes();
	
	if (schemeNo >= numSchemes || !SchemeExists(schemeNo))
		return;
	
	// delete the scheme by loading each scheme after it and moving them down a slot
	for(i=schemeNo;i<numSchemes-1;i++)
	{
		tempscheme.LoadScheme(i+1);
		tempscheme.SaveScheme(i);
	}
	
	// now the last scheme is a duplicate, get rid of it
	lastScheme = numSchemes - 1;

	for(i=0;i<NUM_COLORS;i++)
	{
		char cfg_key[80];
		
		GetColorConfigKey(lastScheme, i, cfg_key, "fg");
		settings->RemoveName(cfg_key);
		
		GetColorConfigKey(lastScheme, i, cfg_key, "bg");
		settings->RemoveName(cfg_key);
		
		GetColorConfigKey(lastScheme, i, cfg_key, "bold");
		settings->RemoveName(cfg_key);
	}
}

/*
void c------------------------------() {}
*/

rgb_color GetEditFGColor(int colorNum)
{
	return CurrentColorScheme._colors[colorNum].fg;
}
rgb_color GetEditBGColor(int colorNum)
{
	return CurrentColorScheme._colors[colorNum].bg;
}
bool GetColorBoldState(int colorNum)
{
	return CurrentColorScheme._colors[colorNum].isBold;
}

// identical to GetEditFGColor:
//	this is a clearer name for those colors such as selection that do not
//  have both an fg and bg and only have one color.
rgb_color GetEditColor(int colorNum)
{
	return CurrentColorScheme._colors[colorNum].fg;
}

// returns true on colors that use both fg and bg,
// and false on colors that use only the fg.
bool GetColorUsesBothColors(int colorNum)
{
	switch(colorNum)
	{
		case COLOR_SELECTION:
		case COLOR_TABLINE:
		case COLOR_TABLINE_ACTIVE:
		case COLOR_CURSOR:
			return false;
		
		default:
			return true;
	}
}

/*
void c------------------------------() {}
*/

void ColorScheme::SetFGColor(int index, rgb_color new_color)
{
	if (index >= 0 && index < MAX_COLORS)
		_colors[index].fg = new_color;

	if (this == &CurrentColorScheme && MainWindow)
		MainWindow->EditorColorsChanged();
}

void ColorScheme::SetBGColor(int index, rgb_color new_color)
{
	if (index >= 0 && index < MAX_COLORS)
		_colors[index].bg = new_color;

	if (this == &CurrentColorScheme && MainWindow)
		MainWindow->EditorColorsChanged();
}

void ColorScheme::SetBoldState(int index, bool boldState)
{
	if (index >= 0 && index < MAX_COLORS)
		_colors[index].isBold = boldState;
}







/***********************************************************************
Author: Zihan Chen (vczh)
Licensed under https://github.com/vczh-libraries/License
***********************************************************************/

#include "../../../Source/TUI/TUI.h"
#include <limits>

using namespace vl;
using namespace vl::presentation;
using namespace vl::collections;
using namespace vl::console;

struct SetForegroundColorCommand
{
	TuiColor						color = { 255, 255, 255 };
};

struct SetBackgroundColorCommand
{
	Nullable<TuiColor>				color;
};

struct DrawLineVCommand
{
	TuiMergeableGlyph				glyph = TuiMergeableGlyph::ThinLine;
	vint							x = 0;
	vint							y1 = 0;
	vint							y2 = 0;
};

struct DrawLineHCommand
{
	TuiMergeableGlyph				glyph = TuiMergeableGlyph::ThinLine;
	vint							x1 = 0;
	vint							x2 = 0;
	vint							y = 0;
};

struct DrawRectCommand
{
	TuiMergeableGlyph				glyph = TuiMergeableGlyph::ThinLine;
	TuiRectCorner					corner = TuiRectCorner::Sharp;
	vint							x1 = 0;
	vint							y1 = 0;
	vint							x2 = 0;
	vint							y2 = 0;
};

struct ClearRectCommand
{
	TuiColor						backgroundColor = { 0, 0, 0 };
	vint							x1 = 0;
	vint							y1 = 0;
	vint							x2 = 0;
	vint							y2 = 0;
};

struct TypeCommand
{
	vint							x = 0;
	vint							y = 0;
	U32String						text;
};

using PaintingCommand = Variant<
	SetForegroundColorCommand,
	SetBackgroundColorCommand,
	DrawLineVCommand,
	DrawLineHCommand,
	DrawRectCommand,
	ClearRectCommand,
	TypeCommand
	>;

struct Information
{
	List<U32String>					content;
};

struct CommandRecord
{
	PaintingCommand					command;
	U32String						text;
};

enum class PlaygroundPage
{
	Canvas,
	History,
};

struct DrawingOperation
{
	bool							dragging = false;
	vint							anchorX = 0;
	vint							anchorY = 0;
	vint							endX = 0;
	vint							endY = 0;
};

struct PlaygroundState
{
	Nullable<Information>			information;
	U32String						typingCommand;
#if defined VCZH_WCHAR_UTF16
	wchar_t							pendingHighSurrogate = 0;
#endif
	List<CommandRecord>			commands;
	bool							cursorVisible = true;
	PlaygroundPage					page = PlaygroundPage::Canvas;
	bool							shapesMenu = false;
	vint							selectedShape = 0;
	vint							highlightedShape = 0;
	Nullable<DrawingOperation>		drawing;
	vint							historyTop = -1;
	vint							wheelRemainder = 0;
};

struct PositionedScalar
{
	char32_t						code = 0;
	vint							x = 0;
	vint							row = 0;
};

struct WrappedText
{
	List<PositionedScalar>			scalars;
	List<vint>						rowWidths;
	vint							cursorX = 0;
	vint							cursorRow = 0;
};

bool IsScalar(char32_t code)
{
	return code <= 0x10FFFF && !(code >= 0xD800 && code <= 0xDFFF);
}

char32_t ToUpperAscii(char32_t code)
{
	return code >= U'a' && code <= U'z' ? code - U'a' + U'A' : code;
}

bool EqualsAsciiIgnoreCase(const U32String& text, const char32_t* expected)
{
	auto expectedText = U32String::Unmanaged(expected);
	if (text.Length() != expectedText.Length()) return false;
	for (vint i = 0; i < text.Length(); i++)
	{
		if (ToUpperAscii(text[i]) != ToUpperAscii(expectedText[i])) return false;
	}
	return true;
}

vint FindScalar(const U32String& text, char32_t code, vint begin = 0)
{
	for (vint i = begin; i < text.Length(); i++)
	{
		if (text[i] == code) return i;
	}
	return -1;
}

template<vint Count>
bool SplitExactSpaces(const U32String& text, U32String(&tokens)[Count])
{
	vint token = 0;
	vint begin = 0;
	for (vint i = 0; i <= text.Length(); i++)
	{
		if (i == text.Length() || text[i] == U' ')
		{
			if (i == begin || token >= Count) return false;
			tokens[token++] = text.Sub(begin, i - begin);
			begin = i + 1;
		}
	}
	return token == Count;
}

bool TryParseColor(const U32String& text, TuiColor& color)
{
	if (text.Length() != 6) return false;
	vuint8_t values[6];
	for (vint i = 0; i < 6; i++)
	{
		auto code = ToUpperAscii(text[i]);
		if (code >= U'0' && code <= U'9')
		{
			values[i] = (vuint8_t)(code - U'0');
		}
		else if (code >= U'A' && code <= U'F')
		{
			values[i] = (vuint8_t)(code - U'A' + 10);
		}
		else
		{
			return false;
		}
	}
	color =
	{
		(vuint8_t)(values[0] * 16 + values[1]),
		(vuint8_t)(values[2] * 16 + values[3]),
		(vuint8_t)(values[4] * 16 + values[5]),
	};
	return true;
}

bool TryParseVint(const U32String& text, vint& value)
{
	if (text.Length() == 0) return false;
	auto negative = text[0] == U'-';
	auto begin = negative ? 1 : 0;
	if (begin == text.Length()) return false;

	auto positiveLimit = (vuint64_t)std::numeric_limits<vint>::max();
	auto limit = negative ? positiveLimit + 1 : positiveLimit;
	vuint64_t parsed = 0;
	for (vint i = begin; i < text.Length(); i++)
	{
		auto code = text[i];
		if (code < U'0' || code > U'9') return false;
		auto digit = (vuint64_t)(code - U'0');
		if (parsed > (limit - digit) / 10) return false;
		parsed = parsed * 10 + digit;
	}

	if (negative)
	{
		value = parsed == positiveLimit + 1
			? std::numeric_limits<vint>::min()
			: -(vint)parsed;
	}
	else
	{
		value = (vint)parsed;
	}
	return true;
}

bool TryParseLineFormat(const U32String& text, TuiMergeableGlyph& glyph)
{
	if (EqualsAsciiIgnoreCase(text, U"THIN"))
	{
		glyph = TuiMergeableGlyph::ThinLine;
		return true;
	}
	if (EqualsAsciiIgnoreCase(text, U"THICK"))
	{
		glyph = TuiMergeableGlyph::ThickLine;
		return true;
	}
	if (EqualsAsciiIgnoreCase(text, U"DOUBLE"))
	{
		glyph = TuiMergeableGlyph::DoubleLine;
		return true;
	}
	return false;
}

bool TryParseCommand(const U32String& text, PaintingCommand& command, U32String& reason)
{
	auto firstSpace = FindScalar(text, U' ');
	auto name = firstSpace == -1 ? text : text.Sub(0, firstSpace);

	if (EqualsAsciiIgnoreCase(name, U"FC"))
	{
		U32String tokens[2];
		TuiColor color;
		if (!SplitExactSpaces(text, tokens) || !TryParseColor(tokens[1], color))
		{
			reason = U"Expected FC RRGGBB.";
			return false;
		}
		command = SetForegroundColorCommand{ .color = color };
		return true;
	}

	if (EqualsAsciiIgnoreCase(name, U"BC"))
	{
		U32String tokens[2];
		if (!SplitExactSpaces(text, tokens))
		{
			reason = U"Expected BC CLEAR or BC RRGGBB.";
			return false;
		}
		SetBackgroundColorCommand parsed;
		if (!EqualsAsciiIgnoreCase(tokens[1], U"CLEAR"))
		{
			TuiColor color;
			if (!TryParseColor(tokens[1], color))
			{
				reason = U"Expected BC CLEAR or BC RRGGBB.";
				return false;
			}
			parsed.color = color;
		}
		command = parsed;
		return true;
	}

	if (EqualsAsciiIgnoreCase(name, U"LINEV"))
	{
		U32String tokens[5];
		DrawLineVCommand parsed;
		if (!SplitExactSpaces(text, tokens) ||
			!TryParseLineFormat(tokens[1], parsed.glyph) ||
			!TryParseVint(tokens[2], parsed.x) ||
			!TryParseVint(tokens[3], parsed.y1) ||
			!TryParseVint(tokens[4], parsed.y2))
		{
			reason = U"Expected LINEV THIN|THICK|DOUBLE x y1 y2 using signed vint coordinates.";
			return false;
		}
		if (parsed.y1 > parsed.y2)
		{
			reason = U"LINEV requires y1 <= y2.";
			return false;
		}
		command = parsed;
		return true;
	}

	if (EqualsAsciiIgnoreCase(name, U"LINEH"))
	{
		U32String tokens[5];
		DrawLineHCommand parsed;
		if (!SplitExactSpaces(text, tokens) ||
			!TryParseLineFormat(tokens[1], parsed.glyph) ||
			!TryParseVint(tokens[2], parsed.x1) ||
			!TryParseVint(tokens[3], parsed.x2) ||
			!TryParseVint(tokens[4], parsed.y))
		{
			reason = U"Expected LINEH THIN|THICK|DOUBLE x1 x2 y using signed vint coordinates.";
			return false;
		}
		if (parsed.x1 > parsed.x2)
		{
			reason = U"LINEH requires x1 <= x2.";
			return false;
		}
		command = parsed;
		return true;
	}

	if (EqualsAsciiIgnoreCase(name, U"RECT"))
	{
		U32String tokens[6];
		DrawRectCommand parsed;
		if (!SplitExactSpaces(text, tokens) ||
			!TryParseVint(tokens[2], parsed.x1) ||
			!TryParseVint(tokens[3], parsed.y1) ||
			!TryParseVint(tokens[4], parsed.x2) ||
			!TryParseVint(tokens[5], parsed.y2))
		{
			reason = U"Expected RECT THIN|THICK|DOUBLE|ROUND x1 y1 x2 y2 using signed vint coordinates.";
			return false;
		}
		if (EqualsAsciiIgnoreCase(tokens[1], U"ROUND"))
		{
			parsed.glyph = TuiMergeableGlyph::ThinLine;
			parsed.corner = TuiRectCorner::Round;
		}
		else if (!TryParseLineFormat(tokens[1], parsed.glyph))
		{
			reason = U"Expected RECT THIN|THICK|DOUBLE|ROUND x1 y1 x2 y2 using signed vint coordinates.";
			return false;
		}
		if (parsed.x1 >= parsed.x2 || parsed.y1 >= parsed.y2)
		{
			reason = U"RECT requires x1 < x2 and y1 < y2.";
			return false;
		}
		command = parsed;
		return true;
	}

	if (EqualsAsciiIgnoreCase(name, U"CLEAR"))
	{
		U32String tokens[6];
		ClearRectCommand parsed;
		if (!SplitExactSpaces(text, tokens) ||
			!TryParseColor(tokens[1], parsed.backgroundColor) ||
			!TryParseVint(tokens[2], parsed.x1) ||
			!TryParseVint(tokens[3], parsed.y1) ||
			!TryParseVint(tokens[4], parsed.x2) ||
			!TryParseVint(tokens[5], parsed.y2))
		{
			reason = U"Expected CLEAR RRGGBB x1 y1 x2 y2 using signed vint coordinates.";
			return false;
		}
		if (parsed.x1 > parsed.x2 || parsed.y1 > parsed.y2)
		{
			reason = U"CLEAR requires x1 <= x2 and y1 <= y2.";
			return false;
		}
		command = parsed;
		return true;
	}

	if (EqualsAsciiIgnoreCase(name, U"TYPE"))
	{
		auto secondSpace = firstSpace == -1 ? -1 : FindScalar(text, U' ', firstSpace + 1);
		auto colon = secondSpace == -1 ? -1 : FindScalar(text, U':', secondSpace + 1);
		TypeCommand parsed;
		if (firstSpace == -1 ||
			secondSpace == -1 ||
			firstSpace + 1 == secondSpace ||
			colon == -1 ||
			secondSpace + 1 == colon ||
			FindScalar(text.Sub(secondSpace + 1, colon - secondSpace - 1), U' ') != -1 ||
			!TryParseVint(text.Sub(firstSpace + 1, secondSpace - firstSpace - 1), parsed.x) ||
			!TryParseVint(text.Sub(secondSpace + 1, colon - secondSpace - 1), parsed.y))
		{
			reason = U"Expected TYPE x y:TEXT using signed vint coordinates.";
			return false;
		}
		parsed.text = text.Sub(colon + 1, text.Length() - colon - 1);
		if (parsed.text.Length() == 0)
		{
			reason = U"TYPE requires at least one payload scalar after ':'.";
			return false;
		}
		command = parsed;
		return true;
	}

	reason = text.Length() == 0
		? U32String(U"Command cannot be empty.")
		: U32String(U"Unknown command.");
	return false;
}

Information CreateHelpInformation()
{
	Information information;
	information.content.Add(U32String(U"FC RRGGBB"));
	information.content.Add(U32String(U"BC CLEAR|RRGGBB"));
	information.content.Add(U32String(U"LINEV THIN|THICK|DOUBLE x y1 y2"));
	information.content.Add(U32String(U"LINEH THIN|THICK|DOUBLE x1 x2 y"));
	information.content.Add(U32String(U"RECT THIN|THICK|DOUBLE|ROUND x1 y1 x2 y2"));
	information.content.Add(U32String(U"CLEAR RRGGBB x1 y1 x2 y2"));
	information.content.Add(U32String(U"TYPE x y:TEXT"));
	information.content.Add(U32String(U"HELP"));
	information.content.Add(U32String(U"EXIT"));
	return information;
}

WrappedText WrapText(const U32String& text, vint width, bool includeCursor)
{
	WrappedText result;
	vint x = 0;
	vint row = 0;
	for (vint i = 0; i < text.Length(); i++)
	{
		auto code = text[i];
		auto charWidth = TUI::MeasureChar(code);
		if ((charWidth != 1 && charWidth != 2) || charWidth > width) continue;
		if (x + charWidth > width)
		{
			result.rowWidths.Add(x);
			x = 0;
			row++;
		}
		result.scalars.Add({ .code = code, .x = x, .row = row });
		x += charWidth;
	}
	result.rowWidths.Add(x);
	if (includeCursor)
	{
		if (x == width)
		{
			result.cursorX = 0;
			result.cursorRow = row + 1;
			result.rowWidths.Add(0);
		}
		else
		{
			result.cursorX = x;
			result.cursorRow = row;
		}
	}
	return result;
}

void ApplyCommand(TuiPixel* buffer, vint width, vint height, const PaintingCommand& command, TuiColor& foreground, Nullable<TuiColor>& background)
{
	if (auto parsed = command.TryGet<SetForegroundColorCommand>())
	{
		foreground = parsed->color;
	}
	else if (auto parsed = command.TryGet<SetBackgroundColorCommand>())
	{
		background = parsed->color;
	}
	else if (auto parsed = command.TryGet<DrawLineVCommand>())
	{
		TuiLineOptions options;
		options.glyph = parsed->glyph;
		options.foregroundColor = foreground;
		options.backgroundColor = background;
		TUI::DrawLineV(buffer, width, height, options, parsed->x, parsed->y1, parsed->y2);
	}
	else if (auto parsed = command.TryGet<DrawLineHCommand>())
	{
		TuiLineOptions options;
		options.glyph = parsed->glyph;
		options.foregroundColor = foreground;
		options.backgroundColor = background;
		TUI::DrawLineH(buffer, width, height, options, parsed->x1, parsed->x2, parsed->y);
	}
	else if (auto parsed = command.TryGet<DrawRectCommand>())
	{
		TuiRectOptions options;
		options.glyph = parsed->glyph;
		options.foregroundColor = foreground;
		options.backgroundColor = background;
		options.corner = parsed->corner;
		TUI::DrawRect(buffer, width, height, options, parsed->x1, parsed->y1, parsed->x2, parsed->y2);
	}
	else if (auto parsed = command.TryGet<ClearRectCommand>())
	{
		TUI::Clear(buffer, width, height, parsed->backgroundColor, parsed->x1, parsed->y1, parsed->x2, parsed->y2);
	}
	else if (auto parsed = command.TryGet<TypeCommand>())
	{
		auto x = parsed->x;
		for (vint i = 0; i < parsed->text.Length(); i++)
		{
			auto code = parsed->text[i];
			auto charWidth = TUI::MeasureChar(code);
			if (charWidth != 1 && charWidth != 2) continue;
			if (x >= 0 && x < width && parsed->y >= 0 && parsed->y < height)
			{
				TuiPrintOptions options;
				options.foregroundColor = foreground;
				options.backgroundColor = buffer[parsed->y * width + x].backgroundColor;
				TUI::PrintChar(buffer, width, height, options, code, x, parsed->y);
			}
			if (x > std::numeric_limits<vint>::max() - charWidth) break;
			x += charWidth;
		}
	}
}

U32String ShapeName(vint shape)
{
	const char32_t* names[] = {
		U"LINEV THIN", U"LINEV THICK", U"LINEV DOUBLE",
		U"LINEH THIN", U"LINEH THICK", U"LINEH DOUBLE",
		U"RECT THIN", U"RECT THICK", U"RECT DOUBLE", U"RECT ROUND",
	};
	return U32String::Unmanaged(names[shape]);
}

Nullable<CommandRecord> CurrentShape(const PlaygroundState& state)
{
	if (!state.drawing || !state.drawing.Value().dragging) return {};
	auto&& drawing = state.drawing.Value();
	auto x1 = drawing.anchorX < drawing.endX ? drawing.anchorX : drawing.endX;
	auto y1 = drawing.anchorY < drawing.endY ? drawing.anchorY : drawing.endY;
	auto x2 = drawing.anchorX > drawing.endX ? drawing.anchorX : drawing.endX;
	auto y2 = drawing.anchorY > drawing.endY ? drawing.anchorY : drawing.endY;
	auto text = ShapeName(state.selectedShape);
	if (state.selectedShape < 3)
	{
		text += wtou32(L" " + itow(drawing.anchorX) + L" " + itow(y1) + L" " + itow(y2));
	}
	else if (state.selectedShape < 6)
	{
		text += wtou32(L" " + itow(x1) + L" " + itow(x2) + L" " + itow(drawing.anchorY));
	}
	else
	{
		if (x1 == x2 || y1 == y2) return {};
		text += wtou32(L" " + itow(x1) + L" " + itow(y1) + L" " + itow(x2) + L" " + itow(y2));
	}
	CommandRecord record;
	record.text = text;
	U32String reason;
	CHECK_ERROR(TryParseCommand(text, record.command, reason), L"TuiPlayground generated an invalid shape command.");
	return record;
}

void ReplayCommands(TuiPixel* buffer, vint width, vint height, const PlaygroundState& state)
{
	auto foreground = TuiColor{ 255, 255, 255 };
	Nullable<TuiColor> background;
	for (auto&& record : state.commands)
	{
		ApplyCommand(buffer, width, height, record.command, foreground, background);
	}
	if (auto preview = CurrentShape(state))
	{
		ApplyCommand(buffer, width, height, preview.Value().command, foreground, background);
	}
}

struct PlaygroundLayout
{
	vint							width = 0;
	vint							height = 0;
	vint							boxTop = 0;
	vint							boxHeight = 0;
	vint							paperWidth = 0;
	vint							paperHeight = 0;
	vint							menuX = 0;
	vint							menuWidth = 0;
	vint							menuRows = 0;
	vint							menuFirst = 0;
	WrappedText						input;
};

PlaygroundLayout GetLayout(const PlaygroundState& state)
{
	PlaygroundLayout layout;
	layout.width = TUI::GetBufferWidth();
	layout.height = TUI::GetBufferHeight();
	layout.input = WrapText(state.typingCommand, layout.width, true);
	auto available = layout.height > 0 ? layout.height - 1 : 0;
	layout.boxHeight = layout.input.rowWidths.Count() < available ? layout.input.rowWidths.Count() : available;
	layout.boxTop = layout.height - layout.boxHeight;
	layout.paperWidth = layout.width > 2 ? layout.width - 2 : 0;
	layout.paperHeight = layout.boxTop > 3 ? layout.boxTop - 3 : 0;
	layout.menuWidth = layout.width < 14 ? layout.width : 14;
	layout.menuX = layout.width - layout.menuWidth < 17 ? layout.width - layout.menuWidth : 17;
	layout.menuRows = available < 10 ? available : 10;
	layout.menuFirst = state.highlightedShape >= layout.menuRows && layout.menuRows > 0 ? state.highlightedShape - layout.menuRows + 1 : 0;
	return layout;
}

WrappedText WrapHistory(const PlaygroundState& state, vint width)
{
	WrappedText result;
	for (auto&& record : state.commands)
	{
		auto wrapped = WrapText(record.text, width, false);
		auto row = result.rowWidths.Count();
		for (auto scalar : wrapped.scalars)
		{
			scalar.row += row;
			result.scalars.Add(scalar);
		}
		for (auto rowWidth : wrapped.rowWidths) result.rowWidths.Add(rowWidth);
	}
	return result;
}

vint HistoryLimit(const WrappedText& history, vint height)
{
	auto rows = height > 0 ? height - 1 : 0;
	return history.rowWidths.Count() > rows ? history.rowWidths.Count() - rows : 0;
}

void DrawInformationOverlay(TuiPixel* buffer, vint width, vint height, const Information& information)
{
	if (width <= 0 || height <= 0) return;

	auto drawBorder = width >= 2 && height >= 2;
	auto textWidth = drawBorder ? width - 2 : width;
	List<WrappedText> wrappedItems;
	vint textRows = 0;
	if (textWidth > 0)
	{
		for (auto&& item : information.content)
		{
			auto wrapped = WrapText(item, textWidth, false);
			textRows += wrapped.rowWidths.Count();
			wrappedItems.Add(std::move(wrapped));
		}
	}
	auto overlayHeight = drawBorder
		? (textRows > height - 2 ? height : textRows + 2)
		: (textRows > height ? height : textRows);
	auto top = (height - overlayHeight) / 2;
	auto contentTop = top;
	auto contentRows = overlayHeight;

	if (drawBorder)
	{
		TuiRectOptions options;
		options.glyph = TuiMergeableGlyph::ThinLine;
		options.foregroundColor = { 255, 96, 96 };
		options.corner = TuiRectCorner::Round;
		TUI::DrawRect(buffer, width, height, options, 0, top, width - 1, top + overlayHeight - 1);
		contentTop++;
		contentRows -= 2;
	}

	TuiPrintOptions options;
	options.foregroundColor = { 255, 192, 192 };
	vint rowOffset = 0;
	for (auto&& wrapped : wrappedItems)
	{
		for (auto&& scalar : wrapped.scalars)
		{
			auto logicalRow = rowOffset + scalar.row;
			if (logicalRow >= contentRows) continue;
			auto centered = (textWidth - wrapped.rowWidths[scalar.row]) / 2;
			auto x = (drawBorder ? 1 : 0) + centered + scalar.x;
			auto y = contentTop + logicalRow;
			options.backgroundColor = buffer[y * width + x].backgroundColor;
			TUI::PrintChar(buffer, width, height, options, scalar.code, x, y);
		}
		rowOffset += wrapped.rowWidths.Count();
	}
}

class PlaygroundCallback : public ITuiCallback
{
private:
	void ResetPendingUnit()
	{
#if defined VCZH_WCHAR_UTF16
		state.pendingHighSurrogate = 0;
#endif
	}

	Nullable<char32_t> DecodeNativeUnit(wchar_t codeUnit)
	{
#if defined VCZH_WCHAR_UTF16
		auto isHigh = codeUnit >= (wchar_t)0xD800 && codeUnit <= (wchar_t)0xDBFF;
		auto isLow = codeUnit >= (wchar_t)0xDC00 && codeUnit <= (wchar_t)0xDFFF;
		if (state.pendingHighSurrogate)
		{
			if (isLow)
			{
				wchar_t units[] = { state.pendingHighSurrogate, codeUnit };
				char32_t code = 0;
				state.pendingHighSurrogate = 0;
				CHECK_ERROR(encoding::UtfConversion<wchar_t>::To32(units, 2, code) == 2, L"TuiPlayground failed to decode a valid UTF-16 surrogate pair.");
				return code;
			}
			state.pendingHighSurrogate = 0;
		}
		if (isHigh)
		{
			state.pendingHighSurrogate = codeUnit;
			return {};
		}
		if (isLow) return {};
#endif
		wchar_t units[] = { codeUnit };
		char32_t code = 0;
		if (encoding::UtfConversion<wchar_t>::To32(units, 1, code) != 1 || !IsScalar(code)) return {};
		return code;
	}

	void ClearTypingCommand()
	{
		state.typingCommand = {};
		ResetPendingUnit();
	}

	bool SubmitCommand()
	{
		auto original = state.typingCommand;
		ClearTypingCommand();
		state.cursorVisible = true;
		if (EqualsAsciiIgnoreCase(original, U"HELP"))
		{
			state.information = CreateHelpInformation();
			return true;
		}
		if (EqualsAsciiIgnoreCase(original, U"EXIT"))
		{
			TUI::Stop();
			return false;
		}
		PaintingCommand command;
		U32String reason;
		if (TryParseCommand(original, command, reason))
		{
			state.commands.Add({ .command = std::move(command), .text = original });
		}
		else
		{
			Information information;
			information.content.Add(U32String::Unmanaged(U"ERROR, original command:") + original);
			information.content.Add(U32String::Unmanaged(U"REASON:") + reason);
			state.information = std::move(information);
		}
		return true;
	}


	void CancelDrawing()
	{
		state.drawing.Reset();
		ResetPendingUnit();
		state.cursorVisible = true;
	}

	void Navigate(vint tab)
	{
		CancelDrawing();
		state.shapesMenu = tab == 2;
		if (state.shapesMenu) state.highlightedShape = state.selectedShape;
		else state.page = tab == 0 ? PlaygroundPage::Canvas : PlaygroundPage::History;
	}

	void AcceptShape()
	{
		state.selectedShape = state.highlightedShape;
		state.shapesMenu = false;
		state.page = PlaygroundPage::Canvas;
		state.drawing = DrawingOperation{};
		ResetPendingUnit();
	}

	void SetEndpoint(const WindowMouseInfo& info, const PlaygroundLayout& layout)
	{
		auto drawing = state.drawing.Value();
		drawing.endX = info.x < 1 ? 0 : info.x > layout.paperWidth ? layout.paperWidth - 1 : info.x - 1;
		drawing.endY = info.y < 2 ? 0 : info.y > layout.paperHeight + 1 ? layout.paperHeight - 1 : info.y - 2;
		state.drawing = drawing;
	}

	void Redraw()
	{
		auto layout = GetLayout(state);
		auto width = layout.width;
		auto height = layout.height;
		if (width <= 0 || height <= 0) return;
		TUI::Clear({ 0, 0, 0 }, 0, 0, width - 1, height - 1);

		auto history = WrapHistory(state, width);
		auto historyLimit = HistoryLimit(history, height);
		if (state.historyTop >= historyLimit) state.historyTop = -1;
		if (state.page == PlaygroundPage::History)
		{
			auto top = state.historyTop < 0 ? historyLimit : state.historyTop;
			for (auto&& scalar : history.scalars)
			{
				if (scalar.row >= top && scalar.row < top + height - 1)
				{
					TUI::PrintChar({}, scalar.code, scalar.x, scalar.row - top + 1);
				}
			}
		}
		else
		{
			if (layout.paperWidth > 0 && layout.paperHeight > 0)
			{
				Array<TuiPixel> paper(layout.paperWidth * layout.paperHeight);
				ReplayCommands(&paper[0], layout.paperWidth, layout.paperHeight, state);
				if (state.information)
				{
					DrawInformationOverlay(&paper[0], layout.paperWidth, layout.paperHeight, state.information.Value());
				}
				auto buffer = TUI::GetBuffer();
				for (vint y = 0; y < layout.paperHeight; y++)
				for (vint x = 0; x < layout.paperWidth; x++)
				{
					buffer[(y + 2) * width + x + 1] = paper[y * layout.paperWidth + x];
				}
			}
			if (width >= 2 && layout.boxTop >= 3)
			{
				TuiRectOptions options;
				options.glyph = TuiMergeableGlyph::DoubleLine;
				TUI::DrawRect(options, 0, 1, width - 1, layout.boxTop - 1);
			}
			if (layout.boxHeight > 0)
			{
				TUI::Clear({ 64, 64, 64 }, 0, layout.boxTop, width - 1, height - 1);
				TuiPrintOptions inputOptions;
				inputOptions.foregroundColor = state.drawing ? TuiColor{ 128, 128, 128 } : TuiColor{ 255, 255, 255 };
				inputOptions.backgroundColor = { 64, 64, 64 };
				auto firstRow = layout.input.rowWidths.Count() - layout.boxHeight;
				for (auto&& scalar : layout.input.scalars)
				{
					if (scalar.row < firstRow) continue;
					TUI::PrintChar(inputOptions, scalar.code, scalar.x, layout.boxTop + scalar.row - firstRow);
				}
				if (!state.information && !state.shapesMenu && !state.drawing && state.cursorVisible && layout.input.cursorRow >= firstRow)
				{
					TUI::PrintChar(inputOptions, U'\u2588', layout.input.cursorX, layout.boxTop + layout.input.cursorRow - firstRow);
				}
			}
		}

		const char32_t* headers[] = { U" Canvas ", U" History ", U" Shapes " };
		vint x = 0;
		for (vint tab = 0; tab < 3; tab++)
		{
			auto selected = state.shapesMenu ? tab == 2 : tab == (state.page == PlaygroundPage::Canvas ? 0 : 1);
			TuiPrintOptions options;
			options.backgroundColor = selected ? TuiColor{ 0, 0, 128 } : TuiColor{ 128, 128, 128 };
			auto text = U32String::Unmanaged(headers[tab]);
			for (vint i = 0; i < text.Length() && x < width; i++, x++)
			{
				TUI::PrintChar(options, text[i], x, 0);
			}
		}
		if (state.shapesMenu && layout.menuWidth > 0)
		{
			for (vint row = 0; row < layout.menuRows; row++)
			{
				auto index = layout.menuFirst + row;
				TuiPrintOptions options;
				options.backgroundColor = index == state.highlightedShape ? TuiColor{ 0, 0, 128 } : TuiColor{ 128, 128, 128 };
				TUI::Clear(options.backgroundColor, layout.menuX, row + 1, layout.menuX + layout.menuWidth - 1, row + 1);
				auto text = U32String::Unmanaged(U" ") + ShapeName(index);
				for (vint i = 0; i < text.Length() && i < layout.menuWidth; i++)
				{
					TUI::PrintChar(options, text[i], layout.menuX + i, row + 1);
				}
			}
		}
		TUI::RenderBuffer();
	}

public:
	PlaygroundState					state;

	void Starting() override
	{
		ResetPendingUnit();
		state.cursorVisible = true;
		TUI::StartTimer(500);
		Redraw();
	}

	void Stopping() override
	{
		TUI::StopTimer();
		ResetPendingUnit();
	}

	void BufferSizeChanged() override
	{
		CancelDrawing();
		Redraw();
	}


	void KeyDown(const NativeWindowKeyInfo& info) override
	{
		if (state.information) return;
		if (info.code == VKEY::KEY_TAB)
		{
			auto current = state.shapesMenu ? 2 : state.page == PlaygroundPage::Canvas ? 0 : 1;
			Navigate((current + (info.shift ? 2 : 1)) % 3);
			Redraw();
		}
		else if (state.shapesMenu && (info.code == VKEY::KEY_UP || info.code == VKEY::KEY_DOWN))
		{
			auto next = state.highlightedShape + (info.code == VKEY::KEY_UP ? -1 : 1);
			state.highlightedShape = next < 0 ? 0 : next > 9 ? 9 : next;
			Redraw();
		}
	}

	void Char(const NativeWindowCharInfo& info) override
	{
		// Tab is entirely handled by KeyDown, even after it changed the active page.
		if (info.code == L'\t') return;
		if (state.information)
		{
			ResetPendingUnit();
			if (info.code == L'\r' || info.code == L'\n')
			{
				state.information.Reset();
				state.cursorVisible = true;
				Redraw();
			}
			return;
		}
		if (state.shapesMenu)
		{
			ResetPendingUnit();
			if (info.code == L'\r' || info.code == L'\n') AcceptShape();
			else if (info.code == 0x1B) state.shapesMenu = false;
			else return;
			Redraw();
			return;
		}
		if (state.drawing)
		{
			ResetPendingUnit();
			if (info.code == 0x1B)
			{
				CancelDrawing();
				Redraw();
			}
			return;
		}
		if (state.page != PlaygroundPage::Canvas)
		{
			ResetPendingUnit();
			return;
		}
		auto decoded = DecodeNativeUnit(info.code);
		if (!decoded) return;
		auto code = decoded.Value();
		if (code == U'\r' || code == U'\n')
		{
			if (SubmitCommand()) Redraw();
			return;
		}
		if (code == U'\b' || code == U'\x7F')
		{
			if (state.typingCommand.Length() > 0)
			{
				state.typingCommand = state.typingCommand.Sub(0, state.typingCommand.Length() - 1);
			}
		}
		else
		{
			auto charWidth = TUI::MeasureChar(code);
			if (charWidth != 1 && charWidth != 2) return;
			state.typingCommand += U32String::FromChar(code);
		}
		state.cursorVisible = true;
		Redraw();
	}

	void MouseDown(NativeMouseButton button, const WindowMouseInfo& info) override
	{
		if (button != NativeMouseButton::Left || state.information) return;
		auto layout = GetLayout(state);
		if (info.x < 0 || info.y < 0 || info.x >= layout.width || info.y >= layout.height) return;
		if (info.y == 0 && info.x < 25)
		{
			auto tab = info.x < 8 ? 0 : info.x < 17 ? 1 : 2;
			if (tab == 2 && state.shapesMenu) state.shapesMenu = false;
			else Navigate(tab);
			Redraw();
			return;
		}
		if (state.shapesMenu)
		{
			if (info.x >= layout.menuX && info.x < layout.menuX + layout.menuWidth && info.y >= 1 && info.y <= layout.menuRows)
			{
				state.highlightedShape = layout.menuFirst + info.y - 1;
				AcceptShape();
			}
			else state.shapesMenu = false;
			Redraw();
			return;
		}
		if (!state.drawing || state.drawing.Value().dragging || layout.paperWidth <= 0 || layout.paperHeight <= 0) return;
		if (info.x < 1 || info.x > layout.paperWidth || info.y < 2 || info.y > layout.paperHeight + 1) return;
		auto drawing = state.drawing.Value();
		drawing.dragging = true;
		drawing.anchorX = info.x - 1;
		drawing.anchorY = info.y - 2;
		state.drawing = drawing;
		SetEndpoint(info, layout);
		Redraw();
	}

	void MouseDoubleClick(NativeMouseButton button, const WindowMouseInfo& info) override
	{
		MouseDown(button, info);
	}

	void MouseMove(const WindowMouseInfo& info) override
	{
		if (state.information || !state.drawing || !state.drawing.Value().dragging) return;
		if (!info.left) CancelDrawing();
		else SetEndpoint(info, GetLayout(state));
		Redraw();
	}

	void MouseUp(NativeMouseButton button, const WindowMouseInfo& info) override
	{
		if (button != NativeMouseButton::Left || state.information || !state.drawing || !state.drawing.Value().dragging) return;
		SetEndpoint(info, GetLayout(state));
		if (auto record = CurrentShape(state))
		{
			state.commands.Add(record.Value());
			CancelDrawing();
		}
		else state.drawing = DrawingOperation{};
		Redraw();
	}

	void MouseVerticalWheel(const WindowMouseInfo& info) override
	{
		if (state.information || state.shapesMenu || state.page != PlaygroundPage::History) return;
		auto layout = GetLayout(state);
		if (info.x < 0 || info.x >= layout.width || info.y < 1 || info.y >= layout.height) return;
		auto partial = state.wheelRemainder + info.wheel % 120;
		auto rows = (info.wheel / 120 + partial / 120) * 3;
		state.wheelRemainder = partial % 120;
		auto history = WrapHistory(state, layout.width);
		auto limit = HistoryLimit(history, layout.height);
		auto top = state.historyTop < 0 ? limit : state.historyTop;
		// Saturate before subtracting to accept every signed wheel value.
		top = rows > 0 && rows > top ? 0 : rows < 0 && -rows > limit - top ? limit : top - rows;
		state.historyTop = top == limit ? -1 : top;
		Redraw();
	}

	void Timer() override
	{
		state.cursorVisible = !state.cursorVisible;
		Redraw();
	}
};

#if !defined VCZH_TUI_PLAYGROUND_TEST
vint RunPlayground()
{
	PlaygroundCallback callback;
	CHECK_ERROR(TUI::InstallListener(&callback), L"TuiPlayground failed to install its callback.");
	try
	{
		TUI::Start({});
	}
	catch (...)
	{
		TUI::UninstallListener(&callback);
		throw;
	}
	CHECK_ERROR(TUI::UninstallListener(&callback), L"TuiPlayground failed to uninstall its callback.");
	return 0;
}
#endif

#if !defined VCZH_TUI_PLAYGROUND_TEST
#if defined VCZH_MSVC
int wmain()
#elif defined VCZH_GCC
int main()
#endif
{
	vint result = 1;
	try
	{
		result = RunPlayground();
	}
	catch (const Exception& exception)
	{
		Console::WriteLine(L"Error: " + exception.Message());
	}
	catch (const Error& error)
	{
		Console::WriteLine(L"Error: " + WString(error.Description()));
	}
	catch (...)
	{
		Console::WriteLine(L"Error: Unknown application failure.");
	}

	ThreadLocalStorage::DisposeStorages();
	FinalizeGlobalStorage();
#if defined VCZH_MSVC && defined VCZH_CHECK_MEMORY_LEAKS
	_CrtDumpMemoryLeaks();
#endif
	return (int)result;
}
#endif

/***********************************************************************
Author: Zihan Chen (vczh)
Licensed under https://github.com/vczh-libraries/License
***********************************************************************/

#include "../../../Source/TUI/TUI.h"
#include <limits>

using namespace vl;
using namespace vl::collections;
using namespace vl::console;

namespace
{
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

	struct PlaygroundState
	{
		Nullable<Information>			information;
		U32String						typingCommand;
#if defined VCZH_WCHAR_UTF16
		wchar_t							pendingHighSurrogate = 0;
#endif
		List<PaintingCommand>			commands;
		bool							cursorVisible = true;
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

	void ReplayCommands(TuiPixel* buffer, vint width, vint height, const PlaygroundState& state)
	{
		auto foreground = TuiColor{ 255, 255, 255 };
		Nullable<TuiColor> background;
		for (auto&& command : state.commands)
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
				state.commands.Add(std::move(command));
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

		void Redraw()
		{
			auto width = TUI::GetBufferWidth();
			auto height = TUI::GetBufferHeight();
			auto wrappedInput = WrapText(state.typingCommand, width, true);
			auto textBoxHeight = wrappedInput.rowWidths.Count() > height ? height : wrappedInput.rowWidths.Count();
			auto paintingHeight = height - textBoxHeight;
			auto firstInputRow = wrappedInput.rowWidths.Count() - textBoxHeight;

			TUI::Clear({ 0, 0, 0 }, 0, 0, width - 1, height - 1);

			auto interiorWidth = width >= 2 ? width - 2 : 0;
			auto interiorHeight = paintingHeight >= 2 ? paintingHeight - 2 : 0;
			if (interiorWidth > 0 && interiorHeight > 0)
			{
				Array<TuiPixel> paper(interiorWidth * interiorHeight);
				ReplayCommands(&paper[0], interiorWidth, interiorHeight, state);
				if (state.information)
				{
					DrawInformationOverlay(&paper[0], interiorWidth, interiorHeight, state.information.Value());
				}
				auto buffer = TUI::GetBuffer();
				for (vint y = 0; y < interiorHeight; y++)
				{
					for (vint x = 0; x < interiorWidth; x++)
					{
						buffer[(y + 1) * width + x + 1] = paper[y * interiorWidth + x];
					}
				}
			}

			if (width >= 2 && paintingHeight >= 2)
			{
				TuiRectOptions options;
				options.glyph = TuiMergeableGlyph::DoubleLine;
				options.foregroundColor = { 255, 255, 255 };
				TUI::DrawRect(options, 0, 0, width - 1, paintingHeight - 1);
			}

			TUI::Clear({ 64, 64, 64 }, 0, paintingHeight, width - 1, height - 1);
			TuiPrintOptions inputOptions;
			inputOptions.foregroundColor = { 255, 255, 255 };
			inputOptions.backgroundColor = { 64, 64, 64 };
			for (auto&& scalar : wrappedInput.scalars)
			{
				if (scalar.row < firstInputRow) continue;
				TUI::PrintChar(inputOptions, scalar.code, scalar.x, paintingHeight + scalar.row - firstInputRow);
			}
			if (!state.information && state.cursorVisible && wrappedInput.cursorRow >= firstInputRow)
			{
				TUI::PrintChar(inputOptions, U'\u2588', wrappedInput.cursorX, paintingHeight + wrappedInput.cursorRow - firstInputRow);
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
			Redraw();
		}

		void Char(const TuiCharInfo& info) override
		{
			if (state.information)
			{
				ResetPendingUnit();
				auto code = DecodeNativeUnit(info.code);
				ResetPendingUnit();
				if (!code) return;
				if (code.Value() == U'\r' || code.Value() == U'\n')
				{
					state.information.Reset();
					state.cursorVisible = true;
					Redraw();
				}
				return;
			}

			auto decoded = DecodeNativeUnit(info.code);
			if (!decoded) return;
			auto code = decoded.Value();
			if (code == U'\r' || code == U'\n')
			{
				if (SubmitCommand())
				{
					Redraw();
				}
				return;
			}
			if (code == U'\b' || code == U'\x7F')
			{
				if (state.typingCommand.Length() > 0)
				{
					state.typingCommand = state.typingCommand.Sub(0, state.typingCommand.Length() - 1);
				}
				state.cursorVisible = true;
				Redraw();
				return;
			}
			auto charWidth = TUI::MeasureChar(code);
			if (charWidth != 1 && charWidth != 2) return;
			state.typingCommand += U32String::FromChar(code);
			state.cursorVisible = true;
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
}

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

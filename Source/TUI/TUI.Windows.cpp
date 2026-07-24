/***********************************************************************
Author: Zihan Chen (vczh)
Licensed under https://github.com/vczh-libraries/License
***********************************************************************/

#include "TUI.Internal.h"
#define _WINSOCKAPI_
#include <Windows.h>
#include <exception>

#ifndef VCZH_MSVC
static_assert(false, "Do not build this file for non-Windows applications.");
#endif

using namespace vl;
using namespace vl::collections;

namespace vl
{
	namespace console
	{
		vint TUI::MeasureChar(char32_t code)
		{
#define ERROR_MESSAGE_PREFIX L"vl::console::TUI::MeasureChar(char32_t)#"
			if (!tui_internal::IsScalar(code)) return 0;

			wchar_t text[2];
			auto length = 1;
			if (code <= 0xFFFF)
			{
				text[0] = (wchar_t)code;
			}
			else
			{
				code -= 0x10000;
				text[0] = (wchar_t)(0xD800 + (code >> 10));
				text[1] = (wchar_t)(0xDC00 + (code & 0x3FF));
				length = 2;
			}

			WORD ctype1[2] = {};
			WORD ctype3[2] = {};
			CHECK_ERROR(
				GetStringTypeW(CT_CTYPE1, text, length, ctype1) &&
				GetStringTypeW(CT_CTYPE3, text, length, ctype3),
				ERROR_MESSAGE_PREFIX L"Failed to query character types."
				);
			if (ctype1[0] & C1_CNTRL) return 0;
			if (ctype3[0] & (C3_NONSPACING | C3_DIACRITIC | C3_VOWELMARK)) return 0;
			if (ctype3[0] & C3_HALFWIDTH) return 1;
			if (length == 2 || (ctype3[0] & (C3_FULLWIDTH | C3_IDEOGRAPH | C3_HIRAGANA | C3_KATAKANA))) return 2;
#undef ERROR_MESSAGE_PREFIX
			return 1;
		}

		namespace tui_internal
		{
			void WriteConsoleAll(HANDLE handle, const wchar_t* text, vint length)
			{
				vint written = 0;
				while (written < length)
				{
					DWORD count = 0;
					CHECK_ERROR(WriteConsoleW(handle, text + written, (DWORD)(length - written), &count, nullptr), L"vl::console::TUI Windows backend failed to write terminal output.");
					CHECK_ERROR(count > 0, L"vl::console::TUI Windows backend made no progress while writing terminal output.");
					written += count;
				}
			}

			vint AnsiToWindowsColor(vint color)
			{
				return (color & 8) | ((color & 1) << 2) | (color & 2) | ((color & 4) >> 2);
			}

			class WindowsTuiBackend : public unittest::ITuiBackend
			{
			private:
				HANDLE						inputHandle = INVALID_HANDLE_VALUE;
				HANDLE						originalOutputHandle = INVALID_HANDLE_VALUE;
				HANDLE						outputHandle = INVALID_HANDLE_VALUE;
				HANDLE						classicOutputHandle = INVALID_HANDLE_VALUE;
				DWORD						inputMode = 0;
				DWORD						outputMode = 0;
				CONSOLE_CURSOR_INFO			cursorInfo = {};
				CONSOLE_SCREEN_BUFFER_INFOEX	screenInfo = {};
				CONSOLE_SCREEN_BUFFER_INFO	originalGeometry = {};
				List<unittest::TuiBackendEvent> pendingEvents;
				DWORD						mouseButtons = 0;
				vint						viewportWidth = 0;
				vint						viewportHeight = 0;
				bool						started = false;
				bool						inputModeChanged = false;
				bool						outputModeChanged = false;
				bool						cursorInfoSaved = false;
				bool						geometrySaved = false;
				bool						usingVt = false;

				void SetConsoleGeometry(HANDLE handle, COORD bufferSize, SMALL_RECT window)
				{
					CONSOLE_SCREEN_BUFFER_INFO current = {};
					CHECK_ERROR(GetConsoleScreenBufferInfo(handle, &current), L"vl::console::TUI Windows backend failed to query console geometry.");
					auto currentWidth = (SHORT)(current.srWindow.Right - current.srWindow.Left + 1);
					auto currentHeight = (SHORT)(current.srWindow.Bottom - current.srWindow.Top + 1);
					auto temporaryWidth = currentWidth < bufferSize.X ? currentWidth : bufferSize.X;
					auto temporaryHeight = currentHeight < bufferSize.Y ? currentHeight : bufferSize.Y;
					SMALL_RECT temporary = { 0, 0, (SHORT)(temporaryWidth - 1), (SHORT)(temporaryHeight - 1) };
					if (current.srWindow.Left != temporary.Left ||
						current.srWindow.Top != temporary.Top ||
						current.srWindow.Right != temporary.Right ||
						current.srWindow.Bottom != temporary.Bottom)
					{
						CHECK_ERROR(SetConsoleWindowInfo(handle, TRUE, &temporary), L"vl::console::TUI Windows backend failed to prepare the console window for resizing.");
					}
					if (current.dwSize.X != bufferSize.X || current.dwSize.Y != bufferSize.Y)
					{
						CHECK_ERROR(SetConsoleScreenBufferSize(handle, bufferSize), L"vl::console::TUI Windows backend failed to resize the console screen buffer.");
					}
					if (temporary.Left != window.Left ||
						temporary.Top != window.Top ||
						temporary.Right != window.Right ||
						temporary.Bottom != window.Bottom)
					{
						CHECK_ERROR(SetConsoleWindowInfo(handle, TRUE, &window), L"vl::console::TUI Windows backend failed to resize the console window.");
					}
				}

				void QueueResize(vint width, vint height)
				{
					unittest::TuiBackendEvent event;
					event.type = unittest::TuiBackendEventType::Resize;
					event.width = width;
					event.height = height;
					pendingEvents.Add(event);
				}

				void SynchronizeViewport(bool queueEvent)
				{
					CONSOLE_SCREEN_BUFFER_INFO info = {};
					CHECK_ERROR(GetConsoleScreenBufferInfo(outputHandle, &info), L"vl::console::TUI Windows backend failed to query the console viewport.");
					auto width = (vint)(info.srWindow.Right - info.srWindow.Left + 1);
					auto height = (vint)(info.srWindow.Bottom - info.srWindow.Top + 1);
					CHECK_ERROR(width > 0 && height > 0, L"vl::console::TUI Windows backend received an invalid console viewport.");
					auto changed = width != viewportWidth || height != viewportHeight;
					COORD bufferSize = { (SHORT)width, (SHORT)height };
					SMALL_RECT window = { 0, 0, (SHORT)(width - 1), (SHORT)(height - 1) };
					if (info.dwSize.X != bufferSize.X ||
						info.dwSize.Y != bufferSize.Y ||
						info.srWindow.Left != 0 ||
						info.srWindow.Top != 0)
					{
						SetConsoleGeometry(outputHandle, bufferSize, window);
					}
					viewportWidth = width;
					viewportHeight = height;
					if (queueEvent && changed)
					{
						QueueResize(width, height);
					}
				}

				TuiMouseInfo GetMouseInfo(const MOUSE_EVENT_RECORD& record)
				{
					CONSOLE_SCREEN_BUFFER_INFO info = {};
					CHECK_ERROR(GetConsoleScreenBufferInfo(outputHandle, &info), L"vl::console::TUI Windows backend failed to query the console viewport.");
					TuiMouseInfo result;
					result.x = record.dwMousePosition.X - info.srWindow.Left;
					result.y = record.dwMousePosition.Y - info.srWindow.Top;
					result.ctrl = (record.dwControlKeyState & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED)) != 0;
					result.shift = (record.dwControlKeyState & SHIFT_PRESSED) != 0;
					result.alt = (record.dwControlKeyState & (LEFT_ALT_PRESSED | RIGHT_ALT_PRESSED)) != 0;
					result.left = (record.dwButtonState & FROM_LEFT_1ST_BUTTON_PRESSED) != 0;
					result.middle = (record.dwButtonState & FROM_LEFT_2ND_BUTTON_PRESSED) != 0;
					result.right = (record.dwButtonState & RIGHTMOST_BUTTON_PRESSED) != 0;
					return result;
				}

				TuiKeyInfo GetKeyInfo(const KEY_EVENT_RECORD& record)
				{
					TuiKeyInfo result;
					result.ctrl = (record.dwControlKeyState & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED)) != 0;
					result.shift = (record.dwControlKeyState & SHIFT_PRESSED) != 0;
					result.alt = (record.dwControlKeyState & (LEFT_ALT_PRESSED | RIGHT_ALT_PRESSED)) != 0;
					result.capslock = (record.dwControlKeyState & CAPSLOCK_ON) != 0;
					result.autoRepeatKeyDown = record.bKeyDown && record.wRepeatCount > 1;
					return result;
				}

				void QueueChar(wchar_t code, const KEY_EVENT_RECORD& record)
				{
					unittest::TuiBackendEvent event;
					event.type = unittest::TuiBackendEventType::Char;
					event.charInfo.code = code;
					event.charInfo.ctrl = (record.dwControlKeyState & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED)) != 0;
					event.charInfo.shift = (record.dwControlKeyState & SHIFT_PRESSED) != 0;
					event.charInfo.alt = (record.dwControlKeyState & (LEFT_ALT_PRESSED | RIGHT_ALT_PRESSED)) != 0;
					event.charInfo.capslock = (record.dwControlKeyState & CAPSLOCK_ON) != 0;
					pendingEvents.Add(event);
				}

				void DecodeKey(const KEY_EVENT_RECORD& record)
				{
					unittest::TuiBackendEvent keyEvent;
					keyEvent.type = record.bKeyDown ? unittest::TuiBackendEventType::KeyDown : unittest::TuiBackendEventType::KeyUp;
					keyEvent.keyInfo = GetKeyInfo(record);
					pendingEvents.Add(keyEvent);

					if (!record.bKeyDown || record.uChar.UnicodeChar == 0) return;
					auto codeUnit = record.uChar.UnicodeChar;
					auto repeat = record.wRepeatCount == 0 ? 1 : record.wRepeatCount;
					for (vint i = 0; i < repeat; i++)
					{
						QueueChar(codeUnit, record);
					}
				}

				void QueueMouseButton(unittest::TuiBackendEventType type, TuiMouseButton button, const TuiMouseInfo& info)
				{
					unittest::TuiBackendEvent event;
					event.type = type;
					event.mouseButton = button;
					event.mouseInfo = info;
					pendingEvents.Add(event);
				}

				void DecodeMouse(const MOUSE_EVENT_RECORD& record)
				{
					auto info = GetMouseInfo(record);
					if (record.dwEventFlags == MOUSE_MOVED)
					{
						unittest::TuiBackendEvent event;
						event.type = unittest::TuiBackendEventType::MouseMove;
						event.mouseInfo = info;
						pendingEvents.Add(event);
					}
					else if (record.dwEventFlags == MOUSE_WHEELED || record.dwEventFlags == MOUSE_HWHEELED)
					{
						unittest::TuiBackendEvent event;
						event.type = record.dwEventFlags == MOUSE_WHEELED ? unittest::TuiBackendEventType::MouseVerticalWheel : unittest::TuiBackendEventType::MouseHorizontalWheel;
						event.mouseInfo = info;
						event.mouseInfo.wheel = (SHORT)HIWORD(record.dwButtonState);
						pendingEvents.Add(event);
					}
					else if (record.dwEventFlags == DOUBLE_CLICK)
					{
						auto button = (record.dwButtonState & FROM_LEFT_1ST_BUTTON_PRESSED) ? TuiMouseButton::Left
							: (record.dwButtonState & FROM_LEFT_2ND_BUTTON_PRESSED) ? TuiMouseButton::Middle
							: TuiMouseButton::Right;
						QueueMouseButton(unittest::TuiBackendEventType::MouseDoubleClick, button, info);
					}
					else if (record.dwEventFlags == 0)
					{
						struct ButtonMapping
						{
							DWORD			mask;
							TuiMouseButton	button;
						};
						ButtonMapping mappings[] =
						{
							{ FROM_LEFT_1ST_BUTTON_PRESSED, TuiMouseButton::Left },
							{ FROM_LEFT_2ND_BUTTON_PRESSED, TuiMouseButton::Middle },
							{ RIGHTMOST_BUTTON_PRESSED, TuiMouseButton::Right },
						};
						for (auto mapping : mappings)
						{
							auto before = (mouseButtons & mapping.mask) != 0;
							auto after = (record.dwButtonState & mapping.mask) != 0;
							if (before != after)
							{
								QueueMouseButton(after ? unittest::TuiBackendEventType::MouseDown : unittest::TuiBackendEventType::MouseUp, mapping.button, info);
							}
						}
					}
					mouseButtons = record.dwButtonState;
				}

				void DecodeRecord(const INPUT_RECORD& record)
				{
					switch (record.EventType)
					{
					case KEY_EVENT:
						DecodeKey(record.Event.KeyEvent);
						break;
					case MOUSE_EVENT:
						DecodeMouse(record.Event.MouseEvent);
						break;
					case WINDOW_BUFFER_SIZE_EVENT:
						SynchronizeViewport(true);
						break;
					}
				}

				void AppendColor(WString& output, TuiColor foreground, TuiColor background, TuiColorMode colorMode)
				{
					if (colorMode == TuiColorMode::TrueColor)
					{
						output += L"\x1B[38;2;" + itow(foreground.r) + L";" + itow(foreground.g) + L";" + itow(foreground.b)
							+ L";48;2;" + itow(background.r) + L";" + itow(background.g) + L";" + itow(background.b) + L"m";
					}
					else
					{
						TuiColor custom16[16];
						const TuiColor* custom = nullptr;
						if (colorMode == TuiColorMode::Color16 && screenInfo.cbSize == sizeof(screenInfo))
						{
							for (vint i = 0; i < 16; i++)
							{
								auto color = screenInfo.ColorTable[AnsiToWindowsColor(i)];
								custom16[i] = { GetRValue(color), GetGValue(color), GetBValue(color) };
							}
							custom = custom16;
						}
						auto foregroundIndex = QuantizeColor(foreground, colorMode, custom);
						auto backgroundIndex = QuantizeColor(background, colorMode, custom);
						if (colorMode == TuiColorMode::Color256)
						{
							output += L"\x1B[38;5;" + itow(foregroundIndex) + L";48;5;" + itow(backgroundIndex) + L"m";
						}
						else
						{
							auto foregroundCode = foregroundIndex < 8 ? 30 + foregroundIndex : 90 + foregroundIndex - 8;
							auto backgroundCode = backgroundIndex < 8 ? 40 + backgroundIndex : 100 + backgroundIndex - 8;
							output += L"\x1B[" + itow(foregroundCode) + L";" + itow(backgroundCode) + L"m";
						}
					}
				}

			public:
				TuiColorMode Start(const TuiStartOptions& options) override
				{
					CHECK_ERROR(!started, L"vl::console::TUI Windows backend is already active.");
					inputHandle = GetStdHandle(STD_INPUT_HANDLE);
					originalOutputHandle = GetStdHandle(STD_OUTPUT_HANDLE);
					outputHandle = originalOutputHandle;
					CHECK_ERROR(inputHandle != INVALID_HANDLE_VALUE && originalOutputHandle != INVALID_HANDLE_VALUE, L"vl::console::TUI requires valid console handles.");
					CHECK_ERROR(GetConsoleMode(inputHandle, &inputMode) && GetConsoleMode(originalOutputHandle, &outputMode), L"vl::console::TUI requires interactive console handles.");
					cursorInfoSaved = GetConsoleCursorInfo(originalOutputHandle, &cursorInfo) != 0;
					screenInfo = {};
					screenInfo.cbSize = sizeof(screenInfo);
					if (!GetConsoleScreenBufferInfoEx(originalOutputHandle, &screenInfo))
					{
						screenInfo.cbSize = 0;
					}
					CHECK_ERROR(GetConsoleScreenBufferInfo(originalOutputHandle, &originalGeometry), L"vl::console::TUI failed to save the original console geometry.");
					geometrySaved = true;
					started = true;

					try
					{
						auto newInputMode = inputMode;
						newInputMode &= ~(ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT | ENABLE_PROCESSED_INPUT | ENABLE_QUICK_EDIT_MODE | ENABLE_VIRTUAL_TERMINAL_INPUT);
						newInputMode |= ENABLE_EXTENDED_FLAGS | ENABLE_WINDOW_INPUT | ENABLE_MOUSE_INPUT;
						CHECK_ERROR(SetConsoleMode(inputHandle, newInputMode), L"vl::console::TUI failed to activate console input mode.");
						inputModeChanged = true;

						auto newOutputMode = outputMode | ENABLE_PROCESSED_OUTPUT | ENABLE_VIRTUAL_TERMINAL_PROCESSING;
						usingVt = SetConsoleMode(originalOutputHandle, newOutputMode) != 0;
						if (usingVt)
						{
							outputModeChanged = true;
							const wchar_t sequence[] = L"\x1B[?1049h\x1B[?25l";
							WriteConsoleAll(outputHandle, sequence, sizeof(sequence) / sizeof(*sequence) - 1);
							SynchronizeViewport(false);
							if (options.colorMode == TuiColorMode::Auto) return TuiColorMode::TrueColor;
							return options.colorMode;
						}

						classicOutputHandle = CreateConsoleScreenBuffer(GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, CONSOLE_TEXTMODE_BUFFER, nullptr);
						CHECK_ERROR(classicOutputHandle != INVALID_HANDLE_VALUE, L"vl::console::TUI failed to create an alternate console screen buffer.");
						auto width = (SHORT)(originalGeometry.srWindow.Right - originalGeometry.srWindow.Left + 1);
						auto height = (SHORT)(originalGeometry.srWindow.Bottom - originalGeometry.srWindow.Top + 1);
						SetConsoleGeometry(classicOutputHandle, { width, height }, { 0, 0, (SHORT)(width - 1), (SHORT)(height - 1) });
						CHECK_ERROR(SetConsoleActiveScreenBuffer(classicOutputHandle), L"vl::console::TUI failed to activate the alternate console screen buffer.");
						outputHandle = classicOutputHandle;
						viewportWidth = width;
						viewportHeight = height;
						return TuiColorMode::Color16;
					}
					catch (...)
					{
						auto exception = std::current_exception();
						try
						{
							Stop();
						}
						catch (...)
						{
						}
						std::rethrow_exception(exception);
					}
				}

				void Stop() override
				{
					if (!started) return;
					auto restored = true;
					if (usingVt)
					{
						auto width = originalGeometry.srWindow.Right - originalGeometry.srWindow.Left + 1;
						auto height = originalGeometry.srWindow.Bottom - originalGeometry.srWindow.Top + 1;
						auto sequence = WString::Unmanaged(L"\x1B[0m\x1B[8;")
							+ itow(height) + L";" + itow(width) + L"t\x1B[?1049l\x1B[?25h";
						try
						{
							WriteConsoleAll(outputHandle, sequence.Buffer(), sequence.Length());
							auto deadline = GetTickCount64() + 250;
							vint stableCount = 0;
							while (true)
							{
								CONSOLE_SCREEN_BUFFER_INFO info = {};
								CHECK_ERROR(GetConsoleScreenBufferInfo(outputHandle, &info), L"vl::console::TUI Windows backend failed to wait for terminal restoration.");
								auto currentWidth = info.srWindow.Right - info.srWindow.Left + 1;
								auto currentHeight = info.srWindow.Bottom - info.srWindow.Top + 1;
								if (currentWidth == width && currentHeight == height)
								{
									if (++stableCount == 10) break;
								}
								else
								{
									stableCount = 0;
								}
								if (GetTickCount64() >= deadline) break;
								::Sleep(1);
							}
						}
						catch (...)
						{
							restored = false;
						}
					}
					if (classicOutputHandle != INVALID_HANDLE_VALUE)
					{
						if (!SetConsoleActiveScreenBuffer(originalOutputHandle)) restored = false;
						if (!CloseHandle(classicOutputHandle)) restored = false;
						classicOutputHandle = INVALID_HANDLE_VALUE;
					}
					if (geometrySaved)
					{
						try
						{
							SetConsoleGeometry(originalOutputHandle, originalGeometry.dwSize, originalGeometry.srWindow);
						}
						catch (...)
						{
							restored = false;
						}
					}
					if (cursorInfoSaved && !SetConsoleCursorInfo(originalOutputHandle, &cursorInfo)) restored = false;
					if (outputModeChanged && !SetConsoleMode(originalOutputHandle, outputMode)) restored = false;
					if (inputModeChanged && !SetConsoleMode(inputHandle, inputMode)) restored = false;
					pendingEvents.Clear();
					mouseButtons = 0;
					viewportWidth = 0;
					viewportHeight = 0;
					outputHandle = INVALID_HANDLE_VALUE;
					originalOutputHandle = INVALID_HANDLE_VALUE;
					inputHandle = INVALID_HANDLE_VALUE;
					usingVt = false;
					inputModeChanged = false;
					outputModeChanged = false;
					cursorInfoSaved = false;
					geometrySaved = false;
					started = false;
					CHECK_ERROR(restored, L"vl::console::TUI Windows backend failed to restore the original console state.");
				}

				bool TryGetConsoleSize(vint& width, vint& height) override
				{
					auto handle = outputHandle != INVALID_HANDLE_VALUE ? outputHandle : GetStdHandle(STD_OUTPUT_HANDLE);
					CONSOLE_SCREEN_BUFFER_INFO info = {};
					if (handle == INVALID_HANDLE_VALUE || !GetConsoleScreenBufferInfo(handle, &info)) return false;
					width = info.srWindow.Right - info.srWindow.Left + 1;
					height = info.srWindow.Bottom - info.srWindow.Top + 1;
					return width > 0 && height > 0;
				}

				vuint64_t GetMonotonicTime() override
				{
					return GetTickCount64();
				}

				bool ReadEvent(vint milliseconds, unittest::TuiBackendEvent& event) override
				{
					if (pendingEvents.Count() == 0)
					{
						SynchronizeViewport(true);
					}
					if (pendingEvents.Count() == 0)
					{
						auto result = WaitForSingleObject(inputHandle, milliseconds < 0 ? INFINITE : (DWORD)milliseconds);
						if (result == WAIT_TIMEOUT)
						{
							SynchronizeViewport(true);
							if (pendingEvents.Count() == 0) return false;
						}
						else
						{
							CHECK_ERROR(result == WAIT_OBJECT_0, L"vl::console::TUI Windows backend failed while waiting for console input.");
							INPUT_RECORD records[32];
							DWORD count = 0;
							CHECK_ERROR(ReadConsoleInputW(inputHandle, records, sizeof(records) / sizeof(*records), &count), L"vl::console::TUI Windows backend failed to read console input.");
							for (DWORD i = 0; i < count; i++) DecodeRecord(records[i]);
							SynchronizeViewport(true);
						}
					}
					if (pendingEvents.Count() == 0) return false;
					event = pendingEvents[0];
					pendingEvents.RemoveAt(0);
					return true;
				}

				void Render(const TuiPixel* buffer, vint width, vint height, TuiColorMode colorMode) override
				{
					if (usingVt)
					{
						WString output;
						TuiColor lastForeground;
						TuiColor lastBackground;
						bool hasLastColor = false;
						for (vint y = 0; y < height; y++)
						{
							output += L"\x1B[" + itow(y + 1) + L";1H";
							for (vint x = 0; x < width; x++)
							{
								auto& pixel = buffer[y * width + x];
								if (pixel.glyph == TuiPixelGlyph::WideCharContinuation) continue;
								if (!hasLastColor || pixel.foregroundColor != lastForeground || pixel.backgroundColor != lastBackground)
								{
									AppendColor(output, pixel.foregroundColor, pixel.backgroundColor, colorMode);
									lastForeground = pixel.foregroundColor;
									lastBackground = pixel.backgroundColor;
									hasLastColor = true;
								}
								auto code = pixel.GetChar32();
								if (code == 0) output += L" ";
								else output += u32tow(U32String::CopyFrom(&code, 1));
							}
						}
						output += L"\x1B[0m";
						WriteConsoleAll(outputHandle, output.Buffer(), output.Length());
					}
					else
					{
						Array<CHAR_INFO> output(width * height);
						TuiColor custom16[16];
						for (vint i = 0; i < 16; i++)
						{
							auto color = screenInfo.ColorTable[AnsiToWindowsColor(i)];
							custom16[i] = { GetRValue(color), GetGValue(color), GetBValue(color) };
						}
						for (vint i = 0; i < width * height; i++)
						{
							auto& pixel = buffer[i];
							auto code = pixel.GetChar32();
							auto measured = code == 0 ? 1 : TUI::MeasureChar(code);
							output[i].Char.UnicodeChar = code == 0 ? L' ' : measured == 2 || code > 0xFFFF ? L'?' : (wchar_t)code;
							if (pixel.glyph == TuiPixelGlyph::WideCharContinuation) output[i].Char.UnicodeChar = L' ';
							auto foreground = AnsiToWindowsColor(QuantizeColor(pixel.foregroundColor, TuiColorMode::Color16, custom16));
							auto background = AnsiToWindowsColor(QuantizeColor(pixel.backgroundColor, TuiColorMode::Color16, custom16));
							output[i].Attributes = (WORD)(foreground | (background << 4));
						}
						COORD size = { (SHORT)width, (SHORT)height };
						COORD origin = {};
						SMALL_RECT area = { 0, 0, (SHORT)(width - 1), (SHORT)(height - 1) };
						CHECK_ERROR(WriteConsoleOutputW(outputHandle, &output[0], size, origin, &area), L"vl::console::TUI Windows backend failed to render the classic console buffer.");
					}
				}
			};

			Ptr<unittest::ITuiBackend> CreateTuiBackend()
			{
				return Ptr(new WindowsTuiBackend);
			}
		}
	}
}

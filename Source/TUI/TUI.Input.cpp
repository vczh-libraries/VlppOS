/***********************************************************************
Author: Zihan Chen (vczh)
Licensed under https://github.com/vczh-libraries/License
***********************************************************************/

#include "TUI.Input.h"
#include <limits>

using namespace vl;
using namespace vl::collections;
using namespace vl::presentation;

namespace vl
{
	namespace console
	{
		namespace tui_internal
		{
			VKEY KeyFromAscii(char32_t code)
			{
				if (code >= U'a' && code <= U'z') return (VKEY)(code - U'a' + U'A');
				if ((code >= U'A' && code <= U'Z') || (code >= U'0' && code <= U'9')) return (VKEY)code;
				switch (code)
				{
				case U'\t': return VKEY::KEY_TAB;
				case U'\r': case U'\n': return VKEY::KEY_RETURN;
				case U'\b': case 0x7F: return VKEY::KEY_BACK;
				case 0x1B: return VKEY::KEY_ESCAPE;
				case U' ': return VKEY::KEY_SPACE;
				case U';': case U':': return VKEY::KEY_SEMICOLON;
				case U'/': case U'?': return VKEY::KEY_SLASH;
				case U'`': case U'~': return VKEY::KEY_GRAVE_ACCENT;
				case U'[': case U'{': return VKEY::KEY_LEFT_BRACKET;
				case U']': case U'}': return VKEY::KEY_RIGHT_BRACKET;
				case U'\\': case U'|': return VKEY::KEY_BACKSLASH;
				case U'\'': case U'"': return VKEY::KEY_APOSTROPHE;
				case U'=': case U'+': return VKEY::KEY_OEM_PLUS;
				case U',': case U'<': return VKEY::KEY_OEM_COMMA;
				case U'-': case U'_': return VKEY::KEY_OEM_MINUS;
				case U'.': case U'>': return VKEY::KEY_OEM_PERIOD;
				default:
					const char32_t shifted[] = U")!@#$%^&*(";
					for (vint i = 0; i < 10; i++)
					{
						if (code == shifted[i]) return (VKEY)((vint)VKEY::KEY_0 + i);
					}
					return VKEY::KEY_UNKNOWN;
				}
			}

/***********************************************************************
PosixTuiInputDecoder
***********************************************************************/

			void PosixTuiInputDecoder::QueueKey(NativeWindowKeyInfo info, Nullable<char32_t> text)
			{
				unittest::TuiBackendEvent event;
				event.type = unittest::TuiBackendEventType::KeyDown;
				event.keyInfo = info;
				pendingEvents.Add(event);
				if (text)
				{
					wchar_t units[encoding::UtfConversion<wchar_t>::BufferLength];
					auto count = encoding::UtfConversion<wchar_t>::From32(text.Value(), units);
					for (vint i = 0; i < count; i++)
					{
						event.type = unittest::TuiBackendEventType::Char;
						event.charInfo.code = units[i];
						event.charInfo.ctrl = info.ctrl;
						event.charInfo.shift = info.shift;
						event.charInfo.alt = info.alt;
						event.charInfo.osSuper = info.osSuper;
						pendingEvents.Add(event);
					}
				}
			}

			void PosixTuiInputDecoder::DecodeSequence(vint end, vuint64_t now)
			{
				auto mouse = inputBytes[1] == '[' && inputBytes[2] == '<';
				auto final = inputBytes[end];
				vint values[3] = {};
				vint count = 0;
				auto begin = mouse ? 3 : 2;
				if (begin < end)
				{
					count = 1;
					auto hasDigit = false;
					for (vint i = begin; i < end; i++)
					{
						auto byte = inputBytes[i];
						if (byte >= '0' && byte <= '9')
						{
							auto digit = byte - '0';
							if (values[count - 1] > (std::numeric_limits<vint>::max() - digit) / 10) return;
							values[count - 1] = values[count - 1] * 10 + digit;
							hasDigit = true;
						}
						else if (byte == ';' && hasDigit && count < 3)
						{
							count++;
							hasDigit = false;
						}
						else return;
					}
					if (!hasDigit) return;
				}

				if (mouse)
				{
					if (count != 3 || values[1] <= 0 || values[2] <= 0 || (final != 'M' && final != 'm')) return;
					auto cb = values[0];
					auto base = cb & ~(4 | 8 | 16 | 32);
					if (base > 3 && (base < 64 || base > 67)) return;
					unittest::TuiBackendEvent event;
					auto& info = event.mouseInfo;
					info.x = values[1] - 1;
					info.y = values[2] - 1;
					info.shift = (cb & 4) != 0;
					info.alt = (cb & 8) != 0;
					info.ctrl = (cb & 16) != 0;
					if (base >= 64)
					{
						if (final != 'M' || (cb & 32)) return;
						event.type = base <= 65 ? unittest::TuiBackendEventType::MouseVerticalWheel : unittest::TuiBackendEventType::MouseHorizontalWheel;
						info.wheel = base == 64 || base == 67 ? 120 : -120;
					}
					else if (cb & 32)
					{
						if (final != 'M') return;
						event.type = unittest::TuiBackendEventType::MouseMove;
						// SGR motion identifies a held button; button 3 explicitly means none.
						if (base == 3) left = middle = right = false;
						if (base == 0) left = true;
						if (base == 1) middle = true;
						if (base == 2) right = true;
					}
					else
					{
						if (base == 3) return;
						auto button = base == 0 ? NativeMouseButton::Left : base == 1 ? NativeMouseButton::Middle : NativeMouseButton::Right;
						event.mouseButton = button;
						auto pressed = final == 'M';
						if (base == 0) left = pressed;
						if (base == 1) middle = pressed;
						if (base == 2) right = pressed;
						event.type = pressed ? unittest::TuiBackendEventType::MouseDown : unittest::TuiBackendEventType::MouseUp;
						if (pressed)
						{
							if (lastClickTime && now - lastClickTime.Value() <= 500 && lastClickX == info.x && lastClickY == info.y && lastClickButton == button)
							{
								event.type = unittest::TuiBackendEventType::MouseDoubleClick;
								lastClickTime.Reset();
							}
							else
							{
								lastClickTime = now;
								lastClickX = info.x;
								lastClickY = info.y;
								lastClickButton = button;
							}
						}
					}
					info.left = left;
					info.middle = middle;
					info.right = right;
					pendingEvents.Add(event);
					return;
				}

				NativeWindowKeyInfo info;
				Nullable<char32_t> text;
				if (count > 2) return;
				if (count == 2)
				{
					auto modifiers = values[1];
					if (modifiers < 1 || modifiers > 16) return;
					modifiers--;
					info.shift = (modifiers & 1) != 0;
					// Terminal Alt/Meta modifiers do not identify an OS Super key.
					info.alt = (modifiers & (2 | 8)) != 0;
					info.ctrl = (modifiers & 4) != 0;
				}
				if (final == '~' && inputBytes[1] == '[')
				{
					if (count == 0) return;
					switch (values[0])
					{
					case 1: case 7: info.code = VKEY::KEY_HOME; break;
					case 2: info.code = VKEY::KEY_INSERT; break;
					case 3: info.code = VKEY::KEY_DELETE; break;
					case 4: case 8: info.code = VKEY::KEY_END; break;
					case 5: info.code = VKEY::KEY_PRIOR; break;
					case 6: info.code = VKEY::KEY_NEXT; break;
					default:
						const vint functionNumbers[] = { 11, 12, 13, 14, 15, 17, 18, 19, 20, 21, 23, 24, 25, 26, 28, 29, 31, 32, 33, 34 };
						for (vint i = 0; i < (vint)(sizeof(functionNumbers) / sizeof(*functionNumbers)); i++)
						{
							if (values[0] == functionNumbers[i]) info.code = (VKEY)((vint)VKEY::KEY_F1 + i);
						}
					}
				}
				else
				{
					if (count > 0 && values[0] != 1) return;
					switch (final)
					{
					case 'A': info.code = VKEY::KEY_UP; break;
					case 'B': info.code = VKEY::KEY_DOWN; break;
					case 'C': info.code = VKEY::KEY_RIGHT; break;
					case 'D': info.code = VKEY::KEY_LEFT; break;
					case 'H': info.code = VKEY::KEY_HOME; break;
					case 'F': info.code = VKEY::KEY_END; break;
					case 'P': info.code = VKEY::KEY_F1; break;
					case 'Q': info.code = VKEY::KEY_F2; break;
					case 'R': info.code = VKEY::KEY_F3; break;
					case 'S': info.code = VKEY::KEY_F4; break;
					case 'Z': info.code = VKEY::KEY_TAB; info.shift = true; text = U'\t'; break;
					default:
						if (inputBytes[1] != 'O') return;
						if (final >= 'p' && final <= 'y')
						{
							info.code = (VKEY)((vint)VKEY::KEY_NUMPAD0 + final - 'p');
							text = U'0' + final - 'p';
						}
						else switch (final)
						{
						case 'M': info.code = VKEY::KEY_RETURN; text = U'\r'; break;
						case 'X': info.code = VKEY::KEY_OEM_NEC_EQUAL; text = U'='; break;
						case 'j': info.code = VKEY::KEY_MULTIPLY; text = U'*'; break;
						case 'k': info.code = VKEY::KEY_ADD; text = U'+'; break;
						case 'l': info.code = VKEY::KEY_SEPARATOR; text = U','; break;
						case 'm': info.code = VKEY::KEY_SUBTRACT; text = U'-'; break;
						case 'n': info.code = VKEY::KEY_DECIMAL; text = U'.'; break;
						case 'o': info.code = VKEY::KEY_DIVIDE; text = U'/'; break;
						}
					}
				}
				if (info.code != VKEY::KEY_UNKNOWN) QueueKey(info, text);
			}

			bool PosixTuiInputDecoder::DecodeText(bool alt)
			{
				auto offset = alt ? 1 : 0;
				auto first = inputBytes[offset];
				vint length = first < 0x80 ? 1 : first >= 0xC2 && first <= 0xDF ? 2 : first >= 0xE0 && first <= 0xEF ? 3 : first >= 0xF0 && first <= 0xF4 ? 4 : 0;
				char32_t code = length == 1 ? first : length == 2 ? first & 0x1F : length == 3 ? first & 0x0F : first & 0x07;
				vint consumed = 1;
				for (vint i = 1; i < length; i++)
				{
					if (offset + i >= inputBytes.Count()) return false;
					auto next = inputBytes[offset + i];
					if ((next & 0xC0) != 0x80)
					{
						length = 0;
						break;
					}
					code = (code << 6) | (next & 0x3F);
					consumed++;
				}
				char32_t minimum = length == 1 ? 0 : length == 2 ? 0x80 : length == 3 ? 0x800 : 0x10000;
				if (length == 0 || code < minimum || !IsScalar(code)) code = U'\uFFFD';
				inputBytes.RemoveRange(0, offset + consumed);
				NativeWindowKeyInfo info;
				info.code = KeyFromAscii(code);
				info.alt = alt;
				if (info.code == VKEY::KEY_UNKNOWN && code > 0 && code < 0x20)
				{
					info.code = KeyFromAscii(code + U'A' - 1);
					info.ctrl = true;
				}
				QueueKey(info, code);
				return true;
			}

			void PosixTuiInputDecoder::ParseInput(vuint64_t now)
			{
				while (pendingEvents.Count() == 0 && inputBytes.Count() > 0)
				{
					if (controlString)
					{
						if (inputBytes[0] == 0x1B)
						{
							if (inputBytes.Count() == 1) return;
							controlString = 0;
							if (inputBytes[1] == '\\') inputBytes.RemoveRange(0, 2);
						}
						else
						{
							if (inputBytes[0] == 7 && controlString == ']') controlString = 0;
							inputBytes.RemoveAt(0);
						}
						continue;
					}
					if (discardSequence)
					{
						auto byte = inputBytes[0];
						if (byte == 0x1B) discardSequence = false;
						else
						{
							inputBytes.RemoveAt(0);
							if (byte >= 0x40 && byte <= 0x7E) discardSequence = false;
							continue;
						}
					}
					if (inputBytes[0] != 0x1B)
					{
						escapeDeadline.Reset();
						if (!DecodeText(false)) return;
					}
					else if (inputBytes.Count() == 1)
					{
						if (!escapeDeadline) escapeDeadline = now + 30;
						if (now < escapeDeadline.Value()) return;
						escapeDeadline.Reset();
						DecodeText(false);
					}
					else
					{
						escapeDeadline.Reset();
						if (inputBytes[1] == ']' || inputBytes[1] == 'P' || inputBytes[1] == '^' || inputBytes[1] == '_' || inputBytes[1] == 'X')
						{
							controlString = inputBytes[1];
							inputBytes.RemoveRange(0, 2);
						}
						else if (inputBytes[1] == '[' || inputBytes[1] == 'O')
						{
							vint end = 2;
							while (end < inputBytes.Count() && inputBytes[end] >= 0x20 && inputBytes[end] < 0x40) end++;
							if (end == inputBytes.Count())
							{
								if (end > 256)
								{
									inputBytes.Clear();
									discardSequence = true;
								}
								return;
							}
							if (inputBytes[end] >= 0x40 && inputBytes[end] <= 0x7E)
							{
								DecodeSequence(end, now);
								inputBytes.RemoveRange(0, end + 1);
							}
							else inputBytes.RemoveRange(0, end);
						}
						else if (inputBytes[1] == 0x1B) DecodeText(false);
						else if (!DecodeText(true)) return;
					}
				}
			}
		}
	}
}

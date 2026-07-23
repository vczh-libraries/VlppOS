/***********************************************************************
Author: Zihan Chen (vczh)
Licensed under https://github.com/vczh-libraries/License
***********************************************************************/

#include "TUI.Internal.h"

#ifdef VCZH_GCC

#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <string>
#include <sys/ioctl.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

using namespace vl;
using namespace vl::collections;

namespace vl
{
	namespace console
	{
		namespace tui_internal
		{
			int resizePipe[2] = { -1, -1 };

			void ResizeSignalHandler(int)
			{
				auto savedError = errno;
				char byte = 1;
				while (write(resizePipe[1], &byte, 1) == -1 && errno == EINTR)
				{
				}
				errno = savedError;
			}

			void EnsureResizePipe()
			{
				if (resizePipe[0] != -1) return;
				CHECK_ERROR(pipe(resizePipe) == 0, L"vl::console::TUI POSIX backend failed to create its resize pipe.");
				for (vint i = 0; i < 2; i++)
				{
					auto descriptorFlags = fcntl(resizePipe[i], F_GETFD);
					auto statusFlags = fcntl(resizePipe[i], F_GETFL);
					CHECK_ERROR(descriptorFlags != -1 && statusFlags != -1, L"vl::console::TUI POSIX backend failed to query resize-pipe flags.");
					CHECK_ERROR(fcntl(resizePipe[i], F_SETFD, descriptorFlags | FD_CLOEXEC) != -1, L"vl::console::TUI POSIX backend failed to make the resize pipe close-on-exec.");
					CHECK_ERROR(fcntl(resizePipe[i], F_SETFL, statusFlags | O_NONBLOCK) != -1, L"vl::console::TUI POSIX backend failed to make the resize pipe nonblocking.");
				}
			}

			void WriteAll(int descriptor, const char* text, vint length)
			{
				vint written = 0;
				while (written < length)
				{
					auto count = write(descriptor, text + written, (size_t)(length - written));
					if (count == -1 && errno == EINTR) continue;
					CHECK_ERROR(count > 0, L"vl::console::TUI POSIX backend failed while writing terminal output.");
					written += count;
				}
			}

			void AppendUtf8(std::string& output, char32_t code)
			{
				if (code <= 0x7F)
				{
					output.push_back((char)code);
				}
				else if (code <= 0x7FF)
				{
					output.push_back((char)(0xC0 | (code >> 6)));
					output.push_back((char)(0x80 | (code & 0x3F)));
				}
				else if (code <= 0xFFFF)
				{
					output.push_back((char)(0xE0 | (code >> 12)));
					output.push_back((char)(0x80 | ((code >> 6) & 0x3F)));
					output.push_back((char)(0x80 | (code & 0x3F)));
				}
				else
				{
					output.push_back((char)(0xF0 | (code >> 18)));
					output.push_back((char)(0x80 | ((code >> 12) & 0x3F)));
					output.push_back((char)(0x80 | ((code >> 6) & 0x3F)));
					output.push_back((char)(0x80 | (code & 0x3F)));
				}
			}

			void AppendNumber(std::string& output, vint value)
			{
				output += std::to_string((long long)value);
			}

			class PosixTuiBackend : public unittest::ITuiBackend
			{
			private:
				termios						savedTermios = {};
				struct sigaction				savedAction = {};
				sigset_t					savedMask = {};
				List<vuint8_t>				inputBytes;
				List<unittest::TuiBackendEvent> pendingEvents;
				vuint64_t					escapeDeadline = 0;
				vuint64_t					lastClickTime = 0;
				vint						lastClickX = -1;
				vint						lastClickY = -1;
				TuiMouseButton				lastClickButton = TuiMouseButton::Left;
				bool						left = false;
				bool						middle = false;
				bool						right = false;
				bool						started = false;
				bool						termiosChanged = false;
				bool						signalInstalled = false;

				void QueueChar(char32_t code, bool alt = false)
				{
					unittest::TuiBackendEvent event;
					event.type = unittest::TuiBackendEventType::Char;
					event.charInfo.code = code;
					event.charInfo.alt = alt;
					pendingEvents.Add(event);
				}

				void DrainResizePipe()
				{
					char buffer[64];
					for (;;)
					{
						auto count = read(resizePipe[0], buffer, sizeof(buffer));
						if (count > 0) continue;
						if (count == -1 && errno == EINTR) continue;
						break;
					}
				}

				void QueueResize()
				{
					vint width = 0;
					vint height = 0;
					if (TryGetConsoleSize(width, height))
					{
						unittest::TuiBackendEvent event;
						event.type = unittest::TuiBackendEventType::Resize;
						event.width = width;
						event.height = height;
						pendingEvents.Add(event);
					}
				}

				bool TryParseMouse()
				{
					if (inputBytes.Count() < 4 || inputBytes[0] != 0x1B || inputBytes[1] != '[' || inputBytes[2] != '<') return false;
					vint end = -1;
					for (vint i = 3; i < inputBytes.Count(); i++)
					{
						if (inputBytes[i] == 'M' || inputBytes[i] == 'm')
						{
							end = i;
							break;
						}
						if (!(inputBytes[i] == ';' || (inputBytes[i] >= '0' && inputBytes[i] <= '9')))
						{
							inputBytes.RemoveRange(0, i + 1);
							QueueChar(U'\uFFFD');
							return true;
						}
					}
					if (end == -1) return false;

					vint values[3] = {};
					vint valueIndex = 0;
					for (vint i = 3; i < end; i++)
					{
						if (inputBytes[i] == ';')
						{
							valueIndex++;
							if (valueIndex >= 3) break;
						}
						else
						{
							values[valueIndex] = values[valueIndex] * 10 + inputBytes[i] - '0';
						}
					}
					auto final = inputBytes[end];
					inputBytes.RemoveRange(0, end + 1);
					escapeDeadline = 0;
					if (valueIndex != 2) return true;

					auto cb = values[0];
					TuiMouseInfo info;
					info.x = values[1] - 1;
					info.y = values[2] - 1;
					info.shift = (cb & 4) != 0;
					info.alt = (cb & 8) != 0;
					info.ctrl = (cb & 16) != 0;
					auto motion = (cb & 32) != 0;
					auto base = cb & ~(4 | 8 | 16 | 32);
					info.left = left;
					info.middle = middle;
					info.right = right;

					unittest::TuiBackendEvent event;
					event.mouseInfo = info;
					if (base >= 64 && base <= 67)
					{
						if (base == 64 || base == 65)
						{
							event.type = unittest::TuiBackendEventType::MouseVerticalWheel;
							event.mouseInfo.wheel = base == 64 ? 120 : -120;
						}
						else
						{
							event.type = unittest::TuiBackendEventType::MouseHorizontalWheel;
							event.mouseInfo.wheel = base == 66 ? 120 : -120;
						}
						pendingEvents.Add(event);
						return true;
					}
					if (motion)
					{
						event.type = unittest::TuiBackendEventType::MouseMove;
						pendingEvents.Add(event);
						return true;
					}

					auto released = final == 'm' || base == 3;
					auto button = base == 0 ? TuiMouseButton::Left : base == 1 ? TuiMouseButton::Middle : TuiMouseButton::Right;
					event.mouseButton = button;
					if (released)
					{
						event.type = unittest::TuiBackendEventType::MouseUp;
						if (button == TuiMouseButton::Left) left = false;
						if (button == TuiMouseButton::Middle) middle = false;
						if (button == TuiMouseButton::Right) right = false;
					}
					else
					{
						auto now = GetMonotonicTime();
						auto doubleClick = lastClickTime != 0 && now - lastClickTime <= 500 && lastClickX == info.x && lastClickY == info.y && lastClickButton == button;
						event.type = doubleClick ? unittest::TuiBackendEventType::MouseDoubleClick : unittest::TuiBackendEventType::MouseDown;
						if (button == TuiMouseButton::Left) left = true;
						if (button == TuiMouseButton::Middle) middle = true;
						if (button == TuiMouseButton::Right) right = true;
						if (doubleClick)
						{
							lastClickTime = 0;
						}
						else
						{
							lastClickTime = now;
							lastClickX = info.x;
							lastClickY = info.y;
							lastClickButton = button;
						}
					}
					event.mouseInfo.left = left;
					event.mouseInfo.middle = middle;
					event.mouseInfo.right = right;
					pendingEvents.Add(event);
					return true;
				}

				bool TryConsumeSpecialSequence()
				{
					if (inputBytes.Count() < 2 || inputBytes[0] != 0x1B || inputBytes[1] != '[') return false;
					for (vint i = 2; i < inputBytes.Count(); i++)
					{
						auto byte = inputBytes[i];
						if (byte >= 0x40 && byte <= 0x7E)
						{
							inputBytes.RemoveRange(0, i + 1);
							escapeDeadline = 0;
							return true;
						}
					}
					return false;
				}

				bool TryDecodeUtf8(bool alt = false)
				{
					auto offset = alt ? 1 : 0;
					if (inputBytes.Count() <= offset) return false;
					auto first = inputBytes[offset];
					vint length = 0;
					char32_t code = 0;
					if (first < 0x80)
					{
						length = 1;
						code = first;
					}
					else if (first >= 0xC2 && first <= 0xDF)
					{
						length = 2;
						code = first & 0x1F;
					}
					else if (first >= 0xE0 && first <= 0xEF)
					{
						length = 3;
						code = first & 0x0F;
					}
					else if (first >= 0xF0 && first <= 0xF4)
					{
						length = 4;
						code = first & 0x07;
					}
					else
					{
						inputBytes.RemoveRange(0, offset + 1);
						QueueChar(U'\uFFFD', alt);
						return true;
					}
					if (inputBytes.Count() < offset + length) return false;
					for (vint i = 1; i < length; i++)
					{
						auto next = inputBytes[offset + i];
						if ((next & 0xC0) != 0x80)
						{
							inputBytes.RemoveRange(0, offset + 1);
							QueueChar(U'\uFFFD', alt);
							return true;
						}
						code = (code << 6) | (next & 0x3F);
					}
					auto minimum = length == 1 ? 0 : length == 2 ? 0x80 : length == 3 ? 0x800 : 0x10000;
					if (code < minimum || code > 0x10FFFF || (code >= 0xD800 && code <= 0xDFFF))
					{
						inputBytes.RemoveRange(0, offset + length);
						QueueChar(U'\uFFFD', alt);
						return true;
					}
					inputBytes.RemoveRange(0, offset + length);
					escapeDeadline = 0;
					QueueChar(code, alt);
					return true;
				}

				bool ParseInput()
				{
					if (inputBytes.Count() == 0) return false;
					if (inputBytes[0] == 0x1B)
					{
						if (TryParseMouse()) return true;
						if (TryConsumeSpecialSequence()) return true;
						if (inputBytes.Count() > 1 && inputBytes[1] != '[')
						{
							return TryDecodeUtf8(true);
						}
						if (escapeDeadline == 0) escapeDeadline = GetMonotonicTime() + 30;
						if (GetMonotonicTime() >= escapeDeadline)
						{
							inputBytes.RemoveAt(0);
							escapeDeadline = 0;
							QueueChar((char32_t)0x1B);
							return true;
						}
						return false;
					}
					return TryDecodeUtf8();
				}

				void AppendColor(std::string& output, TuiColor foreground, TuiColor background, TuiColorMode colorMode)
				{
					output += "\x1B[";
					if (colorMode == TuiColorMode::TrueColor)
					{
						output += "38;2;";
						AppendNumber(output, foreground.r);
						output += ";";
						AppendNumber(output, foreground.g);
						output += ";";
						AppendNumber(output, foreground.b);
						output += ";48;2;";
						AppendNumber(output, background.r);
						output += ";";
						AppendNumber(output, background.g);
						output += ";";
						AppendNumber(output, background.b);
					}
					else if (colorMode == TuiColorMode::Color256)
					{
						output += "38;5;";
						AppendNumber(output, QuantizeColor(foreground, colorMode));
						output += ";48;5;";
						AppendNumber(output, QuantizeColor(background, colorMode));
					}
					else
					{
						auto foregroundIndex = QuantizeColor(foreground, colorMode);
						auto backgroundIndex = QuantizeColor(background, colorMode);
						AppendNumber(output, foregroundIndex < 8 ? 30 + foregroundIndex : 90 + foregroundIndex - 8);
						output += ";";
						AppendNumber(output, backgroundIndex < 8 ? 40 + backgroundIndex : 100 + backgroundIndex - 8);
					}
					output += "m";
				}

			public:
				TuiColorMode Start(const TuiStartOptions& options) override
				{
					CHECK_ERROR(!started, L"vl::console::TUI POSIX backend is already active.");
					CHECK_ERROR(isatty(STDIN_FILENO) && isatty(STDOUT_FILENO), L"vl::console::TUI requires interactive terminal descriptors.");
					EnsureResizePipe();
					DrainResizePipe();
					CHECK_ERROR(tcgetattr(STDIN_FILENO, &savedTermios) == 0, L"vl::console::TUI failed to query terminal attributes.");
					started = true;

					try
					{
						auto raw = savedTermios;
						cfmakeraw(&raw);
						raw.c_cc[VMIN] = 0;
						raw.c_cc[VTIME] = 0;
						CHECK_ERROR(tcsetattr(STDIN_FILENO, TCSANOW, &raw) == 0, L"vl::console::TUI failed to activate raw terminal input.");
						termiosChanged = true;

						sigset_t blocked;
						sigemptyset(&blocked);
						sigaddset(&blocked, SIGWINCH);
						CHECK_ERROR(pthread_sigmask(SIG_BLOCK, &blocked, &savedMask) == 0, L"vl::console::TUI failed to block SIGWINCH during handler installation.");
						struct sigaction action = {};
						action.sa_handler = ResizeSignalHandler;
						sigemptyset(&action.sa_mask);
						auto signalResult = sigaction(SIGWINCH, &action, &savedAction);
						signalInstalled = signalResult == 0;
						auto maskResult = pthread_sigmask(SIG_SETMASK, &savedMask, nullptr);
						CHECK_ERROR(signalResult == 0, L"vl::console::TUI failed to install its SIGWINCH handler.");
						CHECK_ERROR(maskResult == 0, L"vl::console::TUI failed to restore the owner thread signal mask.");

						const char sequence[] = "\x1B[?1049h\x1B[?25l\x1B[?1003h\x1B[?1006h";
						WriteAll(STDOUT_FILENO, sequence, sizeof(sequence) - 1);
						if (options.colorMode != TuiColorMode::Auto) return options.colorMode;
						auto colorTerm = getenv("COLORTERM");
						if (colorTerm && (strstr(colorTerm, "truecolor") || strstr(colorTerm, "24bit"))) return TuiColorMode::TrueColor;
						auto term = getenv("TERM");
						if (term && strstr(term, "256color")) return TuiColorMode::Color256;
						return TuiColorMode::Color16;
					}
					catch (...)
					{
						Stop();
						throw;
					}
				}

				void Stop() override
				{
					if (!started) return;
					const char sequence[] = "\x1B[?1006l\x1B[?1003l\x1B[0m\x1B[?25h\x1B[?1049l";
					auto ignored = write(STDOUT_FILENO, sequence, sizeof(sequence) - 1);
					(void)ignored;
					if (termiosChanged) tcsetattr(STDIN_FILENO, TCSANOW, &savedTermios);
					if (signalInstalled)
					{
						sigset_t blocked;
						sigemptyset(&blocked);
						sigaddset(&blocked, SIGWINCH);
						pthread_sigmask(SIG_BLOCK, &blocked, nullptr);
						sigaction(SIGWINCH, &savedAction, nullptr);
						pthread_sigmask(SIG_SETMASK, &savedMask, nullptr);
					}
					inputBytes.Clear();
					pendingEvents.Clear();
					escapeDeadline = 0;
					left = middle = right = false;
					termiosChanged = false;
					signalInstalled = false;
					started = false;
				}

				bool TryGetConsoleSize(vint& width, vint& height) override
				{
					winsize size = {};
					if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &size) != 0) return false;
					width = size.ws_col;
					height = size.ws_row;
					return width > 0 && height > 0;
				}

				vuint64_t GetMonotonicTime() override
				{
					timespec time = {};
					clock_gettime(CLOCK_MONOTONIC, &time);
					return (vuint64_t)time.tv_sec * 1000 + time.tv_nsec / 1000000;
				}

				bool ReadEvent(vint milliseconds, unittest::TuiBackendEvent& event) override
				{
					if (pendingEvents.Count() == 0) ParseInput();
					if (pendingEvents.Count() == 0)
					{
						auto wait = milliseconds;
						if (escapeDeadline != 0)
						{
							auto now = GetMonotonicTime();
							auto remaining = escapeDeadline <= now ? 0 : (vint)(escapeDeadline - now);
							if (wait < 0 || remaining < wait) wait = remaining;
						}
						pollfd descriptors[] =
						{
							{ STDIN_FILENO, POLLIN, 0 },
							{ resizePipe[0], POLLIN, 0 },
						};
						vint result = 0;
						do
						{
							result = poll(descriptors, sizeof(descriptors) / sizeof(*descriptors), wait);
						} while (result == -1 && errno == EINTR);
						CHECK_ERROR(result >= 0, L"vl::console::TUI POSIX backend failed while waiting for terminal input.");
						if (descriptors[1].revents & POLLIN)
						{
							DrainResizePipe();
							QueueResize();
						}
						if (descriptors[0].revents & POLLIN)
						{
							vuint8_t bytes[256];
							auto count = read(STDIN_FILENO, bytes, sizeof(bytes));
							if (count > 0)
							{
								for (vint i = 0; i < count; i++) inputBytes.Add(bytes[i]);
							}
						}
						if (pendingEvents.Count() == 0) ParseInput();
					}
					if (pendingEvents.Count() == 0) return false;
					event = pendingEvents[0];
					pendingEvents.RemoveAt(0);
					return true;
				}

				void Render(const TuiPixel* buffer, vint width, vint height, TuiColorMode colorMode) override
				{
					std::string output;
					TuiColor lastForeground;
					TuiColor lastBackground;
					bool hasLastColor = false;
					for (vint y = 0; y < height; y++)
					{
						output += "\x1B[";
						AppendNumber(output, y + 1);
						output += ";1H";
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
							AppendUtf8(output, code == 0 ? U' ' : code);
						}
					}
					output += "\x1B[0m";
					WriteAll(STDOUT_FILENO, output.data(), (vint)output.size());
				}
			};

			Ptr<unittest::ITuiBackend> CreateTuiBackend()
			{
				return Ptr(new PosixTuiBackend);
			}
		}
	}
}

#endif

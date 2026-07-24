/***********************************************************************
Author: Zihan Chen (vczh)
Licensed under https://github.com/vczh-libraries/License
***********************************************************************/

#include "TUI.Internal.h"
#include <exception>

using namespace vl;
using namespace vl::collections;

namespace vl
{
	namespace console
	{
		namespace tui_internal
		{
			struct ListenerEntry
			{
				ITuiCallback*				listener = nullptr;
				vuint64_t					generation = 0;
			};
		}

		class TUI::Impl
		{
		public:
			bool							active = false;
			bool							stopRequested = false;
			bool							shuttingDown = false;
			bool							backendStarted = false;
			bool							consoleDisabled = false;
			vint							ownerThreadId = -1;
			vint							width = 0;
			vint							height = 0;
			vint							timerPeriod = 0;
			vuint64_t						nextTimer = 0;
			TuiColorMode					colorMode = TuiColorMode::Auto;
			Array<TuiPixel>					buffer;
			List<unittest::TuiBackendEvent>	eventQueue;
			Ptr<unittest::ITuiBackend>		backend;
			std::exception_ptr				callbackException;
			std::exception_ptr*				cleanupException = nullptr;

			~Impl()
			{
				if (backendStarted)
				{
					try
					{
						backend->Stop();
					}
					catch (...)
					{
						if (cleanupException && !*cleanupException)
						{
							*cleanupException = std::current_exception();
						}
					}
				}
				if (consoleDisabled)
				{
					Console::Enable();
				}
			}
		};

		class TUI::ListenerStorage
		{
		public:
			vuint64_t						nextGeneration = 0;
			List<tui_internal::ListenerEntry>	listeners;
		};

		TUI::Impl* TUI::impl = nullptr;
		TUI::ListenerStorage* TUI::listenerStorage = nullptr;
		Ptr<unittest::ITuiBackend>* TUI::injectedBackend = nullptr;

		namespace tui_internal
		{
			constexpr vuint8_t PackState(vuint8_t up, vuint8_t down, vuint8_t left, vuint8_t right)
			{
				return (vuint8_t)((up << 6) | (down << 4) | (left << 2) | right);
			}

			char32_t GetMergeableChar(const TuiMergeablePixel& pixel)
			{
				auto up = (vuint8_t)pixel.up;
				auto down = (vuint8_t)pixel.down;
				auto left = (vuint8_t)pixel.left;
				auto right = (vuint8_t)pixel.right;
				if (up > 3 || down > 3 || left > 3 || right > 3) return 0;

				switch (PackState(up, down, left, right))
				{
				case PackState(0, 0, 0, 0): return 0;
				case PackState(0, 0, 1, 1): return U'\u2500';
				case PackState(0, 0, 2, 2): return U'\u2501';
				case PackState(1, 1, 0, 0): return U'\u2502';
				case PackState(2, 2, 0, 0): return U'\u2503';
				case PackState(0, 1, 0, 1): return U'\u250C';
				case PackState(0, 1, 0, 2): return U'\u250D';
				case PackState(0, 2, 0, 1): return U'\u250E';
				case PackState(0, 2, 0, 2): return U'\u250F';
				case PackState(0, 1, 1, 0): return U'\u2510';
				case PackState(0, 1, 2, 0): return U'\u2511';
				case PackState(0, 2, 1, 0): return U'\u2512';
				case PackState(0, 2, 2, 0): return U'\u2513';
				case PackState(1, 0, 0, 1): return U'\u2514';
				case PackState(1, 0, 0, 2): return U'\u2515';
				case PackState(2, 0, 0, 1): return U'\u2516';
				case PackState(2, 0, 0, 2): return U'\u2517';
				case PackState(1, 0, 1, 0): return U'\u2518';
				case PackState(1, 0, 2, 0): return U'\u2519';
				case PackState(2, 0, 1, 0): return U'\u251A';
				case PackState(2, 0, 2, 0): return U'\u251B';
				case PackState(1, 1, 0, 1): return U'\u251C';
				case PackState(1, 1, 0, 2): return U'\u251D';
				case PackState(2, 1, 0, 1): return U'\u251E';
				case PackState(1, 2, 0, 1): return U'\u251F';
				case PackState(2, 2, 0, 1): return U'\u2520';
				case PackState(2, 1, 0, 2): return U'\u2521';
				case PackState(1, 2, 0, 2): return U'\u2522';
				case PackState(2, 2, 0, 2): return U'\u2523';
				case PackState(1, 1, 1, 0): return U'\u2524';
				case PackState(1, 1, 2, 0): return U'\u2525';
				case PackState(2, 1, 1, 0): return U'\u2526';
				case PackState(1, 2, 1, 0): return U'\u2527';
				case PackState(2, 2, 1, 0): return U'\u2528';
				case PackState(2, 1, 2, 0): return U'\u2529';
				case PackState(1, 2, 2, 0): return U'\u252A';
				case PackState(2, 2, 2, 0): return U'\u252B';
				case PackState(0, 1, 1, 1): return U'\u252C';
				case PackState(0, 1, 2, 1): return U'\u252D';
				case PackState(0, 1, 1, 2): return U'\u252E';
				case PackState(0, 1, 2, 2): return U'\u252F';
				case PackState(0, 2, 1, 1): return U'\u2530';
				case PackState(0, 2, 2, 1): return U'\u2531';
				case PackState(0, 2, 1, 2): return U'\u2532';
				case PackState(0, 2, 2, 2): return U'\u2533';
				case PackState(1, 0, 1, 1): return U'\u2534';
				case PackState(1, 0, 2, 1): return U'\u2535';
				case PackState(1, 0, 1, 2): return U'\u2536';
				case PackState(1, 0, 2, 2): return U'\u2537';
				case PackState(2, 0, 1, 1): return U'\u2538';
				case PackState(2, 0, 2, 1): return U'\u2539';
				case PackState(2, 0, 1, 2): return U'\u253A';
				case PackState(2, 0, 2, 2): return U'\u253B';
				case PackState(1, 1, 1, 1): return U'\u253C';
				case PackState(1, 1, 2, 1): return U'\u253D';
				case PackState(1, 1, 1, 2): return U'\u253E';
				case PackState(1, 1, 2, 2): return U'\u253F';
				case PackState(2, 1, 1, 1): return U'\u2540';
				case PackState(1, 2, 1, 1): return U'\u2541';
				case PackState(2, 2, 1, 1): return U'\u2542';
				case PackState(2, 1, 2, 1): return U'\u2543';
				case PackState(2, 1, 1, 2): return U'\u2544';
				case PackState(1, 2, 2, 1): return U'\u2545';
				case PackState(1, 2, 1, 2): return U'\u2546';
				case PackState(2, 1, 2, 2): return U'\u2547';
				case PackState(1, 2, 2, 2): return U'\u2548';
				case PackState(2, 2, 2, 1): return U'\u2549';
				case PackState(2, 2, 1, 2): return U'\u254A';
				case PackState(2, 2, 2, 2): return U'\u254B';
				case PackState(0, 0, 3, 3): return U'\u2550';
				case PackState(3, 3, 0, 0): return U'\u2551';
				case PackState(0, 1, 0, 3): return U'\u2552';
				case PackState(0, 3, 0, 1): return U'\u2553';
				case PackState(0, 3, 0, 3): return U'\u2554';
				case PackState(0, 1, 3, 0): return U'\u2555';
				case PackState(0, 3, 1, 0): return U'\u2556';
				case PackState(0, 3, 3, 0): return U'\u2557';
				case PackState(1, 0, 0, 3): return U'\u2558';
				case PackState(3, 0, 0, 1): return U'\u2559';
				case PackState(3, 0, 0, 3): return U'\u255A';
				case PackState(1, 0, 3, 0): return U'\u255B';
				case PackState(3, 0, 1, 0): return U'\u255C';
				case PackState(3, 0, 3, 0): return U'\u255D';
				case PackState(1, 1, 0, 3): return U'\u255E';
				case PackState(3, 3, 0, 1): return U'\u255F';
				case PackState(3, 3, 0, 3): return U'\u2560';
				case PackState(1, 1, 3, 0): return U'\u2561';
				case PackState(3, 3, 1, 0): return U'\u2562';
				case PackState(3, 3, 3, 0): return U'\u2563';
				case PackState(0, 1, 3, 3): return U'\u2564';
				case PackState(0, 3, 1, 1): return U'\u2565';
				case PackState(0, 3, 3, 3): return U'\u2566';
				case PackState(1, 0, 3, 3): return U'\u2567';
				case PackState(3, 0, 1, 1): return U'\u2568';
				case PackState(3, 0, 3, 3): return U'\u2569';
				case PackState(1, 1, 3, 3): return U'\u256A';
				case PackState(3, 3, 1, 1): return U'\u256B';
				case PackState(3, 3, 3, 3): return U'\u256C';
				case PackState(0, 0, 1, 0): return U'\u2574';
				case PackState(1, 0, 0, 0): return U'\u2575';
				case PackState(0, 0, 0, 1): return U'\u2576';
				case PackState(0, 1, 0, 0): return U'\u2577';
				case PackState(0, 0, 2, 0): return U'\u2578';
				case PackState(2, 0, 0, 0): return U'\u2579';
				case PackState(0, 0, 0, 2): return U'\u257A';
				case PackState(0, 2, 0, 0): return U'\u257B';
				case PackState(0, 0, 1, 2): return U'\u257C';
				case PackState(1, 2, 0, 0): return U'\u257D';
				case PackState(0, 0, 2, 1): return U'\u257E';
				case PackState(2, 1, 0, 0): return U'\u257F';
				default: return 0;
				}
			}

			bool IsEmptyMergeable(const TuiMergeablePixel& pixel)
			{
				return
					pixel.up == TuiMergeableGlyph::None &&
					pixel.down == TuiMergeableGlyph::None &&
					pixel.left == TuiMergeableGlyph::None &&
					pixel.right == TuiMergeableGlyph::None;
			}

			bool IsLineGlyph(TuiMergeableGlyph glyph)
			{
				return
					glyph == TuiMergeableGlyph::ThinLine ||
					glyph == TuiMergeableGlyph::ThickLine ||
					glyph == TuiMergeableGlyph::DoubleLine;
			}

			bool IsColorMode(TuiColorMode colorMode, bool allowAuto)
			{
				return
					(allowAuto && colorMode == TuiColorMode::Auto) ||
					colorMode == TuiColorMode::TrueColor ||
					colorMode == TuiColorMode::Color256 ||
					colorMode == TuiColorMode::Color16;
			}

			char32_t GetUnmergeableChar(const TuiUnmergeablePixel& pixel)
			{
				if (pixel.glyph != TuiUnmergeableGlyph::RoundCorner) return 0;
				switch (pixel.direction)
				{
				case TuiUnmergeableDirection::LeftTop: return U'\u256D';
				case TuiUnmergeableDirection::RightTop: return U'\u256E';
				case TuiUnmergeableDirection::LeftBottom: return U'\u2570';
				case TuiUnmergeableDirection::RightBottom: return U'\u256F';
				default: return 0;
				}
			}

			bool IsScalar(char32_t code)
			{
				return code <= 0x10FFFF && !(code >= 0xD800 && code <= 0xDFFF);
			}

			TuiPixel EmptyPixel(TuiColor background = { 0, 0, 0 })
			{
				TuiPixel pixel;
				pixel.backgroundColor = background;
				return pixel;
			}

			void CheckBuffer(TuiPixel* buffer, vint width, vint height)
			{
				CHECK_ERROR(buffer != nullptr, L"vl::console::TUI drawing helper requires a non-null buffer.");
				CHECK_ERROR(width >= 0 && height >= 0, L"vl::console::TUI drawing helper requires non-negative dimensions.");
			}

			void RepairWide(TuiPixel* buffer, vint width, vint height, vint x, vint y)
			{
				if (x < 0 || x >= width || y < 0 || y >= height) return;
				auto index = y * width + x;
				auto& pixel = buffer[index];
				if (pixel.glyph == TuiPixelGlyph::WideCharContinuation)
				{
					pixel = EmptyPixel(pixel.backgroundColor);
					if (x > 0)
					{
						auto& leading = buffer[index - 1];
						if (leading.glyph == TuiPixelGlyph::Char && TUI::MeasureChar(leading.c) == 2)
						{
							leading = EmptyPixel(leading.backgroundColor);
						}
					}
				}
				else if (pixel.glyph == TuiPixelGlyph::Char && TUI::MeasureChar(pixel.c) == 2)
				{
					pixel = EmptyPixel(pixel.backgroundColor);
					if (x + 1 < width && buffer[index + 1].glyph == TuiPixelGlyph::WideCharContinuation)
					{
						auto background = buffer[index + 1].backgroundColor;
						buffer[index + 1] = EmptyPixel(background);
					}
				}
			}

			void PlaceMergeable(TuiPixel* buffer, vint width, vint height, vint x, vint y, const TuiMergeablePixel& drawing, TuiColor foreground, Nullable<TuiColor> background)
			{
				if (x < 0 || x >= width || y < 0 || y >= height) return;
				RepairWide(buffer, width, height, x, y);
				auto& pixel = buffer[y * width + x];
				auto candidate = drawing;
				if (pixel.glyph == TuiPixelGlyph::Mergeable)
				{
					candidate = pixel.mergeable;
					if (drawing.up != TuiMergeableGlyph::None) candidate.up = drawing.up;
					if (drawing.down != TuiMergeableGlyph::None) candidate.down = drawing.down;
					if (drawing.left != TuiMergeableGlyph::None) candidate.left = drawing.left;
					if (drawing.right != TuiMergeableGlyph::None) candidate.right = drawing.right;
					if (GetMergeableChar(candidate) == 0)
					{
						candidate = drawing;
					}
				}
				pixel.glyph = TuiPixelGlyph::Mergeable;
				pixel.mergeable = candidate;
				pixel.foregroundColor = foreground;
				if (background) pixel.backgroundColor = background.Value();
			}

			void PlaceUnmergeable(TuiPixel* buffer, vint width, vint height, vint x, vint y, TuiUnmergeableDirection direction, TuiColor foreground, Nullable<TuiColor> background)
			{
				if (x < 0 || x >= width || y < 0 || y >= height) return;
				RepairWide(buffer, width, height, x, y);
				auto& pixel = buffer[y * width + x];
				pixel.glyph = TuiPixelGlyph::Unmergeable;
				pixel.unmergeable = { TuiUnmergeableGlyph::RoundCorner, direction };
				pixel.foregroundColor = foreground;
				if (background) pixel.backgroundColor = background.Value();
			}

			void CheckOwner(auto& storage)
			{
				CHECK_ERROR(storage.active, L"vl::console::TUI operation requires an active TUI.");
				CHECK_ERROR(storage.ownerThreadId == Thread::GetCurrentThreadId(), L"vl::console::TUI operation must run on the owner thread.");
			}

			vint FindListener(auto* storage, ITuiCallback* listener, vuint64_t generation = 0)
			{
				if (!storage) return -1;
				for (vint i = 0; i < storage->listeners.Count(); i++)
				{
					auto entry = storage->listeners[i];
					if (entry.listener == listener && (generation == 0 || entry.generation == generation)) return i;
				}
				return -1;
			}

			template<typename TCallback>
			void InvokeListeners(auto& storage, auto* listenerStorage, TCallback&& callback, bool stopping = false)
			{
				if (!listenerStorage) return;
				List<ListenerEntry> snapshot;
				for (auto entry : listenerStorage->listeners) snapshot.Add(entry);
				for (auto entry : snapshot)
				{
					if (!stopping && storage.stopRequested) break;
					if (FindListener(listenerStorage, entry.listener, entry.generation) == -1) continue;
					try
					{
						callback(entry.listener);
					}
					catch (...)
					{
						if (!storage.callbackException) storage.callbackException = std::current_exception();
						storage.stopRequested = true;
						throw;
					}
					if (storage.callbackException) std::rethrow_exception(storage.callbackException);
				}
			}

			void ResizeBuffer(auto& storage, vint width, vint height)
			{
				CHECK_ERROR(width > 0 && height > 0, L"vl::console::TUI backend returned an invalid terminal size.");
				Array<TuiPixel> newBuffer(width * height);
				auto copyWidth = width < storage.width ? width : storage.width;
				auto copyHeight = height < storage.height ? height : storage.height;
				for (vint y = 0; y < copyHeight; y++)
				{
					for (vint x = 0; x < copyWidth; x++)
					{
						newBuffer[y * width + x] = storage.buffer[y * storage.width + x];
					}
				}
				for (vint y = 0; y < height; y++)
				{
					for (vint x = 0; x < width; x++)
					{
						auto index = y * width + x;
						auto& pixel = newBuffer[index];
						if (pixel.glyph == TuiPixelGlyph::WideCharContinuation)
						{
							if (x == 0 || newBuffer[index - 1].glyph != TuiPixelGlyph::Char || TUI::MeasureChar(newBuffer[index - 1].c) != 2)
							{
								pixel = EmptyPixel(pixel.backgroundColor);
							}
						}
						else if (pixel.glyph == TuiPixelGlyph::Char && TUI::MeasureChar(pixel.c) == 2)
						{
							if (x + 1 >= width || newBuffer[index + 1].glyph != TuiPixelGlyph::WideCharContinuation)
							{
								pixel = EmptyPixel(pixel.backgroundColor);
								if (x + 1 < width && newBuffer[index + 1].glyph == TuiPixelGlyph::WideCharContinuation)
								{
									auto background = newBuffer[index + 1].backgroundColor;
									newBuffer[index + 1] = EmptyPixel(background);
								}
							}
						}
					}
				}
				storage.buffer = std::move(newBuffer);
				storage.width = width;
				storage.height = height;
			}

			void DispatchEvent(auto& storage, auto* listenerStorage, const unittest::TuiBackendEvent& event)
			{
				if (storage.stopRequested) return;
				switch (event.type)
				{
				case unittest::TuiBackendEventType::Resize:
					if (event.width != storage.width || event.height != storage.height)
					{
						ResizeBuffer(storage, event.width, event.height);
						InvokeListeners(storage, listenerStorage, [](ITuiCallback* listener) { listener->BufferSizeChanged(); });
					}
					break;
				case unittest::TuiBackendEventType::MouseMove:
					InvokeListeners(storage, listenerStorage, [&](ITuiCallback* listener) { listener->MouseMove(event.mouseInfo); });
					break;
				case unittest::TuiBackendEventType::MouseDown:
					InvokeListeners(storage, listenerStorage, [&](ITuiCallback* listener) { listener->MouseDown(event.mouseButton, event.mouseInfo); });
					break;
				case unittest::TuiBackendEventType::MouseUp:
					InvokeListeners(storage, listenerStorage, [&](ITuiCallback* listener) { listener->MouseUp(event.mouseButton, event.mouseInfo); });
					break;
				case unittest::TuiBackendEventType::MouseDoubleClick:
					InvokeListeners(storage, listenerStorage, [&](ITuiCallback* listener) { listener->MouseDoubleClick(event.mouseButton, event.mouseInfo); });
					break;
				case unittest::TuiBackendEventType::MouseVerticalWheel:
					InvokeListeners(storage, listenerStorage, [&](ITuiCallback* listener) { listener->MouseVerticalWheel(event.mouseInfo); });
					break;
				case unittest::TuiBackendEventType::MouseHorizontalWheel:
					InvokeListeners(storage, listenerStorage, [&](ITuiCallback* listener) { listener->MouseHorizontalWheel(event.mouseInfo); });
					break;
				case unittest::TuiBackendEventType::KeyDown:
					InvokeListeners(storage, listenerStorage, [&](ITuiCallback* listener) { listener->KeyDown(event.keyInfo); });
					break;
				case unittest::TuiBackendEventType::KeyUp:
					InvokeListeners(storage, listenerStorage, [&](ITuiCallback* listener) { listener->KeyUp(event.keyInfo); });
					break;
				case unittest::TuiBackendEventType::Char:
					InvokeListeners(storage, listenerStorage, [&](ITuiCallback* listener) { listener->Char(event.charInfo); });
					break;
				default:
					break;
				}
			}

			const TuiColor canonicalColor16[] =
			{
				{ 0x00, 0x00, 0x00 }, { 0x80, 0x00, 0x00 }, { 0x00, 0x80, 0x00 }, { 0x80, 0x80, 0x00 },
				{ 0x00, 0x00, 0x80 }, { 0x80, 0x00, 0x80 }, { 0x00, 0x80, 0x80 }, { 0xC0, 0xC0, 0xC0 },
				{ 0x80, 0x80, 0x80 }, { 0xFF, 0x00, 0x00 }, { 0x00, 0xFF, 0x00 }, { 0xFF, 0xFF, 0x00 },
				{ 0x00, 0x00, 0xFF }, { 0xFF, 0x00, 0xFF }, { 0x00, 0xFF, 0xFF }, { 0xFF, 0xFF, 0xFF },
			};

			TuiColor GetCanonicalColor(vint index)
			{
				CHECK_ERROR(index >= 0 && index < 256, L"vl::console::tui_internal::GetCanonicalColor(vint)#Index out of range.");
				if (index < 16) return canonicalColor16[index];
				if (index < 232)
				{
					auto value = index - 16;
					vuint8_t levels[] = { 0, 95, 135, 175, 215, 255 };
					return { levels[value / 36], levels[(value / 6) % 6], levels[value % 6] };
				}
				auto level = (vuint8_t)(8 + 10 * (index - 232));
				return { level, level, level };
			}

			vint QuantizeColor(TuiColor color, TuiColorMode colorMode, const TuiColor* customColor16)
			{
				auto count = colorMode == TuiColorMode::Color16 ? 16 : 256;
				vint best = 0;
				vint bestDistance = -1;
				for (vint i = 0; i < count; i++)
				{
					auto candidate = customColor16 && i < 16 ? customColor16[i] : GetCanonicalColor(i);
					auto dr = (vint)color.r - candidate.r;
					auto dg = (vint)color.g - candidate.g;
					auto db = (vint)color.b - candidate.b;
					auto distance = dr * dr + dg * dg + db * db;
					if (bestDistance == -1 || distance < bestDistance)
					{
						best = i;
						bestDistance = distance;
					}
				}
				return best;
			}
		}

		using namespace tui_internal;

/***********************************************************************
TuiPixel
***********************************************************************/

		char32_t TuiPixel::GetChar32() const
		{
			switch (glyph)
			{
			case TuiPixelGlyph::Char:
				return c;
			case TuiPixelGlyph::Mergeable:
				return GetMergeableChar(mergeable);
			case TuiPixelGlyph::Unmergeable:
				return GetUnmergeableChar(unmergeable);
			default:
				return 0;
			}
		}

		wchar_t TuiPixel::GetWChar() const
		{
			auto code = GetChar32();
			if (!IsScalar(code) || code == 0) return 0;
			if constexpr (sizeof(wchar_t) == 2)
			{
				return code <= 0xFFFF ? (wchar_t)code : 0;
			}
			else
			{
				return (wchar_t)code;
			}
		}

/***********************************************************************
ITuiCallback
***********************************************************************/

		void ITuiCallback::Starting() {}
		void ITuiCallback::Stopping() {}
		void ITuiCallback::BufferSizeChanged() {}
		void ITuiCallback::MouseMove(const TuiMouseInfo&) {}
		void ITuiCallback::MouseDown(TuiMouseButton, const TuiMouseInfo&) {}
		void ITuiCallback::MouseUp(TuiMouseButton, const TuiMouseInfo&) {}
		void ITuiCallback::MouseDoubleClick(TuiMouseButton, const TuiMouseInfo&) {}
		void ITuiCallback::MouseVerticalWheel(const TuiMouseInfo&) {}
		void ITuiCallback::MouseHorizontalWheel(const TuiMouseInfo&) {}
		void ITuiCallback::KeyDown(const TuiKeyInfo&) {}
		void ITuiCallback::KeyUp(const TuiKeyInfo&) {}
		void ITuiCallback::Char(const TuiCharInfo&) {}
		void ITuiCallback::Timer() {}

/***********************************************************************
TUI
***********************************************************************/

		TUI::Impl& TUI::GetImpl()
		{
			CHECK_ERROR(impl, L"vl::console::TUI operation requires an active TUI.");
			CheckOwner(*impl);
			return *impl;
		}

		bool TUI::TryGetConsoleSize(vint& width, vint& height)
		{
			if (impl && impl->active)
			{
				CheckOwner(*impl);
				return impl->backend->TryGetConsoleSize(width, height);
			}
			if (injectedBackend)
			{
				return (*injectedBackend)->TryGetConsoleSize(width, height);
			}
			auto backend = CreateTuiBackend();
			return backend->TryGetConsoleSize(width, height);
		}

		void TUI::Start(const TuiStartOptions& options)
		{
			if (impl)
			{
				if (impl->active) CheckOwner(*impl);
				return;
			}
			CHECK_ERROR(IsColorMode(options.colorMode, true), L"vl::console::TUI::Start(const TuiStartOptions&)#The requested color mode is invalid.");
			CHECK_ERROR(Console::IsEnabled(), L"vl::console::TUI::Start(const TuiStartOptions&)#Console must be enabled before TUI starts.");

			auto storage = new Impl;
			impl = storage;
			std::exception_ptr thrown;
			try
			{
				storage->backend = injectedBackend ? *injectedBackend : CreateTuiBackend();
				storage->colorMode = storage->backend->Start(options);
				storage->backendStarted = true;
				CHECK_ERROR(IsColorMode(storage->colorMode, false), L"vl::console::TUI::Start(const TuiStartOptions&)#The backend selected an invalid color mode.");

				vint width = 0;
				vint height = 0;
				CHECK_ERROR(storage->backend->TryGetConsoleSize(width, height), L"vl::console::TUI::Start(const TuiStartOptions&)#Failed to query the terminal size.");
				Console::Disable();
				storage->consoleDisabled = true;
				ResizeBuffer(*storage, width, height);
				storage->ownerThreadId = Thread::GetCurrentThreadId();
				storage->active = true;

				InvokeListeners(*storage, listenerStorage, [](ITuiCallback* listener) { listener->Starting(); });
				if (!storage->stopRequested)
				{
					InvokeListeners(*storage, listenerStorage, [](ITuiCallback* listener) { listener->BufferSizeChanged(); });
				}
				while (!storage->stopRequested)
				{
					if (!RunOneCycle()) break;
				}
				if (!storage->callbackException)
				{
					storage->shuttingDown = true;
					InvokeListeners(*storage, listenerStorage, [](ITuiCallback* listener) { listener->Stopping(); }, true);
				}
			}
			catch (...)
			{
				thrown = std::current_exception();
			}
			storage->cleanupException = &thrown;
			impl = nullptr;
			delete storage;
			if (listenerStorage && listenerStorage->listeners.Count() == 0)
			{
				delete listenerStorage;
				listenerStorage = nullptr;
			}
			if (thrown) std::rethrow_exception(thrown);
		}

		bool TUI::RunOneCycle()
		{
			auto& storage = GetImpl();
			if (storage.callbackException) std::rethrow_exception(storage.callbackException);
			if (storage.stopRequested) return false;

			if (storage.eventQueue.Count() == 0)
			{
				auto now = storage.backend->GetMonotonicTime();
				vint timeout = -1;
				if (storage.timerPeriod > 0)
				{
					if (now >= storage.nextTimer)
					{
						storage.nextTimer += storage.timerPeriod;
						InvokeListeners(storage, listenerStorage, [](ITuiCallback* listener) { listener->Timer(); });
						return !storage.stopRequested;
					}
					auto remaining = storage.nextTimer - now;
					timeout = remaining > (vuint64_t)0x7FFFFFFF ? 0x7FFFFFFF : (vint)remaining;
				}
				unittest::TuiBackendEvent event;
				if (storage.backend->ReadEvent(timeout, event))
				{
					storage.eventQueue.Add(event);
				}
				else if (storage.timerPeriod > 0 && storage.backend->GetMonotonicTime() >= storage.nextTimer)
				{
					storage.nextTimer += storage.timerPeriod;
					InvokeListeners(storage, listenerStorage, [](ITuiCallback* listener) { listener->Timer(); });
					return !storage.stopRequested;
				}
			}

			if (storage.eventQueue.Count() > 0)
			{
				auto event = storage.eventQueue[0];
				storage.eventQueue.RemoveAt(0);
				DispatchEvent(storage, listenerStorage, event);
			}
			if (storage.callbackException) std::rethrow_exception(storage.callbackException);
			return !storage.stopRequested;
		}

		void TUI::Stop()
		{
			if (!impl || !impl->active) return;
			auto& storage = *impl;
			CheckOwner(storage);
			if (storage.shuttingDown) return;
			storage.stopRequested = true;
		}

		bool TUI::IsInUse()
		{
			if (!impl || !impl->active) return false;
			CheckOwner(*impl);
			return true;
		}

		bool TUI::IsStopRequested()
		{
			if (!impl || !impl->active) return false;
			auto& storage = *impl;
			CheckOwner(storage);
			return storage.stopRequested;
		}

		TuiColorMode TUI::GetColorMode()
		{
			auto& storage = GetImpl();
			return storage.colorMode;
		}

		bool TUI::InstallListener(ITuiCallback* listener)
		{
			if (impl && impl->active) CheckOwner(*impl);
			if (!listener || FindListener(listenerStorage, listener) != -1) return false;
			if (!listenerStorage) listenerStorage = new ListenerStorage;
			listenerStorage->listeners.Add({ listener, ++listenerStorage->nextGeneration });
			return true;
		}

		bool TUI::UninstallListener(ITuiCallback* listener)
		{
			if (impl && impl->active) CheckOwner(*impl);
			if (!listener) return false;
			auto index = FindListener(listenerStorage, listener);
			if (index == -1) return false;
			listenerStorage->listeners.RemoveAt(index);
			if (!impl && listenerStorage->listeners.Count() == 0)
			{
				delete listenerStorage;
				listenerStorage = nullptr;
			}
			return true;
		}

		void TUI::StartTimer(vint milliseconds)
		{
			auto& storage = GetImpl();
			CHECK_ERROR(milliseconds > 0, L"vl::console::TUI::StartTimer(vint)#The timer period must be positive.");
			storage.timerPeriod = milliseconds;
			storage.nextTimer = storage.backend->GetMonotonicTime() + milliseconds;
		}

		void TUI::StopTimer()
		{
			auto& storage = GetImpl();
			storage.timerPeriod = 0;
			storage.nextTimer = 0;
		}

		TuiPixel* TUI::GetBuffer()
		{
			auto& storage = GetImpl();
			return storage.buffer.Count() == 0 ? nullptr : &storage.buffer[0];
		}

		vint TUI::GetBufferWidth()
		{
			auto& storage = GetImpl();
			return storage.width;
		}

		vint TUI::GetBufferHeight()
		{
			auto& storage = GetImpl();
			return storage.height;
		}

		void TUI::RenderBuffer()
		{
			auto& storage = GetImpl();
			for (vint y = 0; y < storage.height; y++)
			{
				for (vint x = 0; x < storage.width; x++)
				{
					auto index = y * storage.width + x;
					auto& pixel = storage.buffer[index];
					switch (pixel.glyph)
					{
					case TuiPixelGlyph::Char:
						if (pixel.c != 0)
						{
							CHECK_ERROR(IsScalar(pixel.c), L"vl::console::TUI::RenderBuffer()#A Char cell contains an invalid Unicode scalar.");
							auto width = MeasureChar(pixel.c);
							CHECK_ERROR(width == 1 || width == 2, L"vl::console::TUI::RenderBuffer()#A Char cell contains a zero-width or non-printable scalar.");
							if (width == 2)
							{
								CHECK_ERROR(x + 1 < storage.width && storage.buffer[index + 1].glyph == TuiPixelGlyph::WideCharContinuation, L"vl::console::TUI::RenderBuffer()#A width-two Char cell has no continuation.");
								CHECK_ERROR(storage.buffer[index + 1].foregroundColor == pixel.foregroundColor && storage.buffer[index + 1].backgroundColor == pixel.backgroundColor, L"vl::console::TUI::RenderBuffer()#A width-two continuation has different colors.");
							}
						}
						break;
					case TuiPixelGlyph::Mergeable:
						CHECK_ERROR(GetMergeableChar(pixel.mergeable) != 0 || IsEmptyMergeable(pixel.mergeable), L"vl::console::TUI::RenderBuffer()#A Mergeable cell contains an unsupported arm state.");
						break;
					case TuiPixelGlyph::Unmergeable:
						CHECK_ERROR(GetUnmergeableChar(pixel.unmergeable) != 0, L"vl::console::TUI::RenderBuffer()#An Unmergeable cell contains an invalid glyph.");
						break;
					case TuiPixelGlyph::WideCharContinuation:
						CHECK_ERROR(x > 0 && storage.buffer[index - 1].glyph == TuiPixelGlyph::Char && MeasureChar(storage.buffer[index - 1].c) == 2, L"vl::console::TUI::RenderBuffer()#A continuation cell has no width-two leading cell.");
						break;
					default:
						CHECK_FAIL(L"vl::console::TUI::RenderBuffer()#A cell contains an invalid glyph type.");
					}
				}
			}
			storage.backend->Render(&storage.buffer[0], storage.width, storage.height, storage.colorMode);
		}

		void TUI::PrintChar(const TuiPrintOptions& options, char32_t code, vint x, vint y)
		{
			PrintChar(GetBuffer(), GetBufferWidth(), GetBufferHeight(), options, code, x, y);
		}

		void TUI::DrawLineV(const TuiLineOptions& options, vint x, vint y1, vint y2)
		{
			DrawLineV(GetBuffer(), GetBufferWidth(), GetBufferHeight(), options, x, y1, y2);
		}

		void TUI::DrawLineH(const TuiLineOptions& options, vint x1, vint x2, vint y)
		{
			DrawLineH(GetBuffer(), GetBufferWidth(), GetBufferHeight(), options, x1, x2, y);
		}

		void TUI::DrawRect(const TuiRectOptions& options, vint x1, vint y1, vint x2, vint y2)
		{
			DrawRect(GetBuffer(), GetBufferWidth(), GetBufferHeight(), options, x1, y1, x2, y2);
		}

		void TUI::Clear(TuiColor backgroundColor, vint x1, vint y1, vint x2, vint y2)
		{
			Clear(GetBuffer(), GetBufferWidth(), GetBufferHeight(), backgroundColor, x1, y1, x2, y2);
		}

		void TUI::PrintChar(TuiPixel* buffer, vint width, vint height, const TuiPrintOptions& options, char32_t code, vint x, vint y)
		{
			CheckBuffer(buffer, width, height);
			CHECK_ERROR(IsScalar(code), L"vl::console::TUI::PrintChar(...)#The character must be a Unicode scalar.");
			auto charWidth = MeasureChar(code);
			if (charWidth == 0 || x < 0 || x >= width || y < 0 || y >= height) return;
			if (charWidth == 2 && x + 1 >= width) return;
			RepairWide(buffer, width, height, x, y);
			if (charWidth == 2) RepairWide(buffer, width, height, x + 1, y);
			auto& leading = buffer[y * width + x];
			leading.glyph = TuiPixelGlyph::Char;
			leading.c = code;
			leading.foregroundColor = options.foregroundColor;
			leading.backgroundColor = options.backgroundColor;
			if (charWidth == 2)
			{
				auto& continuation = buffer[y * width + x + 1];
				continuation.glyph = TuiPixelGlyph::WideCharContinuation;
				continuation.c = 0;
				continuation.foregroundColor = options.foregroundColor;
				continuation.backgroundColor = options.backgroundColor;
			}
		}

		void TUI::DrawLineV(TuiPixel* buffer, vint width, vint height, const TuiLineOptions& options, vint x, vint y1, vint y2)
		{
			CheckBuffer(buffer, width, height);
			CHECK_ERROR(y1 <= y2, L"vl::console::TUI::DrawLineV(...)#The ordered range is invalid.");
			CHECK_ERROR(IsLineGlyph(options.glyph), L"vl::console::TUI::DrawLineV(...)#The line style is invalid.");
			if (x < 0 || x >= width || y2 < 0 || y1 >= height) return;
			auto begin = y1 < 0 ? 0 : y1;
			auto end = y2 >= height ? height - 1 : y2;
			TuiMergeablePixel drawing = { options.glyph, options.glyph, TuiMergeableGlyph::None, TuiMergeableGlyph::None };
			for (vint y = begin; y <= end; y++) PlaceMergeable(buffer, width, height, x, y, drawing, options.foregroundColor, options.backgroundColor);
		}

		void TUI::DrawLineH(TuiPixel* buffer, vint width, vint height, const TuiLineOptions& options, vint x1, vint x2, vint y)
		{
			CheckBuffer(buffer, width, height);
			CHECK_ERROR(x1 <= x2, L"vl::console::TUI::DrawLineH(...)#The ordered range is invalid.");
			CHECK_ERROR(IsLineGlyph(options.glyph), L"vl::console::TUI::DrawLineH(...)#The line style is invalid.");
			if (y < 0 || y >= height || x2 < 0 || x1 >= width) return;
			auto begin = x1 < 0 ? 0 : x1;
			auto end = x2 >= width ? width - 1 : x2;
			TuiMergeablePixel drawing = { TuiMergeableGlyph::None, TuiMergeableGlyph::None, options.glyph, options.glyph };
			for (vint x = begin; x <= end; x++) PlaceMergeable(buffer, width, height, x, y, drawing, options.foregroundColor, options.backgroundColor);
		}

		void TUI::DrawRect(TuiPixel* buffer, vint width, vint height, const TuiRectOptions& options, vint x1, vint y1, vint x2, vint y2)
		{
			CheckBuffer(buffer, width, height);
			CHECK_ERROR(x1 < x2 && y1 < y2, L"vl::console::TUI::DrawRect(...)#A rectangle must have distinct corners.");
			CHECK_ERROR(IsLineGlyph(options.glyph), L"vl::console::TUI::DrawRect(...)#The line style is invalid.");
			CHECK_ERROR(options.corner == TuiRectCorner::Sharp || options.corner == TuiRectCorner::Round, L"vl::console::TUI::DrawRect(...)#The corner style is invalid.");
			CHECK_ERROR(options.corner == TuiRectCorner::Sharp || options.glyph == TuiMergeableGlyph::ThinLine, L"vl::console::TUI::DrawRect(...)#Rounded corners require a thin line.");
			if (x2 < 0 || y2 < 0 || x1 >= width || y1 >= height) return;

			TuiMergeablePixel horizontal = { TuiMergeableGlyph::None, TuiMergeableGlyph::None, options.glyph, options.glyph };
			TuiMergeablePixel vertical = { options.glyph, options.glyph, TuiMergeableGlyph::None, TuiMergeableGlyph::None };
			auto left = x1 + 1 < 0 ? 0 : x1 + 1;
			auto right = x2 - 1 >= width ? width - 1 : x2 - 1;
			for (vint x = left; x <= right; x++)
			{
				PlaceMergeable(buffer, width, height, x, y1, horizontal, options.foregroundColor, options.backgroundColor);
				PlaceMergeable(buffer, width, height, x, y2, horizontal, options.foregroundColor, options.backgroundColor);
			}
			auto top = y1 + 1 < 0 ? 0 : y1 + 1;
			auto bottom = y2 - 1 >= height ? height - 1 : y2 - 1;
			for (vint y = top; y <= bottom; y++)
			{
				PlaceMergeable(buffer, width, height, x1, y, vertical, options.foregroundColor, options.backgroundColor);
				PlaceMergeable(buffer, width, height, x2, y, vertical, options.foregroundColor, options.backgroundColor);
			}

			if (options.corner == TuiRectCorner::Round)
			{
				PlaceUnmergeable(buffer, width, height, x1, y1, TuiUnmergeableDirection::LeftTop, options.foregroundColor, options.backgroundColor);
				PlaceUnmergeable(buffer, width, height, x2, y1, TuiUnmergeableDirection::RightTop, options.foregroundColor, options.backgroundColor);
				PlaceUnmergeable(buffer, width, height, x1, y2, TuiUnmergeableDirection::LeftBottom, options.foregroundColor, options.backgroundColor);
				PlaceUnmergeable(buffer, width, height, x2, y2, TuiUnmergeableDirection::RightBottom, options.foregroundColor, options.backgroundColor);
			}
			else
			{
				PlaceMergeable(buffer, width, height, x1, y1, { TuiMergeableGlyph::None, options.glyph, TuiMergeableGlyph::None, options.glyph }, options.foregroundColor, options.backgroundColor);
				PlaceMergeable(buffer, width, height, x2, y1, { TuiMergeableGlyph::None, options.glyph, options.glyph, TuiMergeableGlyph::None }, options.foregroundColor, options.backgroundColor);
				PlaceMergeable(buffer, width, height, x1, y2, { options.glyph, TuiMergeableGlyph::None, TuiMergeableGlyph::None, options.glyph }, options.foregroundColor, options.backgroundColor);
				PlaceMergeable(buffer, width, height, x2, y2, { options.glyph, TuiMergeableGlyph::None, options.glyph, TuiMergeableGlyph::None }, options.foregroundColor, options.backgroundColor);
			}
		}

		void TUI::Clear(TuiPixel* buffer, vint width, vint height, TuiColor backgroundColor, vint x1, vint y1, vint x2, vint y2)
		{
			CheckBuffer(buffer, width, height);
			CHECK_ERROR(x1 <= x2 && y1 <= y2, L"vl::console::TUI::Clear(...)#The ordered rectangle is invalid.");
			if (x2 < 0 || y2 < 0 || x1 >= width || y1 >= height) return;
			auto left = x1 < 0 ? 0 : x1;
			auto top = y1 < 0 ? 0 : y1;
			auto right = x2 >= width ? width - 1 : x2;
			auto bottom = y2 >= height ? height - 1 : y2;
			for (vint y = top; y <= bottom; y++)
			{
				for (vint x = left; x <= right; x++)
				{
					RepairWide(buffer, width, height, x, y);
					buffer[y * width + x] = EmptyPixel(backgroundColor);
				}
			}
		}

/***********************************************************************
ScopedTuiBackend
***********************************************************************/

		namespace unittest
		{
			ScopedTuiBackend::ScopedTuiBackend(Ptr<ITuiBackend> backend)
			{
				CHECK_ERROR(!TUI::impl, L"vl::console::unittest::ScopedTuiBackend::ScopedTuiBackend(...)#Cannot replace the backend while TUI is active.");
				CHECK_ERROR(backend, L"vl::console::unittest::ScopedTuiBackend::ScopedTuiBackend(...)#The backend cannot be null.");
				previous = TUI::injectedBackend;
				current = backend;
				TUI::injectedBackend = &current;
			}

			ScopedTuiBackend::~ScopedTuiBackend() noexcept(false)
			{
				CHECK_ERROR(!TUI::impl, L"vl::console::unittest::ScopedTuiBackend::~ScopedTuiBackend()#Cannot restore the backend while TUI is active.");
				TUI::injectedBackend = previous;
			}
		}
	}
}

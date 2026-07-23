/***********************************************************************
Author: Zihan Chen (vczh)
Licensed under https://github.com/vczh-libraries/License
***********************************************************************/

#ifndef VCZH_TUI
#define VCZH_TUI

#include "../Threading.h"

namespace vl
{
	namespace console
	{
		enum class TuiColorMode
		{
			Auto,
			TrueColor,
			Color256,
			Color16,
		};

		struct TuiStartOptions
		{
			TuiColorMode					colorMode = TuiColorMode::Auto;
		};

		struct TuiColor
		{
			vuint8_t						r = 0;
			vuint8_t						g = 0;
			vuint8_t						b = 0;

			auto operator<=>(const TuiColor&) const = default;
		};

		enum class TuiMergeableGlyph : vuint8_t
		{
			None = 0,
			ThinLine = 1,
			ThickLine = 2,
			DoubleLine = 3,
		};

		struct TuiMergeablePixel
		{
			TuiMergeableGlyph				up = TuiMergeableGlyph::None;
			TuiMergeableGlyph				down = TuiMergeableGlyph::None;
			TuiMergeableGlyph				left = TuiMergeableGlyph::None;
			TuiMergeableGlyph				right = TuiMergeableGlyph::None;
		};

		enum class TuiUnmergeableGlyph : vuint8_t
		{
			RoundCorner,
		};

		enum class TuiUnmergeableDirection : vuint8_t
		{
			LeftTop,
			RightTop,
			LeftBottom,
			RightBottom,
		};

		struct TuiUnmergeablePixel
		{
			TuiUnmergeableGlyph				glyph = TuiUnmergeableGlyph::RoundCorner;
			TuiUnmergeableDirection			direction = TuiUnmergeableDirection::LeftTop;
		};

		enum class TuiPixelGlyph : vuint8_t
		{
			Char,
			Mergeable,
			Unmergeable,
			WideCharContinuation,
		};

		struct TuiPixel
		{
			TuiPixelGlyph					glyph = TuiPixelGlyph::Char;
			union
			{
				char32_t					c = 0;
				TuiMergeablePixel			mergeable;
				TuiUnmergeablePixel			unmergeable;
			};
			TuiColor						foregroundColor = { 255, 255, 255 };
			TuiColor						backgroundColor = { 0, 0, 0 };

			char32_t						GetChar32() const;
			wchar_t							GetWChar() const;
		};

		struct TuiMouseInfo
		{
			vint							x = 0;
			vint							y = 0;
			vint							wheel = 0;
			bool							ctrl = false;
			bool							shift = false;
			bool							alt = false;
			bool							left = false;
			bool							middle = false;
			bool							right = false;
		};

		enum class TuiMouseButton
		{
			Left,
			Middle,
			Right,
		};

		struct TuiKeyInfo
		{
			vint							code = 0;
			bool							ctrl = false;
			bool							shift = false;
			bool							alt = false;
			bool							capslock = false;
			bool							autoRepeatKeyDown = false;
		};

		struct TuiCharInfo
		{
			char32_t						code = 0;
			bool							ctrl = false;
			bool							shift = false;
			bool							alt = false;
			bool							capslock = false;
		};

		class ITuiCallback : public Interface
		{
		public:
			virtual void					Starting();
			virtual void					Stopping();
			virtual void					BufferSizeChanged();
			virtual void					MouseMove(const TuiMouseInfo& info);
			virtual void					MouseDown(TuiMouseButton button, const TuiMouseInfo& info);
			virtual void					MouseUp(TuiMouseButton button, const TuiMouseInfo& info);
			virtual void					MouseDoubleClick(TuiMouseButton button, const TuiMouseInfo& info);
			virtual void					MouseVerticalWheel(const TuiMouseInfo& info);
			virtual void					MouseHorizontalWheel(const TuiMouseInfo& info);
			virtual void					KeyDown(const TuiKeyInfo& info);
			virtual void					KeyUp(const TuiKeyInfo& info);
			virtual void					Char(const TuiCharInfo& info);
			virtual void					Timer();
		};

		struct TuiPrintOptions
		{
			TuiColor						foregroundColor = { 255, 255, 255 };
			TuiColor						backgroundColor = { 0, 0, 0 };
		};

		struct TuiLineOptions
		{
			TuiMergeableGlyph				glyph = TuiMergeableGlyph::ThinLine;
			TuiColor						foregroundColor = { 255, 255, 255 };
			Nullable<TuiColor>				backgroundColor;
		};

		enum class TuiRectCorner
		{
			Sharp,
			Round,
		};

		struct TuiRectOptions
		{
			TuiMergeableGlyph				glyph = TuiMergeableGlyph::ThinLine;
			TuiColor						foregroundColor = { 255, 255, 255 };
			Nullable<TuiColor>				backgroundColor;
			TuiRectCorner					corner = TuiRectCorner::Sharp;
		};

		class TUI abstract
		{
		public:
			static bool						TryGetConsoleSize(vint& width, vint& height);
			static void						Start(const TuiStartOptions& options);
			static bool						RunOneCycle();
			static void						Stop();
			static bool						IsInUse();
			static bool						IsStopRequested();
			static TuiColorMode				GetColorMode();

			static bool						InstallListener(ITuiCallback* listener);
			static bool						UninstallListener(ITuiCallback* listener);
			static void						StartTimer(vint milliseconds);
			static void						StopTimer();

			static TuiPixel*				GetBuffer();
			static vint						GetBufferWidth();
			static vint						GetBufferHeight();
			static vint						MeasureChar(char32_t code);
			static void						RenderBuffer();

			static void						PrintChar(const TuiPrintOptions& options, char32_t code, vint x, vint y);
			static void						DrawLineV(const TuiLineOptions& options, vint x, vint y1, vint y2);
			static void						DrawLineH(const TuiLineOptions& options, vint x1, vint x2, vint y);
			static void						DrawRect(const TuiRectOptions& options, vint x1, vint y1, vint x2, vint y2);
			static void						Clear(TuiColor backgroundColor, vint x1, vint y1, vint x2, vint y2);

			static void						PrintChar(TuiPixel* buffer, vint width, vint height, const TuiPrintOptions& options, char32_t code, vint x, vint y);
			static void						DrawLineV(TuiPixel* buffer, vint width, vint height, const TuiLineOptions& options, vint x, vint y1, vint y2);
			static void						DrawLineH(TuiPixel* buffer, vint width, vint height, const TuiLineOptions& options, vint x1, vint x2, vint y);
			static void						DrawRect(TuiPixel* buffer, vint width, vint height, const TuiRectOptions& options, vint x1, vint y1, vint x2, vint y2);
			static void						Clear(TuiPixel* buffer, vint width, vint height, TuiColor backgroundColor, vint x1, vint y1, vint x2, vint y2);
		};

		namespace unittest
		{
			enum class TuiBackendEventType
			{
				None,
				Resize,
				MouseMove,
				MouseDown,
				MouseUp,
				MouseDoubleClick,
				MouseVerticalWheel,
				MouseHorizontalWheel,
				KeyDown,
				KeyUp,
				Char,
			};

			struct TuiBackendEvent
			{
				TuiBackendEventType			type = TuiBackendEventType::None;
				vint						width = 0;
				vint						height = 0;
				TuiMouseButton				mouseButton = TuiMouseButton::Left;
				TuiMouseInfo				mouseInfo;
				TuiKeyInfo					keyInfo;
				TuiCharInfo					charInfo;
			};

			class ITuiBackend : public Interface
			{
			public:
				virtual TuiColorMode			Start(const TuiStartOptions& options) = 0;
				virtual void				Stop() = 0;
				virtual bool				TryGetConsoleSize(vint& width, vint& height) = 0;
				virtual vuint64_t			GetMonotonicTime() = 0;
				virtual bool				ReadEvent(vint milliseconds, TuiBackendEvent& event) = 0;
				virtual void				Render(const TuiPixel* buffer, vint width, vint height, TuiColorMode colorMode) = 0;
			};

			class ScopedTuiBackend
			{
			private:
				Ptr<ITuiBackend>				previous;

			public:
				NOT_COPYABLE(ScopedTuiBackend);
				ScopedTuiBackend(Ptr<ITuiBackend> backend);
				~ScopedTuiBackend() noexcept(false);
			};
		}
	}
}

#endif

# TODO

## 2.0

- `Utf32Encoder` and `Utf32Decoder`.
- Rewrite all encoders and decoders using `UtfFrom32ReaderBase<T>` and `UtfTo32ReaderBase<T>`.
- Optimize and eliminate unnecessary calculation when
  - `VCZH_WCHAR_UTF16`
  - `VCZH_WCHAR_UTF32`
- Move platform-dependent code to separated files as what `Vlpp` does.

## Optional

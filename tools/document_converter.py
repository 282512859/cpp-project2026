"""Bundled MarkItDown bridge used only by cloud_server.exe.

The C++ server passes three local paths: input, Markdown output, and error output.
Only convert_local() is used so document contents cannot request remote URLs.
"""

from pathlib import Path
import sys

from markitdown import MarkItDown


def main() -> int:
    if len(sys.argv) != 4:
        return 2
    source = Path(sys.argv[1])
    output = Path(sys.argv[2])
    error = Path(sys.argv[3])
    try:
        converter = MarkItDown(enable_plugins=False)
        result = converter.convert_local(source)
        markdown = getattr(result, "text_content", None)
        if markdown is None:
            markdown = result.markdown
        output.write_text(markdown, encoding="utf-8", newline="\n")
        return 0
    except Exception as exception:  # The C++ side returns this as a service error.
        message = f"conversion failed: {type(exception).__name__}: {exception}"
        try:
            error.write_text(message[:4096], encoding="utf-8", newline="\n")
        except Exception:
            pass
        return 1


if __name__ == "__main__":
    raise SystemExit(main())

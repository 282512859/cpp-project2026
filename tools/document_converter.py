"""Bundled document preview/conversion bridge used by cloud_server.exe.

Legacy mode converts DOCX/PDF to Markdown:
  document_converter.exe <input> <markdown-output> <error-output>
PDF asset mode renders one PDF page to PNG:
  document_converter.exe --render-pdf-page <input> <png-output> <page> <error-output>
Video asset mode renders a short animated GIF preview:
  document_converter.exe --render-video-preview <input> <gif-output> <error-output>
"""

from pathlib import Path
import sys


def _write_error(path: Path, exception: Exception) -> None:
    message = f"conversion failed: {type(exception).__name__}: {exception}"
    try:
        path.write_text(message[:4096], encoding="utf-8", newline="\n")
    except Exception:
        pass


def _convert_markdown(source: Path, output: Path) -> None:
    from markitdown import MarkItDown

    converter = MarkItDown(enable_plugins=False)
    result = converter.convert_local(source)
    markdown = getattr(result, "text_content", None)
    if markdown is None:
        markdown = result.markdown
    output.write_text(markdown, encoding="utf-8", newline="\n")


def _render_pdf_page(source: Path, output: Path, page_number: int) -> None:
    import pypdfium2 as pdfium
    from PIL import Image

    document = pdfium.PdfDocument(str(source))
    try:
        if page_number < 1 or page_number > len(document):
            raise ValueError(f"PDF page {page_number} is outside 1..{len(document)}")
        page = document[page_number - 1]
        try:
            width, _ = page.get_size()
            scale = max(1.0, min(2.4, 1400.0 / max(width, 1.0)))
            bitmap = page.render(scale=scale)
            image = bitmap.to_pil()
        finally:
            page.close()
    finally:
        document.close()

    if image.mode not in ("RGB", "RGBA"):
        image = image.convert("RGB")
    output.parent.mkdir(parents=True, exist_ok=True)
    image.save(output, format="PNG", optimize=True)

    limit = 3_750_000
    while output.stat().st_size > limit and image.width > 640:
        image = image.resize((int(image.width * 0.82), int(image.height * 0.82)), Image.Resampling.LANCZOS)
        image.save(output, format="PNG", optimize=True)


def _render_video_preview(source: Path, output: Path) -> None:
    import subprocess
    import imageio_ffmpeg

    ffmpeg = imageio_ffmpeg.get_ffmpeg_exe()
    output.parent.mkdir(parents=True, exist_ok=True)
    profiles = [(3.0, 6, 480, 96), (2.5, 5, 420, 80), (2.0, 4, 360, 64)]
    last_error = ""
    for duration, fps, width, colors in profiles:
        filter_graph = (
            f"fps={fps},scale={width}:-2:flags=lanczos,split[s0][s1];"
            f"[s0]palettegen=max_colors={colors}[p];[s1][p]paletteuse=dither=bayer:bayer_scale=5"
        )
        command = [
            ffmpeg, "-hide_banner", "-loglevel", "error", "-y",
            "-ss", "0.5", "-i", str(source), "-t", str(duration),
            "-an", "-vf", filter_graph, "-loop", "0", str(output),
        ]
        completed = subprocess.run(
            command, capture_output=True, text=True, timeout=25,
            creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0),
        )
        if completed.returncode == 0 and output.is_file() and 0 < output.stat().st_size <= 3_750_000:
            return
        last_error = (completed.stderr or completed.stdout or "video preview rendering failed")[-3000:]
        try:
            output.unlink(missing_ok=True)
        except Exception:
            pass
    raise RuntimeError(last_error or "video preview exceeds the protocol size limit")


def main() -> int:
    if len(sys.argv) == 4:
        source = Path(sys.argv[1])
        output = Path(sys.argv[2])
        error = Path(sys.argv[3])
        try:
            _convert_markdown(source, output)
            return 0
        except Exception as exception:
            _write_error(error, exception)
            return 1

    if len(sys.argv) == 5 and sys.argv[1] == "--render-video-preview":
        source = Path(sys.argv[2])
        output = Path(sys.argv[3])
        error = Path(sys.argv[4])
        try:
            _render_video_preview(source, output)
            return 0
        except Exception as exception:
            _write_error(error, exception)
            return 1

    if len(sys.argv) == 6 and sys.argv[1] == "--render-pdf-page":
        source = Path(sys.argv[2])
        output = Path(sys.argv[3])
        error = Path(sys.argv[5])
        try:
            _render_pdf_page(source, output, int(sys.argv[4]))
            return 0
        except Exception as exception:
            _write_error(error, exception)
            return 1

    return 2


if __name__ == "__main__":
    raise SystemExit(main())

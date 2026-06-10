# Block Folder Thumbnail Cache

A Windhawk mod that prevents Windows File Explorer from generating preview thumbnails for folders while keeping file thumbnails enabled.

## Features

- Keeps thumbnails for images, videos, PDFs, and other files.
- Blocks generated preview thumbnails for folders.
- Preserves normal folder icons from the current icon theme.
- Does not force a specific folder icon such as the default yellow Windows folder.
- Can remove the old `Logo` registry workaround if enabled.

## Why?

Windows 11 generates preview thumbnails for folders that contain media files. This can break visual consistency when using custom folder icon themes, because media folders show generated previews instead of normal themed folder icons.

## Notes

This mod is experimental. File Explorer has multiple internal paths for icons and thumbnails, so some cached previews may remain until the icon and thumbnail cache is cleared.

## Installation

1. Open Windhawk.
2. Click **Create new mod**.
3. Paste the code from `block-folder-thumbnail-cache.wh.cpp`.
4. Compile and enable the mod.
5. Keep **Clear thumbnail cache once** enabled after installing or updating.

## License

MIT

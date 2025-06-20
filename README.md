# Kilo Text Editor

Kilo is a minimal text editor written in C, designed to be simple yet functional. It provides basic text editing capabilities with a terminal-based interface.

## Features

- Basic text editing operations (insert, delete, move cursor)
- File I/O operations (open, save)
- Search functionality
- Terminal-based UI with status bar
- Support for tabs and special characters
- Scrolling support for large files
- Unsaved changes protection

## Code Structure

The codebase is organized into several key sections:

### 1. Includes and Defines
- Standard C library includes for basic functionality
- Custom defines for editor configuration
- Special key codes for editor navigation

### 2. Data Structures
- `erow`: Represents a row in the editor
  - Stores both raw characters and rendered characters (with tabs expanded)
  - Maintains size information for both raw and rendered content
- `editorConfig`: Global editor configuration
  - Cursor position and screen dimensions
  - File information and status
  - Terminal settings

### 3. Terminal Functions
- Raw mode handling for direct terminal input
- Terminal size detection
- Cursor position management
- Error handling

### 4. Row Operations
- Row manipulation (insert, delete, update)
- Tab handling and rendering
- Position conversion between cursor and render coordinates

### 5. File I/O
- File opening and saving
- String conversion for file operations
- Error handling for file operations

### 6. Find Functionality
- Text search with navigation
- Search result highlighting
- Directional search support

### 7. Output Functions
- Screen rendering
- Status bar and message display
- Scrolling implementation
- Cursor positioning

### 8. Input Handling
- Key press processing
- Special key handling
- Command processing

## Key Commands

- `Ctrl-S`: Save file
- `Ctrl-Q`: Quit editor (requires multiple presses if there are unsaved changes)
- `Ctrl-F`: Search in file
- Arrow keys: Navigate
- `Home/End`: Move to start/end of line
- `Page Up/Down`: Scroll up/down

## Building and Running

Build the editor:
```bash
make
```
Building without make, C23 can be replaced with your version of C
```bash
cc kilo.c -o kilo -Wall -Wextra -pedantic -std=c23
```
To run the editor with an existing file:
```bash
./kilo [filename]
```

Or to run the editor without any input file
```bash
./kilo 
```
## Dependencies

The editor requires a POSIX-compliant system with the following headers:
- termios.h
- sys/ioctl.h
- unistd.h


## Acknowledgements

This project is based on Salvatore Sanfilippo's (antirez) [Kilo - A Text Editor in 1000 lines of C code](https://github.com/antirez/kilo). An excellent [tutorial](https://viewsourcecode.org/snaptoken/kilo/) for this project was made by Jeremy Ruten (snaptoken) [here](https://viewsourcecode.org/snaptoken/).

- [Original Project](https://github.com/antirez/kilo), [Blog Post by Antirez](https://antirez.com/news/108)
- [Tutorial for the project](https://viewsourcecode.org/snaptoken/kilo/)



## License

The original project by **Salvatore Sanfilippo (antirez)** and the tutorial by **Jeremy Ruten (snaptoken)** are both licensed under the **BSD 2-Clause License**. This repository is also licensed under the **BSD 2-Clause License**.

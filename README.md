# glycerin

Simple utility for logging an output stream continuesly.

## Usage

- one single required argument for naming the application that is being logged, may not contain `/` and `.`
- option `-t` add a timestamp to every line
    - a single `-t` adds time in unix epoch
    - with `-tt` the format `YYYY-MM-DD_HH:MM:SS.xxxxx` in UTC is used
    - and `-ttt` uses `YYYY-MM-DDTHH:MM:SS.xxxxx` in UTC
- option `-b u64` sets the size of the buffer for buffering stdin (default 1 KiB)
- option `-s u64` size in bytes before the log file gets rotated (default 10 MiB)
- option `-a u64` maximum age of the log file before it's rotated (default 1 day)
- option `-n u64` number of log files to keep excluding the active log (default 7)
- option `-d DIR` base path for all logs (default /var/log if rootful, otherwise $HOME/.local/share/glycerin/logs)
- option `-f` only use files, a do not create a subdirectory
